//
// Created by MaaBlock on 2025/11/1.
//

#ifndef TEXTURESAMPLERSYSTEM_H
#define TEXTURESAMPLERSYSTEM_H
#include "../thirdparty/thirdparty.h"
#include "BindedSystem.h"
namespace MQEngine {
    /**
     * @brief
     */
    class TextureSamplerSystem : public BindedSystem
    {
    public:
        TextureSamplerSystem(Context* context);
        std::vector<FCT::SamplerSlot> getSamplerSlots() const override;
        // std::vector<FCT::ResourceLayout> getResourceSlots() const override;
        void bindResources(FCT::Layout* layout) override;
        void updateLogic() override;
        void updateRender() override;

    private:
        Sampler* m_diffuseSampler;
    };
} // MQEngine

#endif //TEXTURESAMPLERSYSTEM_H
