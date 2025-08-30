//
// Created by Administrator on 2025/1/20.
//

#ifndef MESHRENDERYSTEM_H
#define MESHRENDERYSTEM_H
#include "../data/DataManager.h"
#include "../data/Component.h"
#include "../core/Uniform.h"
#include <unordered_map>
#include <vector>

namespace MQEngine {
    struct MeshRenderData {
        FCT::StaticMesh<uint32_t>* mesh;
        FCT::Mat4 modelMatrix;
        std::string modelUuid;
        std::string meshName;
    };

    class ENGINE_API MeshRenderSystem {
    public:
        MeshRenderSystem(FCT::Context* ctx, DataManager* dataManager);
        ~MeshRenderSystem();
        
        void update();
        void collectMeshes();
        void loadMesh(const std::string& modelUuid, const std::string& meshName);
        
        const std::vector<MeshRenderData>& getRenderData() const { return m_renderData; }
        
    private:
        FCT::Mat4 calculateModelMatrix(const PositionComponent& position, const RotationComponent& rotation, const ScaleComponent& scale);
        
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        
        std::vector<MeshRenderData> m_renderData;
        std::unordered_map<std::string, FCT::StaticMesh<uint32_t>*> m_loadedMeshes; // key: modelUuid + "|" + meshName
    };
}

#endif //MESHRENDERYSTEM_H