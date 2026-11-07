#pragma once

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <atomic>
#include <tuple>
#include <utility>
#include <mutex>

namespace MQEngine {

    /// 延迟实体句柄：统一封装真实实体与延迟占位实体
    class ENGINE_API DeferredEntity {
    public:
        static constexpr uint32_t kPlaceholderBit = 0x80000000u;
        static constexpr uint32_t kEntityMask     = 0x7FFFFFFFu;

        constexpr DeferredEntity() noexcept
            : m_id(static_cast<uint32_t>(entt::null)), m_registry(nullptr), m_isPlaceholder(false) {}

        DeferredEntity(entt::entity entity, entt::registry* registry = nullptr) noexcept
            : m_id(static_cast<uint32_t>(entity)), m_registry(registry), m_isPlaceholder(false) {}

        explicit DeferredEntity(uint32_t placeholderId, entt::registry* registry) noexcept
            : m_id(placeholderId | kPlaceholderBit), m_registry(registry), m_isPlaceholder(true) {}

        [[nodiscard]] bool isPlaceholder() const noexcept { return m_isPlaceholder; }
        [[nodiscard]] bool isDeferred() const noexcept { return m_isPlaceholder; }
        [[nodiscard]] bool isValid() const noexcept { return m_id != static_cast<uint32_t>(entt::null); }
        [[nodiscard]] uint32_t placeholderIndex() const noexcept { return m_id & kEntityMask; }
        [[nodiscard]] uint32_t rawId() const noexcept { return m_id; }
        [[nodiscard]] entt::entity toEntity() const noexcept {
            return m_isPlaceholder ? entt::null : static_cast<entt::entity>(m_id);
        }
        [[nodiscard]] entt::entity raw() const noexcept { return toEntity(); }
        [[nodiscard]] entt::registry* registry() const noexcept { return m_registry; }

        void setRegistry(entt::registry* reg) noexcept { m_registry = reg; }

        explicit operator entt::entity() const noexcept { return toEntity(); }
        operator uint32_t() const noexcept { return m_id; }

        bool operator==(const DeferredEntity& other) const noexcept {
            return m_id == other.m_id && m_registry == other.m_registry;
        }
        bool operator!=(const DeferredEntity& other) const noexcept {
            return !(*this == other);
        }

    private:
        uint32_t m_id;
        entt::registry* m_registry;
        bool m_isPlaceholder;
    };

    /// 回放执行上下文
    struct PlaybackContext {
        entt::registry* defaultRegistry{nullptr};
        std::unordered_map<uint32_t, entt::entity> tempToReal;

        entt::entity resolve(const DeferredEntity& deferred, entt::registry* reg) const {
            if (!deferred.isValid()) {
                return entt::null;
            }
            if (!deferred.isPlaceholder()) {
                return deferred.toEntity();
            }
            auto it = tempToReal.find(deferred.placeholderIndex());
            if (it != tempToReal.end()) {
                return it->second;
            }
            return entt::null;
        }
    };

    /// 延迟指令接口基类
    struct IEntityCommand {
        IEntityCommand* nextQueued{nullptr};
        virtual ~IEntityCommand() = default;
        virtual void execute(PlaybackContext& context) = 0;
    };

    /// 实体指令缓冲区：支持无锁本地录制与确定性批量回放
    class ENGINE_API EntityCommandBuffer {
    public:
        EntityCommandBuffer();
        ~EntityCommandBuffer() = default;

        EntityCommandBuffer(EntityCommandBuffer&& other) noexcept;
        EntityCommandBuffer& operator=(EntityCommandBuffer&& other) noexcept;

        EntityCommandBuffer(const EntityCommandBuffer&) = delete;
        EntityCommandBuffer& operator=(const EntityCommandBuffer&) = delete;

        DeferredEntity createEntity(entt::registry* registry = nullptr,
                                    std::function<void(entt::entity)> onCreated = nullptr);

        void destroyEntity(DeferredEntity entity, bool destroyHierarchy = true);
        void destroyEntity(entt::registry* registry, DeferredEntity entity, bool destroyHierarchy = true);

        template <typename T, typename... Args>
        void emplace(DeferredEntity entity, Args&&... args);

        template <typename T, typename... Args>
        void emplace(entt::registry* registry, DeferredEntity entity, Args&&... args);

        template <typename T>
        void remove(DeferredEntity entity);

        template <typename T>
        void remove(entt::registry* registry, DeferredEntity entity);

        template <typename T>
        void clear(entt::registry* registry = nullptr);

        template <typename T, typename Func>
        void patch(DeferredEntity entity, Func&& func);

        template <typename T, typename Func>
        void patch(entt::registry* registry, DeferredEntity entity, Func&& func);

        void addCustomCommand(entt::registry* registry,
                              std::function<void(entt::registry&, const PlaybackContext&)> command);

        void playback(entt::registry* fallbackRegistry = nullptr);
        void playback(entt::registry& fallbackRegistry) { playback(&fallbackRegistry); }
        void drainTo(std::vector<UniquePtr<IEntityCommand>>& dest);

        [[nodiscard]] entt::entity resolve(const DeferredEntity& deferred) const;

        [[nodiscard]] bool empty() const noexcept { return m_commands.empty(); }
        [[nodiscard]] size_t size() const noexcept { return m_commands.size(); }
        [[nodiscard]] size_t pendingCommandCount() const noexcept { return m_commands.size(); }
        void reset();
        void clear() { reset(); }

        static void destroyHierarchyRecursive(entt::registry* reg, entt::entity entity);
        static uint32_t generatePlaceholderId() noexcept;

    private:
        std::vector<UniquePtr<IEntityCommand>> m_commands;
        std::unordered_map<uint32_t, entt::entity> m_lastResolvedMapping;
        static std::atomic<uint32_t> s_globalPlaceholderCounter;
    };

    namespace Internal {

        struct CreateEntityCommand : public IEntityCommand {
            uint32_t placeholderId;
            entt::registry* registry;
            std::function<void(entt::entity)> onCreated;

            CreateEntityCommand(uint32_t id, entt::registry* reg, std::function<void(entt::entity)> cb)
                : placeholderId(id & DeferredEntity::kEntityMask), registry(reg), onCreated(std::move(cb)) {}

            void execute(PlaybackContext& context) override {
                entt::registry* reg = registry ? registry : context.defaultRegistry;
                if (!reg) return;
                entt::entity concrete = reg->create();
                context.tempToReal[placeholderId] = concrete;
                if (onCreated) {
                    onCreated(concrete);
                }
            }
        };

        struct DestroyEntityCommand : public IEntityCommand {
            DeferredEntity entity;
            entt::registry* registry;
            bool destroyHierarchy;

            DestroyEntityCommand(DeferredEntity ent, entt::registry* reg, bool hierarchy)
                : entity(ent), registry(reg), destroyHierarchy(hierarchy) {}

            void execute(PlaybackContext& context) override {
                entt::registry* reg = registry ? registry : (entity.registry() ? entity.registry() : context.defaultRegistry);
                if (!reg) return;
                entt::entity target = context.resolve(entity, reg);
                if (target != entt::null && reg->valid(target)) {
                    if (destroyHierarchy) {
                        EntityCommandBuffer::destroyHierarchyRecursive(reg, target);
                    } else {
                        reg->destroy(target);
                    }
                }
                if (entity.isPlaceholder()) {
                    context.tempToReal.erase(entity.placeholderIndex());
                }
            }
        };

        struct EmplaceComponentCommand : public IEntityCommand {
            DeferredEntity entity;
            entt::registry* registry;
            std::function<void(entt::registry&, entt::entity)> emplacer;

            EmplaceComponentCommand(DeferredEntity ent, entt::registry* reg,
                                    std::function<void(entt::registry&, entt::entity)> fn)
                : entity(ent), registry(reg), emplacer(std::move(fn)) {}

            void execute(PlaybackContext& context) override {
                entt::registry* reg = registry ? registry : (entity.registry() ? entity.registry() : context.defaultRegistry);
                if (!reg) return;
                entt::entity target = context.resolve(entity, reg);
                if (target != entt::null && reg->valid(target)) {
                    emplacer(*reg, target);
                }
            }
        };

        struct RemoveComponentCommand : public IEntityCommand {
            DeferredEntity entity;
            entt::registry* registry;
            std::function<void(entt::registry&, entt::entity)> remover;

            RemoveComponentCommand(DeferredEntity ent, entt::registry* reg,
                                   std::function<void(entt::registry&, entt::entity)> fn)
                : entity(ent), registry(reg), remover(std::move(fn)) {}

            void execute(PlaybackContext& context) override {
                entt::registry* reg = registry ? registry : (entity.registry() ? entity.registry() : context.defaultRegistry);
                if (!reg) return;
                entt::entity target = context.resolve(entity, reg);
                if (target != entt::null && reg->valid(target)) {
                    remover(*reg, target);
                }
            }
        };

        struct ClearComponentCommand : public IEntityCommand {
            entt::registry* registry;
            std::function<void(entt::registry&)> clearer;

            ClearComponentCommand(entt::registry* reg, std::function<void(entt::registry&)> fn)
                : registry(reg), clearer(std::move(fn)) {}

            void execute(PlaybackContext& context) override {
                entt::registry* reg = registry ? registry : context.defaultRegistry;
                if (!reg) return;
                clearer(*reg);
            }
        };

        struct PatchComponentCommand : public IEntityCommand {
            DeferredEntity entity;
            entt::registry* registry;
            std::function<void(entt::registry&, entt::entity)> patcher;

            PatchComponentCommand(DeferredEntity ent, entt::registry* reg,
                                  std::function<void(entt::registry&, entt::entity)> fn)
                : entity(ent), registry(reg), patcher(std::move(fn)) {}

            void execute(PlaybackContext& context) override {
                entt::registry* reg = registry ? registry : (entity.registry() ? entity.registry() : context.defaultRegistry);
                if (!reg) return;
                entt::entity target = context.resolve(entity, reg);
                if (target != entt::null && reg->valid(target)) {
                    patcher(*reg, target);
                }
            }
        };

        struct CustomCommand : public IEntityCommand {
            entt::registry* registry;
            std::function<void(entt::registry&, const PlaybackContext&)> command;

            CustomCommand(entt::registry* reg, std::function<void(entt::registry&, const PlaybackContext&)> fn)
                : registry(reg), command(std::move(fn)) {}

            void execute(PlaybackContext& context) override {
                entt::registry* reg = registry ? registry : context.defaultRegistry;
                if (!reg) return;
                command(*reg, context);
            }
        };

    } // namespace Internal

    template <typename T, typename... Args>
    void EntityCommandBuffer::emplace(DeferredEntity entity, Args&&... args) {
        emplace<T>(entity.registry(), entity, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    void EntityCommandBuffer::emplace(entt::registry* registry, DeferredEntity entity, Args&&... args) {
        auto argsTuple = std::make_tuple(std::forward<Args>(args)...);
        auto emplacer = [argsTuple = std::move(argsTuple)](entt::registry& reg, entt::entity ent) mutable {
            std::apply([&](auto&&... unpacked) {
                reg.template emplace_or_replace<T>(ent, std::forward<decltype(unpacked)>(unpacked)...);
            }, std::move(argsTuple));
        };
        m_commands.push_back(makeUnique<Internal::EmplaceComponentCommand>(entity, registry, std::move(emplacer)));
    }

    template <typename T>
    void EntityCommandBuffer::remove(DeferredEntity entity) {
        remove<T>(entity.registry(), entity);
    }

    template <typename T>
    void EntityCommandBuffer::remove(entt::registry* registry, DeferredEntity entity) {
        auto remover = [](entt::registry& reg, entt::entity ent) {
            if (reg.template all_of<T>(ent)) {
                reg.template remove<T>(ent);
            }
        };
        m_commands.push_back(makeUnique<Internal::RemoveComponentCommand>(entity, registry, std::move(remover)));
    }

    template <typename T>
    void EntityCommandBuffer::clear(entt::registry* registry) {
        auto clearer = [](entt::registry& reg) {
            reg.template clear<T>();
        };
        m_commands.push_back(makeUnique<Internal::ClearComponentCommand>(registry, std::move(clearer)));
    }

    template <typename T, typename Func>
    void EntityCommandBuffer::patch(DeferredEntity entity, Func&& func) {
        patch<T>(entity.registry(), entity, std::forward<Func>(func));
    }

    template <typename T, typename Func>
    void EntityCommandBuffer::patch(entt::registry* registry, DeferredEntity entity, Func&& func) {
        auto patcher = [func = std::forward<Func>(func)](entt::registry& reg, entt::entity ent) {
            auto& comp = reg.template get_or_emplace<T>(ent);
            func(comp);
        };
        m_commands.push_back(makeUnique<Internal::PatchComponentCommand>(entity, registry, std::move(patcher)));
    }

} // namespace MQEngine
