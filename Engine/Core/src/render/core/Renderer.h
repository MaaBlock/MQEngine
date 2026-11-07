#ifndef ENGINE_RENDERER_RENDERER_H
#define ENGINE_RENDERER_RENDERER_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include "../../base/Computation.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../data/RenderPacket.h"
#include "../manager/PassManager.h"
#include "../manager/PipelineManager.h"
#include "../manager/ShaderFileWatcher.h"
#include "../manager/ShaderGraphManager.h"
#include "../manager/ShaderSnippetManager.h"
#include "../manager/TechManager.h"
#include "../system/CameraSystem.h"
#include "../system/GpuDrivenSystem.h"
#include "../system/LightingSystem.h"
#include "../system/MatrixCacheSystem.h"
#include "../system/MeshCacheSystem.h"
#include "../system/ShininessSystem.h"
#include "../system/SkyboxCacheSystem.h"
#include "../system/TextureCacheSystem.h"
#include "../system/TextureSamplerSystem.h"
#include "./VertexLayouts.h"

#ifndef ENGINE_API
#ifdef _WIN32
    #ifdef BUILD_ENGINE
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
    #endif
#else
    #define ENGINE_API
#endif
#endif

namespace MQEngine {
    class DataManager;
    class MemoryManager;
    class RegistriesManager;
    class ResourceActiveSystem;
    class SystemManager;

    namespace Platform {
        class Platform;
    }

    namespace Runtime {
        class Application;
    }

    namespace Render {

        /**
         * @brief 内部未决异步计算项封装
         */
        struct PendingComputation {
            Computation<RenderPacket> computation;      ///< 异步计算对象
            uint32_t ageInFrames{0};                    ///< 队列滞留帧龄计数
        };

        /**
         * @brief 渲染目标截图统计与输出元数据
         */
        struct FrameCaptureStats {
            bool success{false};
            uint32_t width{0};
            uint32_t height{0};
            double nonBlackRatio{0.0};
            float avgR{0.0f};
            float avgG{0.0f};
            float avgB{0.0f};
            std::string targetName;
            std::string outputPath;
            std::string errorMessage;
        };

        /**
         * @brief 待在 RHI 提交线程执行的帧截图请求
         */
        struct PendingFrameCapture {
            uint64_t requestId{0};
            std::string outputPath;
            std::string targetName{"SceneColorTarget"};
        };

        /**
         * @brief MQEngine 核心渲染器模块 (Renderer)
         * 
         * 职责：
         * 1. 渲染流程管理：内嵌 PassManager，负责管线配置与 RenderGraph 拓扑构建。
         * 2. 跨线程同步与数据投递：负责把逻辑线程数据通过异步信箱与 CacheSystem 同步到渲染提交线程（RHI Submit Thread），
         *    管理帧间同步点（syncTickers）与提交调度图（submitTickers）。
         */
        class ENGINE_API Renderer : public CoreEngineModule<Renderer> {
        public:
            Renderer();
            ~Renderer();

            Renderer(const Renderer&) = delete;
            Renderer& operator=(const Renderer&) = delete;
            Renderer(Renderer&&) = delete;
            Renderer& operator=(Renderer&&) = delete;

            // ==================== 生命周期与管线装配 ====================
            void registerInfo();
            Status init();

            void setupSystems(FCT::Context* ctx, DataManager* dataManager, SystemManager* systemManager);
            void setupShaderWatcher(FCT::JS::NodeEnvironment* nodeEnv);
            void registerShaderSnippets();
            void setupResources(FCT::Context* ctx);
            void setupPass(FCT::Context* ctx, FCT::Window* wnd);
            void setupTechs();

            /**
             * @brief 在进入主循环前统一装配 submitTickers 与 syncTickers
             */
            void setupTickers();

            void setupSubmitTicker(FCT::Context* ctx,
                                   FCT::Window* wnd,
                                   MemoryManager* memoryManager,
                                   ResourceActiveSystem* resourceActiveSystem,
                                   SystemManager* systemManager,
                                   float fixedTimestep);

            void setupSync(FCT::Context* ctx,
                           RegistriesManager* registriesManager);

            void updateLogicCaches();
            void flushAndSync(Platform::Platform* platform);
            void shutdownWatcher();
            void term();
            bool isInitialized() const noexcept;
            void onSync();

            // ==================== 子系统与资源访问器 ====================
            PassManager* getPassManager() noexcept { return &passManager; }
            const PassManager* getPassManager() const noexcept { return &passManager; }
            PipelineManager* getPipelineManager() noexcept { return &passManager; }
            const PipelineManager* getPipelineManager() const noexcept { return &passManager; }

            TechManager* getTechManager() const noexcept;
            ShaderSnippetManager* getShaderSnippetManager() const noexcept;
            ShaderGraphManager* getShaderGraphManager() const noexcept;
            ShaderFileWatcher* getShaderFileWatcher() const noexcept;

            CameraSystem* getCameraSystem() const noexcept { return m_cameraSystem.get(); }
            MeshCacheSystem* getMeshCacheSystem() const noexcept { return m_meshRenderSystem.get(); }
            SkyboxCacheSystem* getSkyboxCacheSystem() const noexcept { return m_skyboxCacheSystem.get(); }
            MatrixCacheSystem* getMatrixCacheSystem() const noexcept { return m_matrixCacheSystem.get(); }
            LightingSystem* getLightingSystem() const noexcept { return m_lightingSystem.get(); }
            TextureCacheSystem* getTextureCacheSystem() const noexcept { return m_textureRenderSystem.get(); }
            ShininessSystem* getShininessSystem() const noexcept { return m_shininessSystem.get(); }
            TextureSamplerSystem* getTextureSamplerSystem() const noexcept { return m_textureSamplerSystem.get(); }
            GpuDrivenSystem* getGpuDrivenSystem() const noexcept { return m_gpuDrivenSystem.get(); }

            FCT::StaticMesh<uint32_t>* getSkyboxMesh() const noexcept { return m_skyboxMesh; }
            FCT::StaticMesh<uint32_t>* getFullScreenMesh() const noexcept { return m_fullScreenMesh; }
            FCT::Sampler* getShadowSampler() const noexcept { return m_shadowSampler; }
            FCT::Sampler* getDiffuseSampler() const noexcept { return m_diffuseSampler; }

            void processCapture();
            // ==================== 生产者 API (Producer API) ====================
            Status submitComputation(Computation<RenderPacket>&& comp);
            Status submitPacket(RenderPacket&& packet);
            Status submitComputations(std::vector<Computation<RenderPacket>>&& batch);

            // ==================== 消费者 API (Consumer API) ====================
            void renderTick(float deltaTime = 0.016667f);

            // ==================== 查询与遥测 API (Query & Telemetry API) ====================
            const RenderFrameState& getActiveFrameState() const noexcept;
            uint64_t getConsumedPacketCount() const noexcept;
            uint64_t getDroppedPacketCount() const noexcept;
            uint64_t getRetainedFrameCount() const noexcept;
            uint64_t getRenderedFrameCount() const noexcept;
            size_t getPendingComputationCount() const;
            size_t getInboundQueueSize() const;
            void setMaxPendingFrames(uint32_t frames) noexcept;
            uint32_t getMaxPendingFrames() const noexcept;

            uint64_t getTimedOutPacketCount() const noexcept { return getDroppedPacketCount(); }
            size_t getMailboxQueueSize() const { return getInboundQueueSize(); }

            // ==================== 帧截图与视觉验证 API (Frame Capture API) ====================
            /// @brief 帧截图默认跨线程同步超时时间 (毫秒)
            static constexpr uint32_t kCaptureWaitTimeoutMs = 5000;

            uint64_t requestFrameCapture(const std::string& outputPath,
                                         const std::string& targetName = "SceneColorTarget");
            bool hasPendingCaptures() const;
            bool waitForPendingCaptures(uint32_t timeoutMs = kCaptureWaitTimeoutMs);
            void processPendingCaptures(FCT::Context* ctx, FCT::Window* wnd);
            FrameCaptureStats getLastCaptureStats() const;

            PassManager passManager;

        private:
            void drainInboundQueue(std::vector<Computation<RenderPacket>>& outBatch);
            void processComputations(std::vector<Computation<RenderPacket>>& inboundBatch,
                                     RenderPacket& outLatestReadyPacket,
                                     bool& outHasReadyPacket);
            void applyReadyPacket(RenderPacket&& packet);
            void dropTimedOutComputations();
            void startReclaimThread();
            void stopReclaimThread();
            FrameCaptureStats executeSingleTargetCapture(FCT::Context* ctx,
                                                         FCT::Window* wnd,
                                                         const std::string& outputPath,
                                                         const std::string& targetName);

        private:
            uint64_t m_frameCount = 0;
            std::atomic<bool> m_initialized{false};
            bool m_cliScreenshotCaptured{false};

            // 渲染缓存与 GPU 驱动子系统
            UniquePtr<CameraSystem> m_cameraSystem;
            UniquePtr<MeshCacheSystem> m_meshRenderSystem;
            UniquePtr<SkyboxCacheSystem> m_skyboxCacheSystem;
            UniquePtr<MatrixCacheSystem> m_matrixCacheSystem;
            UniquePtr<LightingSystem> m_lightingSystem;
            UniquePtr<TextureCacheSystem> m_textureRenderSystem;
            UniquePtr<ShininessSystem> m_shininessSystem;
            UniquePtr<TextureSamplerSystem> m_textureSamplerSystem;
            UniquePtr<GpuDrivenSystem> m_gpuDrivenSystem;

            // 内置布局、采样器与几何网格
            FCT::VertexLayout m_vertexLayout = StandardMeshVertexLayout;
            FCT::Sampler* m_shadowSampler = nullptr;
            FCT::Sampler* m_diffuseSampler = nullptr;
            FCT::StaticMesh<uint32_t>* m_skyboxMesh = nullptr;
            FCT::StaticMesh<uint32_t>* m_fullScreenMesh = nullptr;
            FCT::OutputInfo m_shadowPassOutputInfo;
            FCT::OutputInfo m_objectPassOutputInfo;

            // 帧截图跨线程队列与同步状态
            mutable std::mutex m_captureMutex;
            std::condition_variable m_captureCv;
            std::vector<PendingFrameCapture> m_pendingCaptures;
            uint64_t m_nextCaptureRequestId{1};
            uint64_t m_latestCaptureRequestId{0};       ///< 最新已分配的截图请求 ID (受 m_captureMutex 保护)
            uint64_t m_completedCaptureRequestId{0};   ///< 最新已完成回读并落盘的截图请求 ID
            FrameCaptureStats m_lastCaptureStats;

            // 双缓冲互斥入站信箱队列
            mutable std::mutex m_mailboxMutex;
            std::vector<Computation<RenderPacket>> m_inboundQueue;
            std::vector<Computation<RenderPacket>> m_consumerBatch;

            // 异步延迟垃圾回收机制
            std::thread m_reclaimThread;
            std::mutex m_reclaimMutex;
            std::condition_variable m_reclaimCv;
            std::vector<std::vector<Computation<RenderPacket>>> m_reclaimBatches;
            std::vector<std::vector<Computation<RenderPacket>>> m_idleBatches;
            std::atomic<bool> m_reclaimRunning{false};

            // 渲染线程私有未决计算队列
            std::vector<PendingComputation> m_pendingComputations;

            // 活跃持久帧状态
            RenderFrameState m_activeFrameState;

            // 遥测与控制计数器
            std::atomic<uint64_t> m_consumedPacketCount{0};
            std::atomic<uint64_t> m_droppedPacketCount{0};
            std::atomic<uint64_t> m_retainedFrameCount{0};
            std::atomic<uint64_t> m_renderedFrameCount{0};
            std::atomic<uint32_t> m_maxPendingFrames{120};
        };

    } // namespace Render
} // namespace MQEngine

#endif // ENGINE_RENDERER_RENDERER_H
