#ifndef MQENGINE_RESOURCE_TABLE_DATABASE_H
#define MQENGINE_RESOURCE_TABLE_DATABASE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/resource/table/EntityKey.h>
#include <cassert>
#include <cstdint>
#include <functional>
#include <utility>
#include <atomic>
#include <vector>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 统一内存数据库容器（DataBase）：
         *        将 entt::registry 封装为内存数据库表底座。
         *        以原生实体句柄 FCT::ECS::entity 作为天然主键，每种 Component 存储池为一元关系子表，
         *        view<A, B> 等价于关系代数 INNER JOIN。
         */
        class ENGINE_API DataBase
        {
        public:
            DataBase() = default;
            ~DataBase() = default;

            DataBase(const DataBase&) = delete;
            DataBase& operator=(const DataBase&) = delete;

            DataBase(DataBase&& other) noexcept = default;
            DataBase& operator=(DataBase&& other) noexcept = default;

            // 实体天然主键生成与销毁
            FCT::ECS::entity createEntity()
            {
                return m_registry.create();
            }

            void destroyEntity(FCT::ECS::entity entity)
            {
                if (entityExists(entity)) {
                    m_registry.destroy(entity);
                }
            }

            bool entityExists(FCT::ECS::entity entity) const noexcept
            {
                return (entity != FCT::ECS::null) && m_registry.valid(entity);
            }

            size_t entityCount() const noexcept
            {
                return m_registry.storage<FCT::ECS::entity>()->size();
            }

            // 组件操作（通过天然主键 FCT::ECS::entity 索引）
            template <typename T, typename... Args>
            T& emplace(FCT::ECS::entity entity, Args&&... args)
            {
                assert(entityExists(entity) && "DataBase: 实体主键无效或已失效");
                return m_registry.emplace<T>(entity, std::forward<Args>(args)...);
            }

            template <typename T, typename... Args>
            T& emplaceOrReplace(FCT::ECS::entity entity, Args&&... args)
            {
                assert(entityExists(entity) && "DataBase: 实体主键无效或已失效");
                return m_registry.emplace_or_replace<T>(entity, std::forward<Args>(args)...);
            }

            template <typename T>
            void remove(FCT::ECS::entity entity)
            {
                assert(entityExists(entity) && "DataBase: 实体主键无效或已失效");
                m_registry.remove<T>(entity);
            }

            template <typename T>
            bool has(FCT::ECS::entity entity) const noexcept
            {
                if (!entityExists(entity)) {
                    return false;
                }
                return m_registry.all_of<T>(entity);
            }

            template <typename T>
            T& get(FCT::ECS::entity entity)
            {
                assert(entityExists(entity) && "DataBase: 实体主键无效或已失效");
                assert(has<T>(entity) && "DataBase: 实体未附加指定组件");
                return m_registry.get<T>(entity);
            }

            template <typename T>
            const T& get(FCT::ECS::entity entity) const
            {
                assert(entityExists(entity) && "DataBase: 实体主键无效或已失效");
                assert(has<T>(entity) && "DataBase: 实体未附加指定组件");
                return m_registry.get<T>(entity);
            }

            template <typename T>
            T* tryGet(FCT::ECS::entity entity) noexcept
            {
                if (!entityExists(entity)) {
                    return nullptr;
                }
                return m_registry.try_get<T>(entity);
            }

            template <typename T>
            const T* tryGet(FCT::ECS::entity entity) const noexcept
            {
                if (!entityExists(entity)) {
                    return nullptr;
                }
                return m_registry.try_get<T>(entity);
            }

            // 关系代数 INNER JOIN 视图
            template <typename... Components>
            auto view()
            {
                return m_registry.view<Components...>();
            }

            template <typename... Components>
            auto view() const
            {
                return m_registry.view<Components...>();
            }

            // 实体迭代遍历
            template <typename... Components, typename Func>
            void each(Func&& func)
            {
                auto v = view<Components...>();
                for (auto entity : v) {
                    if constexpr (sizeof...(Components) == 0) {
                        func(entity);
                    } else {
                        func(entity, v.template get<Components>(entity)...);
                    }
                }
            }

            void clear() noexcept
            {
                m_registry.clear();
            }

            FCT::ECS::registry& getRegistry() noexcept { return m_registry; }
            const FCT::ECS::registry& getRegistry() const noexcept { return m_registry; }

            FCT::ECS::registry& registry() noexcept { return m_registry; }
            const FCT::ECS::registry& registry() const noexcept { return m_registry; }

            operator FCT::ECS::registry&() noexcept { return m_registry; }
            operator const FCT::ECS::registry&() const noexcept { return m_registry; }

        private:
            FCT::ECS::registry m_registry;
        };

        /**
         * @brief 纯裸指针主键无锁资源表模板基类（LockFreeResourceTable）
         *        遵循 Docs 02/05/06 契约，直接使用原生 GPU 资源裸指针作为 Primary Key，
         *        零合成 Key，基于 CAS 开地址哈希实现 100% 严格无锁。
         */
        template <typename NativeGpuResourcePtr, typename Metadata>
        class LockFreeResourceTable
        {
        public:
            static constexpr size_t kDefaultTableCapacity = 16384;

            LockFreeResourceTable(size_t capacity = kDefaultTableCapacity)
                : m_capacity(capacity > 64 ? capacity : 64)
                , m_entries(m_capacity)
            {
            }

            ~LockFreeResourceTable()
            {
                clear();
            }

            LockFreeResourceTable(const LockFreeResourceTable&) = delete;
            LockFreeResourceTable& operator=(const LockFreeResourceTable&) = delete;

            void registerResource(NativeGpuResourcePtr ptr, Metadata meta)
            {
                if (!ptr) {
                    return;
                }

                auto* newRecord = new Metadata(std::move(meta));
                const size_t h = hashPtr(ptr);

                for (size_t step = 0; step < m_capacity; ++step) {
                    size_t idx = (h + step * step + step) % m_capacity;
                    auto& entry = m_entries[idx];

                    NativeGpuResourcePtr expected = nullptr;
                    if (entry.key.compare_exchange_strong(expected, ptr, std::memory_order_acq_rel)) {
                        Metadata* old = entry.value.exchange(newRecord, std::memory_order_acq_rel);
                        if (old) {
                            delete old;
                        } else {
                            m_count.fetch_add(1, std::memory_order_relaxed);
                        }
                        return;
                    }

                    if (expected == ptr) {
                        Metadata* old = entry.value.exchange(newRecord, std::memory_order_acq_rel);
                        if (old) {
                            delete old;
                        } else {
                            m_count.fetch_add(1, std::memory_order_relaxed);
                        }
                        return;
                    }
                }

                // 表满防护
                delete newRecord;
            }

            Metadata* findResource(NativeGpuResourcePtr ptr)
            {
                if (!ptr) {
                    return nullptr;
                }

                const size_t h = hashPtr(ptr);
                for (size_t step = 0; step < m_capacity; ++step) {
                    size_t idx = (h + step * step + step) % m_capacity;
                    auto& entry = m_entries[idx];

                    NativeGpuResourcePtr key = entry.key.load(std::memory_order_acquire);
                    if (key == ptr) {
                        return entry.value.load(std::memory_order_acquire);
                    }
                    if (key == nullptr) {
                        return nullptr;
                    }
                }
                return nullptr;
            }

            const Metadata* findResource(NativeGpuResourcePtr ptr) const
            {
                return const_cast<LockFreeResourceTable*>(this)->findResource(ptr);
            }

            bool contains(NativeGpuResourcePtr ptr) const
            {
                return findResource(ptr) != nullptr;
            }

            bool unregisterResource(NativeGpuResourcePtr ptr)
            {
                if (!ptr) {
                    return false;
                }

                const size_t h = hashPtr(ptr);
                for (size_t step = 0; step < m_capacity; ++step) {
                    size_t idx = (h + step * step + step) % m_capacity;
                    auto& entry = m_entries[idx];

                    NativeGpuResourcePtr key = entry.key.load(std::memory_order_acquire);
                    if (key == ptr) {
                        Metadata* old = entry.value.exchange(nullptr, std::memory_order_acq_rel);
                        if (old) {
                            delete old;
                            m_count.fetch_sub(1, std::memory_order_relaxed);
                            return true;
                        }
                        return false;
                    }
                    if (key == nullptr) {
                        return false;
                    }
                }
                return false;
            }

            size_t size() const noexcept
            {
                return m_count.load(std::memory_order_relaxed);
            }

            template <typename Fn>
            void forEach(Fn&& fn) const
            {
                for (size_t i = 0; i < m_capacity; ++i) {
                    NativeGpuResourcePtr key = m_entries[i].key.load(std::memory_order_acquire);
                    Metadata* val = m_entries[i].value.load(std::memory_order_acquire);
                    if (key && val) {
                        fn(key, *val);
                    }
                }
            }

            std::vector<Metadata> snapshot() const
            {
                std::vector<Metadata> result;
                result.reserve(size());
                forEach([&result](NativeGpuResourcePtr, const Metadata& val) {
                    result.push_back(val);
                });
                return result;
            }

            void clear() noexcept
            {
                for (size_t i = 0; i < m_capacity; ++i) {
                    auto& entry = m_entries[i];
                    entry.key.store(nullptr, std::memory_order_relaxed);
                    Metadata* old = entry.value.exchange(nullptr, std::memory_order_relaxed);
                    if (old) {
                        delete old;
                    }
                }
                m_count.store(0, std::memory_order_relaxed);
            }

        private:
            static size_t hashPtr(NativeGpuResourcePtr ptr) noexcept
            {
                uintptr_t v = reinterpret_cast<uintptr_t>(ptr);
                v ^= (v >> 30);
                v *= 0xbf58476d1ce4e5b9ULL;
                v ^= (v >> 27);
                v *= 0x94d049bb133111ebULL;
                v ^= (v >> 31);
                return static_cast<size_t>(v);
            }

            struct Entry
            {
                std::atomic<NativeGpuResourcePtr> key{nullptr};
                std::atomic<Metadata*> value{nullptr};
            };

            size_t m_capacity = 0;
            std::vector<Entry> m_entries;
            std::atomic<size_t> m_count{0};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_DATABASE_H
