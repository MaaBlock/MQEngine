#ifndef REGISTRIESMANAGER_HPP
#define REGISTRIESMANAGER_HPP
namespace MQEngine
{
    template <typename T, typename... Args>
    void RegistriesManager::requestEmplaceComponent(entt::registry* registry, entt::entity entity, Args&&... args)
    {
        auto args_tuple = std::make_tuple(std::forward<Args>(args)...);
        m_requestQueue.enqueue(Request::Emplace{
            registry, entity, [args_tuple = std::move(args_tuple)](entt::registry& reg, entt::entity ent) mutable
            {
                std::apply([&](auto&&... unpacked_args)
                           { reg.template emplace<T>(ent, std::forward<decltype(unpacked_args)>(unpacked_args)...); },
                           std::move(args_tuple));
            }});
    }
    template <typename T>
    void RegistriesManager::requestRemoveComponent(entt::registry* registry, entt::entity entity)
    {
        m_requestQueue.enqueue(Request::RemoveComponent{registry, entity, [](entt::registry& reg, entt::entity ent)
                                                        { reg.template remove<T>(ent); }});
    }
} // namespace MQEngine
#endif // REGISTRIESMANAGER_HPP
