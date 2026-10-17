#ifndef ENGINE_PIPELINECONFIG_H
#define ENGINE_PIPELINECONFIG_H

#include "../thirdparty/thirdparty.h"
#include <string>
#include <vector>

namespace MQEngine {

/**
 * @brief 管线清除参数描述
 */
struct PipelineClearDesc {
    std::vector<std::string> types; // 清除类型: "color", "depth", "stencil", "depthStencil"
    FCT::Vec4 color = FCT::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float depth = 1.0f;
    uint8_t stencil = 0;
};

/**
 * @brief 渲染目标描述
 */
struct PipelineTargetDesc {
    std::string name;
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
    bool inheritWindow = false;
};

/**
 * @brief 深度模板缓冲区描述
 */
struct PipelineDepthStencilDesc {
    std::string name;
    std::string format = "D32_SFLOAT";
    uint32_t width = 0;
    uint32_t height = 0;
    bool inheritWindow = false;
};

/**
 * @brief 输入纹理描述
 */
struct PipelineTextureDesc {
    std::string name;
    std::vector<std::string> stages; // 访问阶段: "fragment", "vertex", "compute"
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
};

/**
 * @brief 渲染通道 (Pass) 描述
 */
struct PipelinePassDesc {
    std::string name;
    std::string type = "raster"; // "raster", "compute", "external"
    PipelineClearDesc clear;
    std::vector<PipelineTextureDesc> inputTextures;
    std::vector<PipelineTargetDesc> targets;
    std::vector<PipelineDepthStencilDesc> depthStencils;
};

/**
 * @brief 组件过滤器描述 (对应 ComponentFilter)
 */
struct PipelineComponentFilterDesc {
    std::vector<std::string> include;
    std::vector<std::string> exclude;
};

/**
 * @brief 渲染技术 (Technique) 描述
 */
struct PipelineTechDesc {
    std::string name;
    std::string pass;
    std::string vertexShader;
    std::string pixelShader;
    std::string vertexLayout = "StandardMeshVertexLayout";
    std::string pixelLayout = "StandardPixelLayout";
    std::vector<std::string> uniformSlots;
    std::vector<std::string> samplerSlots;
    std::vector<std::string> textureSlots;
    std::vector<std::string> bindedSystems;
    PipelineComponentFilterDesc componentFilter;
    std::string entityCallback = "UniversalMeshDrawCallback";
};

/**
 * @brief 完整数据驱动管线配置
 */
struct PipelineConfig {
    std::string name;
    std::string version = "1.0";
    std::string defaultTarget = "window";
    std::vector<PipelinePassDesc> passes;
    std::vector<PipelineTechDesc> techniques;
};

} // namespace MQEngine

#endif // ENGINE_PIPELINECONFIG_H
