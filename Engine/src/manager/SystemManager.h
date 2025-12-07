/**
 *@file SystemManager.h
 *@brief class header for start and stop system
 */
#ifndef SYSTEMMANAGER_H
#define SYSTEMMANAGER_H
#include "../system/ISystem.h"
#include "../core/SingleQueueEventSystem.h"
#include <unordered_set>

namespace MQEngine {
    class ENGINE_API SystemManager {
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
        };

        SystemManager();
        void init();
        void term();
        
        void requestAddSystem(const std::string& name, ISystem* system, const std::vector<std::string>& pre = {}, const std::vector<std::string>& succ = {});
        void requestSetSystemEnabled(const std::string& name, bool enabled);
        
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

    private:
        SystemGraph m_systemGraph;
        SingleQueueEventSystem m_requestQueue;
        std::unordered_map<std::string, SystemConfig> m_configs;
        bool m_dirty = false;
        
        void processRequests();
    };

}
#endif //SYSTEMMANAGER_H
