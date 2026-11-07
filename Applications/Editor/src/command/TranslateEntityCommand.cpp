#include "TranslateEntityCommand.h"
#include <Engine/headers.h>
#include <Engine/render/system/MatrixCacheSystem.h>
#include <Engine/render/data/Camera.h>
#include <Engine/physics/data/PhysicsCommon.h>

namespace MQEngine {

    TranslateEntityCommand::TranslateEntityCommand(entt::registry* registry, entt::entity entity,
                                                   const FCT::Vec3<>& initialPosition, const FCT::Vec3<>& finalPosition,
                                                   MatrixCacheSystem* matrixCacheSystem)
        : m_registry(registry)
        , m_entity(entity)
        , m_initialPosition(initialPosition)
        , m_finalPosition(finalPosition)
        , m_matrixCacheSystem(matrixCacheSystem) {
    }

    Status TranslateEntityCommand::applyPosition(const FCT::Vec3<>& targetPosition) {
        if (!m_registry) {
            return InvalidArgumentError("Registry pointer is null in TranslateEntityCommand");
        }
        if (!m_registry->valid(m_entity)) {
            return NotFoundError(absl::StrCat("Entity ", static_cast<uint32_t>(m_entity), " is no longer valid in registry"));
        }

        auto* posComp = m_registry->try_get<PositionComponent>(m_entity);
        if (!posComp) {
            return NotFoundError(absl::StrCat("Entity ", static_cast<uint32_t>(m_entity), " does not have PositionComponent"));
        }

        posComp->position = targetPosition;

        // 同步刷新矩阵缓存与 Uniform
        MatrixCacheSystem* matrixSys = m_matrixCacheSystem ? m_matrixCacheSystem : (Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem() : nullptr);
        if (matrixSys) {
            if (auto* cacheModel = m_registry->try_get<CacheModelMatrix>(m_entity)) {
                const FCT::Mat4 worldMat = matrixSys->computeEntityWorldMatrix(m_registry, m_entity);
                const FCT::Mat4 localMat = matrixSys->computeEntityLocalMatrix(m_registry, m_entity);
                cacheModel->localMatrix = localMat;
                cacheModel->worldMatrix = worldMat;
                if (cacheModel->uniform) {
                    cacheModel->uniform->setValue(FCT::UniformType::ModelMatrix, worldMat);
                    cacheModel->uniform->setValue("modelInverseTransposeMatrix", worldMat.inverse().transpose());
                }
                cacheModel->init = true;
            }
        }

        // 若实体挂载了 PhysX 物理刚体缓存，同步更新其全局物理位姿
        if (auto* physCache = m_registry->try_get<CachePhysicsActor>(m_entity)) {
            if (physCache->actor) {
                FCT::Vec3<> worldPos = matrixSys
                    ? matrixSys->computeEntityWorldPosition(m_registry, m_entity)
                    : targetPosition;
                physx::PxTransform pose = physCache->actor->getGlobalPose();
                pose.p = physx::PxVec3(worldPos.x, worldPos.y, worldPos.z);
                physCache->actor->setGlobalPose(pose);
            }
        }

        return OkStatus();
    }

    Status TranslateEntityCommand::execute() {
        return applyPosition(m_finalPosition);
    }

    Status TranslateEntityCommand::undo() {
        return applyPosition(m_initialPosition);
    }

    Status TranslateEntityCommand::redo() {
        return applyPosition(m_finalPosition);
    }

} // namespace MQEngine
