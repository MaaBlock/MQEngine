//
// Created by MaaBlock on 2025/11/1.
//

#include "TextureSamplerSystem.h"

namespace MQEngine {
    constexpr FCT::ResourceLayout TextureSamplerSlot{
        FCT::SamplerElement{"textureSampler"}
    };
    std::vector<FCT::ResourceLayout> TextureSamplerSystem::getResourceSlots()
    {
        return {
            TextureSamplerSlot
        };
    }
    void TextureSamplerSystem::bindResources(FCT::Layout* layout) {

    }
} // MQEngine