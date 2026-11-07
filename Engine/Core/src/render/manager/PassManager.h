#ifndef ENGINE_PASSMANAGER_H
#define ENGINE_PASSMANAGER_H

#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "../../base/CoreEngineManager.h"
#include "../../base/SingleQueueEventSystem.h"
#include "../../runtime/interface/Application.h"
#include "../../thirdparty/thirdparty.h"
#include "PipelineConfig.h"
#include "PipelineRegistries.h"
#include "TechManager.h"

namespace FCT {
    class RenderGraph;
    class Window;
}

namespace MQEngine {

/**
 * @brief 渲染通道管理器（PassManager）：
 *        负责管理渲染通道配置（PipelinePassDesc）、自定义 Pass 注册、KeepImage 与 Pass 绘制订阅，
 *        通过 SingleQueueEventSystem 接收 request 请求，并在 loop 前及线程汇聚点（syncTicker）统一编译生效到 RenderGraph。
 */
class ENGINE_API PassManager : public CoreEngineManager<PassManager> {
public:
    using CustomPassCallback = std::function<void(FCT::RenderGraph*)>;
    using KeepImageCallback = std::function<void(FCT::RenderGraph*)>;
    using PassSubmitCallback = std::function<void(FCT::PassSubmitEvent)>;

    struct Request {
        struct LoadPipeline {
            std::string path;
        };
        struct SetConfig {
            PipelineConfig config;
        };
        struct AddPassDesc {
            PipelinePassDesc pass;
        };
        struct AddCustomPass {
            std::string name;
            CustomPassCallback callback;
        };
        struct KeepImage {
            KeepImageCallback callback;
        };
        struct SubscribePass {
            std::string passName;
            PassSubmitCallback callback;
        };
    };

    PassManager();
    virtual ~PassManager() = default;

    void registerInfo() {}

    /**
     * @brief 从指定路径加载 JSON 管线配置文件
     */
    StatusOr<PipelineConfig> loadPipeline(const std::string& path);

    /**
     * @brief 根据管线配置向 RenderGraph 添加 Pass 描述
     */
    Status buildRenderGraph(const PipelineConfig& config,
                            FCT::RenderGraph* graph,
                            FCT::Window* wnd = nullptr,
                            RenderTarget targetType = RenderTarget::Window);

    /**
     * @brief 委托 TechManager 构建管线配置中的 Technique 集合
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
     * @brief 请求队列接口（在 loop 前或 syncTickers 汇聚点生效）
     */
    void requestLoadPipeline(const std::string& path);
    void requestSetConfig(const PipelineConfig& config);
    void requestAddPassDesc(const PipelinePassDesc& pass);
    void requestAddCustomPass(const std::string& name, CustomPassCallback callback);
    void requestKeepImage(KeepImageCallback callback);
    void requestSubscribePass(const std::string& passName, PassSubmitCallback callback);

    /**
     * @brief 线程汇聚点（及 loop 前）统一消费挂起的 Pass 请求并完成 RenderGraph 编译与回调绑定
     */
    void syncTicker();
    void flushRequests() { syncTicker(); }

    const PipelineConfig& getConfig() const { return m_currentConfig; }
    bool isGraphBuilt() const { return m_isGraphBuilt; }
    bool isGraphCompiled() const { return m_isGraphCompiled; }

    void setExternalTargets(FCT::Image* target, FCT::Image* depthStencil = nullptr) {
        m_externalTarget = target;
        m_externalDepthStencil = depthStencil;
    }

    static PipelineConfig createDefaultDeferredPipelineConfig();

    Status applyDefaultDeferredPipeline(FCT::RenderGraph* graph,
                                        TechManager* techMgr,
                                        FCT::Window* wnd = nullptr,
                                        RenderTarget targetType = RenderTarget::Window);

private:
    FCT::PassDesc buildPassDesc(const PipelinePassDesc& pass,
                                FCT::Window* wnd,
                                RenderTarget targetType) const;
    StatusOr<Tech> buildTech(const PipelineTechDesc& techDesc) const;
    void compileAndBindGraph();

    PipelineConfig m_currentConfig;
    bool m_isGraphBuilt = false;
    bool m_isGraphCompiled = false;
    bool m_graphDirty = false;
    bool m_techsDirty = false;
    FCT::Image* m_externalTarget = nullptr;
    FCT::Image* m_externalDepthStencil = nullptr;

    struct NamedCustomPass {
        std::string name;
        CustomPassCallback callback;
    };
    struct NamedPassSubscription {
        std::string passName;
        PassSubmitCallback callback;
    };

    std::vector<NamedCustomPass> m_customPassCallbacks;
    std::vector<KeepImageCallback> m_keepImageCallbacks;
    std::vector<NamedPassSubscription> m_subscribePassCallbacks;

    mutable std::mutex m_requestMutex;
    SingleQueueEventSystem m_requestQueue;
};

using PipelineManager = PassManager;

} // namespace MQEngine

#endif // ENGINE_PASSMANAGER_H
