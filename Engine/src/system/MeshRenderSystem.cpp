//
// Created by Administrator on 2025/1/20.
//

#include "./MeshRenderSystem.h"
#include "../core/EngineGlobal.h"
#include "../core/VertexLayouts.h"

namespace MQEngine
{
    MeshRenderSystem::MeshRenderSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager)
    {
    }

    MeshRenderSystem::~MeshRenderSystem()
    {
        for (auto& pair : m_loadedMeshes) {
            if (pair.second) {
                delete pair.second;
            }
        }
        m_loadedMeshes.clear();
    }

    void MeshRenderSystem::update()
    {
        collectMeshes();
    }

    void MeshRenderSystem::collectMeshes()
    {
        m_renderData.clear();
        
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            auto view = registry->view<StaticMeshInstance>();
            for (auto entity : view)
            {
                auto& meshInstance = view.get<StaticMeshInstance>(entity);

                if (!meshInstance.mesh && !meshInstance.modelUuid.empty() && !meshInstance.meshName.empty()) {
                    loadMesh(meshInstance.modelUuid, meshInstance.meshName);

                    std::string meshKey = meshInstance.modelUuid + "|" + meshInstance.meshName;
                    auto it = m_loadedMeshes.find(meshKey);
                    if (it != m_loadedMeshes.end()) {
                        const_cast<StaticMeshInstance&>(meshInstance).mesh = it->second;
                    }
                }

                if (meshInstance.mesh) {
                    PositionComponent defaultPosition{{0.0f, 0.0f, 0.0f}};
                    RotationComponent defaultRotation{{0.0f, 0.0f, 0.0f}};
                    ScaleComponent defaultScale{{1.0f, 1.0f, 1.0f}};
                    
                    const PositionComponent* position = registry->try_get<PositionComponent>(entity);
                    const RotationComponent* rotation = registry->try_get<RotationComponent>(entity);
                    const ScaleComponent* scale = registry->try_get<ScaleComponent>(entity);
                    
                    MeshRenderData renderData;
                    renderData.mesh = meshInstance.mesh;
                    renderData.modelMatrix = calculateModelMatrix(
                        position ? *position : defaultPosition,
                        rotation ? *rotation : defaultRotation,
                        scale ? *scale : defaultScale
                    );
                    renderData.modelUuid = meshInstance.modelUuid;
                    renderData.meshName = meshInstance.meshName;
                    
                    m_renderData.push_back(renderData);
                }
            }
        }
    }

    void MeshRenderSystem::loadMesh(const std::string& modelUuid, const std::string& meshName)
    {
        std::string meshKey = modelUuid + "|" + meshName;

        if (m_loadedMeshes.find(meshKey) != m_loadedMeshes.end()) {
            return;
        }
        
        try {
            m_dataManager->updateModelPathList();
            std::string modelPath = m_dataManager->getModelPathByUuid(modelUuid);
            if (modelPath.empty()) {
                FCT::fout << "无法找到模型UUID对应的路径: " << modelUuid << std::endl;
                return;
            }

            std::string modelRelativePath = m_dataManager->getModelRelativePathByUuid(modelUuid);
            if (modelRelativePath.empty()) {
                FCT::fout << "无法找到模型UUID对应的相对路径: " << modelUuid << std::endl;
                return;
            }

            std::string fullModelPath = modelPath + "/" + modelRelativePath;

            FCT::StaticMesh<uint32_t>* mesh = m_ctx->loadMesh(fullModelPath, meshName, StandardMeshVertexLayout);
            
            if (mesh) {
                m_loadedMeshes[meshKey] = mesh;
                FCT::fout << "成功加载mesh: " << meshName << " from " << modelPath << std::endl;
            } else {
                FCT::fout << "加载mesh失败: " << meshName << " from " << modelPath << std::endl;
            }
            
        } catch (const std::exception& e) {
            FCT::fout << "加载mesh异常: " << e.what() << std::endl;
        }
    }

    FCT::Mat4 MeshRenderSystem::calculateModelMatrix(const PositionComponent& position, const RotationComponent& rotation, const ScaleComponent& scale)
    {
        FCT::Mat4 translationMatrix = FCT::Mat4::Translate(position.position.x, position.position.y, position.position.z);

        FCT::Mat4 rotationMatrix;
        rotationMatrix.rotateX(rotation.rotation.x);
        rotationMatrix.rotateY(rotation.rotation.y);
        rotationMatrix.rotateZ(rotation.rotation.z);

        FCT::Mat4 scaleMatrix = FCT::Mat4::Scale(scale.scale.x, scale.scale.y, scale.scale.z);

        return translationMatrix * rotationMatrix * scaleMatrix;
    }
}