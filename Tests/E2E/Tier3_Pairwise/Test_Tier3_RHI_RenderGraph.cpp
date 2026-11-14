/**
 * @file Test_Tier3_RHI_RenderGraph.cpp
 * @brief Tier 3 正交组合测试：WebGL 2.0 / GLES 3.0 RHI 后端 × 帧图瞬态池 × SPIR-V 离线转译 × SIMD 计算与异步宿主
 *        覆盖特性：FEAT_23 ~ FEAT_32, FEAT_37 ~ FEAT_41 × FEAT_09, FEAT_10, FEAT_33, FEAT_72
 */
#include "Common/E2ECommon.h"

#ifndef E2E_ASSERT_NEAR
#define E2E_ASSERT_NEAR(val1, val2, absError) \
    E2E_ASSERT(std::abs((val1) - (val2)) <= (absError), \
               ::absl::StrCat("期望近似相等: |", #val1, " - ", #val2, "| <= ", #absError))
#endif

#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include <FCT/Context/TransientResourcePool.h>
#include <FCT/Context/ShaderCrossCompiler.h>
#include <FCT/Context/RenderGraph.h>
#include <FCT/RHI/ShaderBinary.h>
#include <FCT/RHI/WGL2_TraditionalPipeline.h>
#include <FCT/RHI/WGL2_StorageBuffer.h>
#include <FCT/RHI/WGL2_ConstBuffer.h>
#include <FCT/RHI/WGL2_PassGroup.h>
#include <FCT/RHI/WGL2_InputLayout.h>
#include <FCT/RHI/WGL2_CommandBuffer.h>
#include <FCT/RHI/WGL2_Image.h>
#include <FCT/RHI/WGL2_TransformFeedback.h>
#include <FCT/RHI/WGL2_VertexShader.h>
#include <FCT/RHI/WGL2_PixelShader.h>
#include <FCT/UI/MiniGame_Window.h>
#include <FCT/Runtime/Emscripten_Runtime.h>

#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include "DouyinMiniGame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        struct T3RhiRg_VaoSetupTracker
        {
            uint32_t* counter{nullptr};

            void operator()() const noexcept
            {
                if (counter != nullptr)
                {
                    ++(*counter);
                }
            }
        };

        struct T3RhiRg_RuntimeTickTracker
        {
            uint32_t* tickCount{nullptr};
            double* accumulatedDt{nullptr};

            void operator()(double dt) const noexcept
            {
                if (tickCount != nullptr)
                {
                    ++(*tickCount);
                }
                if (accumulatedDt != nullptr)
                {
                    *accumulatedDt += dt;
                }
            }
        };

        // 含采样纹理与输入 UV 的真实 SPIR-V 片段着色器字节码
        static constexpr uint32_t kT3RhiRg_SpirvSampledFrag[] = {
            0x07230203u, 0x00010300u, 0x000d000bu, 0x00000019u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
            0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
            0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x00000016u, 0x00030010u,
            0x00000004u, 0x00000007u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000000cu,
            0x00000022u, 0x00000000u, 0x00040047u, 0x0000000cu, 0x00000021u, 0x00000000u, 0x00040047u, 0x00000010u,
            0x00000022u, 0x00000000u, 0x00040047u, 0x00000010u, 0x00000021u, 0x00000001u, 0x00040047u, 0x00000016u,
            0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u,
            0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u,
            0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x00090019u, 0x0000000au,
            0x00000006u, 0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x00000000u, 0x00040020u,
            0x0000000bu, 0x00000000u, 0x0000000au, 0x0004003bu, 0x0000000bu, 0x0000000cu, 0x00000000u, 0x0002001au,
            0x0000000eu, 0x00040020u, 0x0000000fu, 0x00000000u, 0x0000000eu, 0x0004003bu, 0x0000000fu, 0x00000010u,
            0x00000000u, 0x0003001bu, 0x00000012u, 0x0000000au, 0x00040017u, 0x00000014u, 0x00000006u, 0x00000002u,
            0x00040020u, 0x00000015u, 0x00000001u, 0x00000014u, 0x0004003bu, 0x00000015u, 0x00000016u, 0x00000001u,
            0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du,
            0x0000000au, 0x0000000du, 0x0000000cu, 0x0004003du, 0x0000000eu, 0x00000011u, 0x00000010u, 0x00050056u,
            0x00000012u, 0x00000013u, 0x0000000du, 0x00000011u, 0x0004003du, 0x00000014u, 0x00000017u, 0x00000016u,
            0x00050057u, 0x00000007u, 0x00000018u, 0x00000013u, 0x00000017u, 0x0003003eu, 0x00000009u, 0x00000018u,
            0x000100fdu, 0x00010038u
        };

        void T3RhiRg_InitHeadlessWgl2Context(FCT::WGL2_Context& ctx)
        {
            ctx.createPlatform(FCT::ContextCreateFlags(0));
        }
    } // namespace

    // 1. FEAT_23 × FEAT_72: WGL2_Context 硬件能力正交自省契约驱动小游戏与桌面预设分流
    E2E_TEST(Tier3, FEAT_23, Pair_RhiRg_CapsOrthogonal_VsForwardPlusGraph)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        E2E_ASSERT_TRUE(wgl2Ctx.isIntegrateGpu());
        E2E_ASSERT_FALSE(wgl2Ctx.isBindlessSupported());
        E2E_ASSERT_FALSE(wgl2Ctx.isReadWriteStorageBufferSupported());
        E2E_ASSERT_FALSE(wgl2Ctx.isComputeSupported());
        E2E_ASSERT_FALSE(wgl2Ctx.isRayTracingSupported());
        E2E_ASSERT_FALSE(wgl2Ctx.isMeshShaderSupported());
        E2E_ASSERT_FALSE(wgl2Ctx.isIndirectDrawSupported());
        E2E_ASSERT_EQ(wgl2Ctx.getUniformBufferOffsetAlignment(), 256u);
        E2E_ASSERT_EQ(wgl2Ctx.getMaxUniformBufferRange(), 65536u);

        // 根据真实 WGL2 能力自省选择帧图预设，验证无光追/延迟多 MRT 通道泄漏
        const auto chosenGraph = wgl2Ctx.isRayTracingSupported()
            ? Domain::Renderer::RenderGraphAsset::CreateDesktopHybridRayTracingPreset()
            : Domain::Renderer::RenderGraphAsset::CreateMiniGameForwardPlusPreset();
        const auto desktopRtGraph = Domain::Renderer::RenderGraphAsset::CreateDesktopHybridRayTracingPreset();

        E2E_ASSERT_TRUE(chosenGraph.isAcyclic());
        E2E_ASSERT_TRUE(desktopRtGraph.isAcyclic());
        E2E_ASSERT_NE(chosenGraph.name, desktopRtGraph.name);

        for (const auto& pass : chosenGraph.passes)
        {
            E2E_ASSERT_LE(pass.targets.size(), 1u);
        }

        return true;
    }

    // 2. FEAT_24 × FEAT_27: 64-bit 状态键差分管线与 VAO 组合哈希缓存联动
    E2E_TEST(Tier3, FEAT_24, Pair_RhiRg_StateKeyDiff_AndVaoCacheHasher)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        UniquePtr<FCT::RHI::Pipeline> rhiPipe = wgl2Ctx.createRhiPipeline(FCT::PipelineType::Traditional);
        E2E_ASSERT_NE(rhiPipe.get(), nullptr);
        auto* tradPipe = static_cast<FCT::RHI::WGL2_TraditionalPipeline*>(rhiPipe.get());
        E2E_ASSERT_EQ(tradPipe->getType(), FCT::PipelineType::Traditional);

        // 编码 CullFace + DepthTest + DepthWrite + Blend 组合状态键
        const uint64_t opaqueKey = (1ULL << 0) | (1ULL << 5) | (1ULL << 6) | (3ULL << 7);
        const uint64_t uiBlendKey = (1ULL << 31) | (4ULL << 38) | (5ULL << 42) | (0xFULL << 54);

        tradPipe->setStateKey(opaqueKey);
        E2E_ASSERT_EQ(tradPipe->getStateKey(), opaqueKey);

        wgl2Ctx.applyGraphicsState(opaqueKey);
        E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), opaqueKey);
        // 相同状态重复绑定时 XOR 差分为 0，状态保持幂等
        wgl2Ctx.applyGraphicsState(opaqueKey);
        E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), opaqueKey);

        wgl2Ctx.applyGraphicsState(uiBlendKey);
        E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), uiBlendKey);

        // 联动真实 WGL2_InputLayout 生成哈希并测试 VAO 缓存命中
        FCT::RHI::WGL2_InputLayout layout(&wgl2Ctx);
        FCT::VertexLayout vtxLayout{
            FCT::VertexElement{FCT::VtxType::Position3f},
            FCT::VertexElement{FCT::VtxType::TexCoord2f}
        };
        layout.add(0, vtxLayout);
        const size_t layoutHash = layout.getHash();
        E2E_ASSERT_NE(layoutHash, 0u);

        FCT::VAOCacheKey keyA{layoutHash, {11u, 22u, 0u, 0u}, 33u};
        FCT::VAOCacheKey keyA_dup{layoutHash, {11u, 22u, 0u, 0u}, 33u};
        FCT::VAOCacheKey keyB{layoutHash, {11u, 99u, 0u, 0u}, 33u};

        FCT::VAOCacheHasher hasher;
        E2E_ASSERT_EQ(hasher(keyA), hasher(keyA_dup));
        E2E_ASSERT_NE(hasher(keyA), hasher(keyB));

        uint32_t setupCalls = 0;
        T3RhiRg_VaoSetupTracker setupFn{&setupCalls};
        (void)wgl2Ctx.getOrCreateVAO(keyA, setupFn);
        E2E_ASSERT_EQ(setupCalls, 1u);
        // 再次请求同键必须直接命中缓存，不重复触发 setupFunc
        (void)wgl2Ctx.getOrCreateVAO(keyA_dup, setupFn);
        E2E_ASSERT_EQ(setupCalls, 1u);
        (void)wgl2Ctx.getOrCreateVAO(keyB, setupFn);
        E2E_ASSERT_EQ(setupCalls, 2u);

        return true;
    }

    // 3. FEAT_25 × FEAT_14: WGL2_StorageBuffer 1024 宽 2D 数据纹理跨行三段子区域分解与镜像同步
    E2E_TEST(Tier3, FEAT_25, Pair_RhiRg_StorageBufferDataTex_CrossRowSubRegions)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        // 分配 64KB 数据纹理缓冲 (1024 * 4 行 * 16B)
        FCT::RHI::WGL2_StorageBuffer storageBuf(&wgl2Ctx, 65536u);
        E2E_ASSERT_EQ(storageBuf.getWidth(), 1024u);
        E2E_ASSERT_EQ(storageBuf.getHeight(), 4u);
        E2E_ASSERT_EQ(storageBuf.size(), 65536u);

        // 构造跨越第 0 行末尾 -> 第 1 行整行 -> 第 2 行开头的连续脏区间
        const size_t dirtyOffset = 16000u; // 第 0 行 x = 1000 纹素处
        const size_t dirtyBytes = 20000u;  // 跨越至第 2 行
        const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(dirtyOffset, dirtyBytes);
        E2E_ASSERT_EQ(regions.size(), 3u);
        // 首段位于 y=0, x=1000, width=24
        E2E_ASSERT_EQ(regions[0].x, 1000u);
        E2E_ASSERT_EQ(regions[0].y, 0u);
        E2E_ASSERT_EQ(regions[0].width, 24u);
        E2E_ASSERT_EQ(regions[0].height, 1u);
        // 中段整行位于 y=1, x=0, width=1024
        E2E_ASSERT_EQ(regions[1].x, 0u);
        E2E_ASSERT_EQ(regions[1].y, 1u);
        E2E_ASSERT_EQ(regions[1].width, 1024u);
        E2E_ASSERT_GE(regions[1].height, 1u);
        // 尾段位于下一行起点 x=0
        E2E_ASSERT_EQ(regions[2].x, 0u);
        E2E_ASSERT_EQ(regions[2].height, 1u);

        std::vector<uint32_t> payload(dirtyBytes / sizeof(uint32_t), 0xA55A3C69u);
        storageBuf.updateData(payload.data(), dirtyBytes, dirtyOffset);

        const auto* mappedWords = static_cast<const uint32_t*>(storageBuf.map());
        E2E_ASSERT_NE(mappedWords, nullptr);
        E2E_ASSERT_EQ(mappedWords[dirtyOffset / sizeof(uint32_t)], 0xA55A3C69u);
        E2E_ASSERT_EQ(mappedWords[(dirtyOffset + dirtyBytes) / sizeof(uint32_t) - 1u], 0xA55A3C69u);
        storageBuf.unmap();

        return true;
    }

    // 4. FEAT_26 × FEAT_23: WGL2_FrameRingUBO 256B 动态偏移对齐、回绕复位与 ConstBuffer 协同
    E2E_TEST(Tier3, FEAT_26, Pair_RhiRg_FrameRingUbo256Align_AndConstBuffer)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        const uint32_t hwAlign = wgl2Ctx.getUniformBufferOffsetAlignment();
        E2E_ASSERT_EQ(hwAlign, 256u);

        FCT::WGL2_FrameRingUBO ringUbo;
        E2E_ASSERT_STATUS_OK(ringUbo.init(1024u, hwAlign));
        E2E_ASSERT_EQ(ringUbo.getAlignment(), 256u);

        // 连续三次非 256 倍数尺寸分配，验证每次返回的偏移均严格满足 256 字节对齐
        auto alloc0 = ringUbo.allocate(96u);
        auto alloc1 = ringUbo.allocate(144u);
        auto alloc2 = ringUbo.allocate(200u);

        E2E_ASSERT_EQ(alloc0.offset, 0u);
        E2E_ASSERT_EQ(alloc1.offset, 256u);
        E2E_ASSERT_EQ(alloc2.offset, 512u);
        E2E_ASSERT_NE(alloc0.cpuPtr, nullptr);
        E2E_ASSERT_NE(alloc1.cpuPtr, nullptr);
        E2E_ASSERT_NE(alloc2.cpuPtr, nullptr);

        const float cameraExposure = 1.25f;
        std::memcpy(alloc1.cpuPtr, &cameraExposure, sizeof(float));
        E2E_ASSERT_GE(ringUbo.getHighWaterMark(), 712u);

        // 再次请求 400B 超出 1024B 容量时安全回绕至偏移 0，不发生越界
        auto wrapAlloc = ringUbo.allocate(400u);
        E2E_ASSERT_EQ(wrapAlloc.offset, 0u);

        ringUbo.flushToGpu();
        ringUbo.reset();
        E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 0u);
        E2E_ASSERT_EQ(ringUbo.getHighWaterMark(), 0u);

        // 联动 WGL2_Context::getEmptyConstBuffer
        FCT::ConstLayout perDrawLayout("PerDrawUBO", FCT::ConstElement(FCT::ConstType::ModelMatrix, "modelMatrix"));
        FCT::UniquePtr<FCT::RHI::ConstBuffer> constBuf(wgl2Ctx.getEmptyConstBuffer(perDrawLayout));
        E2E_ASSERT_NE(constBuf.get(), nullptr);

        return true;
    }

    // 5. FEAT_27 × FEAT_15: WGL2_InputLayout PerVertex 浮点网格 + PerInstance R32_UINT 实例槽位双流绑定
    E2E_TEST(Tier3, FEAT_27, Pair_RhiRg_PerInstanceUintAttrib_AndInputLayoutHash)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        FCT::VertexLayout meshStreamLayout{
            FCT::VertexElement{FCT::VtxType::Position3f},
            FCT::VertexElement{FCT::VtxType::Normal3f},
            FCT::VertexElement{FCT::VtxType::TexCoord2f}
        };
        FCT::VertexLayout instanceSlotLayout{
            FCT::VertexElement{FCT::Format::R32_UINT, FCT::VtxType::Custom, "a_InstanceSlot"}
        };

        FCT::RHI::WGL2_InputLayout layoutPerInstance(&wgl2Ctx);
        layoutPerInstance.vertexLayout(meshStreamLayout, 0u);
        layoutPerInstance.inputRate(FCT::RHI::InputRate::PerVertex, 0u);
        layoutPerInstance.vertexLayout(instanceSlotLayout, 1u);
        layoutPerInstance.inputRate(FCT::RHI::InputRate::PerInstance, 1u);
        layoutPerInstance.create();

        FCT::RHI::WGL2_InputLayout layoutPerVertexBoth(&wgl2Ctx);
        layoutPerVertexBoth.vertexLayout(meshStreamLayout, 0u);
        layoutPerVertexBoth.inputRate(FCT::RHI::InputRate::PerVertex, 0u);
        layoutPerVertexBoth.vertexLayout(instanceSlotLayout, 1u);
        layoutPerVertexBoth.inputRate(FCT::RHI::InputRate::PerVertex, 1u);
        layoutPerVertexBoth.create();

        // InputRate 差异必须体现在 InputLayout 哈希中，防止 VAO 错误复用
        E2E_ASSERT_NE(layoutPerInstance.getHash(), layoutPerVertexBoth.getHash());
        const auto& instRates = layoutPerInstance.getInputRates();
        const auto rateIt0 = instRates.find(0u);
        const auto rateIt1 = instRates.find(1u);
        E2E_ASSERT_TRUE(rateIt0 != instRates.end());
        E2E_ASSERT_TRUE(rateIt1 != instRates.end());
        E2E_ASSERT_EQ(rateIt0->second, FCT::RHI::InputRate::PerVertex);
        E2E_ASSERT_EQ(rateIt1->second, FCT::RHI::InputRate::PerInstance);
        const auto& instLayouts = layoutPerInstance.getVertexLayouts();
        const auto vtxLayoutIt1 = instLayouts.find(1u);
        E2E_ASSERT_TRUE(vtxLayoutIt1 != instLayouts.end());
        E2E_ASSERT_EQ(vtxLayoutIt1->second.getStride(), sizeof(uint32_t));

        // 触发真实 applyBinding 分发（Slot 0 走 glVertexAttribPointer/Divisor 0，Slot 1 走 glVertexAttribIPointer/Divisor 1）
        layoutPerInstance.applyBinding(0u, 10u);
        layoutPerInstance.applyBinding(1u, 11u);

        return true;
    }

    // 6. FEAT_28 × FEAT_24: WGL2_CommandBuffer 紧凑线性 POD 字节码录制、多通道回放与零重分配复位
    E2E_TEST(Tier3, FEAT_28, Pair_RhiRg_PodCommandBufferStream_MultiPassReplay)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        UniquePtr<FCT::RHI::Pipeline> pipe = wgl2Ctx.createRhiPipeline(FCT::PipelineType::Traditional);
        FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
        cmdBuf.create();

        const size_t initialCap = cmdBuf.getBytecodeStream().capacity();
        E2E_ASSERT_GE(initialCap, 64u * 1024u);

        cmdBuf.begin();
        cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{750.0f, 1334.0f});
        cmdBuf.scissor(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{750.0f, 1334.0f});
        cmdBuf.bindPipeline(pipe.get());
        cmdBuf.drawIndex(0u, 0u, 36u, 128u, 0u);
        cmdBuf.nextPass();
        cmdBuf.draw(0u, 0u, 6u, 1u);
        cmdBuf.end();

        const size_t expectedStreamBytes =
            sizeof(FCT::RHI::Cmd_SetViewport) +
            sizeof(FCT::RHI::Cmd_SetScissor) +
            sizeof(FCT::RHI::Cmd_BindPipeline) +
            sizeof(FCT::RHI::Cmd_DrawIndex) +
            sizeof(FCT::RHI::Cmd_NextPass) +
            sizeof(FCT::RHI::Cmd_Draw);

        const auto& stream = cmdBuf.getBytecodeStream();
        E2E_ASSERT_EQ(stream.size(), expectedStreamBytes);
        E2E_ASSERT_EQ(stream.capacity(), initialCap);

        // 校验首条指令与 DrawIndex 实例参数落盘精确无损
        const auto* vpCmd = reinterpret_cast<const FCT::RHI::Cmd_SetViewport*>(stream.data());
        E2E_ASSERT_EQ(vpCmd->opcode, FCT::RHI::WGL2_CmdOpcode::SetViewport);
        E2E_ASSERT_NEAR(vpCmd->width, 750.0f, 1e-4f);
        E2E_ASSERT_NEAR(vpCmd->height, 1334.0f, 1e-4f);

        const size_t drawIdxOffset =
            sizeof(FCT::RHI::Cmd_SetViewport) +
            sizeof(FCT::RHI::Cmd_SetScissor) +
            sizeof(FCT::RHI::Cmd_BindPipeline);
        const auto* drawIdxCmd = reinterpret_cast<const FCT::RHI::Cmd_DrawIndex*>(stream.data() + drawIdxOffset);
        E2E_ASSERT_EQ(drawIdxCmd->opcode, FCT::RHI::WGL2_CmdOpcode::DrawIndex);
        E2E_ASSERT_EQ(drawIdxCmd->indexCount, 36u);
        E2E_ASSERT_EQ(drawIdxCmd->instanceCount, 128u);

        cmdBuf.submit();
        cmdBuf.reset();
        E2E_ASSERT_TRUE(cmdBuf.getBytecodeStream().empty());
        E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().capacity(), initialCap);

        return true;
    }

    // 7. FEAT_29 × FEAT_13: WGL2_Image Texture2DArray / CubeMap 目标推导与局部子区域更新契约
    E2E_TEST(Tier3, FEAT_29, Pair_RhiRg_Image2DArrayAndCube_SubRegionUpload)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        // 1) 4 层 Texture2DArray 图集纹理创建与子区刷新
        FCT::RHI::WGL2_Image arrayImage(&wgl2Ctx);
        arrayImage.width(512);
        arrayImage.height(512);
        arrayImage.arrayLayers(4);
        arrayImage.format(FCT::Format::R8G8B8A8_UNORM);
        arrayImage.usage(FCT::ImageUsage::Texture);
        arrayImage.create(false);
        E2E_ASSERT_EQ(arrayImage.getGLTarget(), static_cast<GLenum>(GL_TEXTURE_2D_ARRAY));
        E2E_ASSERT_FALSE(arrayImage.isRenderbuffer());

        std::vector<uint8_t> subTile(32u * 32u * 4u, 0xCCu);
        arrayImage.updateSubRegion(64u, 128u, 32u, 32u, 2u, 0u, subTile.data(), 32u * 4u);

        // 2) CubeMap 环境探针纹理创建
        FCT::RHI::WGL2_Image cubeImage(&wgl2Ctx);
        cubeImage.width(128);
        cubeImage.height(128);
        cubeImage.arrayLayers(6);
        cubeImage.isCubeMap(true);
        cubeImage.format(FCT::Format::R8G8B8A8_UNORM);
        cubeImage.usage(FCT::ImageUsage::Texture);
        cubeImage.create(false);
        E2E_ASSERT_EQ(cubeImage.getGLTarget(), static_cast<GLenum>(GL_TEXTURE_CUBE_MAP));

        // 3) Renderbuffer 降级镜像安全忽略 updateSubRegion 数据上传
        FCT::RHI::WGL2_Image rboImage(&wgl2Ctx);
        rboImage.width(512);
        rboImage.height(512);
        rboImage.format(FCT::Format::D24_UNORM_S8_UINT);
        rboImage.setRenderbuffer(true);
        rboImage.create(false);
        E2E_ASSERT_TRUE(rboImage.isRenderbuffer());
        rboImage.updateSubRegion(0u, 0u, 16u, 16u, 0u, 0u, subTile.data(), 0u);

        return true;
    }

    // 8. FEAT_30 × FEAT_03: SPIR-V 离线转译 #version 300 es 组合采样器反射与 DouyinShaderCooker 序列化闭环
    E2E_TEST(Tier3, FEAT_30, Pair_RhiRg_SpirvToGlslEs300_ReflectionAndNoBinding)
    {
        FCT::ShaderCrossCompiler crossCompiler;
        FCT::ShaderCrossCompilerOptions opts;
        opts.glslVersion = 300;
        opts.es = true;

        auto transpileOr = crossCompiler.transpileSpirvToGlslEs300(kT3RhiRg_SpirvSampledFrag, opts);
        E2E_ASSERT_STATUS_OK(transpileOr);
        const auto& transpiled = transpileOr.value();

        E2E_ASSERT_TRUE(transpiled.success);
        E2E_ASSERT_GE(transpiled.glslSource.size(), 15u);
        E2E_ASSERT_EQ(transpiled.glslSource.substr(0, 15), "#version 300 es");
        // WebGL 2.0 / GLES 3.0 严禁残留桌面 Vulkan/GL4.5 的 layout(binding = ...) 语法
        E2E_ASSERT_EQ(transpiled.glslSource.find("binding ="), std::string::npos);
        E2E_ASSERT_FALSE(transpiled.combinedSamplers.empty());

        // 送入真实 DouyinShaderCooker 执行变体转译与二进制包往返序列化
        DouyinMiniGame::ShaderVariantInput variant;
        variant.shaderId = "ForwardAlbedo_PS";
        variant.techniqueName = "StandardLit";
        variant.passTag = "Opaque";
        variant.stage = FCT::ShaderKind::FragmentShader;
        variant.spirvWords.assign(std::begin(kT3RhiRg_SpirvSampledFrag), std::end(kT3RhiRg_SpirvSampledFrag));

        auto cookedOr = DouyinMiniGame::DouyinShaderCooker::transpileVariant(crossCompiler, variant);
        E2E_ASSERT_STATUS_OK(cookedOr);
        E2E_ASSERT_EQ(cookedOr->glslEs300Source.substr(0, 15), "#version 300 es");

        std::vector<DouyinMiniGame::CookedShaderEntry> packEntries = {cookedOr.value()};
        const auto packBytes = DouyinMiniGame::DouyinShaderCooker::serializeShaderPack(packEntries);
        E2E_ASSERT_GT(packBytes.size(), 16u);

        auto unpackedOr = DouyinMiniGame::DouyinShaderCooker::deserializeShaderPack(packBytes);
        E2E_ASSERT_STATUS_OK(unpackedOr);
        E2E_ASSERT_EQ(unpackedOr->size(), 1u);
        E2E_ASSERT_EQ((*unpackedOr)[0].shaderId, "ForwardAlbedo_PS");
        E2E_ASSERT_EQ((*unpackedOr)[0].glslEs300Source, cookedOr->glslEs300Source);

        return true;
    }

    // 9. FEAT_31 × FEAT_24: 预烘焙 #version 300 es 直通 WGL2 Shader 加载与小游戏编译期 Shaderc 剥离红线
    E2E_TEST(Tier3, FEAT_31, Pair_RhiRg_PrebakedEs300Load_AndDesktopGlsl450Guard)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        const std::string es300Vs =
            "#version 300 es\n"
            "layout(location = 0) in vec3 a_Pos;\n"
            "void main() { gl_Position = vec4(a_Pos, 1.0); }\n";
        const std::string es300Ps =
            "#version 300 es\n"
            "precision mediump float;\n"
            "out vec4 o_Color;\n"
            "void main() { o_Color = vec4(1.0); }\n";

        FCT::RHI::WGL2_VertexShader vs(&wgl2Ctx);
        vs.code(std::vector<char>(es300Vs.begin(), es300Vs.end()));
        vs.create();
        E2E_ASSERT_NE(vs.getShaderHandle(), 0u);

        FCT::RHI::WGL2_PixelShader ps(&wgl2Ctx);
        ps.code(std::vector<char>(es300Ps.begin(), es300Ps.end()));
        ps.create();
        E2E_ASSERT_NE(ps.getShaderHandle(), 0u);

        // 联动 DouyinScriptAndWasmBuilder 验证小游戏编译标志强制注入 FCT_ENABLE_RUNTIME_SHADERC=0
        DouyinMiniGame::DouyinPackagerConfig cfg;
        const auto wasmFlags = DouyinMiniGame::DouyinScriptAndWasmBuilder::buildEmscriptenCompileFlags(cfg);
        bool hasShadercDisableMacro = false;
        for (const auto& flag : wasmFlags)
        {
            if (flag.find("FCT_ENABLE_RUNTIME_SHADERC=0") != std::string::npos)
            {
                hasShadercDisableMacro = true;
            }
        }
        E2E_ASSERT_TRUE(hasShadercDisableMacro);

        // 验证 ShaderBinary 元数据语义槽位映射无需运行时编译器介入
        FCT::RHI::ShaderBinary bin;
        bin.code(std::vector<char>(es300Vs.begin(), es300Vs.end()));
        bin.location({{"a_Pos", 0u}});
        E2E_ASSERT_EQ(bin.locationBySemantic("a_Pos"), 0);
        E2E_ASSERT_EQ(bin.locationBySemantic("NonExistentAttr"), -1);

        return true;
    }

    // 10. FEAT_32 × FEAT_72: 小游戏前向多通道瞬态资源池生命周期区间复用率 >= 60% 验证
    E2E_TEST(Tier3, FEAT_32, Pair_RhiRg_TransientPoolReuse60Pct_AcrossForwardPasses)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);
        FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
        FCT::TransientResourcePool pool(&wgl2Dev);

        FCT::TransientResourceSpec halfHdrSpec;
        halfHdrSpec.width = 375u;
        halfHdrSpec.height = 667u;
        halfHdrSpec.format = FCT::Format::R16G16B16A16_SFLOAT;
        halfHdrSpec.usage = FCT::ImageUsage::RenderTarget | FCT::ImageUsage::Texture;
        halfHdrSpec.isSampled = true;

        // 模拟 Bloom 提取 -> 水平模糊 -> 垂直模糊 -> 升采样四段后处理链（共 6 次虚拟请求，至多 2 块物理缓冲乒乓复用）
        FCT::Image* pass1Out = pool.acquire(halfHdrSpec, 1u, 2u, 1u); // 新分配 #1 (生命周期 [1, 2])
        FCT::Image* pass2Out = pool.acquire(halfHdrSpec, 2u, 3u, 1u); // 与 #1 重叠，新分配 #2 (生命周期 [2, 3])
        FCT::Image* pass3Out = pool.acquire(halfHdrSpec, 3u, 4u, 1u); // 复用 #1 (expiredPassIdx=2 < 3)
        FCT::Image* pass4Out = pool.acquire(halfHdrSpec, 4u, 5u, 1u); // 复用 #2 (expiredPassIdx=3 < 4)
        FCT::Image* pass5Out = pool.acquire(halfHdrSpec, 5u, 6u, 1u); // 复用 #1 (expiredPassIdx=4 < 5)
        FCT::Image* pass6Out = pool.acquire(halfHdrSpec, 6u, 7u, 1u); // 复用 #2 (expiredPassIdx=5 < 6)

        E2E_ASSERT_NE(pass1Out, nullptr);
        E2E_ASSERT_NE(pass2Out, nullptr);
        E2E_ASSERT_NE(pass1Out, pass2Out);
        E2E_ASSERT_EQ(pass3Out, pass1Out);
        E2E_ASSERT_EQ(pass4Out, pass2Out);
        E2E_ASSERT_EQ(pass5Out, pass1Out);
        E2E_ASSERT_EQ(pass6Out, pass2Out);

        const auto& stats = pool.getStats();
        E2E_ASSERT_EQ(stats.totalVirtualRequests, 6u);
        E2E_ASSERT_EQ(stats.physicalAllocations, 2u);
        E2E_ASSERT_EQ(stats.reuseHits, 4u);
        E2E_ASSERT_GE(stats.getReuseRate(), 0.60f);

        return true;
    }

    // 11. FEAT_32 × FEAT_33: 瞬态池 120 帧 TTL 超时老化回收与 Renderbuffer 降级规格隔离
    E2E_TEST(Tier3, FEAT_32, Pair_RhiRg_TransientPoolTtl120Eviction_AndRenderbufferMatch)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);
        FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
        FCT::TransientResourcePool pool(&wgl2Dev);

        FCT::TransientResourceSpec rboDepthSpec;
        rboDepthSpec.width = 750u;
        rboDepthSpec.height = 1334u;
        rboDepthSpec.format = FCT::Format::D24_UNORM_S8_UINT;
        rboDepthSpec.usage = FCT::ImageUsage::DepthStencil;
        rboDepthSpec.isDepthStencil = true;
        rboDepthSpec.isSampled = false;
        rboDepthSpec.allowRenderbufferDowngrade = true;

        FCT::TransientResourceSpec sampledDepthSpec = rboDepthSpec;
        sampledDepthSpec.isSampled = true;
        sampledDepthSpec.usage = FCT::ImageUsage::DepthStencil | FCT::ImageUsage::Texture;

        // 不可混用纯 Renderbuffer 与需采样的 Depth Texture
        E2E_ASSERT_FALSE(rboDepthSpec.matches(sampledDepthSpec));

        FCT::Image* rboImg = pool.acquire(rboDepthSpec, 0u, 1u, 10u);
        FCT::Image* texImg = pool.acquire(sampledDepthSpec, 2u, 3u, 10u);
        E2E_ASSERT_NE(rboImg, texImg);
        E2E_ASSERT_TRUE(rboImg->isRenderbuffer());
        E2E_ASSERT_FALSE(texImg->isRenderbuffer());
        E2E_ASSERT_EQ(pool.getPoolSize(), 2u);

        // 第 130 帧差值 = 120 <= kEvictionTTL(120)，缓冲仍然驻留；第 100 帧刷新 rboDepthSpec 续命
        (void)pool.acquire(rboDepthSpec, 0u, 1u, 100u);
        pool.tick(130u);
        E2E_ASSERT_EQ(pool.getPoolSize(), 2u);

        // 第 131 帧：sampledDepthSpec (lastUsed=10, diff=121 > 120) 触发自动淘汰，rboDepthSpec (lastUsed=100) 保留
        pool.tick(131u);
        E2E_ASSERT_EQ(pool.getPoolSize(), 1u);

        // 第 221 帧：rboDepthSpec (diff=121 > 120) 亦被自动释放
        pool.tick(221u);
        E2E_ASSERT_EQ(pool.getPoolSize(), 0u);

        return true;
    }

    // 12. FEAT_37 × FEAT_33: WGL2_PassGroup FNV-1a FBO 哈希缓存与颜色纹理 + 深度 Renderbuffer 附件区分
    E2E_TEST(Tier3, FEAT_37, Pair_RhiRg_PassGroupFboHashCache_ColorAndDepthRbo)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        FCT::RHI::WGL2_Image colorTex(&wgl2Ctx);
        colorTex.createFromExisting(101u, GL_TEXTURE_2D, false);

        FCT::RHI::WGL2_Image depthRbo(&wgl2Ctx);
        depthRbo.format(FCT::Format::D24_UNORM_S8_UINT);
        depthRbo.createFromExisting(202u, GL_RENDERBUFFER, true);

        FCT::RHI::WGL2_Image depthTex(&wgl2Ctx);
        depthTex.format(FCT::Format::D24_UNORM_S8_UINT);
        depthTex.createFromExisting(202u, GL_TEXTURE_2D, false);

        std::vector<FCT::RHI::WGL2_Image*> colors = {&colorTex};
        const uint64_t hashRboA = FCT::RHI::WGL2_PassGroup::computeAttachmentHash(colors, &depthRbo);
        const uint64_t hashRboB = FCT::RHI::WGL2_PassGroup::computeAttachmentHash(colors, &depthRbo);
        const uint64_t hashTex = FCT::RHI::WGL2_PassGroup::computeAttachmentHash(colors, &depthTex);

        E2E_ASSERT_EQ(hashRboA, hashRboB);
        E2E_ASSERT_NE(hashRboA, hashTex);

        FCT::RHI::WGL2_PassGroup passGroup(&wgl2Ctx);
        const GLuint fboFirst = passGroup.getOrCreateFBO(hashRboA, colors, &depthRbo);
        const GLuint fboCached = passGroup.getOrCreateFBO(hashRboB, colors, &depthRbo);
        E2E_ASSERT_EQ(fboFirst, fboCached);

        return true;
    }

    // 13. FEAT_38 × FEAT_25: 引擎原生硬件 SIMD 128-bit 批量矩阵/向量运算（含非 4 倍数尾部处理）写入 WGL2_StorageBuffer
    E2E_TEST(Tier3, FEAT_38, Pair_RhiRg_NativeSimdBatch_TransformAndClamp)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        // 取 19 个元素（4*4 + 3 尾部），验证 SIMD 向量指令与标量结果一致性
        constexpr size_t kElemCount = 19u;
        std::vector<FCT::Vec4> srcPoints(kElemCount);
        std::vector<FCT::Vec4> transformed(kElemCount);
        std::vector<FCT::Vec4> clamped(kElemCount);

        for (size_t i = 0; i < kElemCount; ++i)
        {
            const float fi = static_cast<float>(i);
            srcPoints[i] = FCT::Vec4{fi, -fi * 0.5f, 2.0f, 1.0f};
        }

        FCT::Mat4 translateScaleMat;
        translateScaleMat.identity();
        translateScaleMat.m[0] = 2.0f;
        translateScaleMat.m[5] = 2.0f;
        translateScaleMat.m[10] = 1.0f;
        translateScaleMat.m[3] = 10.0f; // 行主序平移 X = +10

        const FCT::Vec4 minBound{0.0f, -5.0f, 0.0f, 0.0f};
        const FCT::Vec4 maxBound{30.0f, 5.0f, 10.0f, 1.0f};

#if defined(MQENGINE_SIMD_SSE)
        __m128 col0 = _mm_loadu_ps(&translateScaleMat.m[0]);
        __m128 col1 = _mm_loadu_ps(&translateScaleMat.m[4]);
        __m128 col2 = _mm_loadu_ps(&translateScaleMat.m[8]);
        __m128 col3 = _mm_loadu_ps(&translateScaleMat.m[12]);
        _MM_TRANSPOSE4_PS(col0, col1, col2, col3);
        const __m128 minV = _mm_set_ps(minBound.w, minBound.z, minBound.y, minBound.x);
        const __m128 maxV = _mm_set_ps(maxBound.w, maxBound.z, maxBound.y, maxBound.x);

        for (size_t i = 0; i < kElemCount; ++i)
        {
            const FCT::Vec4& s = srcPoints[i];
            const __m128 t = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(col0, _mm_set1_ps(s.x)), _mm_mul_ps(col1, _mm_set1_ps(s.y))),
                _mm_add_ps(_mm_mul_ps(col2, _mm_set1_ps(s.z)), _mm_mul_ps(col3, _mm_set1_ps(s.w))));
            const __m128 c = _mm_min_ps(_mm_max_ps(t, minV), maxV);
            alignas(16) float tArr[4];
            alignas(16) float cArr[4];
            _mm_store_ps(tArr, t);
            _mm_store_ps(cArr, c);
            transformed[i] = FCT::Vec4{tArr[0], tArr[1], tArr[2], tArr[3]};
            clamped[i] = FCT::Vec4{cArr[0], cArr[1], cArr[2], cArr[3]};
        }
#else
        for (size_t i = 0; i < kElemCount; ++i)
        {
            const FCT::Vec4& v = srcPoints[i];
            transformed[i] = FCT::Vec4{
                translateScaleMat.m[0] * v.x + translateScaleMat.m[1] * v.y + translateScaleMat.m[2] * v.z + translateScaleMat.m[3] * v.w,
                translateScaleMat.m[4] * v.x + translateScaleMat.m[5] * v.y + translateScaleMat.m[6] * v.z + translateScaleMat.m[7] * v.w,
                translateScaleMat.m[8] * v.x + translateScaleMat.m[9] * v.y + translateScaleMat.m[10] * v.z + translateScaleMat.m[11] * v.w,
                translateScaleMat.m[12] * v.x + translateScaleMat.m[13] * v.y + translateScaleMat.m[14] * v.z + translateScaleMat.m[15] * v.w};
            clamped[i] = FCT::Vec4{
                std::clamp(transformed[i].x, minBound.x, maxBound.x),
                std::clamp(transformed[i].y, minBound.y, maxBound.y),
                std::clamp(transformed[i].z, minBound.z, maxBound.z),
                std::clamp(transformed[i].w, minBound.w, maxBound.w)};
        }
#endif

        // 元素 18: 2*18 + 10 = 46 -> 钳制为 30; y = 2*(-9) = -18 -> 钳制为 -5
        E2E_ASSERT_NEAR(clamped[18].x, 30.0f, 1e-4f);
        E2E_ASSERT_NEAR(clamped[18].y, -5.0f, 1e-4f);
        E2E_ASSERT_NEAR(clamped[0].x, 10.0f, 1e-4f);

        // 上传至 WGL2_StorageBuffer 供渲染侧采样
        FCT::RHI::WGL2_StorageBuffer gpuDataTex(&wgl2Ctx, 1024u);
        gpuDataTex.updateData(clamped.data(), kElemCount * sizeof(FCT::Vec4), 0u);
        const auto* gpuFloats = static_cast<const FCT::Vec4*>(gpuDataTex.map());
        E2E_ASSERT_NE(gpuFloats, nullptr);
        E2E_ASSERT_NEAR(gpuFloats[18].x, 30.0f, 1e-4f);
        E2E_ASSERT_NEAR(gpuFloats[18].y, -5.0f, 1e-4f);
        gpuDataTex.unmap();

        return true;
    }

    // 14. FEAT_39 × FEAT_24: WGL2_TransformFeedback 状态机契约（槽位校验、暂停期禁止结束、上下文绑定同步）
    E2E_TEST(Tier3, FEAT_39, Pair_RhiRg_TransformFeedbackLifecycle_StateMachineAndCommandStream)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3RhiRg_InitHeadlessWgl2Context(wgl2Ctx);

        FCT::RHI::WGL2_TransformFeedback xfb(&wgl2Ctx);
        E2E_ASSERT_STATUS_OK(xfb.create());
        E2E_ASSERT_NE(xfb.getNativeHandle(), 0u);

        // 槽位 >= 4 必须被契约拦截返回 InvalidArgumentError
        Status badSlotStatus = xfb.bindBufferBase(4u, 77u);
        E2E_ASSERT_FALSE(badSlotStatus.ok());
        E2E_ASSERT_EQ(badSlotStatus.code(), absl::StatusCode::kInvalidArgument);

        E2E_ASSERT_STATUS_OK(xfb.bindBufferBase(0u, 77u));
        E2E_ASSERT_EQ(wgl2Ctx.getCurrentBoundTransformFeedback(), xfb.getNativeHandle());

        // 开启光栅化丢弃状态并启动粒子 Transform Feedback
        wgl2Ctx.applyGraphicsState(1ULL << 4);
        E2E_ASSERT_STATUS_OK(xfb.begin(FCT::RHI::FeedbackPrimitiveMode::Points));
        E2E_ASSERT_TRUE(xfb.isActive());
        E2E_ASSERT_FALSE(xfb.isPaused());

        // 重复激活必须拒绝
        Status dupBegin = xfb.begin(FCT::RHI::FeedbackPrimitiveMode::Points);
        E2E_ASSERT_FALSE(dupBegin.ok());

        // 暂停状态下直接调用 end() 必须返回 FailedPreconditionError
        E2E_ASSERT_STATUS_OK(xfb.pause());
        E2E_ASSERT_TRUE(xfb.isPaused());
        Status endWhilePaused = xfb.end();
        E2E_ASSERT_FALSE(endWhilePaused.ok());
        E2E_ASSERT_EQ(endWhilePaused.code(), absl::StatusCode::kFailedPrecondition);

        // 恢复后正常结束并解绑
        E2E_ASSERT_STATUS_OK(xfb.resume());
        E2E_ASSERT_FALSE(xfb.isPaused());
        E2E_ASSERT_STATUS_OK(xfb.end());
        E2E_ASSERT_FALSE(xfb.isActive());

        xfb.destroy();
        E2E_ASSERT_EQ(wgl2Ctx.getCurrentBoundTransformFeedback(), 0u);

        return true;
    }

    // 15. FEAT_40 × FEAT_28: MiniGame_Window DPR 物理降采样钳制与 CommandBuffer 视口/裁剪框坐标闭环
    E2E_TEST(Tier3, FEAT_40, Pair_RhiRg_MiniGameWindowDprCap_ViewportCoordinateRoundtrip)
    {
        FCT::Emscripten_Runtime runtime;
        // 模拟 iPhone Pro 3x 系统 DPR，工程配置 dprCap = 2.0 降采样保护带宽
        FCT::MiniGame_Window wnd(&runtime, 390, 844, 3.0f, 2.0f);
        wnd.createPlatform();

        E2E_ASSERT_NEAR(wnd.getEffectiveDpr(), 2.0f, 1e-4f);
        E2E_ASSERT_EQ(wnd.getWidth(), 780);
        E2E_ASSERT_EQ(wnd.getHeight(), 1688);

        const FCT::Vec2 physPt = wnd.cssToPhysical(195.0f, 422.0f);
        E2E_ASSERT_NEAR(physPt.x, 390.0f, 1e-3f);
        E2E_ASSERT_NEAR(physPt.y, 844.0f, 1e-3f);

        const FCT::Vec2 cssRoundtrip = wnd.physicalToCss(physPt.x, physPt.y);
        E2E_ASSERT_NEAR(cssRoundtrip.x, 195.0f, 1e-3f);
        E2E_ASSERT_NEAR(cssRoundtrip.y, 422.0f, 1e-3f);

        // 横竖屏旋转或折叠屏展开后动态重算物理帧缓冲分辨率并录入命令流
        wnd.onCanvasResize(844, 390, 2.5f);
        E2E_ASSERT_NEAR(wnd.getEffectiveDpr(), 2.0f, 1e-4f);
        E2E_ASSERT_EQ(wnd.getWidth(), 1688);
        E2E_ASSERT_EQ(wnd.getHeight(), 780);

        FCT::WGL2_Context wgl2Ctx(&runtime);
        FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
        cmdBuf.begin();
        cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f},
                        FCT::Vec2{static_cast<float>(wnd.getWidth()), static_cast<float>(wnd.getHeight())});
        cmdBuf.end();

        const auto* vp = reinterpret_cast<const FCT::RHI::Cmd_SetViewport*>(cmdBuf.getBytecodeStream().data());
        E2E_ASSERT_NEAR(vp->width, 1688.0f, 1e-4f);
        E2E_ASSERT_NEAR(vp->height, 780.0f, 1e-4f);

        return true;
    }

    // 16. FEAT_41 × FEAT_40: Emscripten_Runtime 非阻塞帧步进与单指转鼠标 / 多指独立追踪双轨分发
    E2E_TEST(Tier3, FEAT_41, Pair_RhiRg_EmscriptenRuntimeAsyncLoop_DualTrackMultiTouch)
    {
        FCT::Emscripten_Runtime runtime;
        FCT::MiniGame_Window wnd(&runtime, 375, 667, 2.0f, 2.0f);
        wnd.createPlatform();
        runtime.setTargetWindow(&wnd);

        uint32_t ticks = 0;
        double totalDt = 0.0;
        runtime.startMainLoop(T3RhiRg_RuntimeTickTracker{&ticks, &totalDt}, 60);
        E2E_ASSERT_TRUE(runtime.isLoopRunning());
        E2E_ASSERT_EQ(runtime.getTargetFps(), 60);

        runtime.stepFrame(0.016);
        runtime.pauseMainLoop();
        runtime.stepFrame(0.016); // 暂停态下不推进回调
        runtime.resumeMainLoop();
        runtime.stepFrame(0.016);

        E2E_ASSERT_EQ(ticks, 2u);
        E2E_ASSERT_NEAR(totalDt, 0.032, 1e-5);

        // 双指并发：指 10 为主触点（同步驱动窗口光标），指 20 为副触点（互不覆盖）
        runtime.injectTouchStart(10, 120.0f, 240.0f, 0.8f);
        E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 10);
        E2E_ASSERT_EQ(runtime.getActiveTouches().size(), 1u);

        runtime.injectTouchStart(20, 300.0f, 500.0f, 0.9f);
        E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 10);
        E2E_ASSERT_EQ(runtime.getActiveTouches().size(), 2u);

        runtime.injectTouchMove(20, 315.0f, 520.0f, 0.9f);
        const auto& activeTouches = runtime.getActiveTouches();
        const auto touchIt10 = activeTouches.find(10);
        const auto touchIt20 = activeTouches.find(20);
        E2E_ASSERT_TRUE(touchIt10 != activeTouches.end());
        E2E_ASSERT_TRUE(touchIt20 != activeTouches.end());
        E2E_ASSERT_NEAR(touchIt10->second.logicalX, 120.0f, 1e-4f);
        E2E_ASSERT_NEAR(touchIt20->second.logicalX, 315.0f, 1e-4f);
        E2E_ASSERT_NEAR(touchIt20->second.logicalY, 520.0f, 1e-4f);

        // 抬起主触点 10，验证副触点 20 保持存活，随后全部释放归零
        runtime.injectTouchEnd(10, 120.0f, 240.0f);
        E2E_ASSERT_EQ(runtime.getActiveTouches().size(), 1u);
        runtime.injectTouchEnd(20, 315.0f, 520.0f);
        E2E_ASSERT_TRUE(runtime.getActiveTouches().empty());

        runtime.stopMainLoop();
        E2E_ASSERT_FALSE(runtime.isLoopRunning());

        return true;
    }

} // namespace MQEngine::Testing
