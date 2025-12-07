/**
* @file systemmanager.cpp
* @brief class source for start and stop system
*/
#include "../engineapi.h"
#include "SystemManager.h"

namespace MQEngine
{
    SystemManager::SystemManager()
    {
        FCT::fout << "SystemManager::SystemManager()\n";
        
        m_requestQueue.subscribe<Request::Add>([this](Request::Add& req) {
            SystemConfig config;
            config.name = req.name;
            config.system = req.system;
            config.pre = req.pre;
            config.succ = req.succ;
            config.enabled = true;
            
            m_configs[req.name] = config;
            
            m_systemGraph.addNode(req.name, req.system, req.pre, req.succ);
            m_dirty = true;
            
            if (req.system) {
                req.system->onActivate();
            }
            return OkStatus();
        });

        m_requestQueue.subscribe<Request::SetEnabled>([this](Request::SetEnabled& req) {
            auto it = m_configs.find(req.name);
            if (it != m_configs.end()) {
                if (it->second.enabled != req.enabled) {
                    it->second.enabled = req.enabled;
                    if (req.enabled) {
                        m_systemGraph.addNode(it->second.name, it->second.system, it->second.pre, it->second.succ);
                        if (it->second.system) it->second.system->onActivate();
                    } else {
                        m_systemGraph.removeNode(req.name);
                        if (it->second.system) it->second.system->onDeactivate();
                    }
                    m_dirty = true;
                }
                return OkStatus();
            }
            return InvalidArgumentError("System not found: " + req.name);
        });
    }

    void SystemManager::init()
    {
        NodeCommon::Init();
    }

    void SystemManager::term()
    {
        NodeCommon::Term();
    }

    void SystemManager::requestAddSystem(const std::string& name, ISystem* system, const std::vector<std::string>& pre, const std::vector<std::string>& succ)
    {
        m_requestQueue.enqueue(Request::Add{name, system, pre, succ});
    }

    void SystemManager::requestSetSystemEnabled(const std::string& name, bool enabled)
    {
        m_requestQueue.enqueue(Request::SetEnabled{name, enabled});
    }

    void SystemManager::processRequests()
    {
        while (!m_requestQueue.empty())
        {
            auto status = m_requestQueue.processNext();
            if (!status.ok()) {
                FCT::ferr << "SystemManager Error: " << status.message() << std::endl;
            }
        }
    }

    void SystemManager::logicTick()
    {
        processRequests();

        if (m_dirty)
        {
            m_systemGraph.update();
            m_dirty = false;
        }

        auto systems = m_systemGraph.order();
        for (auto* system : systems)
        {
            if (system)
            {
                system->updateLogic();
            }
        }
    }
    void SystemManager::renderTick()
    {
        auto systems = m_systemGraph.order();
        for (auto* system : systems)
        {
            if (system)
            {
                system->updateRender();
            }
        }
    }
} // namespace MQEngine
