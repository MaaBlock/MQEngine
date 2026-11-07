#include "ComponentReflection.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../script/system/ScriptSystem.h"
#include "../manager/DataManager.h"
#include "../core/Scene.h"
#include "../core/SceneTrunk.h"
#include "../manager/RegistriesManager.h"
#include <cassert>

namespace MQEngine {

    namespace {
        std::vector<entt::registry*> collectAllKnownRegistries() {
            std::vector<entt::registry*> regs;
            auto appendUnique = [&regs](entt::registry* reg) {
                if (reg && std::find(regs.begin(), regs.end(), reg) == regs.end()) {
                    regs.push_back(reg);
                }
            };
            if (g_engineGlobal.registriesManager) {
                for (entt::registry* reg : g_engineGlobal.registriesManager->currentRegistries()) {
                    appendUnique(reg);
                }
            }
            if (g_engineGlobal.dataManager) {
                for (entt::registry* reg : g_engineGlobal.dataManager->currentRegistries()) {
                    appendUnique(reg);
                }
                if (Scene::Scene* curScene = g_engineGlobal.dataManager->getCurrentScene()) {
                    appendUnique(curScene->getRegistry());
                    for (const auto& trunkName : curScene->getTrunkList()) {
                        if (curScene->isLoad(trunkName)) {
                            if (auto trunk = curScene->getLoadedTrunk(trunkName)) {
                                appendUnique(trunk->getRegistry());
                            }
                        }
                    }
                }
            }
            return regs;
        }
    }

    ComponentReflection::ComponentReflection() = default;

    ComponentReflection::~ComponentReflection() {
        clear();
    }

    void ComponentReflection::registerInfo() {
        g_engineGlobal.componentReflection = this;
    }

    void ComponentReflection::init() {
        clear();
        registerComponentTuple<AllComponentsList>();
        g_engineGlobal.componentReflection = this;
    }

    void ComponentReflection::clear() {
        m_componentInfos.clear();
    }

    void ComponentReflection::unregisterComponent(const std::string& componentName) {
        std::vector<entt::registry*> regs = collectAllKnownRegistries();
        if (!regs.empty()) {
            purgeComponentPools(regs, {componentName});
        }
        ComponentRegistry::instance().unregisterComponent(componentName);
        m_componentInfos.erase(componentName);
    }

    void ComponentReflection::registerScriptStructCallbacks(const std::string& resolvedName,
                                                            JsStructRegisterFn jsReg,
                                                            PyStructRegisterFn pyReg) {
        if (!g_engineGlobal.scriptSystem) {
            return;
        }
        if (jsReg && g_engineGlobal.scriptSystem->getEnginePackage()) {
            jsReg(*g_engineGlobal.scriptSystem->getEnginePackage(), resolvedName);
            if (g_engineGlobal.scriptSystem->getNodeEnvironment()) {
                g_engineGlobal.scriptSystem->getEnginePackage()->mountToEnvironment(
                    *g_engineGlobal.scriptSystem->getNodeEnvironment(), "MQEngine");
            }
        }
        if (pyReg && g_engineGlobal.scriptSystem->getPyEnginePackage()) {
            pyReg(*g_engineGlobal.scriptSystem->getPyEnginePackage(), resolvedName);
            if (g_engineGlobal.scriptSystem->getPyEnvironment()) {
                g_engineGlobal.scriptSystem->getPyEnginePackage()->mountToEnvironment(
                    *g_engineGlobal.scriptSystem->getPyEnvironment(), "MQEngine");
            }
        }
        g_engineGlobal.scriptSystem->exportNbrTypeDefinitions();
    }

    const std::unordered_map<std::string, ComponentInfo>& ComponentReflection::getComponentInfos() const {
        return m_componentInfos;
    }

    bool ComponentReflection::hasComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it == m_componentInfos.end() || !it->second.hasComponent) {
            return false;
        }
        return it->second.hasComponent(registry, entity);
    }

    ComponentValue ComponentReflection::getComponentField(entt::registry& registry, entt::entity entity,
                                                        const std::string& componentName, const std::string& fieldName) const {
        auto componentIt = m_componentInfos.find(componentName);
        assert(componentIt != m_componentInfos.end() && "ComponentReflection::getComponentField: 组件未注册");
        if (componentIt == m_componentInfos.end() || !componentIt->second.hasComponent || !componentIt->second.getComponent) {
            return ComponentValue(0);
        }

        assert(componentIt->second.hasComponent(registry, entity) && "ComponentReflection::getComponentField: 实体不包含该组件");
        if (!componentIt->second.hasComponent(registry, entity)) {
            return ComponentValue(0);
        }

        void* componentPtr = componentIt->second.getComponent(registry, entity);
        assert(componentPtr != nullptr && "ComponentReflection::getComponentField: 获取组件指针为空");
        if (!componentPtr) {
            return ComponentValue(0);
        }

        for (const auto& field : componentIt->second.fields) {
            if (field.name == fieldName) {
                return field.getter(componentPtr);
            }
        }

        assert(false && "ComponentReflection::getComponentField: 字段不存在");
        return ComponentValue(0);
    }

    void ComponentReflection::setComponentField(entt::registry& registry, entt::entity entity,
                                              const std::string& componentName, const std::string& fieldName,
                                              const ComponentValue& value) const {
        auto componentIt = m_componentInfos.find(componentName);
        assert(componentIt != m_componentInfos.end() && "ComponentReflection::setComponentField: 组件未注册");
        if (componentIt == m_componentInfos.end() || !componentIt->second.hasComponent || !componentIt->second.getComponent) {
            return;
        }

        assert(componentIt->second.hasComponent(registry, entity) && "ComponentReflection::setComponentField: 实体不包含该组件");
        if (!componentIt->second.hasComponent(registry, entity)) {
            return;
        }

        void* componentPtr = componentIt->second.getComponent(registry, entity);
        assert(componentPtr != nullptr && "ComponentReflection::setComponentField: 获取组件指针为空");
        if (!componentPtr) {
            return;
        }

        for (const auto& field : componentIt->second.fields) {
            if (field.name == fieldName) {
                field.setter(componentPtr, value);
                return;
            }
        }

        assert(false && "ComponentReflection::setComponentField: 字段不存在");
    }

    void ComponentReflection::addComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it != m_componentInfos.end() && it->second.addComponent) {
            it->second.addComponent(registry, entity);
        }
    }

    void ComponentReflection::removeComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it != m_componentInfos.end() && it->second.removeComponent) {
            it->second.removeComponent(registry, entity);
        }
    }

    std::vector<std::string> ComponentReflection::getComponentFieldNames(const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it != m_componentInfos.end()) {
            std::vector<std::string> fieldNames;
            for (const auto& field : it->second.fields) {
                fieldNames.push_back(field.name);
            }
            return fieldNames;
        }
        return {};
    }

    std::string ComponentReflection::getComponentFieldType(const std::string& componentName, const std::string& fieldName) const {
        auto componentIt = m_componentInfos.find(componentName);
        if (componentIt == m_componentInfos.end()) {
            return "";
        }

        for (const auto& field : componentIt->second.fields) {
            if (field.name == fieldName) {
                return field.type;
            }
        }

        return "";
    }

    std::vector<std::string> ComponentReflection::getRegisteredComponentNames() const {
        std::vector<std::string> componentNames;
        for (const auto& pair : m_componentInfos) {
            componentNames.push_back(pair.first);
        }
        return componentNames;
    }

    ComponentHotReloadSnapshot ComponentReflection::captureSnapshot(
        const std::vector<entt::registry*>& registries,
        const std::vector<std::string>& componentNames) const
    {
        ComponentHotReloadSnapshot snapshot;
        if (componentNames.empty()) {
            return snapshot;
        }

        for (entt::registry* reg : registries) {
            if (!reg) {
                continue;
            }
            RegistryComponentSnapshot regSnap;
            regSnap.registry = reg;

            const auto& entityStorage = reg->storage<entt::entity>();
            for (const auto& compName : componentNames) {
                const ComponentInfo* info = findComponent(compName);
                if (!info || !info->hasComponent || !info->getComponent) {
                    continue;
                }

                std::vector<EntityComponentFieldSnapshot> entitySnaps;
                for (entt::entity ent : entityStorage) {
                    if (!reg->valid(ent) || !info->hasComponent(*reg, ent)) {
                        continue;
                    }
                    void* compPtr = info->getComponent(*reg, ent);
                    if (!compPtr) {
                        continue;
                    }
                    EntityComponentFieldSnapshot entSnap;
                    entSnap.entity = ent;
                    for (const auto& field : info->fields) {
                        if (field.getter) {
                            entSnap.fields[field.name] = field.getter(compPtr);
                        }
                    }
                    entitySnaps.push_back(std::move(entSnap));
                }

                if (!entitySnaps.empty()) {
                    regSnap.components[compName] = std::move(entitySnaps);
                }
            }

            if (!regSnap.components.empty()) {
                snapshot.push_back(std::move(regSnap));
            }
        }
        return snapshot;
    }

    void ComponentReflection::purgeComponentPools(
        const std::vector<entt::registry*>& registries,
        const std::vector<std::string>& componentNames) const
    {
        for (const auto& compName : componentNames) {
            const ComponentInfo* info = findComponent(compName);
            if (!info) {
                continue;
            }
            for (entt::registry* reg : registries) {
                if (!reg) {
                    continue;
                }
                if (info->clearAndResetPool) {
                    info->clearAndResetPool(*reg);
                } else if (info->typeId != 0) {
                    reg->reset(info->typeId);
                }
            }
        }
    }

    void ComponentReflection::restoreSnapshot(const ComponentHotReloadSnapshot& snapshot) const {
        for (const auto& regSnap : snapshot) {
            entt::registry* reg = regSnap.registry;
            if (!reg) {
                continue;
            }
            for (const auto& [compName, entitySnaps] : regSnap.components) {
                const ComponentInfo* info = findComponent(compName);
                if (!info || !info->addComponent || !info->getComponent) {
                    continue;
                }
                for (const auto& entSnap : entitySnaps) {
                    if (!reg->valid(entSnap.entity)) {
                        continue;
                    }
                    info->addComponent(*reg, entSnap.entity);
                    void* compPtr = info->getComponent(*reg, entSnap.entity);
                    if (!compPtr) {
                        continue;
                    }
                    for (const auto& field : info->fields) {
                        auto fieldIt = entSnap.fields.find(field.name);
                        if (fieldIt != entSnap.fields.end() && field.setter) {
                            field.setter(compPtr, fieldIt->second);
                        }
                    }
                }
            }
        }
    }

    void ComponentReflection::warmComponentPools(entt::registry& registry) const {
        for (const auto& [compName, info] : m_componentInfos) {
            if (info.warmPool) {
                info.warmPool(registry);
            }
        }
    }

    void ComponentReflection::warmAllActiveRegistries(const ComponentInfo& info) const {
        if (!info.warmPool) {
            return;
        }
        std::vector<entt::registry*> regs = collectAllKnownRegistries();
        for (entt::registry* reg : regs) {
            if (reg) {
                info.warmPool(*reg);
            }
        }
    }

} // namespace MQEngine
