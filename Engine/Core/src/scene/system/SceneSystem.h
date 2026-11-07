#ifndef SCENESYSTEM_H
#define SCENESYSTEM_H
#include "../../runtime/system/ISystem.h"
namespace MQEngine {
    /**
     * @brief 场景图系统与扩展反射组件（SceneGraph Hierarchy、插件组件等）序列化工具
     */
    class ENGINE_API SceneSystem : public ISystem {
    public:
        void updateLogic() override;
        void updateRender() override;

        static void purgeRuntimeSubEntities(entt::registry& registry);
        static void saveRegistryExtBinary(entt::registry& registry, boost::archive::binary_oarchive& archive);
        static bool loadRegistryExtBinary(entt::registry& registry, boost::archive::binary_iarchive& archive);
        static bool hasExtBinarySection(const std::string& rawBytes) noexcept;
        static bool recoverRegistryFromExtSection(entt::registry& registry, const std::string& rawBytes);
        static void resolveHierarchyParentLinks(entt::registry& registry);
    };

} // MQEngine

#endif //SCENESYSTEM_H
