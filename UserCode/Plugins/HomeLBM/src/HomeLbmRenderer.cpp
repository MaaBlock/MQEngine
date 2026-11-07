#include "HomeLbmRenderer.h"
#include <Engine/render/core/VertexLayouts.h>

namespace MQEngine {

namespace {

constexpr const char* kHomeLbmSyncTickerName = "HomeLbmRendererSyncTicker";

constexpr FCT::UniformSlot LbmCompositeUniformSlot{
    "LbmCompositeUniform",
    FCT::UniformVar{ FCT::UniformType::Vec4, "lbmCameraWorldPos" }
};

constexpr const char* kLbmCompositeVertexHlsl = R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.position = sIn.position;
    sOut.texCoord = sIn.texCoord;
    sOut.color = float4(1.0, 1.0, 1.0, 1.0);
    sOut.normal = float4(0.0, 0.0, 0.0, 0.0);
    sOut.tangent = float4(0.0, 0.0, 0.0, 0.0);
    sOut.bitangent = float4(0.0, 0.0, 0.0, 0.0);
    sOut.worldPos = sIn.position;
    sOut.shadowPos = float4(0.0, 0.0, 0.0, 0.0);
    return sOut;
}
)";

constexpr const char* kLbmCompositePixelHlsl = R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    float4 fluidRad = lbmRadianceTex.Sample(lbmLinearSampler, sIn.texCoord);
    if (fluidRad.a <= 0.002) {
        sOut.target0 = float4(0.0, 0.0, 0.0, 0.0);
        return sOut;
    }

    float hitDist = lbmHitDistTex.Sample(lbmLinearSampler, sIn.texCoord).r;
    float4 emissiveSample = EmissiveTarget.Sample(lbmLinearSampler, sIn.texCoord);

    // Note: ObjectPass 中 shadingMode == 1.0 代表真实不透明网格，shadingMode == 2.0 代表无穷远天空盒
    bool hasOpaqueGeometry = (emissiveSample.a > 0.5) && (emissiveSample.a < 1.5);
    if (hasOpaqueGeometry) {
        float3 opaqueWorldPos = PosTarget.Sample(lbmLinearSampler, sIn.texCoord).xyz;
        float opaqueDist = length(opaqueWorldPos - lbmCameraWorldPos.xyz);
        if (opaqueDist + 0.025 < hitDist) {
            sOut.target0 = float4(0.0, 0.0, 0.0, 0.0);
            return sOut;
        }
    }

    sOut.target0 = fluidRad;
    return sOut;
}
)";

} // namespace

HomeLbmRenderer::~HomeLbmRenderer() {
    shutdown();
}

Status HomeLbmRenderer::initialize(FCT::Context* rhiContext) {
    m_rhiContext = rhiContext;
    if (!m_rhiContext) {
        return OkStatus();
    }

    if (!m_syncTickerRegistered) {
        auto& syncGraph = m_rhiContext->syncTickers();
        syncGraph[kHomeLbmSyncTickerName] = {
            [this]() {
                syncRhiResources();
            },
            {},
            {}
        };
        (void)syncGraph.update();
        m_syncTickerRegistered = true;
    }

    m_renderGraph = m_rhiContext->getModule<FCT::RenderGraph>();
    if (m_renderGraph && !m_subscribed) {
        m_lightingPassSubId = m_renderGraph->subscribe("LightingPass", [this](const FCT::PassSubmitEvent& env) {
            submitLightingPassComposite(env);
        });
        m_subscribed = true;
    }
    return OkStatus();
}

void HomeLbmRenderer::shutdown() {
    if (m_rhiContext && m_syncTickerRegistered) {
        auto& syncGraph = m_rhiContext->syncTickers();
        syncGraph.removeNode(kHomeLbmSyncTickerName);
        (void)syncGraph.update();
        m_syncTickerRegistered = false;
    }

    if (m_renderGraph && m_subscribed) {
        m_renderGraph->unsubscribe(m_lightingPassSubId);
        m_subscribed = false;
        m_lightingPassSubId = 0;
    }

    m_computeRadianceImg = {};
    m_computeHitDistImg = {};
    m_hostRadianceStaging.clear();
    m_hostHitDistStaging.clear();

    m_compositeUniform.reset();
    m_compositeLayout.reset();

    if (m_linearSampler) {
        m_linearSampler->release();
        m_linearSampler = nullptr;
    }
    if (m_premulBlendState) {
        m_premulBlendState->release();
        m_premulBlendState = nullptr;
    }
    if (m_noDepthState) {
        m_noDepthState->release();
        m_noDepthState = nullptr;
    }
    if (m_radianceRhiImage) {
        m_radianceRhiImage->release();
        m_radianceRhiImage = nullptr;
    }
    if (m_hitDistRhiImage) {
        m_hitDistRhiImage->release();
        m_hitDistRhiImage = nullptr;
    }

    m_computeWidth = 0u;
    m_computeHeight = 0u;
    m_rhiWidth = 0u;
    m_rhiHeight = 0u;
    m_usingZeroCopyInterop = false;
    m_boundBackendDeviceHandle = nullptr;
    m_pendingActiveVolume = false;
    m_pendingStagingUpload = false;
    m_hasActiveVolumeThisFrame.store(false, std::memory_order_release);
}

Status HomeLbmRenderer::ensureRhiTargets(uint32_t width, uint32_t height) {
    if (!m_rhiContext) {
        return FailedPreconditionError("RHI Context 未初始化");
    }
    width = std::clamp(width, 64u, 2048u);
    height = std::clamp(height, 64u, 2048u);

    if (m_radianceRhiImage && m_hitDistRhiImage && m_rhiWidth == width && m_rhiHeight == height) {
        return OkStatus();
    }

    if (m_radianceRhiImage) {
        m_radianceRhiImage->release();
        m_radianceRhiImage = nullptr;
    }
    if (m_hitDistRhiImage) {
        m_hitDistRhiImage->release();
        m_hitDistRhiImage = nullptr;
    }

    m_radianceRhiImage = FCT_NEW(FCT::SingleBufferImage, m_rhiContext);
    m_radianceRhiImage->width(static_cast<int>(width));
    m_radianceRhiImage->height(static_cast<int>(height));
    m_radianceRhiImage->format(FCT::Format::R16G16B16A16_SFLOAT);
    m_radianceRhiImage->as(FCT::ImageUsage::Texture | FCT::ImageUsage::Storage);
    m_radianceRhiImage->create();

    m_hitDistRhiImage = FCT_NEW(FCT::SingleBufferImage, m_rhiContext);
    m_hitDistRhiImage->width(static_cast<int>(width));
    m_hitDistRhiImage->height(static_cast<int>(height));
    m_hitDistRhiImage->format(FCT::Format::R32_SFLOAT);
    m_hitDistRhiImage->as(FCT::ImageUsage::Texture | FCT::ImageUsage::Storage);
    m_hitDistRhiImage->create();

    m_rhiWidth = width;
    m_rhiHeight = height;
    return OkStatus();
}

Status HomeLbmRenderer::ensureComputeTargets(
    HomeLbmComputeBackend& computeBackend,
    uint32_t width,
    uint32_t height) {
    if (!m_rhiContext || !computeBackend.isValid()) {
        return FailedPreconditionError("RHI Context 或 ComputeBackend 未初始化");
    }

    width = std::clamp(width, 64u, 2048u);
    height = std::clamp(height, 64u, 2048u);

    void* currentDevHandle = computeBackend.device().nativeHandle();
    const bool wantZeroCopy = computeBackend.isSharingRhiDx12Device();

    if (m_computeRadianceImg.valid() && m_computeHitDistImg.valid() &&
        m_computeWidth == width && m_computeHeight == height &&
        m_usingZeroCopyInterop == wantZeroCopy &&
        m_boundBackendDeviceHandle == currentDevHandle) {
        return OkStatus();
    }

    m_computeRadianceImg = {};
    m_computeHitDistImg = {};
    m_usingZeroCopyInterop = false;

    if (wantZeroCopy) {
        CHECK_STATUS(ensureRhiTargets(width, height));
        auto radImport = computeBackend.importExternalRhiImage<FCT::Format::R16G16B16A16_SFLOAT>(m_radianceRhiImage);
        auto distImport = computeBackend.importExternalRhiImage<FCT::Format::R32_SFLOAT>(m_hitDistRhiImage);
        if (radImport.ok() && distImport.ok()) {
            m_computeRadianceImg = std::move(*radImport);
            m_computeHitDistImg = std::move(*distImport);
            m_usingZeroCopyInterop = true;
        }
    }

    if (!m_usingZeroCopyInterop) {
        m_computeRadianceImg = computeBackend.device().createImageWithFormat<FCT::Format::R16G16B16A16_SFLOAT>(width, height);
        m_computeHitDistImg = computeBackend.device().createImageWithFormat<FCT::Format::R32_SFLOAT>(width, height);
        m_hostRadianceStaging.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 8u);
        m_hostHitDistStaging.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    }

    m_computeWidth = width;
    m_computeHeight = height;
    m_boundBackendDeviceHandle = currentDevHandle;
    return OkStatus();
}

Status HomeLbmRenderer::ensureCompositePipeline() {
    if (m_compositeLayout) {
        return OkStatus();
    }
    if (!m_rhiContext || !m_renderGraph || !m_renderGraph->getPass("LightingPass")) {
        return FailedPreconditionError("LightingPass 尚未在 RenderGraph 中就绪");
    }

    m_linearSampler = m_rhiContext->createResource<FCT::Sampler>();
    m_linearSampler->setFilter(FCT::FilterMode::Linear, FCT::FilterMode::Linear, FCT::FilterMode::Nearest);
    m_linearSampler->setAddressMode(
        FCT::AddressMode::ClampToEdge,
        FCT::AddressMode::ClampToEdge,
        FCT::AddressMode::ClampToEdge);
    m_linearSampler->create();

    m_premulBlendState = m_rhiContext->createResource<FCT::BlendState>();
    m_premulBlendState->enable(true)
        .colorFactors(FCT::BlendFactor::One, FCT::BlendFactor::OneMinusSrcAlpha)
        .alphaFactors(FCT::BlendFactor::One, FCT::BlendFactor::OneMinusSrcAlpha)
        .op(FCT::BlendOp::Add);
    m_premulBlendState->create();

    m_noDepthState = m_rhiContext->createResource<FCT::DepthStencilState>();
    m_noDepthState->depthTestEnable(false);
    m_noDepthState->depthWriteEnable(false);
    m_noDepthState->create();

    m_compositeLayout = makeUnique<FCT::Layout>(
        m_rhiContext,
        FCT::PassName("LightingPass"),
        StandardMeshVertexLayout,
        StandardPixelLayout,
        FCT::TextureSlot{ "lbmRadianceTex" },
        FCT::TextureSlot{ "lbmHitDistTex" },
        FCT::SamplerSlot{ "lbmLinearSampler" },
        LbmCompositeUniformSlot);

    m_vertexShaderRef = m_compositeLayout->cacheVertexShader(kLbmCompositeVertexHlsl);
    m_pixelShaderRef = m_compositeLayout->cachePixelShader(kLbmCompositePixelHlsl);
    m_compositeUniform = makeUnique<FCT::Uniform>(m_rhiContext, LbmCompositeUniformSlot);
    return OkStatus();
}

Status HomeLbmRenderer::dispatchVolumeRender(
    HomeLbmSolver& solver,
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives,
    const HomeLbmCameraView& camera,
    uint32_t targetWidth,
    uint32_t targetHeight) {
    if (!m_rhiContext) {
        m_pendingActiveVolume = false;
        m_hasActiveVolumeThisFrame.store(false, std::memory_order_release);
        return OkStatus();
    }

    CHECK_STATUS(ensureComputeTargets(solver.backend(), targetWidth, targetHeight));
    CHECK_STATUS(solver.renderVolume(
        domain,
        primitives,
        camera,
        m_computeRadianceImg,
        m_computeHitDistImg));

    if (!m_usingZeroCopyInterop) {
        solver.backend().stream().submitAndSync(
            m_computeRadianceImg.copyToHost(m_hostRadianceStaging.data()),
            m_computeHitDistImg.copyToHost(m_hostHitDistStaging.data()));
        m_pendingStagingUpload = true;
    } else {
        m_pendingStagingUpload = false;
    }

    m_pendingCameraPos = camera.position;
    m_pendingActiveVolume = true;
    return OkStatus();
}

void HomeLbmRenderer::syncRhiResources() {
    if (!m_rhiContext || !m_pendingActiveVolume || m_computeWidth == 0u || m_computeHeight == 0u) {
        m_hasActiveVolumeThisFrame.store(false, std::memory_order_release);
        return;
    }

    if (!ensureRhiTargets(m_computeWidth, m_computeHeight).ok()) {
        m_hasActiveVolumeThisFrame.store(false, std::memory_order_release);
        return;
    }

    if (m_pendingStagingUpload && !m_usingZeroCopyInterop) {
        const size_t expectedRadBytes = static_cast<size_t>(m_rhiWidth) * static_cast<size_t>(m_rhiHeight) * 8u;
        const size_t expectedDistFloats = static_cast<size_t>(m_rhiWidth) * static_cast<size_t>(m_rhiHeight);
        if (m_hostRadianceStaging.size() == expectedRadBytes &&
            m_hostHitDistStaging.size() == expectedDistFloats) {
            m_radianceRhiImage->updateData(m_hostRadianceStaging.data(), m_hostRadianceStaging.size());
            m_hitDistRhiImage->updateData(
                m_hostHitDistStaging.data(),
                m_hostHitDistStaging.size() * sizeof(float));
        }
        m_pendingStagingUpload = false;
    }

    (void)ensureCompositePipeline();
    m_lastCameraPos = m_pendingCameraPos;
    m_hasActiveVolumeThisFrame.store(true, std::memory_order_release);
}

void HomeLbmRenderer::submitLightingPassComposite(const FCT::PassSubmitEvent& env) {
    if (!m_hasActiveVolumeThisFrame.load(std::memory_order_acquire) ||
        !m_radianceRhiImage || !m_hitDistRhiImage) {
        return;
    }
    if (!ensureCompositePipeline().ok()) {
        return;
    }

    auto* fullScreenMesh = Runtime::Engine::getInstance()->renderer->getFullScreenMesh();
    if (!fullScreenMesh || !env.cmdBuf) {
        return;
    }

    if (m_renderGraph) {
        if (FCT::Image* hdrTarget = m_renderGraph->getImage("SceneHDRColor")) {
            env.cmdBuf->viewport(
                FCT::Vec2(0.0f, 0.0f),
                FCT::Vec2(static_cast<float>(hdrTarget->width()), static_cast<float>(hdrTarget->height())));
            env.cmdBuf->scissor(
                FCT::Vec2(0.0f, 0.0f),
                FCT::Vec2(static_cast<float>(hdrTarget->width()), static_cast<float>(hdrTarget->height())));
        }
    }

    m_compositeUniform->setValue(
        "lbmCameraWorldPos",
        FCT::Vec4(m_lastCameraPos.x, m_lastCameraPos.y, m_lastCameraPos.z, 1.0f));
    m_compositeUniform->update();

    m_compositeLayout->begin();
    m_compositeLayout->bindBlendState(m_premulBlendState);
    m_compositeLayout->bindDepthStencilState(m_noDepthState);
    m_compositeLayout->bindVertexShader(m_vertexShaderRef);
    m_compositeLayout->bindPixelShader(m_pixelShaderRef);
    m_compositeLayout->bindTexture("lbmRadianceTex", m_radianceRhiImage);
    m_compositeLayout->bindTexture("lbmHitDistTex", m_hitDistRhiImage);
    m_compositeLayout->bindSampler("lbmLinearSampler", m_linearSampler);
    m_compositeLayout->bindUniform(*m_compositeUniform);
    m_compositeLayout->drawMesh(env.cmdBuf, fullScreenMesh);
    m_compositeLayout->end();
}

} // namespace MQEngine
