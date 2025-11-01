//
// Created by MaaBlock on 2025/11/1.
//

#ifndef BINDEDSYSTEM_H
#define BINDEDSYSTEM_H
#include "./ISystem.h"
namespace MQEngine {
    class ENGINE_API BindedSystem : public ISystem
    {
    public:
        virtual void bindUniforms(FCT::Layout* layout)
        {

        }
        virtual void bindResources(FCT::Layout* layout)
        {

        }
        virtual std::vector<FCT::UniformSlot> getUniformSlots()
        {
            return {};
        }
        virtual std::vector<FCT::ResourceLayout> getResourceSlots()
        {
            return {};
        }
    };

} // MQEngine

#endif //BINDEDSYSTEM_H
