#ifndef ENGINE_REGISTRIESMANAGER_H
#define ENGINE_REGISTRIESMANAGER_H
#include "../EnginePCH.h"
#include "../core/EngineGlobal.h"
#include "../thirdparty/thirdparty.h"
#include "../core/SingleQueueEventSystem.h"
namespace MQEngine {
    class ENGINE_API RegistriesManager {
    public:
        struct Request {
            struct save {
                std::string path;
                entt::registry* registry;
            };

            struct add {
                boost::unique_future<entt::registry> future;
            };

            struct remove {
                entt::registry* registry;
            };
        };

        void requestSaveRegistries(std::string path, entt::registry* registry);
        /**
         *
         * @param future
         */
        void requestAddRegistries(boost::unique_future<entt::registry> future);
        /**
         *
         * @param path 表路径
         * @return 读表的future对象
         */
        boost::unique_future<entt::registry*> loadRegistries(std::string path,UniquePtr<entt::registry>& ret);
        /**
         *
         * @return
         */
        Status syncTicker();
    private:
        SingleQueueEventSystem m_requestQueue;
    };
} // MQEngine

#endif //REGISTRIESMANAGER_H
