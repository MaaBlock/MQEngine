#include "RenderGraphViewer.h"
#include "../thirdparty/thirdparty.h"
#include <fstream>
#include <functional>
#include <iostream>
#define NOMINMAX
#ifdef _WIN32
#include <Windows.h>
#endif
using namespace FCT;
namespace MQEngine {
    static int HashPin(int nodeId, const std::string& pinType, int index)
    {
        std::hash<std::string> hasher;
        std::string pinString = std::to_string(nodeId) + "_" + pinType + "_" + std::to_string(index);
        int pinId = static_cast<uint32_t>(hasher(pinString));
        return pinId;
    }

    int PassNode::targetPinId(int index) const
    {
        return HashPin(id, "target", index);
    }

    int PassNode::depthStencilPinId()
    {
        return HashPin(id, "depth", 0);
    }

    int ImageNode::texturePinId()
    {
        return HashPin(id, "texture", 0);
    }

    int ImageNode::targetPinId()
    {
        return HashPin(id, "target", 0);
    }

    int ImageNode::depthStencilPinId()
    {
        return HashPin(id, "depth", 0);
    }
}