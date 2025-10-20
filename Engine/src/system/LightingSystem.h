//
// Created by Administrator on 2025/1/22.
//

#ifndef LIGHTINGSYSTEM_H
#define LIGHTINGSYSTEM_H
#include "../data/DataManager.h"
#include "../data/Component.h"
#include "../core/Uniform.h"

#include "../core/UniformSlots.h"

namespace MQEngine {
    class ENGINE_API LightingSystem {
    public:
        LightingSystem(FCT::Context* ctx, DataManager* dataManager);
        ~LightingSystem() = default;
        
        void update();
        void bind(FCT::Layout* layout);
        
    private:
        void updateDirectionalLight();
        void bindDefaultDirectionalLight();
        
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::Uniform m_directionalLightUniform;
        FCT::Uniform m_shadowUniform;

        bool m_hasDirectionalLight = false;
        
        void updateShadowMatrix();
    };
}

#endif //LIGHTINGSYSTEM_H