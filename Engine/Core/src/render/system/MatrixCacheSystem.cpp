//
// Created by Administrator on 2025/1/21.
//

#include "./MatrixCacheSystem.h"
#include <set>
#include <unordered_map>

#include "../../scene/manager/RegistriesManager.h"

namespace MQEngine {
    namespace {
        constexpr int kMaxSceneGraphDepth = 32;
    }

    MatrixCacheSystem::MatrixCacheSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager), m_defaultModelUniform(ctx, ModelUniformSlot)
    {
        m_defaultModelUniform.setValue("modelMatrix", FCT::Mat4());
        m_defaultModelUniform.setValue("modelInverseTransposeMatrix", FCT::Mat4());
        m_defaultModelUniform.update();
    }

    void MatrixCacheSystem::updateLogic()
    {
        ZoneScoped();
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            if (!registry) {
                continue;
            }
            syncHierarchyLinks(registry);
            cleanupCacheComponents(registry);
            
            std::set<entt::entity> entitiesToProcess;
            collectTransformEntities(registry, entitiesToProcess);

            for (auto entity : entitiesToProcess)
            {
                processEntity(registry, entity);
            }
        }
    }

    void MatrixCacheSystem::syncHierarchyLinks(entt::registry* registry)
    {
        std::unordered_map<std::string, entt::entity> uuidToEntity;
        auto nameView = registry->view<NameTag>();
        for (auto entity : nameView)
        {
            const auto& tag = nameView.get<NameTag>(entity);
            if (!tag.uuid.empty())
            {
                uuidToEntity[tag.uuid] = entity;
            }
        }

        auto hierarchyView = registry->view<HierarchyComponent>();
        for (auto entity : hierarchyView)
        {
            auto& hierarchy = hierarchyView.get<HierarchyComponent>(entity);
            entt::entity currentParent = static_cast<entt::entity>(hierarchy.parentEntity);

            if (!hierarchy.parentUuid.empty())
            {
                auto it = uuidToEntity.find(hierarchy.parentUuid);
                if (it != uuidToEntity.end() && it->second != entity)
                {
                    hierarchy.parentEntity = static_cast<uint32_t>(it->second);
                }
                else if (currentParent == entt::null || !registry->valid(currentParent))
                {
                    hierarchy.parentEntity = static_cast<uint32_t>(entt::null);
                }
            }
            else if (currentParent != entt::null && registry->valid(currentParent))
            {
                if (const auto* parentTag = registry->try_get<NameTag>(currentParent))
                {
                    hierarchy.parentUuid = parentTag->uuid;
                }
            }
            else
            {
                hierarchy.parentEntity = static_cast<uint32_t>(entt::null);
            }
        }
    }

    void MatrixCacheSystem::collectTransformEntities(entt::registry* registry, std::set<entt::entity>& outEntities)
    {
        for (auto entity : registry->view<PositionComponent>())
        {
            outEntities.insert(entity);
        }
        for (auto entity : registry->view<RotationComponent>())
        {
            outEntities.insert(entity);
        }
        for (auto entity : registry->view<ScaleComponent>())
        {
            outEntities.insert(entity);
        }
        for (auto entity : registry->view<HierarchyComponent>())
        {
            outEntities.insert(entity);
        }
    }

    entt::entity MatrixCacheSystem::resolveParentEntity(entt::registry* registry, entt::entity entity)
    {
        if (!registry || !registry->valid(entity))
        {
            return entt::null;
        }
        const auto* hierarchy = registry->try_get<HierarchyComponent>(entity);
        if (!hierarchy)
        {
            return entt::null;
        }
        entt::entity parent = static_cast<entt::entity>(hierarchy->parentEntity);
        if (parent != entt::null && parent != entity && registry->valid(parent))
        {
            return parent;
        }
        return entt::null;
    }

    FCT::Mat4 MatrixCacheSystem::computeEntityLocalMatrix(entt::registry* registry, entt::entity entity)
    {
        if (!registry || !registry->valid(entity))
        {
            return FCT::Mat4();
        }
        const PositionComponent* position = registry->try_get<PositionComponent>(entity);
        const RotationComponent* rotation = registry->try_get<RotationComponent>(entity);
        const ScaleComponent* scale = registry->try_get<ScaleComponent>(entity);

        PositionComponent defaultPosition{{0.0f, 0.0f, 0.0f}};
        RotationComponent defaultRotation{{0.0f, 0.0f, 0.0f}};
        ScaleComponent defaultScale{{1.0f, 1.0f, 1.0f}};

        return calculateModelMatrix(
            position ? *position : defaultPosition,
            rotation ? *rotation : defaultRotation,
            scale ? *scale : defaultScale);
    }

    FCT::Mat4 MatrixCacheSystem::computeWorldMatrixRecursive(entt::registry* registry, entt::entity entity, int depth)
    {
        if (!registry || !registry->valid(entity) || depth > kMaxSceneGraphDepth)
        {
            return FCT::Mat4();
        }
        FCT::Mat4 localMat = computeEntityLocalMatrix(registry, entity);
        entt::entity parent = resolveParentEntity(registry, entity);
        if (parent != entt::null)
        {
            return computeWorldMatrixRecursive(registry, parent, depth + 1) * localMat;
        }
        return localMat;
    }

    FCT::Mat4 MatrixCacheSystem::computeWorldRotationRecursive(entt::registry* registry, entt::entity entity, int depth)
    {
        if (!registry || !registry->valid(entity) || depth > kMaxSceneGraphDepth)
        {
            return FCT::Mat4();
        }
        const RotationComponent* rotation = registry->try_get<RotationComponent>(entity);
        RotationComponent defaultRotation{{0.0f, 0.0f, 0.0f}};
        FCT::Mat4 localRot = calculateRotationMatrix(rotation ? *rotation : defaultRotation);

        entt::entity parent = resolveParentEntity(registry, entity);
        if (parent != entt::null)
        {
            return computeWorldRotationRecursive(registry, parent, depth + 1) * localRot;
        }
        return localRot;
    }

    FCT::Mat4 MatrixCacheSystem::computeEntityWorldMatrix(entt::registry* registry, entt::entity entity)
    {
        return computeWorldMatrixRecursive(registry, entity, 0);
    }

    FCT::Mat4 MatrixCacheSystem::computeEntityWorldRotationMatrix(entt::registry* registry, entt::entity entity)
    {
        return computeWorldRotationRecursive(registry, entity, 0);
    }

    FCT::Vec3<> MatrixCacheSystem::computeEntityWorldPosition(entt::registry* registry, entt::entity entity)
    {
        FCT::Mat4 worldMat = computeEntityWorldMatrix(registry, entity);
        FCT::Vec4 origin(0.0f, 0.0f, 0.0f, 1.0f);
        FCT::Vec4 worldPos = worldMat * origin;
        return FCT::Vec3<>(worldPos.x, worldPos.y, worldPos.z);
    }

    void MatrixCacheSystem::processEntity(entt::registry* registry, entt::entity entity)
    {
        const RotationComponent* rotation = registry->try_get<RotationComponent>(entity);
        const HierarchyComponent* hierarchy = registry->try_get<HierarchyComponent>(entity);
        RotationComponent defaultRotation{{0.0f, 0.0f, 0.0f}};
        const RotationComponent& rot = rotation ? *rotation : defaultRotation;

        if (rotation || hierarchy)
        {
            CacheRotationMatrix* cacheRotation = registry->try_get<CacheRotationMatrix>(entity);
            if (!cacheRotation)
            {
                g_engineGlobal.registriesManager->requestEmplaceComponent<CacheRotationMatrix>(registry, entity);
            }
            else
            {
                cacheRotation->rotationMatrix = calculateRotationMatrix(rot);
                cacheRotation->worldRotationMatrix = computeEntityWorldRotationMatrix(registry, entity);
            }
        }

        CacheModelMatrix* cacheModel = registry->try_get<CacheModelMatrix>(entity);
        if (!cacheModel)
        {
            g_engineGlobal.registriesManager->requestEmplaceComponent<CacheModelMatrix>(registry, entity, m_ctx);
        }
        else
        {
            FCT::Mat4 localMatrix = computeEntityLocalMatrix(registry, entity);
            FCT::Mat4 worldMatrix = computeEntityWorldMatrix(registry, entity);
            cacheModel->localMatrix = localMatrix;
            cacheModel->worldMatrix = worldMatrix;
            cacheModel->uniform->setValue(FCT::UniformType::ModelMatrix, worldMatrix);
            cacheModel->uniform->setValue("modelInverseTransposeMatrix", calculateModelInverseTransposeMatrix(worldMatrix));
            cacheModel->init = true;
        }
    }

    FCT::Mat4 MatrixCacheSystem::calculateModelInverseTransposeMatrix(const FCT::Mat4& modelMatrix)
    {
        FCT::Mat4 inverseMatrix = modelMatrix.inverse();
        FCT::Mat4 inverseTransposeMatrix = inverseMatrix.transpose();

        return inverseTransposeMatrix;
    }

    void MatrixCacheSystem::updateRender()
    {
        ZoneScoped();

        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            registry->view<CacheModelMatrix>().each([](CacheModelMatrix& cacheModel)
            {
                if (cacheModel.init)
                {
                    cacheModel.uniform->update();
                }
            });
        }
    }

    FCT::Mat4 MatrixCacheSystem::calculateRotationMatrix(const RotationComponent& rotation)
    {
        FCT::Mat4 rotX = FCT::Mat4::RotateX(rotation.rotation.x);
        FCT::Mat4 rotY = FCT::Mat4::RotateY(rotation.rotation.y);
        FCT::Mat4 rotZ = FCT::Mat4::RotateZ(rotation.rotation.z);
        return rotX * rotY * rotZ;
    }

    FCT::Mat4 MatrixCacheSystem::calculateModelMatrix(const PositionComponent& position, const RotationComponent& rotation, const ScaleComponent& scale)
    {
        FCT::Mat4 translationMatrix = FCT::Mat4::Translate(position.position.x, position.position.y, position.position.z);
        
        FCT::Mat4 rotX = FCT::Mat4::RotateX(rotation.rotation.x);
        FCT::Mat4 rotY = FCT::Mat4::RotateY(rotation.rotation.y);
        FCT::Mat4 rotZ = FCT::Mat4::RotateZ(rotation.rotation.z);
        FCT::Mat4 rotationMatrix = rotX * rotY * rotZ;
        
        FCT::Mat4 scaleMatrix = FCT::Mat4::Scale(scale.scale.x, scale.scale.y, scale.scale.z);
        
        return translationMatrix * rotationMatrix * scaleMatrix;
    }

    void MatrixCacheSystem::bindModelMatrix(entt::registry* registry, entt::entity entity, FCT::Layout* layout)
    {
        if (!registry)
        {
            layout->bindUniform(m_defaultModelUniform);
            return;
        }

        const CacheModelMatrix* cacheMatrix = registry->try_get<CacheModelMatrix>(entity);
        if (cacheMatrix && cacheMatrix->init)
        {
            layout->bindUniform(cacheMatrix->uniform);
            return;
        }

        if (const auto* subTag = registry->try_get<RuntimeSubEntityTag>(entity))
        {
            entt::entity owner = static_cast<entt::entity>(subTag->ownerEntity);
            if (owner != entt::null && registry->valid(owner))
            {
                const CacheModelMatrix* ownerCache = registry->try_get<CacheModelMatrix>(owner);
                if (ownerCache && ownerCache->init)
                {
                    layout->bindUniform(ownerCache->uniform);
                    return;
                }
            }
        }

        layout->bindUniform(m_defaultModelUniform);
    }

    void MatrixCacheSystem::cleanupCacheComponents(entt::registry* registry)
    {
        auto rotationCacheView = registry->view<CacheRotationMatrix>();
        for (auto entity : rotationCacheView)
        {
            if (!registry->all_of<RotationComponent>(entity) && !registry->all_of<HierarchyComponent>(entity))
            {
                g_engineGlobal.registriesManager->requestRemoveComponent<CacheRotationMatrix>(registry, entity);
            }
        }
        
        auto modelCacheView = registry->view<CacheModelMatrix>();
        for (auto entity : modelCacheView)
        {
            bool hasPosition = registry->all_of<PositionComponent>(entity);
            bool hasRotation = registry->all_of<RotationComponent>(entity);
            bool hasScale = registry->all_of<ScaleComponent>(entity);
            bool hasHierarchy = registry->all_of<HierarchyComponent>(entity);
            
            if (!hasPosition && !hasRotation && !hasScale && !hasHierarchy)
            {
                g_engineGlobal.registriesManager->requestRemoveComponent<CacheModelMatrix>(registry, entity);
            }
        }
    }
}
