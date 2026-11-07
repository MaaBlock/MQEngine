/**
* @file systemmanager.cpp
* @brief class source for start and stop system
*/
#include "../../engineapi.h"
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

        m_requestQueue.subscribe<Request::Remove>([this](Request::Remove& req) {
            auto it = m_configs.find(req.name);
            if (it != m_configs.end()) {
                if (it->second.enabled) {
                    m_systemGraph.removeNode(req.name);
                    if (it->second.system) {
                        it->second.system->onDeactivate();
                    }
                    m_dirty = true;
                }
                m_configs.erase(it);
            }
            return OkStatus();
        });
    }

    void SystemManager::init()
    {
        FCT::JS::NodeCommon::Init();
    }

    void SystemManager::term()
    {
        spdlog::info("[SystemManager::term] 开始调用 NodeCommon::Term");
        FCT::JS::NodeCommon::Term();
        spdlog::info("[SystemManager::term] NodeCommon::Term 完毕");
    }

    void SystemManager::requestAddSystem(const std::string& name, ISystem* system, const std::vector<std::string>& pre, const std::vector<std::string>& succ)
    {
        std::lock_guard<std::mutex> lock(m_graphMutex);
        m_registeredSystems[name] = system;
        m_requestQueue.enqueue(Request::Add{name, system, pre, succ});
    }

    void SystemManager::requestRemoveSystem(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_graphMutex);
        m_registeredSystems.erase(name);
        m_requestQueue.enqueue(Request::Remove{name});
    }

    ISystem* SystemManager::findSystem(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(m_graphMutex);
        auto it = m_registeredSystems.find(name);
        return it != m_registeredSystems.end() ? it->second : nullptr;
    }

    std::vector<std::string> SystemManager::getRegisteredSystemNames() const
    {
        std::lock_guard<std::mutex> lock(m_graphMutex);
        std::vector<std::string> names;
        names.reserve(m_registeredSystems.size());
        for (const auto& pair : m_registeredSystems) {
            names.push_back(pair.first);
        }
        return names;
    }

    void SystemManager::requestSetSystemEnabled(const std::string& name, bool enabled)
    {
        std::lock_guard<std::mutex> lock(m_graphMutex);
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
        if (m_dirty)
        {
            m_systemGraph.update();
            m_dirty = false;
        }
    }

    void SystemManager::flushRequests()
    {
        std::lock_guard<std::mutex> lock(m_graphMutex);
        processRequests();
    }

    void SystemManager::logicTick()
    {
        ZoneScoped();
        std::vector<ISystem*> systems;
        {
            std::lock_guard<std::mutex> lock(m_graphMutex);
            processRequests();
            systems = m_systemGraph.order();
        }

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
        ZoneScoped();
        std::lock_guard<std::mutex> lock(m_graphMutex);
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
