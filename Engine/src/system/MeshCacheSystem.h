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

    class ENGINE_API MeshCacheSystem {
    public:
        MeshCacheSystem(FCT::Context* ctx, DataManager* dataManager);
        ~MeshCacheSystem();
        
        void update();
        void collectMeshes();
        void loadMesh(const std::string& modelUuid, const std::string& meshName);

    private:
        FCT::Mat4 calculateModelMatrix(const PositionComponent& position, const RotationComponent& rotation, const ScaleComponent& scale);
        
        FCT::Context* m_ctx;
        DataManager* m_dataManager;

        std::unordered_map<std::string, FCT::StaticMesh<uint32_t>*> m_loadedMeshes; // key: modelUuid + "|" + meshName
    };
}

#endif //MESHRENDERYSTEM_H