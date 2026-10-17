#ifndef ENGINE_PIPELINEMANAGER_H
#define ENGINE_PIPELINEMANAGER_H

#include "../thirdparty/thirdparty.h"
#include "../core/application.h"
#include "PipelineConfig.h"
#include "PipelineRegistries.h"
#include "TechManager.h"

namespace FCT {
    class RenderGraph;
    class Window;
}

namespace MQEngine {

/**
 * @brief 管线管理器，负责加载 JSON 管线配置并动态构建 RenderGraph 与 TechManager
 */
class ENGINE_API PipelineManager {
public:
    PipelineManager();
    virtual ~PipelineManager() = default;

    /**
     * @brief 从指定路径加载 JSON 管线配置文件
     * @param path 配置文件相对路径或绝对路径
     * @return 解析后的 PipelineConfig 结构体
     */
    StatusOr<PipelineConfig> loadPipeline(const std::string& path);

    /**
     * @brief 根据管线配置动态构建 RenderGraph
     * @param config 管线配置数据
     * @param graph 目标渲染图
     * @param wnd 渲染窗口指针（当 inheritWindow 为 true 时使用）
     * @param targetType 渲染目标类型（Window 或 Texture）
     * @return 构建状态
     */
    Status buildRenderGraph(const PipelineConfig& config,
                            FCT::RenderGraph* graph,
                            FCT::Window* wnd = nullptr,
                            RenderTarget targetType = RenderTarget::Window);

    /**
     * @brief 根据管线配置动态构建 TechManager 中的 Technique 集合
     * @param config 管线配置数据
     * @param techMgr 目标 TechManager 实例
     * @return 构建状态
     */
    Status buildTechManager(const PipelineConfig& config, TechManager* techMgr);

    /**
     * @brief 一站式加载并应用管线配置
     */
    Status loadAndApplyPipeline(const std::string& path,
                                FCT::RenderGraph* graph,
                                TechManager* techMgr,
                                FCT::Window* wnd = nullptr,
                                RenderTarget targetType = RenderTarget::Window);

    /**
     * @brief 获取当前已加载的管线配置
     */
    const PipelineConfig& getConfig() const { return m_currentConfig; }

    bool isGraphBuilt() const { return m_isGraphBuilt; }

private:
    PipelineConfig m_currentConfig;
    bool m_isGraphBuilt = false;
};

} // namespace MQEngine

#endif // ENGINE_PIPELINEMANAGER_H
