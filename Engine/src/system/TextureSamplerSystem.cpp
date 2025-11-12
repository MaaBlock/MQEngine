//
// Created by MaaBlock on 2025/11/1.
//

#include "TextureSamplerSystem.h"

namespace MQEngine {
    constexpr FCT::ResourceLayout TextureSamplerSlot{FCT::SamplerElement{"textureSampler"}};
    TextureSamplerSystem::TextureSamplerSystem(Context* context)
    {
        m_diffuseSampler = context->createResource<Sampler>();
        m_diffuseSampler->setFilter(FCT::FilterMode::Linear, FCT::FilterMode::Linear, FCT::FilterMode::Nearest);
        m_diffuseSampler->setAddressMode(FCT::AddressMode::Repeat, FCT::AddressMode::Repeat, FCT::AddressMode::Repeat);
        m_diffuseSampler->create();
    }
    std::vector<FCT::SamplerSlot> TextureSamplerSystem::getSamplerSlots() const
    {
        return {
                {"textureSampler"}
        };
    }
    void TextureSamplerSystem::bindResources(FCT::Layout* layout) {
        layout->bindSampler("textureSampler", m_diffuseSampler);
    }

    void TextureSamplerSystem::updateRender()
    {

    }
    void TextureSamplerSystem::updateLogic()
    {

    }
} // MQEngine