#ifndef LIGHTINGSYSTEM_H
#define LIGHTINGSYSTEM_H
#include "../thirdparty/thirdparty.h"
#include "../data/DataManager.h"
#include "../data/Component.h"
#include "../core/Uniform.h"

#include "../core/UniformSlots.h"
#include "./BindedSystem.h"

namespace MQEngine {
    class ENGINE_API LightingSystem : public BindedSystem  {
    public:
        LightingSystem(FCT::Context* ctx, DataManager* dataManager);
        ~LightingSystem() = default;
        void updateLogic() override;
        void updateRender() override;
        void bindUniforms(FCT::Layout* layout) override;
        void bindResources(FCT::Layout* layout) override;
        std::vector<FCT::UniformSlot> getUniformSlots() const override;
        std::vector<FCT::SamplerSlot> getSamplerSlots() const override;
    private:
        void updateDirectionalLight();
        void bindDefaultDirectionalLight();
        void setDirectionalLightUniformValues(const DirectionalLightComponent& light);
        void setDefaultDirectionalLightUniformValues();
        void setShadowUniformValues(const FCT::Mat4& directionalLightMvp);
        
        FCT::Context* m_ctx;
        DataManager* m_dataManager;

        FCT::Sampler* m_shadowSampler;
        FCT::Uniform m_directionalLightUniform;
        FCT::Uniform m_shadowUniform;

        bool m_hasDirectionalLight = false;
        
        void updateShadowMatrix();
    };
}

#endif //LIGHTINGSYSTEM_H