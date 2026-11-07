#ifndef REGISTRIESMANAGER_HPP
#define REGISTRIESMANAGER_HPP

namespace MQEngine {

    template <typename T, typename... Args>
    void RegistriesManager::emplaceDeferred(DeferredEntity entity, Args&&... args) {
        emplaceDeferred<T>(entity.registry(), entity, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    void RegistriesManager::emplaceDeferred(entt::registry* registry, DeferredEntity entity, Args&&... args) {
        auto argsTuple = std::make_tuple(std::forward<Args>(args)...);
        auto emplacer = [argsTuple = std::move(argsTuple)](entt::registry& reg, entt::entity ent) mutable {
            std::apply([&](auto&&... unpacked) {
                reg.template emplace_or_replace<T>(ent, std::forward<decltype(unpacked)>(unpacked)...);
            }, std::move(argsTuple));
        };
        enqueueCommand(makeUnique<Internal::EmplaceComponentCommand>(entity, registry, std::move(emplacer)));
    }

    template <typename T>
    void RegistriesManager::removeDeferred(DeferredEntity entity) {
        removeDeferred<T>(entity.registry(), entity);
    }

    template <typename T>
    void RegistriesManager::removeDeferred(entt::registry* registry, DeferredEntity entity) {
        auto remover = [](entt::registry& reg, entt::entity ent) {
            if (reg.template all_of<T>(ent)) {
                reg.template remove<T>(ent);
            }
        };
        enqueueCommand(makeUnique<Internal::RemoveComponentCommand>(entity, registry, std::move(remover)));
    }

    template <typename T>
    void RegistriesManager::clearDeferred(entt::registry* registry) {
        auto clearer = [](entt::registry& reg) {
            reg.template clear<T>();
        };
        enqueueCommand(makeUnique<Internal::ClearComponentCommand>(registry, std::move(clearer)));
    }

    template <typename T, typename Func>
    void RegistriesManager::patchDeferred(DeferredEntity entity, Func&& func) {
        patchDeferred<T>(entity.registry(), entity, std::forward<Func>(func));
    }

    template <typename T, typename Func>
    void RegistriesManager::patchDeferred(entt::registry* registry, DeferredEntity entity, Func&& func) {
        auto patcher = [func = std::forward<Func>(func)](entt::registry& reg, entt::entity ent) {
            auto& comp = reg.template get_or_emplace<T>(ent);
            func(comp);
        };
        enqueueCommand(makeUnique<Internal::PatchComponentCommand>(entity, registry, std::move(patcher)));
    }

} // namespace MQEngine

#endif // REGISTRIESMANAGER_HPP
