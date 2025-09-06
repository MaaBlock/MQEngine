//
// Created by Administrator on 2025/1/20.
//

#ifndef TEXTURERENDERYSTEM_H
#define TEXTURERENDERYSTEM_H
#include "../data/DataManager.h"
#include "../data/Component.h"
#include <unordered_map>
#include <vector>

namespace MQEngine {

    class ENGINE_API TextureCacheSystem {
    public:
        TextureCacheSystem(FCT::Context* ctx, DataManager* dataManager);
        ~TextureCacheSystem();
        
        void update();
        void collectTextures();
        void loadTexture(const std::string& modelUuid, const std::string& texturePath);

        
    private:
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::ModelLoader* m_modelLoader;

        std::unordered_map<std::string, FCT::Image*> m_loadedTextures; // key: modelUuid + "|" + texturePath
    };
}

#endif //TEXTURERENDERYSTEM_H