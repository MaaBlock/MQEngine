#ifndef ENGINE_REGISTRIESMANAGER_H
#define ENGINE_REGISTRIESMANAGER_H
#include "../EnginePCH.h"
#include "../core/EngineGlobal.h"
#include "../thirdparty/thirdparty.h"
#include "../core/SingleQueueEventSystem.h"
namespace MQEngine {
    class ENGINE_API RegistriesManager
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
        void requestSaveRegistries(std::string path, entt::registry* registry);
        /**
         *
         * @param future
         */
        void requestAddRegistries(boost::unique_future<entt::registry*> future);
        void requestRemoveRegistries(entt::registry* registry);
        template <typename T, typename... Args>
        void requestEmplaceComponent(entt::registry* registry, entt::entity entity, Args&&... args);
        template <typename T>
        void requestRemoveComponent(entt::registry* registry, entt::entity entity);
        template <typename T>
        void requestClearComponent(entt::registry* registry);
        template <typename T, typename Func>
        void requestGetOrEmplace(entt::registry* registry, entt::entity entity, Func&& func);
        /**
         * @param path 表路径
         * @return 读表的future对象
         */
        boost::unique_future<entt::registry*> loadRegistries(std::string path, UniquePtr<entt::registry>& ret);
        /**
         *
         */
        void syncTicker();
        /**
         *
         * @return
         */
        std::vector<entt::registry*> currentRegistries() const;
        entt::registry* createRegistry();
        void storage(entt::registry* registry);
    private:
        std::vector<entt::registry*> m_registries;
        SingleQueueEventSystem m_requestQueue;
    };
} // MQEngine
#include "./RegistriesManager.hpp"
#endif //REGISTRIESMANAGER_H
