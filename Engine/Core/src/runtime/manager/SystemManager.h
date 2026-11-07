/**
 *@file SystemManager.h
 *@brief class header for start and stop system
 */
#ifndef SYSTEMMANAGER_H
#define SYSTEMMANAGER_H
#include "../system/ISystem.h"
#include "../../base/CoreEngineManager.h"
#include "../../base/SingleQueueEventSystem.h"
#include <unordered_set>
#include <mutex>

namespace MQEngine {
    class ENGINE_API SystemManager : public CoreEngineManager<SystemManager> {
    public:
        using SystemToken = std::string;
        using SystemGraph = FCT::TokenGraph<SystemToken, ISystem*>;

        struct Request
        {
            struct Add
            {
                std::string name;
                ISystem* system;
                std::vector<std::string> pre;
                std::vector<std::string> succ;
            };
            struct SetEnabled
            {
                std::string name;
                bool enabled;
            };
            struct Remove
            {
                std::string name;
            };
        };

        SystemManager();
        void registerInfo() {}
        void init();
        void term();
        
        void requestAddSystem(const std::string& name, ISystem* system, const std::vector<std::string>& pre = {}, const std::vector<std::string>& succ = {});
        void requestRemoveSystem(const std::string& name);
        void requestSetSystemEnabled(const std::string& name, bool enabled);
        void flushRequests();
        void syncTicker() { flushRequests(); }
        
        void logicTick();
        void renderTick();

        struct SystemConfig
        {
            std::string name;
            ISystem* system;
            std::vector<std::string> pre;
            std::vector<std::string> succ;
            bool enabled = true;
        };

        const std::unordered_map<std::string, SystemConfig>& getConfigs() const { return m_configs; }

        // 包含已请求但尚未被 processRequests 处理的系统
        ISystem* findSystem(const std::string& name) const;
        std::vector<std::string> getRegisteredSystemNames() const;

    private:
        SystemGraph m_systemGraph;
        SingleQueueEventSystem m_requestQueue;
        std::unordered_map<std::string, SystemConfig> m_configs;
        std::unordered_map<std::string, ISystem*> m_registeredSystems;
        mutable std::mutex m_graphMutex;
        bool m_dirty = false;
        
        void processRequests();
    };

}
#endif //SYSTEMMANAGER_H
