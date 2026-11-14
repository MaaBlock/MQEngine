#ifndef MQENGINE_RESOURCE_TABLE_TEXTURETABLE_H
#define MQENGINE_RESOURCE_TABLE_TEXTURETABLE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/table/DataBase.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 纹理资源在内存数据库表中的行记录：
         *        以 GPU 原生纹理指针（FCT::Image* / void*）作为天然 Primary Key
         */
        struct TextureRecord
        {
            const void* handle = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t channels = 4;
            uint32_t mipLevels = 1;
            uint32_t bindlessSlot = UINT32_MAX;
            uint64_t vramBytes = 0;
            std::string debugName;
            GpuResourceState state = GpuResourceState::Ready;
            const void* fallbackHandle = nullptr;
        };

        /**
         * @brief 专用无锁纹理资源表（TextureTable）：
         *        以 GPU 纹理裸指针为主键，提供毫秒级无锁查询、Bindless 槽位分配与回退安全解析
         */
        class ENGINE_API TextureTable : public CoreEngineManager<TextureTable>
        {
        public:
            TextureTable();
            ~TextureTable();

            TextureTable(const TextureTable&) = delete;
            TextureTable& operator=(const TextureTable&) = delete;

            void registerTexture(const void* handle, TextureRecord record);
            void unregisterTexture(const void* handle);

            const TextureRecord* getTexture(const void* handle) const noexcept;
            TextureRecord* getTexture(const void* handle) noexcept;
            bool contains(const void* handle) const noexcept;

            uint32_t allocateBindlessSlot(const void* handle);
            uint32_t getBindlessSlot(const void* handle) const noexcept;

            void setDefaultFallback(const void* fallbackHandle) noexcept;
            const void* getDefaultFallback() const noexcept;
            const void* resolveHandle(const void* handle) const noexcept;

            template <typename ImageT>
            void registerTyped(const ImageT* handle, TextureRecord record)
            {
                registerTexture(static_cast<const void*>(handle), std::move(record));
            }

            template <typename ImageT>
            const ImageT* resolveTyped(const ImageT* handle) const noexcept
            {
                return static_cast<const ImageT*>(resolveHandle(static_cast<const void*>(handle)));
            }

            size_t activeCount() const noexcept { return m_table.size(); }
            uint64_t totalVramBytes() const noexcept { return m_totalVramBytes.load(std::memory_order_relaxed); }
            void clear() noexcept;

        private:
            LockFreeResourceTable<const void*, TextureRecord> m_table;
            std::atomic<uint32_t> m_nextBindlessSlot{0};
            std::atomic<uint64_t> m_totalVramBytes{0};
            std::atomic<const void*> m_defaultFallback{nullptr};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_TEXTURETABLE_H
