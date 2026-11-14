#ifndef MQENGINE_RESOURCE_CORE_ARENA_H
#define MQENGINE_RESOURCE_CORE_ARENA_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 字节向上对齐工具函数
         */
        inline constexpr size_t alignUp(size_t value, size_t alignment) noexcept
        {
            return (value + (alignment - 1)) & ~(alignment - 1);
        }

        /**
         * @brief 线性区域内存分配器（Linear Arena）：
         *        采用单向碰撞指针分配（Bump Allocation），支持溢出动态块扩容与非平凡类型析构函数注册，适用于帧内瞬态对象与批量几何缓冲
         */
        class ENGINE_API Arena
        {
        public:
            explicit Arena(size_t primaryCapacityBytes = 1024 * 1024, MemoryTag tag = MemoryTag::Arena);
            ~Arena();

            Arena(const Arena&) = delete;
            Arena& operator=(const Arena&) = delete;

            void* allocate(size_t byteSize, size_t alignment = alignof(std::max_align_t));

            template <typename T>
            T* allocateArray(size_t count)
            {
                if (count == 0) {
                    return nullptr;
                }
                void* mem = allocate(sizeof(T) * count, alignof(T));
                if constexpr (!std::is_trivially_default_constructible_v<T>) {
                    auto* arr = static_cast<T*>(mem);
                    for (size_t i = 0; i < count; ++i) {
                        new (arr + i) T();
                    }
                    if constexpr (!std::is_trivially_destructible_v<T>) {
                        registerArrayDestructor(arr, count);
                    }
                    return arr;
                } else {
                    return static_cast<T*>(mem);
                }
            }

            template <typename T, typename... Args>
            T* create(Args&&... args)
            {
                void* mem = allocate(sizeof(T), alignof(T));
                auto* obj = new (mem) T(std::forward<Args>(args)...);
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    registerDestructor(obj, [](void* p) {
                        static_cast<T*>(p)->~T();
                    });
                }
                return obj;
            }

            void reset() noexcept;

            size_t usedBytes() const noexcept;
            size_t capacityBytes() const noexcept;
            size_t peakUsedBytes() const noexcept { return m_peakUsedBytes.load(std::memory_order_relaxed); }

            size_t primaryOffset() const noexcept { return m_primaryOffset.load(std::memory_order_relaxed); }
            void rewindPrimary(size_t targetOffset) noexcept;

        private:
            struct OverflowBlockNode
            {
                uint8_t* data = nullptr;
                size_t capacity = 0;
                std::atomic<size_t> offset{0};
                OverflowBlockNode* next = nullptr;
            };

            struct DestructorNode
            {
                void (*destroyFn)(void*) = nullptr;
                void* target = nullptr;
                DestructorNode* next = nullptr;
            };

            template <typename T>
            void registerArrayDestructor(T* arrayPtr, size_t count)
            {
                struct ArraySlice
                {
                    T* ptr;
                    size_t len;
                };
                void* sliceMem = allocate(sizeof(ArraySlice), alignof(ArraySlice));
                auto* slice = new (sliceMem) ArraySlice{arrayPtr, count};
                registerDestructor(slice, [](void* raw) {
                    auto* s = static_cast<ArraySlice*>(raw);
                    for (size_t i = s->len; i > 0; --i) {
                        s->ptr[i - 1].~T();
                    }
                });
            }

            void* tryAllocateFromPrimary(size_t byteSize, size_t alignment) noexcept;
            void* allocateFromOverflow(size_t byteSize, size_t alignment);
            void registerDestructor(void* target, void (*destroyFn)(void*));
            void runDestructors() noexcept;
            void freeOverflowBlocks() noexcept;
            void recordPeakUsage(size_t totalUsed) noexcept;

            uint8_t* m_primaryBlock = nullptr;
            size_t m_primaryCapacity = 0;
            std::atomic<size_t> m_primaryOffset{0};
            std::atomic<size_t> m_overflowUsedBytes{0};
            std::atomic<size_t> m_overflowCapacityBytes{0};
            std::atomic<size_t> m_peakUsedBytes{0};
            MemoryTag m_tag = MemoryTag::Arena;

            std::atomic<OverflowBlockNode*> m_overflowHead{nullptr};
            std::atomic<DestructorNode*> m_destructorHead{nullptr};
        };

        /**
         * @brief 双缓冲帧线性区域分配器（Double Buffered Frame Arena）：
         *        交替重置奇偶帧分配器，保证跨逻辑帧与渲染同步边界的瞬态数据在相邻帧内生命周期安全
         */
        class ENGINE_API DoubleBufferedFrameArena
        {
        public:
            explicit DoubleBufferedFrameArena(size_t perFrameCapacityBytes = 2 * 1024 * 1024);
            ~DoubleBufferedFrameArena();

            DoubleBufferedFrameArena(const DoubleBufferedFrameArena&) = delete;
            DoubleBufferedFrameArena& operator=(const DoubleBufferedFrameArena&) = delete;

            void nextFrame() noexcept;
            void resetAll() noexcept;

            Arena& currentArena() noexcept;
            const Arena& currentArena() const noexcept;
            Arena& previousArena() noexcept;
            const Arena& previousArena() const noexcept;

            void* allocate(size_t byteSize, size_t alignment = alignof(std::max_align_t))
            {
                return currentArena().allocate(byteSize, alignment);
            }

            template <typename T, typename... Args>
            T* create(Args&&... args)
            {
                return currentArena().create<T>(std::forward<Args>(args)...);
            }

            template <typename T>
            T* allocateArray(size_t count)
            {
                return currentArena().allocateArray<T>(count);
            }

            uint64_t frameCount() const noexcept { return m_frameIndex.load(std::memory_order_relaxed); }

        private:
            std::array<UniquePtr<Arena>, 2> m_arenas;
            std::atomic<uint64_t> m_frameIndex{0};
        };

        /**
         * @brief RAII 作用域分配器回退标记（仅适用于无需执行析构函数的纯数据内存）
         */
        class ENGINE_API ScopedArenaMarker
        {
        public:
            explicit ScopedArenaMarker(Arena& arena) noexcept
                : m_arena(arena), m_savedOffset(arena.primaryOffset())
            {
            }

            ~ScopedArenaMarker()
            {
                m_arena.rewindPrimary(m_savedOffset);
            }

            ScopedArenaMarker(const ScopedArenaMarker&) = delete;
            ScopedArenaMarker& operator=(const ScopedArenaMarker&) = delete;

        private:
            Arena& m_arena;
            size_t m_savedOffset = 0;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_ARENA_H
