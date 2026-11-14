/**
 * @file RendererSubsystem.h
 * @brief 渲染子域：RenderGraph 与 PassManager/PipelineManager 的独占管理者，
 *        向 Composite 插件暴露渲染图动态拓扑修改接口，统管高层视口、相机与材质管线
 */
#ifndef MQENGINE_DOMAIN_RENDERER_RENDERERSUBSYSTEM_H
#define MQENGINE_DOMAIN_RENDERER_RENDERERSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/renderer/BindlessSet0Manager.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/ShadowAtlas.h>
#include <MQEngine/domain/renderer/clustering/ClusteredForwardPlus.h>
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            /**
             * @brief 标准 3D 静态网格顶点布局（对齐 FCT StandardMeshVertexLayout）
             */
            inline constexpr FCT::VertexLayout StandardMeshVertexLayout = {
                FCT::VertexElement{FCT::VtxType::Color4f, "color"},
                FCT::VertexElement{FCT::VtxType::Position4f, "position"},
                FCT::VertexElement{FCT::VtxType::TexCoord2f, "texCoord"},
                FCT::VertexElement{FCT::VtxType::Normal3f, "normal"},
                FCT::VertexElement{FCT::VtxType::Tangent3f, "tangent"},
                FCT::VertexElement{FCT::VtxType::Bitangent3f, "bitangent"},
                FCT::VertexElement{FCT::VtxType::BoneIds4i, "boneIds"},
                FCT::VertexElement{FCT::VtxType::BoneWeights4f, "boneWeights"}
            };

            /**
             * @brief 标准像素着色器插值输入布局
             */
            inline constexpr FCT::PixelLayout StandardPixelLayout = {
                StandardMeshVertexLayout,
                FCT::VertexElement{FCT::VtxType::Custom, "worldPos", FCT::Format::R32G32B32A32_SFLOAT},
                FCT::VertexElement{FCT::VtxType::Custom, "shadowPos", FCT::Format::R32G32B32A32_SFLOAT},
                FCT::VertexElement{FCT::VtxType::Custom, "instanceId", FCT::Format::R32_UINT}
            };

            /**
             * @brief 相机视图与投影矩阵常量缓冲区槽位
             */
            inline constexpr FCT::UniformSlot CameraUniformSlot{
                "CameraUniform",
                FCT::UniformVar{FCT::UniformType::ProjectionMatrix},
                FCT::UniformVar{FCT::UniformType::ViewMatrix}
            };

            /**
             * @brief 模型世界矩阵与法线逆转置矩阵常量缓冲区槽位
             */
            inline constexpr FCT::UniformSlot ModelUniformSlot{
                "ModelUniform",
                FCT::UniformVar{FCT::UniformType::ModelMatrix, "modelMatrix"},
                FCT::UniformVar{FCT::UniformType::Mat4, "modelInverseTransposeMatrix"}
            };

            /**
             * @brief 主方向光与场景点光源常量缓冲区槽位
             */
            inline constexpr FCT::UniformSlot DirectionalLightUniformSlot{
                "DirectionalLightUniform",
                FCT::UniformVar{FCT::UniformType::Vec3, "directionalLightDirection"},
                FCT::UniformVar{FCT::UniformType::Vec4, "directionalLightColorIntensity"},
                FCT::UniformVar{FCT::UniformType::Float, "directionalLightEnable"},
                FCT::UniformVar{FCT::UniformType::Vec4, "pointLightPosRange0"},
                FCT::UniformVar{FCT::UniformType::Vec4, "pointLightColorIntensity0"},
                FCT::UniformVar{FCT::UniformType::Vec4, "pointLightPosRange1"},
                FCT::UniformVar{FCT::UniformType::Vec4, "pointLightColorIntensity1"}
            };

            /**
             * @brief 观察者世界空间位置常量缓冲区槽位
             */
            inline constexpr FCT::UniformSlot ViewPosUniformSlot{
                "ViewPosUniform",
                FCT::UniformVar{FCT::UniformType::Vec3, "viewPos"}
            };

            /**
             * @brief PBR 材质参数常量缓冲区槽位
             */
            inline constexpr FCT::UniformSlot PBRMaterialUniformSlot{
                "PBRMaterialUniform",
                FCT::UniformVar{FCT::UniformType::Vec4, "baseColorFactor"},
                FCT::UniformVar{FCT::UniformType::Vec4, "pbrFactors"},
                FCT::UniformVar{FCT::UniformType::Vec4, "emissiveFactor"}
            };

            /**
             * @brief 渲染提交线程单实体绘制快照项
             */
            struct RenderEntityDrawItem
            {
                int primitiveType = 0;
                FCT::StaticMesh<uint32_t>* customMesh = nullptr;
                uint32_t vertexCount = 0;
                uint32_t triangleCount = 0;
                bool cameraVisible = true;
                bool castShadows = true;
                FCT::Vec3<> localHalfExtents{0.5f, 0.5f, 0.5f};
                FCT::Mat4 modelMatrix;
                FCT::Mat4 modelInverseTransposeMatrix;
                FCT::Vec4 baseColorFactor{0.8f, 0.8f, 0.85f, 1.0f};
                FCT::Vec4 pbrFactors{0.1f, 0.45f, 1.0f, 0.0f};
                FCT::Vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 0.0f};
            };

            /**
             * @brief 已加载并上传至 GPU 的 3D 模型网格资产描述信息
             */
            struct LoadedModelMeshInfo
            {
                std::string assetPath;
                FCT::StaticMesh<uint32_t>* mesh = nullptr;
                uint32_t vertexCount = 0;
                uint32_t indexCount = 0;
                uint32_t triangleCount = 0;
                FCT::Vec3<> localAabbMin{-0.5f, -0.5f, -0.5f};
                FCT::Vec3<> localAabbMax{ 0.5f,  0.5f,  0.5f};
            };

            /**
             * @brief 渲染管线单帧遥测与性能统计数据
             */
            struct RenderFrameStats
            {
                uint32_t totalSceneEntities = 0;
                uint32_t visibleEntities = 0;
                uint32_t culledEntities = 0;
                uint32_t drawCalls = 0;
                uint32_t totalVertices = 0;
                uint32_t totalTriangles = 0;
                uint32_t cachedModelCount = 0;
                float cullingDurationMs = 0.0f;
                float uniformUpdateMs = 0.0f;
                float objectPassRecordMs = 0.0f;
                float toneMapPassRecordMs = 0.0f;
            };

            /**
             * @brief 自定义渲染通道回调函数
             */
            using CustomPassCallback = std::function<void(FCT::RenderGraph*)>;
            using KeepImageCallback = std::function<void(FCT::RenderGraph*)>;
            using PassSubmitCallback = std::function<void(FCT::PassSubmitEvent)>;
            using RenderSyncCallback = std::function<void()>;

            /**
             * @brief 渲染通道挂起请求项
             */
            struct NamedCustomPass
            {
                std::string name;
                CustomPassCallback callback;
            };

            struct NamedPassSubscription
            {
                std::string passName;
                PassSubmitCallback callback;
            };

            /**
             * @brief 逻辑线程与渲染线程同步栅栏节点描述（挂载于 FCT::Context::syncTickers()）
             */
            struct RenderSyncNodeDesc
            {
                std::string name;
                RenderSyncCallback callback;
                std::vector<std::string> predecessors;
                std::vector<std::string> successors;
            };

            /**
             * @brief 面向 Composite 插件的结构化 RenderGraph 管线扩展描述：
             *        替代旧版散乱在 Application 上的 RenderCallBack 事件分发器，
             *        将 Pass 拓扑注入、编译后纹理保留、跨线程 SyncTicker 栅栏与 Pass 命令录制统一收归 Domain::Renderer 管控，
             *        并支持按扩展名动态注册与卸载清理。
             */
            struct RenderGraphExtensionDesc
            {
                std::string name;
                CustomPassCallback onSetupPass;
                KeepImageCallback onPostCompile;
                std::vector<RenderSyncNodeDesc> syncNodes;
                std::vector<NamedPassSubscription> passSubscribers;
            };

            struct PendingFrameCapture
            {
                uint64_t requestId = 0;
                std::string outputPath;
                std::string targetName;
            };

            /**
             * @brief 图形管线对象 (PSO) 描述结构：供运行时 Layout 状态绑定与编辑器管线对象面板实时检视/修改
             */
            struct PipelineObjectDesc
            {
                std::string name;
                std::string passName;
                bool isBuiltin = false;
                bool hasColorAttachment = true;
                bool hasVertexShader = true;
                bool hasPixelShader = true;
                std::string vertexLayoutSummary = "StandardMeshVertexLayout (96B)";
                std::string pixelLayoutSummary = "StandardPixelLayout";

                FCT::PolygonMode polygonMode = FCT::PolygonMode::Fill;
                FCT::CullMode cullMode = FCT::CullMode::None;
                FCT::FrontFace frontFace = FCT::FrontFace::CounterClockwise;
                float lineWidth = 1.0f;
                bool depthClampEnable = false;
                bool depthBiasEnable = false;
                float depthBiasConstant = 0.0f;
                float depthBiasClamp = 0.0f;
                float depthBiasSlope = 0.0f;

                bool depthTestEnable = true;
                bool depthWriteEnable = true;
                FCT::CompareOp depthCompareOp = FCT::CompareOp::LessOrEqual;
                bool depthBoundsTestEnable = false;
                bool stencilTestEnable = false;

                bool blendEnable = false;
                FCT::BlendFactor srcColorBlend = FCT::BlendFactor::One;
                FCT::BlendFactor dstColorBlend = FCT::BlendFactor::Zero;
                FCT::BlendOp colorBlendOp = FCT::BlendOp::Add;
                FCT::BlendFactor srcAlphaBlend = FCT::BlendFactor::One;
                FCT::BlendFactor dstAlphaBlend = FCT::BlendFactor::Zero;
                FCT::BlendOp alphaBlendOp = FCT::BlendOp::Add;
                uint8_t colorWriteMask = 0xF;
            };

            /**
             * @brief 每帧活跃相机快照条目（按 targetName 分组与 priority 降序排列）
             */
            struct ActiveCameraEntry
            {
                std::string registryName;
                uint32_t entityId = 0;
                std::string targetName = "MainWindow";
                int priority = 0;
                bool orthographic = false;
                float orthoSize = 24.0f;
                float fov = 60.0f;
                float nearPlane = 0.1f;
                float farPlane = 1000.0f;
                bool reverseZ = false;
                FCT::Vec4 clearColor{0.12f, 0.14f, 0.18f, 1.0f};
                FCT::Vec3<> worldPosition{0.0f, 0.0f, 0.0f};
                FCT::Mat4 viewMatrix;
                FCT::Mat4 projMatrix;
            };

            /**
             * @brief 相机渲染目标绑定描述（将具名目标路由到离屏或主窗口 RT 与分辨率）
             */
            struct RenderTargetBindingDesc
            {
                std::string targetName;
                FCT::Image* colorTarget = nullptr;
                FCT::Image* depthStencilTarget = nullptr;
                uint32_t width = 0;
                uint32_t height = 0;
                int32_t viewportX = 0;
                int32_t viewportY = 0;
            };

            /**
             * @brief 渲染子系统核心类
             */
            class ENGINE_API RendererSubsystem
            {
            public:
                RendererSubsystem();
                ~RendererSubsystem();

                RendererSubsystem(const RendererSubsystem&) = delete;
                RendererSubsystem& operator=(const RendererSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                static RendererSubsystem* InstancePtr() noexcept;

                // ==================== 1. 向 Composite 插件开放的 RenderGraph 动态扩展接口 ====================
                /**
                 * @brief 注册结构化 RenderGraph 管线扩展（支持在编译前或编译后注册，卸载时通过 unregisterRenderGraphExtension 清理）
                 */
                void registerRenderGraphExtension(RenderGraphExtensionDesc extension);

                /**
                 * @brief 按名称注销结构化 RenderGraph 管线扩展并清理关联的 SyncTicker 节点
                 */
                void unregisterRenderGraphExtension(const std::string& name);

                /**
                 * @brief 请求向 RenderGraph 追加自定义 Pass
                 */
                void requestAddCustomPass(const std::string& name, CustomPassCallback callback);

                /**
                 * @brief 请求向 RenderGraph 追加图像保留回调（如 UI 合成或视口读取）
                 */
                void requestKeepImage(KeepImageCallback callback);

                /**
                 * @brief 请求订阅特定 Pass 提交事件
                 */
                void requestSubscribePass(const std::string& passName, PassSubmitCallback callback);

                /**
                 * @brief 请求向 Context::syncTickers() 注册跨线程同步节点
                 */
                void requestSyncTicker(
                    const std::string& name,
                    RenderSyncCallback callback,
                    std::vector<std::string> predecessors = {},
                    std::vector<std::string> successors = {});

                /**
                 * @brief 从 Context::syncTickers() 移除指定名称的跨线程同步节点
                 */
                void unregisterSyncTicker(const std::string& name);

                /**
                 * @brief 编译并绑定 RenderGraph 拓扑
                 */
                Status compileAndBindGraph();

                /**
                 * @brief 热编译并切换 ObjectPass 自定义像素着色器
                 */
                Status updateCustomPixelShader(const std::string& hlslBody);

                /**
                 * @brief 获取底层 RenderGraph 对象指针
                 */
                FCT::RenderGraph* getRenderGraph() const noexcept { return m_renderGraph; }

                /**
                 * @brief 获取 ObjectPass 的 Layout 对象指针（供 ShaderEditor 生成/编译着色器使用）
                 */
                FCT::Layout* getObjectPassLayout() const noexcept { return m_objectPassLayout.get(); }

                // ==================== 4. M3 纯数据 RenderGraph 与 Clustered Forward+ / GPUScene ====================
                RenderGraphMatcher& getMatcher() noexcept { return m_matcher; }
                const RenderGraphMatcher& getMatcher() const noexcept { return m_matcher; }

                ClusteredForwardPlus& getClusteredForwardPlus() noexcept { return m_clusteredForwardPlus; }
                const ClusteredForwardPlus& getClusteredForwardPlus() const noexcept { return m_clusteredForwardPlus; }

                ShadowAtlas& getShadowAtlas() noexcept { return m_shadowAtlas; }
                const ShadowAtlas& getShadowAtlas() const noexcept { return m_shadowAtlas; }

                GPUScene& getGpuScene() noexcept { return m_gpuScene; }
                const GPUScene& getGpuScene() const noexcept { return m_gpuScene; }

                Status loadRenderGraphPreset(std::string_view presetName);
                Status setRenderGraphAsset(RenderGraphAssetDesc desc);
                const RenderGraphAssetDesc& getRenderGraphAsset() const noexcept { return m_activeGraphAsset; }
                bool isUsingDataDrivenGraph() const noexcept { return m_useDataDrivenGraph; }

                void submitDrawBucket(DrawBucket bucket);
                const std::vector<DrawBucket>& getFrameDrawBuckets() const noexcept { return m_frameDrawBuckets; }
                void clearFrameDrawBuckets() noexcept { m_frameDrawBuckets.clear(); }

                // ==================== 2. 外部渲染目标与离屏设置 ====================
                /**
                 * @brief 设置外部目标（如视口离屏渲染目标纹理指针）
                 */
                void setExternalTargets(FCT::Image* colorTarget, FCT::Image* depthStencilTarget = nullptr);

                FCT::Image* getExternalColorTarget() const noexcept
                {
                    return m_externalColorTarget.load(std::memory_order_acquire);
                }
                FCT::Image* getExternalDepthStencilTarget() const noexcept
                {
                    return m_externalDepthStencilTarget.load(std::memory_order_acquire);
                }

                /**
                 * @brief 将具名相机目标（如 "MainWindow" / "EditorViewport" / 离屏 RT 名）绑定到具体渲染目标与分辨率
                 */
                void bindRenderTarget(
                    const std::string& targetName,
                    FCT::Image* colorTarget,
                    FCT::Image* depthStencilTarget = nullptr,
                    uint32_t width = 0,
                    uint32_t height = 0,
                    int32_t viewportX = 0,
                    int32_t viewportY = 0);
                void unbindRenderTarget(std::string_view targetName);
                void setPrimaryPresentTargetName(std::string targetName);
                std::string getPrimaryPresentTargetName() const;
                std::optional<RenderTargetBindingDesc> getRenderTargetBinding(std::string_view targetName) const;

                // ==================== 3. 相机与视口管理 ====================
                /**
                 * @brief 获取当前帧所有已激活相机的排序快照（按 targetName 升序、priority 降序）
                 */
                std::vector<ActiveCameraEntry> getActiveCameras() const;

                /**
                 * @brief 按目标名查询最高优先级的激活相机（无激活相机时返回 std::nullopt）
                 */
                std::optional<ActiveCameraEntry> findActiveCameraForTarget(std::string_view targetName) const;

                /**
                 * @brief 当前主呈现目标是否存在已激活相机（含传统兼容矩阵覆盖）
                 */
                bool hasActiveCameraForPrimaryTarget() const noexcept
                {
                    return m_hasCameraOverride.load(std::memory_order_acquire) ||
                           m_hasActivePrimaryCamera.load(std::memory_order_acquire);
                }

                /**
                 * @brief 从 SceneSubsystem 同步刷新激活相机列表快照
                 */
                void refreshActiveCameras();

                // Note: 仅作旧单测兼容保留，编辑器与业务插件应驱动 CameraComponent 实体与 Target 绑定
                void setViewMatrix(const FCT::Mat4& view) noexcept
                {
                    m_viewMatrix = view;
                    m_hasCameraOverride.store(true, std::memory_order_release);
                }
                const FCT::Mat4& getViewMatrix() const noexcept { return m_viewMatrix; }

                void setCameraWorldPosition(const FCT::Vec3<>& pos) noexcept { m_cameraWorldPos = pos; }
                const FCT::Vec3<>& getCameraWorldPosition() const noexcept { return m_cameraWorldPos; }

                void setProjectionMatrix(const FCT::Mat4& proj) noexcept { m_projMatrix = proj; }
                const FCT::Mat4& getProjectionMatrix() const noexcept { return m_projMatrix; }

                void setCameraOverrideEnabled(bool enabled) noexcept
                {
                    m_hasCameraOverride.store(enabled, std::memory_order_release);
                }
                void clearCameraOverride() noexcept
                {
                    m_hasCameraOverride.store(false, std::memory_order_release);
                }
                bool hasCameraOverride() const noexcept
                {
                    return m_hasCameraOverride.load(std::memory_order_acquire);
                }

                FCT::Mat4 getActiveViewMatrix() const noexcept;
                FCT::Mat4 getActiveProjectionMatrix() const noexcept;

                static FCT::Mat4 buildViewMatrix(
                    const FCT::Vec3<>& eye,
                    const FCT::Vec3<>& target,
                    const FCT::Vec3<>& worldUp = FCT::Vec3<>(0.0f, 1.0f, 0.0f)) noexcept;
                static FCT::Mat4 buildPerspectiveProjection(
                    float fovYDeg,
                    float aspect,
                    float zNear,
                    float zFar,
                    bool reverseZ = false) noexcept;
                static FCT::Mat4 buildOrthoProjection(
                    float halfWidth,
                    float halfHeight,
                    float zNear,
                    float zFar,
                    bool reverseZ = false) noexcept;

                void setViewportSize(uint32_t width, uint32_t height) noexcept
                {
                    m_viewportWidth.store(width, std::memory_order_release);
                    m_viewportHeight.store(height, std::memory_order_release);
                }
                uint32_t getViewportWidth() const noexcept
                {
                    return m_viewportWidth.load(std::memory_order_acquire);
                }
                uint32_t getViewportHeight() const noexcept
                {
                    return m_viewportHeight.load(std::memory_order_acquire);
                }

                void setAntiAliasingEnabled(bool enabled) noexcept
                {
                    m_enableAntiAliasing.store(enabled, std::memory_order_release);
                }
                bool isAntiAliasingEnabled() const noexcept
                {
                    return m_enableAntiAliasing.load(std::memory_order_acquire);
                }

                // ==================== 4. 帧截图与视觉验证 ====================
                uint64_t requestFrameCapture(const std::string& outputPath, const std::string& targetName = "SceneColorTarget");
                void scheduleFrameCapture(const std::string& outputPath, const std::string& targetName = "mainWindowColor", uint64_t captureAtFrame = 4);
                bool hasPendingCaptures() const;
                bool waitForPendingCaptures(uint32_t timeoutMs = 5000);
                void processPendingCaptures(FCT::Context* ctx, FCT::Window* wnd = nullptr);

                // ==================== 5. Bindless Set 0 与硬件能力 ====================
                BindlessSet0Manager* getBindlessManager() const noexcept { return m_bindlessManager.get(); }
                DescriptorIndexingTier getDescriptorIndexingTier() const noexcept
                {
                    return m_bindlessManager ? m_bindlessManager->getTier() : DescriptorIndexingTier::Tier0_AtlasFallback;
                }

                // ==================== 6. RenderGraph ResourceGroup 动态资源组 ====================
                FCT::ResourceGroup* getOrCreateResourceGroup(const std::string& groupName);
                FCT::Image* requireGroupResource(const std::string& groupName, const FCT::ResourceRequireDesc& desc);
                void releaseGroupResource(const std::string& groupName, const std::string& resourceId);
                std::vector<FCT::Image*> getActiveGroupResources(const std::string& groupName) const;

                // ==================== 7. 3D 模型网格加载缓存与实时帧性能统计 ====================
                FCT::StaticMesh<uint32_t>* loadOrGetModelMesh(
                    const std::string& assetPath,
                    LoadedModelMeshInfo* outInfo = nullptr);
                bool getLoadedModelMeshInfo(const std::string& assetPath, LoadedModelMeshInfo& outInfo) const;
                std::vector<LoadedModelMeshInfo> getAllLoadedModelMeshes() const;
                RenderFrameStats getFrameStats() const noexcept;

                // ==================== 8. 管线对象 (PSO) 注册表与实时状态同步 ====================
                std::vector<PipelineObjectDesc> getPipelineObjects() const;
                bool getPipelineObject(std::string_view name, PipelineObjectDesc& outDesc) const;
                void upsertPipelineObject(const PipelineObjectDesc& desc);
                bool removePipelineObject(std::string_view name);
                void resetBuiltinPipelineObjects();

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

            private:
                struct PassRuntimeStates
                {
                    FCT::RasterizationState* rasterState = nullptr;
                    FCT::DepthStencilState* depthStencilState = nullptr;
                    FCT::BlendState* blendState = nullptr;
                };

                static constexpr size_t kSnapshotRingSlots = 4;

                struct TargetBindingSnapshot
                {
                    std::unordered_map<std::string, RenderTargetBindingDesc> bindings;
                    std::string primaryPresentTargetName = "MainWindow";
                };

                struct CameraFrameSnapshot
                {
                    std::vector<ActiveCameraEntry> cameras;
                    FCT::Mat4 activeViewMatrix;
                    FCT::Mat4 activeProjMatrix;
                    FCT::Vec3<> cameraWorldPos{0.0f, 4.5f, -9.5f};
                    bool hasActivePrimaryCamera = false;
                };

                struct SubmitSceneFrameSnapshot
                {
                    uint32_t viewportWidth = 1920;
                    uint32_t viewportHeight = 1080;
                    bool hasRenderCamera = false;
                    bool activeReverseZ = false;
                    bool enableAntiAliasing = true;
                    FCT::Mat4 activeView;
                    FCT::Mat4 activeProj;
                    FCT::Vec3<> activeCamPos{0.0f, 4.5f, -9.5f};

                    FCT::Vec3<> lightDir{-0.42f, -0.74f, 0.52f};
                    FCT::Vec4 lightColorIntensity{1.0f, 0.94f, 0.84f, 4.2f};
                    float lightEnable = 1.0f;
                    float shadowEnable = 1.0f;
                    FCT::Vec4 ptPosRange[2]{};
                    FCT::Vec4 ptColorIntensity[2]{};

                    std::vector<RenderEntityDrawItem> drawItems;
                    RenderFrameStats frameStats{};
                };

                template <typename TSnapshot>
                static void publishRingSnapshot(
                    std::array<TSnapshot, kSnapshotRingSlots>& ring,
                    std::atomic<uint32_t>& publishedSlot,
                    TSnapshot snapshot)
                {
                    const uint32_t nextSlot =
                        (publishedSlot.load(std::memory_order_relaxed) + 1u) %
                        static_cast<uint32_t>(kSnapshotRingSlots);
                    ring[nextSlot] = std::move(snapshot);
                    publishedSlot.store(nextSlot, std::memory_order_release);
                }

                void initDefaultPipelineObjects();
                void rebuildDirtyPipelineStates(FCT::Context* ctx);
                PassRuntimeStates buildRuntimeStatesForDesc(FCT::Context* ctx, const PipelineObjectDesc& desc);
                void initGpuResources(FCT::Context* ctx);
                void createBuiltinMeshes(FCT::Context* ctx);
                void ensureSampleModelAssetsOnDisk();
                void initPassLayouts(FCT::Context* ctx);
                void cachePassShaders();
                void registerRenderTickers(FCT::Context* ctx);
                void syncFrameToSubmitThread(FCT::Context* ctx);
                void extractSceneLightingAndDrawItems(
                    bool hasRenderCamera,
                    const FCT::Mat4& activeView,
                    const FCT::Mat4& activeProj,
                    bool activeReverseZ,
                    SubmitSceneFrameSnapshot& outSnapshot);
                void updateRenderUniformsOnSubmitThread(FCT::Context* ctx);
                void recordShadowMapPass(FCT::PassSubmitEvent env);
                void recordObjectPass(FCT::PassSubmitEvent env);
                void recordToneMappingPass(FCT::PassSubmitEvent env);
                void recordAntiAliasingPass(FCT::PassSubmitEvent env);
                void releaseGpuResources() noexcept;
                void publishTargetBindingSnapshot();
                void publishCameraSnapshot(
                    std::vector<ActiveCameraEntry> cameras,
                    const FCT::Mat4& activeView,
                    const FCT::Mat4& activeProj,
                    const FCT::Vec3<>& camPos,
                    bool hasActivePrimary);
                void publishPipelineObjectSnapshot();
                void publishFrameStatsSnapshot(const RenderFrameStats& stats);
                void collectSortedActiveCameras(
                    uint32_t fallbackWidth,
                    uint32_t fallbackHeight,
                    const std::unordered_map<std::string, RenderTargetBindingDesc>& bindings,
                    std::vector<ActiveCameraEntry>& outEntries) const;
                static bool findCameraInSortedList(
                    const std::vector<ActiveCameraEntry>& entries,
                    std::string_view targetName,
                    ActiveCameraEntry& outEntry);
                bool executeSingleTargetCapture(
                    FCT::Context* ctx,
                    FCT::Window* wnd,
                    const std::string& outputPath,
                    const std::string& targetName);

                std::atomic<bool> m_initialized{false};
                FCT::RenderGraph* m_renderGraph = nullptr;
                bool m_isGraphBuilt = false;
                bool m_isGraphCompiled = false;
                bool m_tickersRegistered = false;

                std::atomic<FCT::Image*> m_externalColorTarget{nullptr};
                std::atomic<FCT::Image*> m_externalDepthStencilTarget{nullptr};
                std::unordered_map<std::string, RenderTargetBindingDesc> m_logicTargetBindings;
                std::string m_logicPrimaryPresentTargetName = "MainWindow";
                std::array<TargetBindingSnapshot, kSnapshotRingSlots> m_targetBindingSnapshots{};
                std::atomic<uint32_t> m_publishedTargetBindingSlot{0};

                FCT::DepthStencilState* m_depthStencilState = nullptr;
                FCT::DepthStencilState* m_noDepthState = nullptr;
                FCT::Sampler* m_linearSampler = nullptr;

                FCT::StaticMesh<uint32_t>* m_cubeMesh = nullptr;
                FCT::StaticMesh<uint32_t>* m_planeMesh = nullptr;
                FCT::StaticMesh<uint32_t>* m_sphereMesh = nullptr;
                FCT::StaticMesh<uint32_t>* m_fullScreenMesh = nullptr;

                UniquePtr<FCT::Layout> m_shadowPassLayout;
                UniquePtr<FCT::Layout> m_objectPassLayout;
                UniquePtr<FCT::Layout> m_toneMappingLayout;
                UniquePtr<FCT::Layout> m_antiAliasingLayout;
                FCT::ShaderRef m_shadowVsRef;
                FCT::ShaderRef m_objectVsRef;
                FCT::ShaderRef m_objectPsRef;
                FCT::ShaderRef m_toneMappingVsRef;
                FCT::ShaderRef m_toneMappingPsRef;
                FCT::ShaderRef m_antiAliasingVsRef;
                FCT::ShaderRef m_antiAliasingPsRef;
                std::string m_logicCustomObjectPsHlsl;
                std::atomic<bool> m_logicCustomObjectPsDirty{false};
                std::string m_submitPendingCustomObjectPsHlsl;
                bool m_submitCustomObjectPsDirty = false;

                UniquePtr<FCT::Uniform> m_cameraUniform;
                UniquePtr<FCT::Uniform> m_lightUniform;
                UniquePtr<FCT::Uniform> m_viewPosUniform;
                std::vector<UniquePtr<FCT::Uniform>> m_modelUniformPool;
                std::vector<UniquePtr<FCT::Uniform>> m_materialUniformPool;
                std::vector<RenderEntityDrawItem> m_renderDrawItems;

                FCT::OutputInfo m_shadowPassOutputInfo{};
                FCT::OutputInfo m_objectPassOutputInfo{};
                FCT::OutputInfo m_toneMappingPassOutputInfo{};
                FCT::OutputInfo m_antiAliasingPassOutputInfo{};

                FCT::Mat4 m_viewMatrix;
                FCT::Mat4 m_projMatrix;
                FCT::Vec3<> m_cameraWorldPos{0.0f, 4.5f, -9.5f};
                std::array<CameraFrameSnapshot, kSnapshotRingSlots> m_cameraSnapshots{};
                std::atomic<uint32_t> m_publishedCameraSlot{0};
                std::atomic<bool> m_hasActivePrimaryCamera{false};
                std::atomic<bool> m_hasSyncedAtLeastOnce{false};
                std::atomic<bool> m_hasCameraOverride{false};
                std::atomic<bool> m_enableAntiAliasing{true};
                std::atomic<uint32_t> m_viewportWidth{1920};
                std::atomic<uint32_t> m_viewportHeight{1080};

                std::vector<RenderGraphExtensionDesc> m_extensions;
                std::unordered_map<std::string, std::vector<FCT::SubscribeId>> m_extensionSubscribeIds;
                std::vector<NamedCustomPass> m_customPasses;
                std::vector<KeepImageCallback> m_keepImageCallbacks;
                std::vector<NamedPassSubscription> m_passSubscriptions;
                std::vector<RenderSyncNodeDesc> m_syncNodes;

                std::atomic<uint64_t> m_nextCaptureId{1};
                std::atomic<uint64_t> m_latestCaptureId{0};
                std::atomic<uint64_t> m_completedCaptureId{0};
                std::vector<PendingFrameCapture> m_pendingCaptures;
                std::string m_cliScreenshotPath;
                std::string m_cliScreenshotTarget = "mainWindowColor";
                uint32_t m_cliScreenshotFrame = 20;
                std::atomic<uint64_t> m_submitFrameCounter{0};
                bool m_cliScreenshotTriggered = false;

                UniquePtr<BindlessSet0Manager> m_bindlessManager;
                std::unordered_map<std::string, UniquePtr<FCT::ResourceGroup>> m_resourceGroups;

                std::unordered_map<std::string, LoadedModelMeshInfo> m_loadedModelMeshes;
                SubmitSceneFrameSnapshot m_submitFrameSnapshot{};
                RenderFrameStats m_frameStats{};
                std::array<RenderFrameStats, kSnapshotRingSlots> m_frameStatsSnapshots{};
                std::atomic<uint32_t> m_publishedFrameStatsSlot{0};
                std::atomic<uint32_t> m_cachedModelCount{0};

                std::vector<PipelineObjectDesc> m_logicPipelineObjects;
                std::atomic<bool> m_logicPipelineObjectsDirty{true};
                std::array<std::vector<PipelineObjectDesc>, kSnapshotRingSlots> m_pipelineObjectSnapshots{};
                std::atomic<uint32_t> m_publishedPipelineObjectSlot{0};
                std::vector<PipelineObjectDesc> m_submitPipelineObjectsPending;
                bool m_submitPipelineObjectsDirty = true;
                PassRuntimeStates m_shadowPassStates{};
                PassRuntimeStates m_objectPassStates{};
                PassRuntimeStates m_toneMappingPassStates{};
                PassRuntimeStates m_antiAliasingPassStates{};
                std::vector<FCT::IPipelineResource*> m_ownedPipelineStateResources;

                RenderGraphMatcher m_matcher;
                ClusteredForwardPlus m_clusteredForwardPlus;
                ShadowAtlas m_shadowAtlas;
                GPUScene m_gpuScene;
                RenderGraphAssetDesc m_activeGraphAsset;
                bool m_useDataDrivenGraph{false};
                std::vector<DrawBucket> m_frameDrawBuckets;
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

namespace FCT
{
    using MQEngine::Domain::Renderer::CameraUniformSlot;
    using MQEngine::Domain::Renderer::ModelUniformSlot;
} // namespace FCT

#endif // MQENGINE_DOMAIN_RENDERER_RENDERERSUBSYSTEM_H
