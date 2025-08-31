//
// Created by Administrator on 2025/1/21.
//

#include "./MatrixCacheSystem.h"
#include <set>

namespace MQEngine {
    MatrixCacheSystem::MatrixCacheSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager)
    {
    }

    void MatrixCacheSystem::update()
    {

        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            cleanupCacheComponents(registry);
            
            std::set<entt::entity> entitiesToProcess;

            auto positionView = registry->view<PositionComponent>();
            for (auto entity : positionView)
            {
                entitiesToProcess.insert(entity);
            }

            auto rotationView = registry->view<RotationComponent>();
            for (auto entity : rotationView)
            {
                entitiesToProcess.insert(entity);
            }

            auto scaleView = registry->view<ScaleComponent>();
            for (auto entity : scaleView)
            {
                entitiesToProcess.insert(entity);
            }

            for (auto entity : entitiesToProcess)
            {
                processEntity(registry, entity);
            }
        }
    }

    void MatrixCacheSystem::processEntity(entt::registry* registry, entt::entity entity)
    {
        const PositionComponent* position = registry->try_get<PositionComponent>(entity);
        const RotationComponent* rotation = registry->try_get<RotationComponent>(entity);
        const ScaleComponent* scale = registry->try_get<ScaleComponent>(entity);

        PositionComponent defaultPosition{{0.0f, 0.0f, 0.0f}};
        RotationComponent defaultRotation{{0.0f, 0.0f, 0.0f}};
        ScaleComponent defaultScale{{1.0f, 1.0f, 1.0f}};
        
        const PositionComponent& pos = position ? *position : defaultPosition;
        const RotationComponent& rot = rotation ? *rotation : defaultRotation;
        const ScaleComponent& scl = scale ? *scale : defaultScale;

        if (rotation)
        {
            CacheRotationMatrix* cacheRotation = registry->try_get<CacheRotationMatrix>(entity);
            if (!cacheRotation)
            {
                cacheRotation = &registry->emplace<CacheRotationMatrix>(entity);
            }
            cacheRotation->rotationMatrix = calculateRotationMatrix(rot);
        }

        CacheModelMatrix* cacheModel = registry->try_get<CacheModelMatrix>(entity);
        if (!cacheModel)
        {
            cacheModel = &registry->emplace<CacheModelMatrix>(entity, m_ctx);
        }

        FCT::Mat4 modelMatrix = calculateModelMatrix(pos, rot, scl);
        cacheModel->uniform->setValue(FCT::UniformType::ModelMatrix, modelMatrix);
        cacheModel->init = true;
    }

    void MatrixCacheSystem::updateUniforms()
    {

        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            auto modelCacheView = registry->view<CacheModelMatrix>();
            for (auto entity : modelCacheView)
            {
                auto& cacheModel = registry->get<CacheModelMatrix>(entity);
                if (cacheModel.init)
                {
                    cacheModel.uniform->update();
                }
            }
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

    void MatrixCacheSystem::cleanupCacheComponents(entt::registry* registry)
    {
        auto rotationCacheView = registry->view<CacheRotationMatrix>();
        for (auto entity : rotationCacheView)
        {
            if (!registry->all_of<RotationComponent>(entity))
            {
                registry->remove<CacheRotationMatrix>(entity);
            }
        }
        
        auto modelCacheView = registry->view<CacheModelMatrix>();
        for (auto entity : modelCacheView)
        {
            bool hasPosition = registry->all_of<PositionComponent>(entity);
            bool hasRotation = registry->all_of<RotationComponent>(entity);
            bool hasScale = registry->all_of<ScaleComponent>(entity);
            
            if (!hasPosition && !hasRotation && !hasScale)
            {
                registry->remove<CacheModelMatrix>(entity);
            }
        }
    }
}