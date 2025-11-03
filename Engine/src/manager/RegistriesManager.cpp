//
// Created by MaaBlock on 2025/11/3.
//

#include "RegistriesManager.h"

namespace MQEngine {
    void RegistriesManager::requestSaveRegistries(std::string path, entt::registry* registry)
    {
        m_requestQueue.enqueue(Request::save{
            path,
            registry
        });
    }
    void RegistriesManager::requestAddRegistries(boost::unique_future<entt::registry> future)
    {
        m_requestQueue.enqueue(Request::add{std::move(future)});
    }
    boost::unique_future<entt::registry*> RegistriesManager::loadRegistries(std::string path,
                                                                            UniquePtr<entt::registry>& ret)
    {
        return boost::async([&ret]()
        {
            ret = FCT::makeUnique<entt::registry>();
            return ret.get();
        });
    }
    Status RegistriesManager::syncTicker()
    {
        while (!m_requestQueue.empty())
        {
            auto status = m_requestQueue.processNext();
            if (!status.ok())
                FCT::ferr << status.message() << std::endl;
        }
        return OkStatus();
    }
} // MQEngine