#ifndef ENGINE_REGISTRIESMANAGER_H
#define ENGINE_REGISTRIESMANAGER_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineManager.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../thirdparty/thirdparty.h"
#include "../../base/SingleQueueEventSystem.h"
#include "EntityCommandBuffer.h"
#include <atomic>
#include <vector>

namespace boost {
#if defined(BOOST_THREAD_PROVIDES_FUTURE)
    template <typename T>
    using unique_future = future<T>;
#endif
}

namespace MQEngine {

    class ENGINE_API RegistriesManager : public CoreEngineManager<RegistriesManager>
    {
    public:
        struct Request
        {
            struct save
            {
                std::string path;
                entt::registry* registry;
            };
            struct add
            {
                boost::unique_future<entt::registry*> future;
            };
            struct remove
            {
                entt::registry* registry;
            };
            struct Emplace
            {
                entt::registry* registry;
                entt::entity entity;
                std::function<void(entt::registry&, entt::entity)> emplacer;
            };
            struct RemoveComponent
            {
                entt::registry* registry;
                entt::entity entity;
                std::function<void(entt::registry&, entt::entity)> remover;
            };
            struct ClearComponent
            {
                entt::registry* registry;
                std::function<void(entt::registry&)> clearer;
            };
            struct Patch
            {
                entt::registry* registry;
                entt::entity entity;
                std::function<void(entt::registry&, entt::entity)> patcher;
            };
        };

        RegistriesManager();
        ~RegistriesManager();

        void registerInfo() {}

        // ------------------ 场景与注册表生命周期 ------------------
        void requestSaveRegistries(std::string path, entt::registry* registry);
        void requestAddRegistries(boost::unique_future<entt::registry*> future);
        void requestRemoveRegistries(entt::registry* registry);
        void removeRegistryImmediately(entt::registry* registry);
        boost::unique_future<entt::registry*> loadRegistries(std::string path, UniquePtr<entt::registry>& ret);
        std::vector<entt::registry*> currentRegistries() const;
        entt::registry* createRegistry();
        void storage(entt::registry* registry);

        // ------------------ 线程安全延迟指令便捷接口 ------------------
        DeferredEntity createEntityDeferred(entt::registry* registry = nullptr,
                                            std::function<void(entt::entity)> onCreated = nullptr);
        void destroyEntityDeferred(DeferredEntity entity, bool destroyHierarchy = true);
        void destroyEntityDeferred(entt::registry* registry, DeferredEntity entity, bool destroyHierarchy = true);

        template <typename T, typename... Args>
        void emplaceDeferred(DeferredEntity entity, Args&&... args);

        template <typename T, typename... Args>
        void emplaceDeferred(entt::registry* registry, DeferredEntity entity, Args&&... args);

        template <typename T>
        void removeDeferred(DeferredEntity entity);

        template <typename T>
        void removeDeferred(entt::registry* registry, DeferredEntity entity);

        template <typename T>
        void clearDeferred(entt::registry* registry = nullptr);

        template <typename T, typename Func>
        void patchDeferred(DeferredEntity entity, Func&& func);

        template <typename T, typename Func>
        void patchDeferred(entt::registry* registry, DeferredEntity entity, Func&& func);

        // ------------------ 批量提交与帧同步回放 ------------------
        void submitCommandBuffer(EntityCommandBuffer buffer);
        void submitCommandBuffer(UniquePtr<EntityCommandBuffer> buffer);

        /// 帧同步边界回放：严格在 Context::flush() 的 executeSyncTickers() 中被调用
        void syncTicker();

        /// 解析延迟占位符实体为具体实体
        entt::entity resolve(const DeferredEntity& deferred) const;

        /// 清空所有已解析的实体映射（常用于场景重置或关卡切换）
        void clearResolvedMappings();

        /// 显式触发一次失效实体的清理（垃圾回收）
        void pruneDeadEntities();

        /// 获取当前维护的已解析映射条目数（诊断与统计）
        [[nodiscard]] size_t resolvedMappingCount() const;

        // ------------------ 历史接口完全兼容别名 ------------------
        template <typename T, typename... Args>
        void requestEmplaceComponent(entt::registry* registry, entt::entity entity, Args&&... args) {
            emplaceDeferred<T>(registry, DeferredEntity(entity, registry), std::forward<Args>(args)...);
        }

        template <typename T>
        void requestRemoveComponent(entt::registry* registry, entt::entity entity) {
            removeDeferred<T>(registry, DeferredEntity(entity, registry));
        }

        template <typename T>
        void requestClearComponent(entt::registry* registry) {
            clearDeferred<T>(registry);
        }

        template <typename T, typename Func>
        void requestGetOrEmplace(entt::registry* registry, entt::entity entity, Func&& func) {
            patchDeferred<T>(registry, DeferredEntity(entity, registry), std::forward<Func>(func));
        }

    private:
        void enqueueCommand(UniquePtr<IEntityCommand> cmd);
        std::vector<UniquePtr<IEntityCommand>> drainPendingCommands();
        void publishResolvedMapping(std::unordered_map<uint32_t, entt::entity>&& nextMap);

        std::vector<entt::registry*> m_registries;
        SingleQueueEventSystem m_requestQueue;

        // Note: 无锁 MPSC 侵入式指令链（CAS 头插，帧同步点反转回放以保持严格 FIFO 顺序）
        std::atomic<IEntityCommand*> m_commandHead{nullptr};
        // Note: 双缓冲无锁实体句柄解析表（帧同步点更新，读取端零锁）
        std::unordered_map<uint32_t, entt::entity> m_resolvedMappings[2];
        std::atomic<uint32_t> m_activeMappingIdx{0};
    };

} // namespace MQEngine

#include "./RegistriesManager.hpp"
#endif //REGISTRIESMANAGER_H
