#pragma once

#include <cstdint>

namespace MQEngine::Testing {

/**
 * @brief 硬件能力参考画像基线（供渲染图与管线选型对照校验）
 */
struct HardwareProfile {
    bool isBindless{false};
    bool isReadWriteStorage{false};
    bool isIndirectDraw{false};
    bool isBaseInstance{false};
    bool isRayTracing{false};
    bool isMeshShader{false};
    bool isCompute{false};
    uint32_t uboOffsetAlignment{256};
    uint32_t maxUboRange{65536};
};

/**
 * @brief 抖音小游戏环境硬件配置画像（约束型移动端 WebGL 2.0 / GLES 3.0）
 */
inline HardwareProfile DouyinMinigameProfile() {
    HardwareProfile profile;
    profile.isBindless = false;
    profile.isReadWriteStorage = false;
    profile.isIndirectDraw = false;
    profile.isBaseInstance = false;
    profile.isRayTracing = false;
    profile.isMeshShader = false;
    profile.isCompute = false;
    profile.uboOffsetAlignment = 256;
    profile.maxUboRange = 65536; // 64 KB
    return profile;
}

/**
 * @brief 桌面旗舰硬件配置画像（满血型 Vulkan / D3D12）
 */
inline HardwareProfile DesktopProfile() {
    HardwareProfile profile;
    profile.isBindless = true;
    profile.isReadWriteStorage = true;
    profile.isIndirectDraw = true;
    profile.isBaseInstance = true;
    profile.isRayTracing = true;
    profile.isMeshShader = true;
    profile.isCompute = true;
    profile.uboOffsetAlignment = 16;
    profile.maxUboRange = 134217728; // 128 MB
    return profile;
}

} // namespace MQEngine::Testing
