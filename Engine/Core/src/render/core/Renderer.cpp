#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include "../../base/Foundation.h"
#include "../../base/MemoryManager.h"
#include "../../effect/core/Effector.h"
#include "../../platform/core/Platform.h"
#include "../../resource/core/Repository.h"
#include "../../resource/system/ResourceActiveSystem.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../runtime/core/Host.h"
#include "../../runtime/core/Runtime.h"
#include "../../runtime/interface/Application.h"
#include "../../runtime/manager/PluginManager.h"
#include "../../runtime/manager/SystemManager.h"
#include "../../runtime/manager/UserCodeManager.h"
#include "../../scene/core/World.h"
#include "../../scene/manager/DataManager.h"
#include "../../scene/manager/RegistriesManager.h"
#include "./GraphicsEnv.h"
#include "./Tech.hpp"

namespace MQEngine {
namespace Render {
    using absl::FailedPreconditionError;
    using absl::InvalidArgumentError;

    Renderer::Renderer() {
        m_inboundQueue.reserve(16384);
        m_consumerBatch.reserve(16384);
        m_pendingComputations.reserve(64);
    }

    Renderer::~Renderer() {
        term();
    }

    void Renderer::registerInfo() {
        if (m_inboundQueue.capacity() < 16384) {
            m_inboundQueue.reserve(16384);
        }
        if (m_consumerBatch.capacity() < 16384) {
            m_consumerBatch.reserve(16384);
        }
        if (m_pendingComputations.capacity() < 64) {
            m_pendingComputations.reserve(64);
        }
        passManager.registerInfo();
        g_engineGlobal.renderer = this;
    }

    Status Renderer::init() {
        if (m_initialized.load(std::memory_order_acquire)) {
            return OkStatus();
        }
        {
            std::lock_guard<std::mutex> lock(m_mailboxMutex);
            m_inboundQueue.clear();
            if (m_inboundQueue.capacity() < 16384) {
                m_inboundQueue.reserve(16384);
            }
        }
        m_consumerBatch.clear();
        if (m_consumerBatch.capacity() < 16384) {
            m_consumerBatch.reserve(16384);
        }
        m_pendingComputations.clear();
        m_activeFrameState.reset();
        m_consumedPacketCount.store(0, std::memory_order_relaxed);
        m_droppedPacketCount.store(0, std::memory_order_relaxed);
        m_retainedFrameCount.store(0, std::memory_order_relaxed);
        m_renderedFrameCount.store(0, std::memory_order_relaxed);
        m_maxPendingFrames.store(120, std::memory_order_relaxed);
        m_frameCount = 0;
        m_cliScreenshotCaptured = false;
        {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            m_pendingCaptures.clear();
            m_nextCaptureRequestId = 1;
            m_latestCaptureRequestId = 0;
            m_completedCaptureRequestId = 0;
            m_lastCaptureStats = FrameCaptureStats{};
        }

        startReclaimThread();
        m_initialized.store(true, std::memory_order_release);

        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        FCT::Context* ctx = engine->platform->getContext();
        DataManager* dataManager = engine->scene->getDataManager();
        SystemManager* systemManager = engine->host->getSystemManager();
        assert(ctx != nullptr && dataManager != nullptr && systemManager != nullptr && "Renderer::init 核心依赖不能为空");

        setupSystems(ctx, dataManager, systemManager);
        setupResources(ctx);

        std::string pipelinePath = "Assets/Pipelines/default_pipeline.json";
        if (engine->foundation->isProjectOpen() &&
            !engine->foundation->getCurrentConfig().resolvedPipelinePath.empty()) {
            pipelinePath = engine->foundation->getCurrentConfig().resolvedPipelinePath;
        }
        passManager.requestLoadPipeline(pipelinePath);
        return OkStatus();
    }

    void Renderer::shutdownWatcher() {
        if (auto* engine = Runtime::Engine::getInstance()) {
            engine->effect->shutdownWatcher();
        }
    }

    void Renderer::term() {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return;
        }
        shutdownWatcher();
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && engine->application != nullptr && "Engine 与 Application 不能为空");
        RenderCallBack::WindowClose callback;
        engine->application->renderCallBackDispatcher.trigger(callback);
        m_initialized.store(false, std::memory_order_release);
        stopReclaimThread();
        {
            std::lock_guard<std::mutex> lock(m_mailboxMutex);
            m_inboundQueue.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            m_pendingCaptures.clear();
        }
        m_captureCv.notify_all();
        m_consumerBatch.clear();
        m_pendingComputations.clear();
        m_activeFrameState.reset();

        m_gpuDrivenSystem.reset();
        m_textureSamplerSystem.reset();
        m_shininessSystem.reset();
        m_textureRenderSystem.reset();
        m_lightingSystem.reset();
        m_matrixCacheSystem.reset();
        m_skyboxCacheSystem.reset();
        m_meshRenderSystem.reset();
        m_cameraSystem.reset();

        if (m_shadowSampler) {
            m_shadowSampler->release();
            m_shadowSampler = nullptr;
        }
        if (m_diffuseSampler) {
            m_diffuseSampler->release();
            m_diffuseSampler = nullptr;
        }
        delete m_skyboxMesh;
        m_skyboxMesh = nullptr;
        delete m_fullScreenMesh;
        m_fullScreenMesh = nullptr;
    }

    TechManager* Renderer::getTechManager() const noexcept {
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        return engine->effect->getTechManager();
    }

    ShaderSnippetManager* Renderer::getShaderSnippetManager() const noexcept {
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        return engine->effect->getShaderSnippetManager();
    }

    ShaderGraphManager* Renderer::getShaderGraphManager() const noexcept {
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        return engine->effect->getShaderGraphManager();
    }

    ShaderFileWatcher* Renderer::getShaderFileWatcher() const noexcept {
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        return engine->effect->getShaderFileWatcher();
    }

    void Renderer::setupSystems(FCT::Context* ctx, DataManager* dataManager, SystemManager* systemManager) {
        ZoneScoped();
        assert(ctx != nullptr && dataManager != nullptr && systemManager != nullptr && "setupSystems 核心依赖不能为空");
        m_cameraSystem = makeUnique<CameraSystem>(ctx, dataManager);
        m_meshRenderSystem = makeUnique<MeshCacheSystem>(ctx, dataManager);
        m_skyboxCacheSystem = makeUnique<SkyboxCacheSystem>(ctx, dataManager);
        m_matrixCacheSystem = makeUnique<MatrixCacheSystem>(ctx, dataManager);
        m_lightingSystem = makeUnique<LightingSystem>(ctx, dataManager);
        m_textureRenderSystem = makeUnique<TextureCacheSystem>(ctx, dataManager);
        m_shininessSystem = makeUnique<ShininessSystem>(ctx, dataManager);
        m_textureSamplerSystem = makeUnique<TextureSamplerSystem>(ctx);
        m_gpuDrivenSystem = makeUnique<GpuDrivenSystem>(ctx, dataManager);

        g_engineGlobal.renderer = this;
        g_engineGlobal.cameraSystem = m_cameraSystem.get();
        g_engineGlobal.lightingSystem = m_lightingSystem.get();
        g_engineGlobal.matrixCacheSystem = m_matrixCacheSystem.get();
        g_engineGlobal.textureRenderSystem = m_textureRenderSystem.get();
        g_engineGlobal.shininessSystem = m_shininessSystem.get();
        g_engineGlobal.gpuDrivenSystem = m_gpuDrivenSystem.get();

        systemManager->requestAddSystem("GpuDrivenSystem", m_gpuDrivenSystem.get());
        systemManager->requestAddSystem("CameraSystem", m_cameraSystem.get());
        systemManager->requestAddSystem("LightingSystem", m_lightingSystem.get());
        systemManager->requestAddSystem("TextureSamplerSystem", m_textureSamplerSystem.get());
    }

    void Renderer::setupShaderWatcher(FCT::JS::NodeEnvironment* nodeEnv) {
        (void)nodeEnv;
    }

    void Renderer::setupResources(FCT::Context* ctx) {
        ZoneScoped();
        assert(ctx != nullptr && "RHI Context 不能为空");
        m_shadowSampler = ctx->createResource<FCT::Sampler>();
        m_shadowSampler->setShadowMap();
        m_shadowSampler->create();

        m_diffuseSampler = ctx->createResource<FCT::Sampler>();
        m_diffuseSampler->setFilter(FCT::FilterMode::Linear, FCT::FilterMode::Linear, FCT::FilterMode::Nearest);
        m_diffuseSampler->setAddressMode(FCT::AddressMode::ClampToEdge, FCT::AddressMode::ClampToEdge, FCT::AddressMode::ClampToEdge);
        m_diffuseSampler->create();

        m_skyboxMesh = new FCT::StaticMesh<uint32_t>(ctx, m_vertexLayout);
        std::vector<FCT::Vec3<>> positions = {
            {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1},
            {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1}
        };
        for (const auto& pos : positions) {
            m_skyboxMesh->addVertex(
                FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),
                FCT::Vec4(pos.x, pos.y, pos.z, 1.0f),
                FCT::Vec2(0.0f, 0.0f),
                FCT::Vec3(0.0f, 0.0f, 1.0f),
                FCT::Vec3(1.0f, 0.0f, 0.0f),
                FCT::Vec3(0.0f, 1.0f, 0.0f)
            );
        }
        std::vector<uint32_t> indices = {
            0, 2, 1, 0, 3, 2,
            5, 7, 6, 5, 4, 7,
            3, 6, 2, 3, 7, 6,
            4, 1, 5, 4, 0, 1,
            1, 6, 5, 1, 2, 6,
            4, 3, 0, 4, 7, 3
        };
        m_skyboxMesh->setIndices(indices);
        m_skyboxMesh->create();

        m_fullScreenMesh = new FCT::StaticMesh<uint32_t>(ctx, m_vertexLayout);
        std::vector<FCT::Vec3<>> fullScreenQuadPositions = {
            {-1, -1, 0}, { 1, -1, 0}, { 1,  1, 0}, {-1,  1, 0}
        };
        for (const auto& pos : fullScreenQuadPositions) {
            m_fullScreenMesh->addVertex(
                FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),
                FCT::Vec4(pos.x, pos.y, pos.z, 1.0f),
                FCT::Vec2(pos.x * 0.5f + 0.5f, 1.0f - (pos.y * 0.5f + 0.5f)),
                FCT::Vec3(0.0f, 0.0f, 1.0f),
                FCT::Vec3(1.0f, 0.0f, 0.0f),
                FCT::Vec3(0.0f, 1.0f, 0.0f)
            );
        }
        std::vector<uint32_t> fullScreenQuadIndices = {0, 2, 1, 0, 3, 2};
        m_fullScreenMesh->setIndices(fullScreenQuadIndices);
        m_fullScreenMesh->create();
    }

    void Renderer::setupPass(FCT::Context* ctx, FCT::Window* wnd) {
        (void)ctx;
        (void)wnd;
        passManager.requestLoadPipeline("Assets/Pipelines/default_pipeline.json");
        passManager.syncTicker();
    }

    void Renderer::setupTechs() {
        auto* techMgr = getTechManager();
        assert(techMgr != nullptr && "TechManager 不能为空");
        techMgr->requestBuildFromPipeline(passManager.getConfig());
        techMgr->syncTicker();
    }

    void Renderer::setupTickers() {
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        FCT::Context* ctx = engine->platform->getContext();
        FCT::Window* wnd = engine->platform->getWindow();
        MemoryManager* memMgr = engine->foundation->getMemoryManager();
        ResourceActiveSystem* resourceActiveSystem = engine->resource->getResourceActiveSystem();
        SystemManager* systemManager = engine->host->getSystemManager();
        RegistriesManager* registriesManager = engine->scene->getRegistriesManager();
        assert(ctx != nullptr && wnd != nullptr && memMgr != nullptr &&
               resourceActiveSystem != nullptr && systemManager != nullptr &&
               registriesManager != nullptr && "setupTickers 核心依赖不能为空");

        setupSubmitTicker(
            ctx,
            wnd,
            memMgr,
            resourceActiveSystem,
            systemManager,
            Runtime::Runtime::instance().getFixedTimestep());
        setupSync(ctx, registriesManager);
    }

    void Renderer::setupSubmitTicker(FCT::Context* ctx,
                                     FCT::Window* wnd,
                                     MemoryManager* memoryManager,
                                     ResourceActiveSystem* resourceActiveSystem,
                                     SystemManager* systemManager,
                                     float fixedTimestep) {
        ZoneScoped();
        assert(ctx != nullptr && "RHI Context 不能为空");

        ctx->pipeHub().passPipe.subscribe<FCT::PassInfo>("ShadowMapPass", [this](FCT::PassInfo& passInfo) {
            m_shadowPassOutputInfo = passInfo.outputInfo;
        });

        ctx->pipeHub().passPipe.subscribe<FCT::PassInfo>("ObjectPass", [this](FCT::PassInfo& passInfo) {
            m_objectPassOutputInfo = passInfo.outputInfo;
        });

        auto& submitTickers = ctx->submitTickers();
        submitTickers["RendererSubmitTicker"] = {
            [this, fixedTimestep]() {
                ZoneScopedN("Renderer::RendererSubmitTicker");
                this->renderTick(fixedTimestep);
            },
            {},
            {FCT::RenderGraphTickers::RenderGraphSubmit}
        };
        submitTickers["MatrixCacheSystemUpdateTicker"] = {
            [this, memoryManager, resourceActiveSystem, systemManager]() {
                ZoneScopedN("Renderer::MatrixCacheSystemUpdateTicker");
                assert(memoryManager != nullptr && resourceActiveSystem != nullptr && systemManager != nullptr && "Ticker 核心依赖不能为空");
                assert(m_textureRenderSystem != nullptr && m_matrixCacheSystem != nullptr && m_shininessSystem != nullptr && "RenderSystems 不能为空");
                memoryManager->beginRenderFrame();
                resourceActiveSystem->updateRender();
                m_textureRenderSystem->updateRender();
                m_matrixCacheSystem->updateRender();
                m_shininessSystem->updateUniforms();
                systemManager->renderTick();
                auto* engine = Runtime::Engine::getInstance();
                assert(engine != nullptr && "Engine 实例不能为空");
                engine->host->getPluginManager()->renderTick();
                engine->host->getUserCodeManager()->renderTick();
            },
            {},
            {FCT::RenderGraphTickers::RenderGraphSubmit}
        };
        submitTickers["ScreenshotCaptureSubmitTicker"] = {
            [this, ctx, wnd]() {
                ZoneScopedN("Renderer::ScreenshotCaptureSubmitTicker");
                this->processPendingCaptures(ctx, wnd);
            },
            {FCT::RenderGraphTickers::RenderGraphSubmit},
            {FCT::SwapBufferSubmitTicker}
        };
        (void)submitTickers.update();
    }

    void Renderer::setupSync(FCT::Context* ctx,
                             RegistriesManager* registriesManager) {
        (void)registriesManager;
        ZoneScoped();
        assert(ctx != nullptr && "RHI Context 不能为空");
        auto& syncGraph = ctx->syncTickers();
        auto* application = Runtime::Runtime::instance().getApplication();
        assert(application != nullptr && "Application 不能为空");
        syncGraph["ManagerSyncTicker"] = {
            []() {
                ZoneScopedN("Renderer::ManagerSyncTicker");
                Runtime::Runtime::instance().flushManagers();
            },
            {},
            {}
        };
        RenderCallBack::SettingSync callback{
            .graph = syncGraph
        };
        application->renderCallBackDispatcher.trigger(callback);
        (void)syncGraph.update();
    }

    void Renderer::updateLogicCaches() {
        ZoneScoped();
        assert(m_matrixCacheSystem != nullptr && m_skyboxCacheSystem != nullptr &&
               m_meshRenderSystem != nullptr && m_textureRenderSystem != nullptr &&
               m_shininessSystem != nullptr && "LogicCache 子系统不能为空");
        m_matrixCacheSystem->updateLogic();
        m_skyboxCacheSystem->updateLogic();
        m_meshRenderSystem->update();
        m_textureRenderSystem->updateLogic();
        m_shininessSystem->update();
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        engine->effect->updateLogic();
    }



    bool Renderer::isInitialized() const noexcept {
        return m_initialized.load(std::memory_order_acquire);
    }

    void Renderer::flushAndSync(Platform::Platform* platform) {
        ZoneScoped();
        assert(platform != nullptr && "Platform 不能为空");
        const bool waitCaptureThisFrame = hasPendingCaptures();
        platform->flush();
        if (waitCaptureThisFrame) {
            if (!waitForPendingCaptures(kCaptureWaitTimeoutMs)) {
                spdlog::error(
                    "[Renderer] 等待帧截图完成超时（超过 {} ms），可能存在渲染提交线程死锁或 GPU 挂起",
                    kCaptureWaitTimeoutMs);
            }
        }
    }

    void Renderer::onSync() {
        // 渲染帧同步钩子：若仍有未决截图请求，执行防御性同步等待
        if (hasPendingCaptures()) {
            if (!waitForPendingCaptures(kCaptureWaitTimeoutMs)) {
                spdlog::error("[Renderer] onSync 等待帧截图完成超时（超过 {} ms）", kCaptureWaitTimeoutMs);
            }
        }
    }

    void Renderer::processCapture()
    {
        auto* engine = Runtime::Engine::getInstance();
        assert(engine != nullptr && "Engine 实例不能为空");
        const auto& engineCfg = engine->foundation->engineConfig;

        if (!m_cliScreenshotCaptured && engineCfg.hasScreenshotPath()) {
            const int32_t capFrame = engineCfg.captureFrame;
            const int32_t testFrames = engineCfg.testFrames;
            const uint64_t triggerFrame = (capFrame > 0)
                ? static_cast<uint64_t>(capFrame)
                : (testFrames > 0 ? static_cast<uint64_t>(testFrames) : 60ULL);
            if (m_frameCount + 1 >= triggerFrame) {
                m_cliScreenshotCaptured = true;
                (void)requestFrameCapture(engineCfg.screenshotPath, engineCfg.captureTarget);
            }
        }

        m_frameCount++;
        if (engineCfg.hasTestFramesLimit() && static_cast<int32_t>(m_frameCount) >= engineCfg.testFrames) {
            engine->host->logicTickManager->requestExit();
        }
    }
    Status Renderer::submitComputation(Computation<RenderPacket>&& comp) {
        if (!isInitialized()) {
            return FailedPreconditionError("Renderer 尚未初始化，禁止提交 Computation");
        }
        if (comp.isEmpty()) {
            return InvalidArgumentError("提交的 Computation 处于空状态 (Empty)");
        }
        {
            std::lock_guard<std::mutex> lock(m_mailboxMutex);
            m_inboundQueue.push_back(std::move(comp));
        }
        return OkStatus();
    }

    Status Renderer::submitPacket(RenderPacket&& packet) {
        return submitComputation(Computation<RenderPacket>::fromValue(std::move(packet)));
    }

    Status Renderer::submitComputations(std::vector<Computation<RenderPacket>>&& batch) {
        if (!isInitialized()) {
            return FailedPreconditionError("Renderer 尚未初始化，禁止批量提交 Computation");
        }
        if (batch.empty()) {
            return OkStatus();
        }
        {
            std::lock_guard<std::mutex> lock(m_mailboxMutex);
            m_inboundQueue.reserve(m_inboundQueue.size() + batch.size());
            for (auto& comp : batch) {
                if (!comp.isEmpty()) {
                    m_inboundQueue.push_back(std::move(comp));
                }
            }
        }
        return OkStatus();
    }

    void Renderer::renderTick(float deltaTime) {
        (void)deltaTime;
        if (!isInitialized()) {
            return;
        }

        drainInboundQueue(m_consumerBatch);

        RenderPacket latestReadyPacket;
        bool hasReadyPacket = false;
        processComputations(m_consumerBatch, latestReadyPacket, hasReadyPacket);

        if (hasReadyPacket) {
            applyReadyPacket(std::move(latestReadyPacket));
        } else if (m_activeFrameState.isValid) {
            // Note: 当前帧无就绪新数据，维持前一帧状态渲染，保证 60FPS 连贯渲染且不产生管线死锁或画面撕裂
            m_retainedFrameCount.fetch_add(1, std::memory_order_relaxed);
        }

        dropTimedOutComputations();

        m_renderedFrameCount.fetch_add(1, std::memory_order_relaxed);
    }

    const RenderFrameState& Renderer::getActiveFrameState() const noexcept {
        return m_activeFrameState;
    }

    uint64_t Renderer::getConsumedPacketCount() const noexcept {
        return m_consumedPacketCount.load(std::memory_order_relaxed);
    }

    uint64_t Renderer::getDroppedPacketCount() const noexcept {
        return m_droppedPacketCount.load(std::memory_order_relaxed);
    }

    uint64_t Renderer::getRetainedFrameCount() const noexcept {
        return m_retainedFrameCount.load(std::memory_order_relaxed);
    }

    uint64_t Renderer::getRenderedFrameCount() const noexcept {
        return m_renderedFrameCount.load(std::memory_order_relaxed);
    }

    size_t Renderer::getPendingComputationCount() const {
        return m_pendingComputations.size();
    }

    size_t Renderer::getInboundQueueSize() const {
        std::lock_guard<std::mutex> lock(m_mailboxMutex);
        return m_inboundQueue.size();
    }

    void Renderer::setMaxPendingFrames(uint32_t frames) noexcept {
        m_maxPendingFrames.store((frames == 0) ? 1 : frames, std::memory_order_relaxed);
    }

    uint32_t Renderer::getMaxPendingFrames() const noexcept {
        return m_maxPendingFrames.load(std::memory_order_relaxed);
    }

    void Renderer::drainInboundQueue(std::vector<Computation<RenderPacket>>& outBatch) {
        if (!outBatch.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_reclaimMutex);
                m_reclaimBatches.push_back(std::move(outBatch));
                if (!m_idleBatches.empty()) {
                    outBatch = std::move(m_idleBatches.back());
                    m_idleBatches.pop_back();
                }
            }
            m_reclaimCv.notify_one();
        }
        if (outBatch.capacity() < 16384) {
            outBatch.reserve(16384);
        }
        std::lock_guard<std::mutex> lock(m_mailboxMutex);
        if (!m_inboundQueue.empty()) {
            outBatch.swap(m_inboundQueue);
        }
    }

    void Renderer::processComputations(std::vector<Computation<RenderPacket>>& inboundBatch,
                                       RenderPacket& outLatestReadyPacket,
                                       bool& outHasReadyPacket) {
        size_t writeIdx = 0;
        uint64_t consumedInPending = 0;
        for (size_t readIdx = 0; readIdx < m_pendingComputations.size(); ++readIdx) {
            auto& pending = m_pendingComputations[readIdx];
            if (const RenderPacket* peek = pending.computation.tryGet()) {
                consumedInPending++;
                if (!m_activeFrameState.isValid || peek->frameIndex >= m_activeFrameState.frameIndex) {
                    if (!outHasReadyPacket || peek->frameIndex >= outLatestReadyPacket.frameIndex) {
                        outLatestReadyPacket = pending.computation.take();
                        outHasReadyPacket = true;
                    }
                }
            } else {
                pending.ageInFrames++;
                if (writeIdx != readIdx) {
                    m_pendingComputations[writeIdx] = std::move(pending);
                }
                writeIdx++;
            }
        }
        if (consumedInPending > 0) {
            m_consumedPacketCount.fetch_add(consumedInPending, std::memory_order_relaxed);
        }
        m_pendingComputations.resize(writeIdx);

        size_t bestInboundIdx = size_t(-1);
        uint64_t highestInboundFrame = outHasReadyPacket ? outLatestReadyPacket.frameIndex : 0;
        bool hasInboundCandidate = outHasReadyPacket;
        uint64_t consumedInBatch = 0;

        for (size_t i = 0; i < inboundBatch.size(); ++i) {
            auto& comp = inboundBatch[i];
            if (comp.isEmpty()) {
                continue;
            }
            if (const RenderPacket* peek = comp.tryGet()) {
                consumedInBatch++;
                if (!m_activeFrameState.isValid || peek->frameIndex >= m_activeFrameState.frameIndex) {
                    if (!hasInboundCandidate || peek->frameIndex >= highestInboundFrame) {
                        highestInboundFrame = peek->frameIndex;
                        bestInboundIdx = i;
                        hasInboundCandidate = true;
                    }
                }
            } else {
                m_pendingComputations.push_back(PendingComputation{
                    .computation = std::move(comp),
                    .ageInFrames = 0
                });
            }
        }
        if (consumedInBatch > 0) {
            m_consumedPacketCount.fetch_add(consumedInBatch, std::memory_order_relaxed);
        }
        if (bestInboundIdx != size_t(-1)) {
            outLatestReadyPacket = inboundBatch[bestInboundIdx].take();
            outHasReadyPacket = true;
        }
    }

    void Renderer::applyReadyPacket(RenderPacket&& packet) {
        m_activeFrameState.updateFromPacket(std::move(packet));
    }

    void Renderer::dropTimedOutComputations() {
        const uint32_t maxFrames = m_maxPendingFrames.load(std::memory_order_relaxed);
        size_t writeIdx = 0;
        uint64_t droppedCount = 0;
        for (size_t readIdx = 0; readIdx < m_pendingComputations.size(); ++readIdx) {
            auto& pending = m_pendingComputations[readIdx];
            if (pending.ageInFrames > maxFrames) {
                droppedCount++;
            } else {
                if (writeIdx != readIdx) {
                    m_pendingComputations[writeIdx] = std::move(pending);
                }
                writeIdx++;
            }
        }
        if (droppedCount > 0) {
            m_droppedPacketCount.fetch_add(droppedCount, std::memory_order_relaxed);
        }
        m_pendingComputations.resize(writeIdx);
    }

    void Renderer::startReclaimThread() {
        if (m_reclaimRunning.load(std::memory_order_acquire)) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_reclaimMutex);
            m_reclaimBatches.clear();
            m_idleBatches.clear();
            for (int i = 0; i < 16; ++i) {
                std::vector<Computation<RenderPacket>> idle;
                idle.reserve(16384);
                m_idleBatches.push_back(std::move(idle));
            }
        }
        m_reclaimRunning.store(true, std::memory_order_release);
        m_reclaimThread = std::thread([this]() {
            while (m_reclaimRunning.load(std::memory_order_acquire)) {
                std::vector<std::vector<Computation<RenderPacket>>> batchesToClear;
                {
                    std::unique_lock<std::mutex> lock(m_reclaimMutex);
                    m_reclaimCv.wait(lock, [this]() {
                        return !m_reclaimRunning.load(std::memory_order_acquire) || !m_reclaimBatches.empty();
                    });
                    if (!m_reclaimRunning.load(std::memory_order_acquire) && m_reclaimBatches.empty()) {
                        break;
                    }
                    batchesToClear.swap(m_reclaimBatches);
                }
                for (auto& batch : batchesToClear) {
                    batch.clear();
                }
                {
                    std::lock_guard<std::mutex> lock(m_reclaimMutex);
                    if (m_idleBatches.size() < 32) {
                        for (auto& batch : batchesToClear) {
                            if (batch.capacity() < 16384) {
                                batch.reserve(16384);
                            }
                            m_idleBatches.push_back(std::move(batch));
                        }
                    }
                }
            }
        });
    }

    void Renderer::stopReclaimThread() {
        if (!m_reclaimRunning.load(std::memory_order_acquire)) {
            return;
        }
        m_reclaimRunning.store(false, std::memory_order_release);
        m_reclaimCv.notify_all();
        if (m_reclaimThread.joinable()) {
            m_reclaimThread.join();
        }
        std::lock_guard<std::mutex> lock(m_reclaimMutex);
        m_reclaimBatches.clear();
        m_idleBatches.clear();
    }

    namespace {

        float halfToFloat(uint16_t h) {
            uint32_t sign = static_cast<uint32_t>(h >> 15) & 0x1u;
            uint32_t exp = static_cast<uint32_t>(h >> 10) & 0x1Fu;
            uint32_t mant = static_cast<uint32_t>(h & 0x03FFu);
            uint32_t fBits = 0;
            if (exp == 0) {
                if (mant == 0) {
                    fBits = sign << 31;
                } else {
                    exp = 1;
                    while ((mant & 0x0400u) == 0) {
                        mant <<= 1;
                        exp--;
                    }
                    mant &= 0x03FFu;
                    fBits = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
                }
            } else if (exp == 31) {
                fBits = (sign << 31) | 0x7F800000u | (mant << 13);
            } else {
                fBits = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
            }
            float out = 0.0f;
            std::memcpy(&out, &fBits, sizeof(float));
            return out;
        }

        uint8_t floatToUnorm8(float v) {
            if (!std::isfinite(v) || v <= 0.0f) {
                return 0;
            }
            float clamped = std::clamp(v, 0.0f, 1.0f);
            return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
        }

        std::vector<std::string> splitTargetNames(const std::string& rawTargets) {
            std::vector<std::string> result;
            size_t start = 0;
            while (start < rawTargets.size()) {
                size_t comma = rawTargets.find(',', start);
                std::string token = (comma == std::string::npos)
                    ? rawTargets.substr(start)
                    : rawTargets.substr(start, comma - start);
                while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
                    token.erase(token.begin());
                }
                while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
                    token.pop_back();
                }
                if (!token.empty()) {
                    result.push_back(token);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            if (result.empty()) {
                result.push_back("SceneColorTarget");
            }
            return result;
        }

        std::string deriveTargetOutputPath(const std::string& baseOutputPath,
                                           const std::string& targetName,
                                           bool isMultiTarget,
                                           size_t targetIndex) {
            if (!isMultiTarget || targetIndex == 0) {
                return baseOutputPath;
            }
            std::filesystem::path p(baseOutputPath);
            std::string stem = p.stem().string();
            std::string ext = p.extension().string();
            if (ext.empty()) {
                ext = ".png";
            }
            std::filesystem::path derived = p.parent_path() / (stem + "_" + targetName + ext);
            return derived.string();
        }

        uint32_t getFormatBytesPerPixel(FCT::Format fmt) {
            switch (fmt) {
                case FCT::Format::R8_UNORM:
                case FCT::Format::R8_UINT:
                case FCT::Format::R8_SINT:
                    return 1;
                case FCT::Format::R8G8_UNORM:
                case FCT::Format::R16_SFLOAT:
                case FCT::Format::D16_UNORM:
                    return 2;
                case FCT::Format::R8G8B8A8_UNORM:
                case FCT::Format::R8G8B8A8_SRGB:
                case FCT::Format::B8G8R8A8_UNORM:
                case FCT::Format::B8G8R8A8_SRGB:
                case FCT::Format::R32_SFLOAT:
                case FCT::Format::D32_SFLOAT:
                case FCT::Format::D24_UNORM_S8_UINT:
                    return 4;
                case FCT::Format::R16G16B16A16_SFLOAT:
                case FCT::Format::R16G16B16A16_UINT:
                case FCT::Format::R16G16B16A16_SINT:
                    return 8;
                case FCT::Format::R32G32B32A32_SFLOAT:
                case FCT::Format::R32G32B32A32_UINT:
                case FCT::Format::R32G32B32A32_SINT:
                    return 16;
                default:
                    return 4;
            }
        }

        FCT::Image* resolveTargetImageAndLayout(FCT::Context* ctx,
                                                FCT::Window* wnd,
                                                const std::string& requestedTarget,
                                                std::string& outResolvedName,
                                                vk::ImageLayout& outLayout) {
            auto* graph = ctx ? ctx->getModule<FCT::RenderGraph>() : nullptr;
            FCT::Image* targetImg = nullptr;
            outResolvedName = requestedTarget.empty() ? "SceneColorTarget" : requestedTarget;

            if (outResolvedName == "window" || outResolvedName == "swapchain" || outResolvedName == "mainWindowColor") {
                if (graph) {
                    targetImg = graph->getImage("mainWindowColor");
                }
                if (!targetImg && wnd && wnd->getCurrentTarget()) {
                    targetImg = wnd->getCurrentTarget()->targetImage();
                }
                outResolvedName = "mainWindowColor";
                outLayout = vk::ImageLayout::ePresentSrcKHR;
                return targetImg;
            }

            if (graph) {
                targetImg = graph->getImage(outResolvedName);
                if (!targetImg && outResolvedName != "SceneColorTarget") {
                    targetImg = graph->getImage("SceneColorTarget");
                    if (targetImg) {
                        outResolvedName = "SceneColorTarget";
                    }
                }
            }
            if (!targetImg && wnd && wnd->getCurrentTarget()) {
                targetImg = wnd->getCurrentTarget()->targetImage();
                outResolvedName = "mainWindowColor";
            }
            if (!targetImg) {
                return nullptr;
            }

            if (targetImg->getType() == FCT::RenderTargetType::WindowTarget || outResolvedName == "mainWindowColor") {
                outLayout = vk::ImageLayout::ePresentSrcKHR;
            } else {
                outLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            }
            return targetImg;
        }

        bool readbackVkImageBytes(FCT::VK_Context* vkCtx,
                                  vk::Image srcImage,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t bytesPerPixel,
                                  vk::ImageLayout origLayout,
                                  bool isDepth,
                                  std::vector<uint8_t>& outBytes) {
            if (!vkCtx || !srcImage || width == 0 || height == 0 || bytesPerPixel == 0) {
                return false;
            }
            vk::Device device = vkCtx->getDevice();
            if (!device) {
                return false;
            }

            VULKAN_HPP_DEFAULT_DISPATCHER.init();
            if (vkCtx->getVkInstance()) {
                VULKAN_HPP_DEFAULT_DISPATCHER.init(vkCtx->getVkInstance());
            }
            VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

            (void)vkCtx->getGraphicsQueue().waitIdle();

            const vk::DeviceSize bufferSize =
                static_cast<vk::DeviceSize>(width) * static_cast<vk::DeviceSize>(height) * static_cast<vk::DeviceSize>(bytesPerPixel);

            vk::BufferCreateInfo bufInfo{};
            bufInfo.setSize(bufferSize);
            bufInfo.setUsage(vk::BufferUsageFlagBits::eTransferDst);
            bufInfo.setSharingMode(vk::SharingMode::eExclusive);

            vk::Buffer stagingBuffer{};
            if (device.createBuffer(&bufInfo, nullptr, &stagingBuffer) != vk::Result::eSuccess) {
                return false;
            }

            vk::MemoryRequirements memReq = device.getBufferMemoryRequirements(stagingBuffer);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.setAllocationSize(memReq.size);
            allocInfo.setMemoryTypeIndex(vkCtx->findMemoryType(
                memReq.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

            vk::DeviceMemory stagingMemory{};
            if (device.allocateMemory(&allocInfo, nullptr, &stagingMemory) != vk::Result::eSuccess) {
                device.destroyBuffer(stagingBuffer);
                return false;
            }
            device.bindBufferMemory(stagingBuffer, stagingMemory, 0);

            vk::ImageAspectFlags aspectMask = isDepth
                ? vk::ImageAspectFlagBits::eDepth
                : vk::ImageAspectFlagBits::eColor;

            vk::CommandBuffer cmd = vkCtx->beginSingleTimeCommands();

            vk::ImageMemoryBarrier toTransferSrc{};
            toTransferSrc.setOldLayout(origLayout);
            toTransferSrc.setNewLayout(vk::ImageLayout::eTransferSrcOptimal);
            toTransferSrc.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
            toTransferSrc.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
            toTransferSrc.setImage(srcImage);
            toTransferSrc.subresourceRange.setAspectMask(aspectMask);
            toTransferSrc.subresourceRange.setBaseMipLevel(0);
            toTransferSrc.subresourceRange.setLevelCount(1);
            toTransferSrc.subresourceRange.setBaseArrayLayer(0);
            toTransferSrc.subresourceRange.setLayerCount(1);
            toTransferSrc.setSrcAccessMask(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eColorAttachmentWrite);
            toTransferSrc.setDstAccessMask(vk::AccessFlagBits::eTransferRead);

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eTransfer,
                vk::DependencyFlags{},
                0, nullptr,
                0, nullptr,
                1, &toTransferSrc);

            vk::BufferImageCopy copyRegion{};
            copyRegion.setBufferOffset(0);
            copyRegion.setBufferRowLength(0);
            copyRegion.setBufferImageHeight(0);
            copyRegion.imageSubresource.setAspectMask(aspectMask);
            copyRegion.imageSubresource.setMipLevel(0);
            copyRegion.imageSubresource.setBaseArrayLayer(0);
            copyRegion.imageSubresource.setLayerCount(1);
            copyRegion.imageOffset = vk::Offset3D{0, 0, 0};
            copyRegion.imageExtent = vk::Extent3D{width, height, 1};

            cmd.copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, stagingBuffer, 1, &copyRegion);

            vk::ImageMemoryBarrier restoreBarrier{};
            restoreBarrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal);
            restoreBarrier.setNewLayout(origLayout);
            restoreBarrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
            restoreBarrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
            restoreBarrier.setImage(srcImage);
            restoreBarrier.subresourceRange = toTransferSrc.subresourceRange;
            restoreBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferRead);
            restoreBarrier.setDstAccessMask(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eShaderRead);

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eAllCommands,
                vk::DependencyFlags{},
                0, nullptr,
                0, nullptr,
                1, &restoreBarrier);

            vkCtx->endSingleTimeCommands(cmd);

            void* mapped = nullptr;
            bool ok = false;
            if (device.mapMemory(stagingMemory, 0, bufferSize, vk::MemoryMapFlags{}, &mapped) == vk::Result::eSuccess && mapped) {
                outBytes.resize(static_cast<size_t>(bufferSize));
                std::memcpy(outBytes.data(), mapped, static_cast<size_t>(bufferSize));
                device.unmapMemory(stagingMemory);
                ok = true;
            }

            device.destroyBuffer(stagingBuffer);
            device.freeMemory(stagingMemory);
            return ok;
        }

        void decodeRawBytesToRgba8(const std::vector<uint8_t>& rawBytes,
                                   uint32_t width,
                                   uint32_t height,
                                   FCT::Format fmt,
                                   FCT::ImageLoader::ImageData& outImg,
                                   FrameCaptureStats& outStats) {
            const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            outImg.width = static_cast<int>(width);
            outImg.height = static_cast<int>(height);
            outImg.channels = 4;
            outImg.data.resize(pixelCount * 4, 255);

            uint64_t sumR = 0;
            uint64_t sumG = 0;
            uint64_t sumB = 0;
            size_t nonBlackCount = 0;

            const uint32_t bpp = getFormatBytesPerPixel(fmt);
            for (size_t i = 0; i < pixelCount; ++i) {
                const size_t srcOff = i * bpp;
                uint8_t r = 0;
                uint8_t g = 0;
                uint8_t b = 0;

                if (srcOff + bpp <= rawBytes.size()) {
                    switch (fmt) {
                        case FCT::Format::B8G8R8A8_UNORM:
                        case FCT::Format::B8G8R8A8_SRGB:
                            b = rawBytes[srcOff + 0];
                            g = rawBytes[srcOff + 1];
                            r = rawBytes[srcOff + 2];
                            break;
                        case FCT::Format::R16G16B16A16_SFLOAT: {
                            uint16_t hr = 0, hg = 0, hb = 0;
                            std::memcpy(&hr, &rawBytes[srcOff + 0], 2);
                            std::memcpy(&hg, &rawBytes[srcOff + 2], 2);
                            std::memcpy(&hb, &rawBytes[srcOff + 4], 2);
                            r = floatToUnorm8(halfToFloat(hr));
                            g = floatToUnorm8(halfToFloat(hg));
                            b = floatToUnorm8(halfToFloat(hb));
                            break;
                        }
                        case FCT::Format::R32G32B32A32_SFLOAT: {
                            float fr = 0.0f, fg = 0.0f, fb = 0.0f;
                            std::memcpy(&fr, &rawBytes[srcOff + 0], 4);
                            std::memcpy(&fg, &rawBytes[srcOff + 4], 4);
                            std::memcpy(&fb, &rawBytes[srcOff + 8], 4);
                            r = floatToUnorm8(fr);
                            g = floatToUnorm8(fg);
                            b = floatToUnorm8(fb);
                            break;
                        }
                        case FCT::Format::D32_SFLOAT:
                        case FCT::Format::R32_SFLOAT: {
                            float fd = 0.0f;
                            std::memcpy(&fd, &rawBytes[srcOff], 4);
                            r = g = b = floatToUnorm8(fd);
                            break;
                        }
                        default:
                            r = rawBytes[srcOff + 0];
                            g = (bpp > 1) ? rawBytes[srcOff + 1] : r;
                            b = (bpp > 2) ? rawBytes[srcOff + 2] : r;
                            break;
                    }
                }

                outImg.data[i * 4 + 0] = r;
                outImg.data[i * 4 + 1] = g;
                outImg.data[i * 4 + 2] = b;
                outImg.data[i * 4 + 3] = 255;

                sumR += r;
                sumG += g;
                sumB += b;
                if (static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b) > 5) {
                    nonBlackCount++;
                }
            }

            outStats.width = width;
            outStats.height = height;
            if (pixelCount > 0) {
                outStats.nonBlackRatio = static_cast<double>(nonBlackCount) / static_cast<double>(pixelCount);
                outStats.avgR = static_cast<float>(static_cast<double>(sumR) / static_cast<double>(pixelCount));
                outStats.avgG = static_cast<float>(static_cast<double>(sumG) / static_cast<double>(pixelCount));
                outStats.avgB = static_cast<float>(static_cast<double>(sumB) / static_cast<double>(pixelCount));
            }
        }

    } // namespace

    uint64_t Renderer::requestFrameCapture(const std::string& outputPath, const std::string& targetName) {
        if (outputPath.empty()) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(m_captureMutex);
        const uint64_t reqId = m_nextCaptureRequestId++;
        m_latestCaptureRequestId = reqId;
        m_pendingCaptures.push_back(PendingFrameCapture{
            .requestId = reqId,
            .outputPath = outputPath,
            .targetName = targetName.empty() ? "SceneColorTarget" : targetName
        });
        return reqId;
    }

    bool Renderer::hasPendingCaptures() const {
        std::lock_guard<std::mutex> lock(m_captureMutex);
        return !m_pendingCaptures.empty() || (m_completedCaptureRequestId < m_latestCaptureRequestId);
    }

    bool Renderer::waitForPendingCaptures(uint32_t timeoutMs) {
        std::unique_lock<std::mutex> lock(m_captureMutex);
        const uint64_t targetId = m_latestCaptureRequestId;
        if (targetId == 0 || m_completedCaptureRequestId >= targetId) {
            return true;
        }
        return m_captureCv.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [this, targetId]() {
                return m_completedCaptureRequestId >= targetId;
            });
    }

    FrameCaptureStats Renderer::getLastCaptureStats() const {
        std::lock_guard<std::mutex> lock(m_captureMutex);
        return m_lastCaptureStats;
    }

    FrameCaptureStats Renderer::executeSingleTargetCapture(FCT::Context* ctx,
                                                           FCT::Window* wnd,
                                                           const std::string& outputPath,
                                                           const std::string& targetName) {
        FrameCaptureStats stats;
        stats.targetName = targetName;
        stats.outputPath = outputPath;

        auto* vkCtx = dynamic_cast<FCT::VK_Context*>(ctx);
        if (!vkCtx) {
            stats.errorMessage = "当前 RHI Context 不是 VK_Context";
            spdlog::error("[Renderer::FrameCapture] 截图失败: {}", stats.errorMessage);
            return stats;
        }

        std::string resolvedName;
        vk::ImageLayout currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        FCT::Image* targetImg = resolveTargetImageAndLayout(ctx, wnd, targetName, resolvedName, currentLayout);
        if (!targetImg) {
            stats.errorMessage = "未找到 RenderGraph 目标图像: " + targetName;
            spdlog::error("[Renderer::FrameCapture] 截图失败: {}", stats.errorMessage);
            return stats;
        }
        stats.targetName = resolvedName;

        auto* rhiImg = dynamic_cast<FCT::RHI::VK_Image*>(targetImg->currentImage());
        if (!rhiImg || !rhiImg->getVkImage()) {
            stats.errorMessage = "目标图像底层 VK_Image 为空: " + resolvedName;
            spdlog::error("[Renderer::FrameCapture] 截图失败: {}", stats.errorMessage);
            return stats;
        }

        const uint32_t width = static_cast<uint32_t>(std::max(1, targetImg->width()));
        const uint32_t height = static_cast<uint32_t>(std::max(1, targetImg->height()));
        const FCT::Format fmt = targetImg->format();
        const uint32_t bpp = getFormatBytesPerPixel(fmt);
        const bool isDepth = (fmt == FCT::Format::D32_SFLOAT ||
                              fmt == FCT::Format::D16_UNORM ||
                              fmt == FCT::Format::D24_UNORM_S8_UINT ||
                              fmt == FCT::Format::D32_SFLOAT_S8_UINT);

        std::vector<uint8_t> rawBytes;
        if (!readbackVkImageBytes(vkCtx, rhiImg->getVkImage(), width, height, bpp, currentLayout, isDepth, rawBytes)) {
            stats.errorMessage = "从 GPU 回读 VK_Image 像素缓冲失败: " + resolvedName;
            spdlog::error("[Renderer::FrameCapture] 截图失败: {}", stats.errorMessage);
            return stats;
        }

        FCT::ImageLoader::ImageData rgbaImage;
        decodeRawBytesToRgba8(rawBytes, width, height, fmt, rgbaImage, stats);

        std::filesystem::path outPath(outputPath);
        if (outPath.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);
        }

        bool saved = false;
        if (g_engineGlobal.rt) {
            FCT::ImageLoader* loader = g_engineGlobal.rt->createImageLoader();
            if (loader) {
                saved = loader->save(outPath.string(), rgbaImage);
                delete loader;
            }
        }

        if (!saved) {
            stats.errorMessage = "保存 PNG 文件失败: " + outPath.string();
            spdlog::error("[Renderer::FrameCapture] {}", stats.errorMessage);
            return stats;
        }

        stats.success = true;
        stats.outputPath = std::filesystem::absolute(outPath).string();
        spdlog::info(
            "[Renderer::FrameCapture] 截图成功: target={}, size={}x{}, nonBlack={:.1f}%, avgRGB=({:.1f}, {:.1f}, {:.1f}), path={}",
            stats.targetName,
            stats.width,
            stats.height,
            stats.nonBlackRatio * 100.0,
            stats.avgR,
            stats.avgG,
            stats.avgB,
            stats.outputPath);
        return stats;
    }

    void Renderer::processPendingCaptures(FCT::Context* ctx, FCT::Window* wnd) {
        std::vector<PendingFrameCapture> requests;
        {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            if (m_pendingCaptures.empty()) {
                return;
            }
            requests.swap(m_pendingCaptures);
        }

        FrameCaptureStats latestStats;
        uint64_t highestReqId = 0;

        for (const auto& req : requests) {
            highestReqId = std::max(highestReqId, req.requestId);
            std::vector<std::string> targets = splitTargetNames(req.targetName);
            const bool isMulti = targets.size() > 1;
            for (size_t i = 0; i < targets.size(); ++i) {
                std::string targetOutPath = deriveTargetOutputPath(req.outputPath, targets[i], isMulti, i);
                latestStats = executeSingleTargetCapture(ctx, wnd, targetOutPath, targets[i]);
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            m_lastCaptureStats = latestStats;
            m_completedCaptureRequestId = std::max(m_completedCaptureRequestId, highestReqId);
        }
        m_captureCv.notify_all();
    }

} // namespace Render
} // namespace MQEngine
