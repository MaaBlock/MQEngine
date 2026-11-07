#ifndef MQENGINE_HOMELBM_RENDERER_H
#define MQENGINE_HOMELBM_RENDERER_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmSolver.h"

namespace MQEngine {

/**
 * @brief 自适应 HOME-LBM 体积/自由液面结果到引擎延迟渲染管线 (LightingPass) 的零拷贝深度遮挡合成器
 */
class HOME_LBM_API HomeLbmRenderer {
public:
    HomeLbmRenderer() = default;
    ~HomeLbmRenderer();

    HomeLbmRenderer(const HomeLbmRenderer&) = delete;
    HomeLbmRenderer& operator=(const HomeLbmRenderer&) = delete;

    Status initialize(FCT::Context* rhiContext);
    void shutdown();

    /**
     * @brief 在逻辑线程调度 GPU 光线步进并回读到主机暂存缓冲（无锁，不跨线程触碰 RHI 命令池）
     */
    Status dispatchVolumeRender(
        HomeLbmSolver& solver,
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives,
        const HomeLbmCameraView& camera,
        uint32_t targetWidth,
        uint32_t targetHeight);

    /**
     * @brief 在 Context::flush() 帧同步屏障处将主机暂存体积纹理上传至 RHI 纹理并确保合成管线就绪
     */
    void syncRhiResources();

    void setActiveForFrame(bool active) noexcept {
        m_pendingActiveVolume = active;
        if (!active) {
            m_hasActiveVolumeThisFrame.store(false, std::memory_order_release);
            m_pendingStagingUpload = false;
        }
    }

private:
    Status ensureComputeTargets(
        HomeLbmComputeBackend& computeBackend,
        uint32_t width,
        uint32_t height);
    Status ensureRhiTargets(uint32_t width, uint32_t height);
    Status ensureCompositePipeline();
    void submitLightingPassComposite(const FCT::PassSubmitEvent& env);

private:
    FCT::Context* m_rhiContext = nullptr;
    FCT::RenderGraph* m_renderGraph = nullptr;
    FCT::SubscribeId m_lightingPassSubId = 0;
    bool m_subscribed = false;
    bool m_syncTickerRegistered = false;

    FCT::SingleBufferImage* m_radianceRhiImage = nullptr;
    FCT::SingleBufferImage* m_hitDistRhiImage = nullptr;

    FCT::Compute::FormatImage<FCT::Format::R16G16B16A16_SFLOAT> m_computeRadianceImg;
    FCT::Compute::FormatImage<FCT::Format::R32_SFLOAT> m_computeHitDistImg;
    std::vector<uint8_t> m_hostRadianceStaging;
    std::vector<float> m_hostHitDistStaging;

    uint32_t m_computeWidth = 0u;
    uint32_t m_computeHeight = 0u;
    uint32_t m_rhiWidth = 0u;
    uint32_t m_rhiHeight = 0u;
    bool m_usingZeroCopyInterop = false;
    void* m_boundBackendDeviceHandle = nullptr;

    bool m_pendingActiveVolume = false;
    bool m_pendingStagingUpload = false;
    FCT::Vec3<> m_pendingCameraPos = FCT::Vec3<>(0.0f, 0.0f, 0.0f);

    UniquePtr<FCT::Layout> m_compositeLayout;
    FCT::ShaderRef m_vertexShaderRef;
    FCT::ShaderRef m_pixelShaderRef;
    FCT::Sampler* m_linearSampler = nullptr;
    FCT::BlendState* m_premulBlendState = nullptr;
    FCT::DepthStencilState* m_noDepthState = nullptr;
    UniquePtr<FCT::Uniform> m_compositeUniform;

    FCT::Vec3<> m_lastCameraPos = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    std::atomic<bool> m_hasActiveVolumeThisFrame{false};
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_RENDERER_H
