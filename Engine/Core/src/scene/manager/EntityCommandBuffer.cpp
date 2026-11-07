#include "EntityCommandBuffer.h"
#include "../../render/data/Camera.h"
#include <unordered_set>

namespace MQEngine {

    std::atomic<uint32_t> EntityCommandBuffer::s_globalPlaceholderCounter{1};

    EntityCommandBuffer::EntityCommandBuffer() = default;

    EntityCommandBuffer::EntityCommandBuffer(EntityCommandBuffer&& other) noexcept
        : m_commands(std::move(other.m_commands)),
          m_lastResolvedMapping(std::move(other.m_lastResolvedMapping)) {
    }

    EntityCommandBuffer& EntityCommandBuffer::operator=(EntityCommandBuffer&& other) noexcept {
        if (this != &other) {
            m_commands = std::move(other.m_commands);
            m_lastResolvedMapping = std::move(other.m_lastResolvedMapping);
        }
        return *this;
    }

    uint32_t EntityCommandBuffer::generatePlaceholderId() noexcept {
        return s_globalPlaceholderCounter.fetch_add(1, std::memory_order_relaxed);
    }

    DeferredEntity EntityCommandBuffer::createEntity(entt::registry* registry,
                                                    std::function<void(entt::entity)> onCreated) {
        uint32_t placeholderId = generatePlaceholderId();
        DeferredEntity deferred(placeholderId, registry);
        m_commands.push_back(makeUnique<Internal::CreateEntityCommand>(placeholderId, registry, std::move(onCreated)));
        return deferred;
    }

    void EntityCommandBuffer::destroyEntity(DeferredEntity entity, bool destroyHierarchy) {
        destroyEntity(entity.registry(), entity, destroyHierarchy);
    }

    void EntityCommandBuffer::destroyEntity(entt::registry* registry, DeferredEntity entity, bool destroyHierarchy) {
        m_commands.push_back(makeUnique<Internal::DestroyEntityCommand>(entity, registry, destroyHierarchy));
    }

    void EntityCommandBuffer::addCustomCommand(entt::registry* registry,
                                              std::function<void(entt::registry&, const PlaybackContext&)> command) {
        m_commands.push_back(makeUnique<Internal::CustomCommand>(registry, std::move(command)));
    }

    void EntityCommandBuffer::playback(entt::registry* fallbackRegistry) {
        PlaybackContext context;
        context.defaultRegistry = fallbackRegistry;
        context.tempToReal = m_lastResolvedMapping;

        for (auto& cmd : m_commands) {
            if (cmd) {
                cmd->execute(context);
            }
        }

        // 清理已销毁实体的映射条目
        if (!context.tempToReal.empty() && fallbackRegistry) {
            for (auto it = context.tempToReal.begin(); it != context.tempToReal.end(); ) {
                if (!fallbackRegistry->valid(it->second)) {
                    it = context.tempToReal.erase(it);
                } else {
                    ++it;
                }
            }
        }

        m_lastResolvedMapping = std::move(context.tempToReal);
        m_commands.clear();
    }

    void EntityCommandBuffer::drainTo(std::vector<UniquePtr<IEntityCommand>>& dest) {
        if (dest.empty()) {
            dest = std::move(m_commands);
        } else {
            for (auto& cmd : m_commands) {
                dest.push_back(std::move(cmd));
            }
            m_commands.clear();
        }
    }

    entt::entity EntityCommandBuffer::resolve(const DeferredEntity& deferred) const {
        if (!deferred.isValid()) {
            return entt::null;
        }
        if (!deferred.isPlaceholder()) {
            return deferred.toEntity();
        }
        auto it = m_lastResolvedMapping.find(deferred.placeholderIndex());
        if (it != m_lastResolvedMapping.end()) {
            return it->second;
        }
        return entt::null;
    }

    void EntityCommandBuffer::reset() {
        m_commands.clear();
        m_lastResolvedMapping.clear();
    }

    void EntityCommandBuffer::destroyHierarchyRecursive(entt::registry* reg, entt::entity entity) {
        if (!reg || !reg->valid(entity)) {
            return;
        }

        auto view = reg->view<HierarchyComponent>();
        if (view.empty()) {
            reg->destroy(entity);
            return;
        }

        // 1. 单次线性遍历构建 parent -> children 邻接表，消除多层递归全表扫描
        std::unordered_map<entt::entity, std::vector<entt::entity>> parentToChildren;
        parentToChildren.reserve(view.size());

        for (auto candidate : view) {
            if (!reg->valid(candidate)) {
                continue;
            }
            const auto& h = view.get<HierarchyComponent>(candidate);
            auto parent = static_cast<entt::entity>(h.parentEntity);
            // 过滤自父级引用（parent == candidate）与非法父实体
            if (parent != entt::null && parent != candidate) {
                parentToChildren[parent].push_back(candidate);
            }
        }

        // 2. 快速退出路径：若当前实体没有任何子节点，直接销毁并返回
        auto itRoot = parentToChildren.find(entity);
        if (itRoot == parentToChildren.end() || itRoot->second.empty()) {
            reg->destroy(entity);
            return;
        }

        // 3. 迭代式工作表遍历，配合 visited 集合防御相互循环引用与死循环
        std::vector<entt::entity> toDestroy;
        toDestroy.reserve(view.size() + 1);

        std::unordered_set<entt::entity> visited;
        visited.reserve(view.size() + 1);

        std::vector<entt::entity> stack;
        stack.reserve(view.size() + 1);

        visited.insert(entity);
        stack.push_back(entity);

        while (!stack.empty()) {
            entt::entity current = stack.back();
            stack.pop_back();
            toDestroy.push_back(current);

            auto it = parentToChildren.find(current);
            if (it != parentToChildren.end()) {
                for (auto child : it->second) {
                    if (child == current || visited.count(child) > 0) {
                        continue;
                    }
                    visited.insert(child);
                    stack.push_back(child);
                }
            }
        }

        // 4. 逆序自底向上后序销毁（先销毁叶子节点，后销毁父节点）
        for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
            if (reg->valid(*it)) {
                reg->destroy(*it);
            }
        }
    }

} // namespace MQEngine
