//
// Created by MaaBlock on 2025/11/3.
//

#include "RegistriesManager.h"
#include "../../render/data/Camera.h"
#include "../data/Component.h"
#include "../system/ComponentReflection.h"

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
                    storage(registry);
                    if (std::find(m_registries.begin(), m_registries.end(), registry) == m_registries.end())
                    {
                        m_registries.push_back(registry);
                    }
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
                    pruneDeadEntities();
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

        m_requestQueue.subscribe<Request::ClearComponent>(
            [](Request::ClearComponent& request)
            {
                if (request.registry) {
                    request.clearer(*request.registry);
                    return OkStatus();
                }
                return InvalidArgumentError("清空组件失败：registry 为空。");
            });

        m_requestQueue.subscribe<Request::Patch>(
            [](Request::Patch& request)
            {
                if (request.registry) {
                    request.patcher(*request.registry, request.entity);
                    return OkStatus();
                }
                return InvalidArgumentError("修改组件失败：registry 为空。");
            });
    }

    RegistriesManager::~RegistriesManager()
    {
        (void)drainPendingCommands();
    }

    void RegistriesManager::enqueueCommand(UniquePtr<IEntityCommand> cmd)
    {
        if (!cmd) {
            return;
        }
        IEntityCommand* raw = cmd.release();
        IEntityCommand* oldHead = m_commandHead.load(std::memory_order_relaxed);
        do {
            raw->nextQueued = oldHead;
        } while (!m_commandHead.compare_exchange_weak(
            oldHead, raw, std::memory_order_release, std::memory_order_relaxed));
    }

    std::vector<UniquePtr<IEntityCommand>> RegistriesManager::drainPendingCommands()
    {
        IEntityCommand* head = m_commandHead.exchange(nullptr, std::memory_order_acq_rel);
        if (!head) {
            return {};
        }
        IEntityCommand* reversed = nullptr;
        size_t count = 0;
        while (head) {
            IEntityCommand* next = head->nextQueued;
            head->nextQueued = reversed;
            reversed = head;
            head = next;
            ++count;
        }
        std::vector<UniquePtr<IEntityCommand>> result;
        result.reserve(count);
        while (reversed) {
            IEntityCommand* next = reversed->nextQueued;
            reversed->nextQueued = nullptr;
            result.emplace_back(reversed);
            reversed = next;
        }
        return result;
    }

    void RegistriesManager::publishResolvedMapping(std::unordered_map<uint32_t, entt::entity>&& nextMap)
    {
        uint32_t curIdx = m_activeMappingIdx.load(std::memory_order_acquire);
        uint32_t nextIdx = 1u - (curIdx & 1u);
        m_resolvedMappings[nextIdx] = std::move(nextMap);
        m_activeMappingIdx.store(nextIdx, std::memory_order_release);
        m_resolvedMappings[curIdx & 1u] = m_resolvedMappings[nextIdx];
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
    void RegistriesManager::removeRegistryImmediately(entt::registry* registry)
    {
        auto it = std::find(m_registries.begin(), m_registries.end(), registry);
        if (it != m_registries.end()) {
            m_registries.erase(it);
        }
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
    DeferredEntity RegistriesManager::createEntityDeferred(entt::registry* registry,
                                                          std::function<void(entt::entity)> onCreated)
    {
        uint32_t placeholderId = EntityCommandBuffer::generatePlaceholderId();
        DeferredEntity deferred(placeholderId, registry);
        enqueueCommand(makeUnique<Internal::CreateEntityCommand>(placeholderId, registry, std::move(onCreated)));
        return deferred;
    }

    void RegistriesManager::destroyEntityDeferred(DeferredEntity entity, bool destroyHierarchy)
    {
        destroyEntityDeferred(entity.registry(), entity, destroyHierarchy);
    }

    void RegistriesManager::destroyEntityDeferred(entt::registry* registry, DeferredEntity entity, bool destroyHierarchy)
    {
        enqueueCommand(makeUnique<Internal::DestroyEntityCommand>(entity, registry, destroyHierarchy));
    }

    void RegistriesManager::submitCommandBuffer(EntityCommandBuffer buffer)
    {
        std::vector<UniquePtr<IEntityCommand>> cmds;
        buffer.drainTo(cmds);
        for (auto& cmd : cmds) {
            enqueueCommand(std::move(cmd));
        }
    }

    void RegistriesManager::submitCommandBuffer(UniquePtr<EntityCommandBuffer> buffer)
    {
        if (buffer) {
            std::vector<UniquePtr<IEntityCommand>> cmds;
            buffer->drainTo(cmds);
            for (auto& cmd : cmds) {
                enqueueCommand(std::move(cmd));
            }
        }
    }

    void RegistriesManager::syncTicker()
    {
        ZoneScoped();
        while (!m_requestQueue.empty())
        {
            auto status = m_requestQueue.processNext();
            if (!status.ok())
                FCT::ferr << status.message() << std::endl;
        }

        std::vector<UniquePtr<IEntityCommand>> commandsToExecute = drainPendingCommands();
        if (commandsToExecute.empty()) {
            return;
        }

        PlaybackContext context;
        uint32_t activeIdx = m_activeMappingIdx.load(std::memory_order_acquire) & 1u;
        context.tempToReal = m_resolvedMappings[activeIdx];
        context.defaultRegistry = !m_registries.empty() ? m_registries.front() : nullptr;

        for (auto& cmd : commandsToExecute) {
            if (cmd) {
                cmd->execute(context);
            }
        }

        // 清理已销毁实体的映射条目（Tier 2 批量垃圾回收）
        if (!context.tempToReal.empty() && (!m_registries.empty() || context.defaultRegistry != nullptr)) {
            for (auto it = context.tempToReal.begin(); it != context.tempToReal.end(); ) {
                entt::entity ent = it->second;
                bool isAlive = false;
                if (!m_registries.empty()) {
                    for (auto* reg : m_registries) {
                        if (reg && reg->valid(ent)) {
                            isAlive = true;
                            break;
                        }
                    }
                } else if (context.defaultRegistry) {
                    isAlive = context.defaultRegistry->valid(ent);
                }

                if (!isAlive) {
                    it = context.tempToReal.erase(it);
                } else {
                    ++it;
                }
            }
        }

        publishResolvedMapping(std::move(context.tempToReal));
    }

    entt::entity RegistriesManager::resolve(const DeferredEntity& deferred) const
    {
        if (!deferred.isValid()) return entt::null;
        if (!deferred.isPlaceholder()) return deferred.toEntity();
        uint32_t activeIdx = m_activeMappingIdx.load(std::memory_order_acquire) & 1u;
        const auto& activeMap = m_resolvedMappings[activeIdx];
        auto it = activeMap.find(deferred.placeholderIndex());
        if (it != activeMap.end()) return it->second;
        return entt::null;
    }

    void RegistriesManager::clearResolvedMappings()
    {
        publishResolvedMapping({});
    }

    void RegistriesManager::pruneDeadEntities()
    {
        uint32_t activeIdx = m_activeMappingIdx.load(std::memory_order_acquire) & 1u;
        auto nextMap = m_resolvedMappings[activeIdx];
        if (nextMap.empty() || m_registries.empty()) return;
        for (auto it = nextMap.begin(); it != nextMap.end(); ) {
            entt::entity ent = it->second;
            bool isAlive = false;
            for (auto* reg : m_registries) {
                if (reg && reg->valid(ent)) {
                    isAlive = true;
                    break;
                }
            }
            if (!isAlive) {
                it = nextMap.erase(it);
            } else {
                ++it;
            }
        }
        publishResolvedMapping(std::move(nextMap));
    }

    size_t RegistriesManager::resolvedMappingCount() const
    {
        uint32_t activeIdx = m_activeMappingIdx.load(std::memory_order_acquire) & 1u;
        return m_resolvedMappings[activeIdx].size();
    }

    std::vector<entt::registry*> RegistriesManager::currentRegistries() const { return m_registries; }

    entt::registry* RegistriesManager::createRegistry()
    {
        ZoneScoped();
        auto registry = FCT_NEW(entt::registry);
        storage(registry);
        return registry;
    }

    void RegistriesManager::storage(entt::registry* registry)
    {
        [&]<typename... Components>(std::tuple<Components...>)
        {
            (registry->storage<Components>(), ...);
        }(AllComponentsList{});

        if (g_engineGlobal.componentReflection) {
            g_engineGlobal.componentReflection->warmComponentPools(*registry);
        }
    }
} // MQEngine
