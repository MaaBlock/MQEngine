//
// Created by MaaBlock on 2025/11/3.
//

#include "RegistriesManager.h"
#include "../data/Camera.h"
#include "../data/Component.h"
namespace MQEngine {
    struct CacheModelMatrix;
    struct CacheRotationMatrix;
    RegistriesManager::RegistriesManager()
    {
        m_requestQueue.subscribe<Request::save>([](Request::save& request)
        {

            return OkStatus();
        });
        m_requestQueue.subscribe<Request::add>(
            [this](Request::add& request)
        {
            try
            {
                if (entt::registry* registry = request.future.get())
                {
                    m_registries.push_back(registry);
                    return OkStatus();
                }

                return InvalidArgumentError("添加 Registries 失败：异步任务返回了一个空指针。");
            }
            catch (const std::exception& e)
            {
                return InternalError(StrCat("添加 Registries 失败，异步任务执行时发生异常: ", e.what()));
            }
            catch (...)
            {
                return InternalError("添加 Registries 失败，异步任务执行时发生未知的非标准异常。");
            }
        });

        m_requestQueue.subscribe<Request::remove>(
            [this](Request::remove& request)
            {
                auto it = std::find(m_registries.begin(), m_registries.end(), request.registry);
                if (it != m_registries.end())
                {
                    m_registries.erase(it);
                    return OkStatus();
                }
                return InvalidArgumentError("移除注册表失败：未找到指定的注册表。");
            });

        m_requestQueue.subscribe<Request::Emplace>(
            [](Request::Emplace& request)
            {
                if (request.registry) {
                    request.emplacer(*request.registry, request.entity);
                    return OkStatus();
                }
                return InvalidArgumentError("添加组件失败：registry 为空。");
            });

        m_requestQueue.subscribe<Request::RemoveComponent>(
            [](Request::RemoveComponent& request)
            {
                if (request.registry) {
                    request.remover(*request.registry, request.entity);
                    return OkStatus();
                }
                return InvalidArgumentError("移除组件失败：registry 为空。");
            });
    }
    void RegistriesManager::requestSaveRegistries(std::string path, entt::registry* registry)
    {
        m_requestQueue.enqueue(Request::save{
            path,
            registry
        });
    }
    void RegistriesManager::requestAddRegistries(boost::unique_future<entt::registry*> future)
    {
        m_requestQueue.enqueue(Request::add{
            std::move(future)
        });
    }
    void RegistriesManager::requestRemoveRegistries(entt::registry* registry)
    {
        m_requestQueue.enqueue(Request::remove{
            registry
        });
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
    void RegistriesManager::syncTicker()
    {
        while (!m_requestQueue.empty())
        {
            auto status = m_requestQueue.processNext();
            if (!status.ok())
                FCT::ferr << status.message() << std::endl;
        }
    }
    std::vector<entt::registry*> RegistriesManager::currentRegistries() const { return m_registries; }
    entt::registry* RegistriesManager::createRegistry()
    {
        auto registry = FCT_NEW(entt::registry);

        [&]<typename... Components>(std::tuple<Components...>)
        {
            (registry->storage<Components>(), ...);

        }(AllComponentsList{});

        return registry;
    }
} // MQEngine