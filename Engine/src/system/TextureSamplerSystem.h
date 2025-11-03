//
// Created by MaaBlock on 2025/11/1.
//

#ifndef TEXTURESAMPLERSYSTEM_H
#define TEXTURESAMPLERSYSTEM_H
#include "BindedSystem.h"
namespace MQEngine {
    class TextureSamplerSystem : public BindedSystem {
    public:
        std::vector<FCT::ResourceLayout> getResourceSlots() override;
        void bindResources(FCT::Layout* layout) override;
    private:

    };
} // MQEngine

#endif //TEXTURESAMPLERSYSTEM_H
