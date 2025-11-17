//
// Created by Administrator on 2025/1/21.
//

#include "./MatrixCacheSystem.h"
#include <set>

#include "../manager/RegistriesManager.h"

namespace MQEngine {
    MatrixCacheSystem::MatrixCacheSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager), m_defaultModelUniform(ctx, ModelUniformSlot)
    {
        // 初始化默认模型矩阵为单位矩阵
        m_defaultModelUniform.setValue("modelMatrix", FCT::Mat4());
        m_defaultModelUniform.setValue("modelInverseTransposeMatrix", FCT::Mat4());
        m_defaultModelUniform.update();
    }

    void MatrixCacheSystem::updateLogic()
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
                g_engineGlobal.registriesManager->requestEmplaceComponent<CacheRotationMatrix>(registry, entity);
            }
            else
            {
                cacheRotation->rotationMatrix = calculateRotationMatrix(rot);
            }
        }

        CacheModelMatrix* cacheModel = registry->try_get<CacheModelMatrix>(entity);
        if (!cacheModel)
        {
            g_engineGlobal.registriesManager->requestEmplaceComponent<CacheModelMatrix>(registry, entity, m_ctx);
        }
        else
        {
            FCT::Mat4 modelMatrix = calculateModelMatrix(pos, rot, scl);
            cacheModel->uniform->setValue(FCT::UniformType::ModelMatrix, modelMatrix);
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
            /*
            for (auto entity : modelCacheView)
            {
                auto& cacheModel = registry->get<CacheModelMatrix>(entity);

            }*/
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
        const CacheModelMatrix* cacheMatrix = registry->try_get<CacheModelMatrix>(entity);
        if (cacheMatrix && cacheMatrix->init)
        {
            layout->bindUniform(cacheMatrix->uniform);
        }
        else
        {
            layout->bindUniform(m_defaultModelUniform);
        }
    }

    void MatrixCacheSystem::cleanupCacheComponents(entt::registry* registry)
    {
        auto rotationCacheView = registry->view<CacheRotationMatrix>();
        for (auto entity : rotationCacheView)
        {
            if (!registry->all_of<RotationComponent>(entity))
            {
                g_engineGlobal.registriesManager->requestRemoveComponent<CacheRotationMatrix>(registry,entity);
                //registry->remove<CacheRotationMatrix>(entity);
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
                g_engineGlobal.registriesManager->requestRemoveComponent<CacheModelMatrix>(registry,entity);
                //registry->remove<CacheModelMatrix>(entity);
            }
        }
    }
}