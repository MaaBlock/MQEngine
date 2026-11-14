/**
 * @file Test_Tier3_Font_HID_UI.cpp
 * @brief Tier 3 正交组合测试：双轨字体系统 × FCT_SAI 原生拼车光栅化 × GameHtmlUI (RmlUi 6.0) × Enhanced Input 优先级栈 × 变体裁剪与空载旁路
 *        覆盖特性：FEAT_45, FEAT_47, FEAT_48, FEAT_49, FEAT_50, FEAT_51, FEAT_52, FEAT_54,
 *                 FEAT_55, FEAT_56, FEAT_57, FEAT_58, FEAT_59, FEAT_60, FEAT_61, FEAT_62,
 *                 FEAT_63, FEAT_64, FEAT_65, FEAT_66, FEAT_73
 */
#include "Common/E2ECommon.h"

#ifndef E2E_ASSERT_NEAR
#define E2E_ASSERT_NEAR(val1, val2, absError) \
    E2E_ASSERT(std::abs((val1) - (val2)) <= (absError), \
               ::absl::StrCat("期望近似相等: |", #val1, " - ", #val2, "| <= ", #absError))
#endif

#include <FCT/Context/WGL2_Context.h>
#include <FCT_SAI/SystemFontProvider.h>

#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>

#include "Composites/DouyinPlatformRuntimeComposite/CapsuleAndOpenDataService.h"
#include "Composites/RmlUiComposite/RmlUiComposite.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        using Domain::HID::ActionBindingMapping;
        using Domain::HID::EInputAccumulationPolicy;
        using Domain::HID::EInputActionValueType;
        using Domain::HID::ESwizzleAxisOrder;
        using Domain::HID::ETriggerState;
        using Domain::HID::HIDSubsystem;
        using Domain::HID::InputAction;
        using Domain::HID::InputActionHandle;
        using Domain::HID::InputMappingContext;
        using Domain::HID::InputModifierDeadZone;
        using Domain::HID::InputModifierDeltaTimeScale;
        using Domain::HID::InputModifierNegate;
        using Domain::HID::InputModifierNormalize2D;
        using Domain::HID::InputModifierScalar;
        using Domain::HID::InputModifierSwizzleAxis;
        using Domain::HID::InputTriggerChordedAction;
        using Domain::HID::InputTriggerDown;
        using Domain::HID::InputTriggerHold;
        using Domain::HID::InputTriggerHoldAndRelease;
        using Domain::HID::InputTriggerPressed;
        using Domain::HID::InputTriggerReleased;
        using Domain::HID::InputTriggerTap;
        using Domain::HID::InputValue;

        using Domain::Renderer::AtlasAllocation;
        using Domain::Renderer::DrawBucket;
        namespace InputTags = Domain::Renderer::InputTags;
        namespace PassTags = Domain::Renderer::PassTags;
        using Domain::Renderer::PipelineBindingDesc;
        using Domain::Renderer::RenderGraphAsset;
        using Domain::Renderer::RenderGraphAssetDesc;
        using Domain::Renderer::RenderGraphMatcher;

        using GameHtmlUI::CapsuleCssVariableInjector;
        using GameHtmlUI::MQRmlFileInterface;
        using GameHtmlUI::MQRmlFontEngine;
        using GameHtmlUI::MQRmlPipelineRenderState;
        using GameHtmlUI::MQRmlRenderInterface;
        using GameHtmlUI::OpenDataLeaderboardController;
        using GameHtmlUI::RmlGeneratedDrawBucket;
        using GameHtmlUI::RmlUiComposite;
        using GameHtmlUI::VirtualTextureRouter;
        using GameHtmlUI::VirtualTouchController;

        using Resource::Font::BakedFont;
        using Resource::Font::CachedUiGlyph;
        using Resource::Font::FontBackendMode;
        using Resource::Font::FontSystem;
        using Resource::Font::FontTrack;
        using Resource::Font::FontTrackClassifier;
        using Resource::Font::GlyphAtlasManager;
        using Resource::Font::GlyphMetric;
        using Resource::Font::GpuInGameTextQuadInstance;
        using Resource::Font::InGameTextComponent;
        using Resource::Font::MqFontBinaryHeader;
        using Resource::Font::MsdfGlyphBitmap;
        using Resource::Font::TextureFormat;

        void T3UiHid_InitHeadlessWgl2(FCT::WGL2_Context& ctx)
        {
            ctx.createPlatform(FCT::ContextCreateFlags(0));
        }

        // 从拼车光栅化批次缓冲中按局部坐标提取单个字形的连续像素块
        std::vector<uint8_t> T3UiHid_ExtractGlyphSliceFromBatch(
            const FCT::SAI::RasterizedGlyphBatch& batch,
            const FCT::SAI::GlyphMetrics& gm)
        {
            const uint32_t bpp = (batch.format == FCT::SAI::FontPixelFormat::RGBA8_UNORM) ? 4u : 1u;
            const uint32_t w = static_cast<uint32_t>(std::max(1, gm.width));
            const uint32_t h = static_cast<uint32_t>(std::max(1, gm.height));
            std::vector<uint8_t> slice(static_cast<size_t>(w) * h * bpp, 0x7Fu);

            if (batch.atlasWidth == 0 || batch.atlasHeight == 0 || batch.pixelData.empty())
            {
                return slice;
            }

            for (uint32_t row = 0; row < h; ++row)
            {
                const uint32_t srcY = gm.atlasY + row;
                if (srcY >= batch.atlasHeight)
                {
                    break;
                }
                for (uint32_t col = 0; col < w; ++col)
                {
                    const uint32_t srcX = gm.atlasX + col;
                    if (srcX >= batch.atlasWidth)
                    {
                        break;
                    }
                    const size_t srcOffset = (static_cast<size_t>(srcY) * batch.atlasWidth + srcX) * bpp;
                    const size_t dstOffset = (static_cast<size_t>(row) * w + col) * bpp;
                    if (srcOffset + bpp <= batch.pixelData.size())
                    {
                        std::memcpy(&slice[dstOffset], &batch.pixelData[srcOffset], bpp);
                    }
                }
            }
            return slice;
        }

        // 构造标准 2D UI 矩形编译网格
        void T3UiHid_BuildUnitUiQuad(std::vector<Rml::Vertex>& outVerts, std::vector<int>& outIndices)
        {
            outVerts.clear();
            outIndices.clear();

            Rml::Vertex v0{};
            v0.position = Rml::Vector2f(0.0f, 0.0f);
            v0.colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
            v0.tex_coord = Rml::Vector2f(0.0f, 0.0f);

            Rml::Vertex v1 = v0;
            v1.position = Rml::Vector2f(120.0f, 0.0f);
            v1.tex_coord = Rml::Vector2f(1.0f, 0.0f);

            Rml::Vertex v2 = v0;
            v2.position = Rml::Vector2f(120.0f, 40.0f);
            v2.tex_coord = Rml::Vector2f(1.0f, 1.0f);

            Rml::Vertex v3 = v0;
            v3.position = Rml::Vector2f(0.0f, 40.0f);
            v3.tex_coord = Rml::Vector2f(0.0f, 1.0f);

            outVerts = {v0, v1, v2, v3};
            outIndices = {0, 1, 2, 0, 2, 3};
        }

        // 具名首屏 Splash 接力回调仿函数：联动 GameHtmlUI 与 DouyinMiniGame 双协调器
        struct T3UiHid_SplashBridgeRecorder
        {
            uint32_t* callCount{nullptr};
            DouyinMiniGame::SplashRelayCoordinator* douyinRelay{nullptr};

            void operator()() const
            {
                if (callCount)
                {
                    ++(*callCount);
                }
                if (douyinRelay)
                {
                    douyinRelay->notifyFirstFrameRendered();
                }
            }
        };

        // 具名排行榜局部上传观察者仿函数
        struct T3UiHid_SubRegionUploadRecorder
        {
            uint32_t* callCount{nullptr};
            uint32_t* lastWidth{nullptr};
            uint32_t* lastHeight{nullptr};

            void operator()(uint32_t, uint32_t, uint32_t width, uint32_t height) const
            {
                if (callCount)
                {
                    ++(*callCount);
                }
                if (lastWidth)
                {
                    *lastWidth = width;
                }
                if (lastHeight)
                {
                    *lastHeight = height;
                }
            }
        };

        // 具名 Enhanced Input 动作事件记录仿函数
        struct T3UiHid_ActionEventRecorder
        {
            uint32_t* fireCount{nullptr};
            InputValue* lastValue{nullptr};
            ETriggerState* lastState{nullptr};

            void operator()(const InputValue& val, ETriggerState state) const
            {
                if (fireCount)
                {
                    ++(*fireCount);
                }
                if (lastValue)
                {
                    *lastValue = val;
                }
                if (lastState)
                {
                    *lastState = state;
                }
            }
        };

        // 为无头确定性回放初始化一致的语义动作与修饰器/触发器链
        struct T3UiHid_ReplayRig
        {
            InputActionHandle moveHandle{0};
            InputActionHandle tapFireHandle{0};
            InputActionHandle holdChargeHandle{0};
            uint32_t moveEvents{0};
            uint32_t fireEvents{0};
            uint32_t chargeEvents{0};
            InputValue lastMoveVal;
            ETriggerState lastFireState{ETriggerState::None};
            ETriggerState lastChargeState{ETriggerState::None};
        };

        Status T3UiHid_SetupReplaySubsystem(HIDSubsystem& hid, T3UiHid_ReplayRig& rig)
        {
            CHECK_STATUS(hid.init());

            auto moveAction = makeUnique<InputAction>(
                "Replay_Move", 0, EInputActionValueType::Axis2D, EInputAccumulationPolicy::Cumulative);
            moveAction->addModifier(makeUnique<InputModifierDeadZone>(0.15f));
            moveAction->addModifier(makeUnique<InputModifierNormalize2D>());
            hid.registerAction(std::move(moveAction));
            rig.moveHandle = hid.getActionHandle("Replay_Move");

            auto tapAction = makeUnique<InputAction>(
                "Replay_TapFire", 0, EInputActionValueType::Digital, EInputAccumulationPolicy::MaxMagnitude);
            tapAction->addTrigger(makeUnique<InputTriggerTap>(0.20f));
            hid.registerAction(std::move(tapAction));
            rig.tapFireHandle = hid.getActionHandle("Replay_TapFire");

            auto holdAction = makeUnique<InputAction>(
                "Replay_HoldCharge", 0, EInputActionValueType::Digital, EInputAccumulationPolicy::MaxMagnitude);
            holdAction->addTrigger(makeUnique<InputTriggerHold>(0.25f, true));
            hid.registerAction(std::move(holdAction));
            rig.holdChargeHandle = hid.getActionHandle("Replay_HoldCharge");

            (void)hid.bindActionEvent(
                rig.moveHandle,
                ETriggerState::Triggered,
                T3UiHid_ActionEventRecorder{&rig.moveEvents, &rig.lastMoveVal, nullptr});
            (void)hid.bindActionEvent(
                rig.tapFireHandle,
                ETriggerState::Triggered,
                T3UiHid_ActionEventRecorder{&rig.fireEvents, nullptr, &rig.lastFireState});
            (void)hid.bindActionEvent(
                rig.holdChargeHandle,
                ETriggerState::Triggered,
                T3UiHid_ActionEventRecorder{&rig.chargeEvents, nullptr, &rig.lastChargeState});
            return OkStatus();
        }

        std::vector<HIDSubsystem::TimestampedAction> T3UiHid_BuildDeterministicSequence(const T3UiHid_ReplayRig& rig)
        {
            std::vector<HIDSubsystem::TimestampedAction> seq;
            seq.push_back({0u, rig.moveHandle, InputValue(FCT::Vec2{1.0f, 1.0f})});
            seq.push_back({50u, rig.tapFireHandle, InputValue(true)});
            seq.push_back({150u, rig.tapFireHandle, InputValue(false)});
            seq.push_back({200u, rig.holdChargeHandle, InputValue(true)});
            seq.push_back({460u, rig.holdChargeHandle, InputValue(true)});
            seq.push_back({520u, rig.holdChargeHandle, InputValue(false)});
            return seq;
        }
    } // namespace

    // =========================================================================
    // 1. FEAT_45: SystemFontProvider 单次拼车批量光栅化 × GlyphAtlasManager 动态图集写入
    // =========================================================================
    E2E_TEST(Tier3, FEAT_45, Pair_FontHidUi_SystemFontProviderBatchCarpoolUploadsIntoGlyphAtlas)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        UniquePtr<FCT::SAI::SystemFontProvider> provider = FCT::SAI::SystemFontProvider::create();
        E2E_ASSERT_TRUE(provider.get() != nullptr);
        E2E_ASSERT_STATUS_OK(provider->init());

        auto metricsOr = provider->getFontMetrics("sans-serif", 24, 4, false);
        E2E_ASSERT_STATUS_OK(metricsOr);
        E2E_ASSERT_GT(metricsOr->ascent, 0.0f);
        E2E_ASSERT_GE(metricsOr->lineSpacing, metricsOr->ascent);

        const std::vector<FCT::SAI::GlyphRequest> carpoolRequests = {
            {static_cast<uint32_t>(U'战'), 24, 4, false, false},
            {static_cast<uint32_t>(U'斗'), 24, 4, false, false},
            {static_cast<uint32_t>(U'A'), 24, 4, false, false},
            {static_cast<uint32_t>(U'9'), 24, 4, false, false},
        };
        auto batchOr = provider->rasterizeBatch("sans-serif", 24, 4, false, carpoolRequests);
        E2E_ASSERT_STATUS_OK(batchOr);
        const FCT::SAI::RasterizedGlyphBatch& batch = *batchOr;
        E2E_ASSERT_EQ(batch.glyphs.size(), carpoolRequests.size());
        E2E_ASSERT_TRUE(batch.format == FCT::SAI::FontPixelFormat::R8_UNORM);
        E2E_ASSERT_GT(batch.pixelData.size(), 0u);

        GlyphAtlasManager atlasMgr;
        E2E_ASSERT_STATUS_OK(atlasMgr.initGpuResources(&wgl2Ctx));

        uint32_t firstPageId = UINT32_MAX;
        for (const FCT::SAI::GlyphMetrics& gm : batch.glyphs)
        {
            const uint32_t gw = static_cast<uint32_t>(std::max(1, gm.width));
            const uint32_t gh = static_cast<uint32_t>(std::max(1, gm.height));
            uint32_t pageId = 0;
            AtlasAllocation alloc{};
            E2E_ASSERT_STATUS_OK(atlasMgr.allocateGlyph(static_cast<char32_t>(gm.codepoint), gw, gh, pageId, alloc));
            if (firstPageId == UINT32_MAX)
            {
                firstPageId = pageId;
            }
            E2E_ASSERT_EQ(pageId, firstPageId);

            const std::vector<uint8_t> slice = T3UiHid_ExtractGlyphSliceFromBatch(batch, gm);
            E2E_ASSERT_STATUS_OK(atlasMgr.uploadGlyphBitmap(pageId, alloc, slice.data(), slice.size()));
        }

        E2E_ASSERT_EQ(atlasMgr.getPageCount(), 1u);
        E2E_ASSERT_TRUE(atlasMgr.getAtlasPageFormat(firstPageId) == TextureFormat::R8_UNORM);
        E2E_ASSERT_TRUE(atlasMgr.getAtlasPageTexture(firstPageId) != nullptr);

        atlasMgr.releaseGpuResources();
        provider->shutdown();
        return true;
    }

    // =========================================================================
    // 2. FEAT_55: 双轨文本分类器 (InGameBaked vs UiRuntime) × BakedFont / FontSystem 路由闭环
    // =========================================================================
    E2E_TEST(Tier3, FEAT_55, Pair_FontHidUi_DualTrackClassifierRoutesCombatVsSocialEmoji)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        E2E_ASSERT_TRUE(FontTrackClassifier::classifyTrack("+9999 暴击!") == FontTrack::InGameBaked);
        E2E_ASSERT_TRUE(FontTrackClassifier::classifyTrack("闪避 格挡 免疫 -1280") == FontTrack::InGameBaked);

        E2E_ASSERT_TRUE(FontTrackClassifier::classifyTrack("公会招募: 今晚八点开荒") == FontTrack::UiRuntime);
        E2E_ASSERT_TRUE(FontTrackClassifier::classifyTrack("首通达成🔥") == FontTrack::UiRuntime);
        // UTF-8 编码的 U+E001 私有保留区图标必须走轨道 B 动态图集
        E2E_ASSERT_TRUE(FontTrackClassifier::classifyTrack("\xEE\x80\x81") == FontTrack::UiRuntime);

        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));
        E2E_ASSERT_STATUS_OK(fontSys.initGpuResources(&wgl2Ctx));
        fontSys.getBakedFont().initDefaultCombatPreset(512, 512);

        const std::string combatStr = "+9999 暴击!";
        if (FontTrackClassifier::classifyTrack(combatStr) == FontTrack::InGameBaked)
        {
            fontSys.getInGameText().spawnDamageNumber(combatStr, FCT::Vec3<>{2.0f, 3.0f, 0.0f}, 1.25f, 0xFF44CCFFu, true);
        }
        std::vector<GpuInGameTextQuadInstance> combatQuads;
        fontSys.getInGameText().buildQuadInstances(fontSys.getBakedFont(), combatQuads);
        E2E_ASSERT_GT(combatQuads.size(), 0u);
        E2E_ASSERT_EQ(fontSys.getBakedFont().getFreeTypeInvocationCount(), 0u);

        const std::vector<char32_t> socialCodepoints = {U'公', U'会', 0xE001u, 0x1F525u};
        E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(socialCodepoints, 20, "@system-ui"));

        CachedUiGlyph puaGlyph{};
        CachedUiGlyph emojiGlyph{};
        E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph("@system-ui", 0xE001u, 20, 4, false, puaGlyph));
        E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph("@system-ui", 0x1F525u, 20, 4, false, emojiGlyph));
        E2E_ASSERT_TRUE(puaGlyph.format == TextureFormat::R8_UNORM);
        E2E_ASSERT_TRUE(emojiGlyph.format == TextureFormat::R8G8B8A8_UNORM);

        fontSys.term();
        return true;
    }

    // =========================================================================
    // 3. FEAT_56: 轨道 A MSDF 距离场与 .mqfont 二进制反序列化 × 零 FreeType 跳字构建
    // =========================================================================
    E2E_TEST(Tier3, FEAT_56, Pair_FontHidUi_BakedFontMsdfBinaryRoundTripFeedsInGameTextZeroFreeType)
    {
        const MsdfGlyphBitmap msdf = MsdfGlyphBitmap::generate(U'暴', 32, 32, 4.0f);
        E2E_ASSERT_EQ(msdf.width, 32u);
        E2E_ASSERT_EQ(msdf.height, 32u);
        E2E_ASSERT_EQ(msdf.channels, 3u);
        E2E_ASSERT_GT(msdf.sampleDistance(16, 16, 1), 0.5f);
        E2E_ASSERT_LT(msdf.sampleDistance(0, 0, 1), 0.5f);

        BakedFont srcFont;
        srcFont.initDefaultCombatPreset(512, 512);
        E2E_ASSERT_TRUE(srcFont.isLoaded());
        E2E_ASSERT_TRUE(srcFont.hasGlyph(U'暴'));
        E2E_ASSERT_TRUE(srcFont.hasGlyph(U'9'));
        E2E_ASSERT_FALSE(srcFont.hasGlyph(U'氪'));
        E2E_ASSERT_EQ(srcFont.getFreeTypeInvocationCount(), 0u);

        const std::vector<uint8_t> serialized = srcFont.saveToBuffer();
        E2E_ASSERT_GT(serialized.size(), sizeof(MqFontBinaryHeader));

        MqFontBinaryHeader header{};
        std::memcpy(&header, serialized.data(), sizeof(MqFontBinaryHeader));
        E2E_ASSERT_EQ(header.magic, BakedFont::Magic);
        E2E_ASSERT_EQ(header.atlasWidth, 512u);
        E2E_ASSERT_EQ(header.atlasHeight, 512u);

        BakedFont dstFont;
        E2E_ASSERT_STATUS_OK(dstFont.loadFromBuffer(serialized));
        E2E_ASSERT_TRUE(dstFont.isLoaded());
        E2E_ASSERT_EQ(dstFont.getFreeTypeInvocationCount(), 0u);

        const GlyphMetric srcCritMetric = srcFont.getGlyphMetric(U'暴');
        const GlyphMetric dstCritMetric = dstFont.getGlyphMetric(U'暴');
        E2E_ASSERT_EQ(srcCritMetric.uvMinMax[2], dstCritMetric.uvMinMax[2]);
        E2E_ASSERT_NEAR(srcCritMetric.advanceX, dstCritMetric.advanceX, 1e-4f);

        InGameTextComponent textComp;
        textComp.spawnDamageNumber("暴击99", FCT::Vec3<>{0.0f, 1.0f, 0.0f}, 1.0f, 0xFFFFFFFFu, true);
        std::vector<GpuInGameTextQuadInstance> quads;
        textComp.buildQuadInstances(dstFont, quads);
        E2E_ASSERT_EQ(quads.size(), 4u);
        E2E_ASSERT_EQ(quads[0].uvMinMax[2], dstCritMetric.uvMinMax[2]);
        return true;
    }

    // =========================================================================
    // 4. FEAT_57: 1000 个紧凑 32B 跳字实例单次 DrawCall 合批 × SIMD 动画推进与过期回收
    // =========================================================================
    E2E_TEST(Tier3, FEAT_57, Pair_FontHidUi_InGameTextThousandQuadsSingleDrawCallAndSimdAnimation)
    {
        E2E_ASSERT_EQ(sizeof(GpuInGameTextQuadInstance), 32u);

        BakedFont combatFont;
        combatFont.initDefaultCombatPreset(512, 512);

        InGameTextComponent textComp;
        // 200 条 "+9999" 各自产出 5 个字形实例，精确构成 1000 个四边形实例
        for (uint32_t i = 0; i < 200u; ++i)
        {
            const float x = static_cast<float>(i % 20u) * 0.5f;
            const float z = static_cast<float>(i / 20u) * 0.5f;
            textComp.spawnDamageNumber("+9999", FCT::Vec3<>{x, 1.5f, z}, 1.0f, 0xFFFFAA00u, (i % 2u) == 0u);
        }
        E2E_ASSERT_EQ(textComp.getActiveItems().size(), 200u);

        std::vector<GpuInGameTextQuadInstance> quads;
        textComp.buildQuadInstances(combatFont, quads);
        E2E_ASSERT_EQ(quads.size(), 1000u);
        E2E_ASSERT_EQ(InGameTextComponent::calculateDrawCallCount(quads.size(), 1024u), 1u);
        E2E_ASSERT_EQ(InGameTextComponent::calculateDrawCallCount(1025u, 1024u), 2u);

        const float initialY0 = quads[0].worldPosAndScale[1];
        const float initialY999 = quads[999].worldPosAndScale[1];
        InGameTextComponent::evaluateBatchAnimations(quads, 1.0f / 60.0f);
        E2E_ASSERT_GT(quads[0].worldPosAndScale[1], initialY0);
        E2E_ASSERT_GT(quads[999].worldPosAndScale[1], initialY999);

        textComp.tickItems(0.5f);
        E2E_ASSERT_EQ(textComp.getActiveItems().size(), 200u);
        textComp.tickItems(1.5f);
        E2E_ASSERT_TRUE(textComp.getActiveItems().empty());
        return true;
    }

    // =========================================================================
    // 5. FEAT_58: 小游戏混合后端 (0KB FreeType + 预烘焙子集命中 + 离屏 Canvas 拼车回退)
    // =========================================================================
    E2E_TEST(Tier3, FEAT_58, Pair_FontHidUi_MiniGameHybridFontSystemPrebakedHitVsCanvasFallback)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));
        E2E_ASSERT_STATUS_OK(fontSys.initGpuResources(&wgl2Ctx));
        E2E_ASSERT_TRUE(fontSys.getBackendMode() == FontBackendMode::MiniGame_Hybrid);
        E2E_ASSERT_FALSE(fontSys.isFreeTypeBackendActive());

        const std::vector<char32_t> prebakedCommon = {U'金', U'币', U'钻', U'石'};
        fontSys.registerPrebakedSubset(prebakedCommon);
        E2E_ASSERT_TRUE(fontSys.isGlyphInPrebakedSubset(U'金'));
        E2E_ASSERT_FALSE(fontSys.queriesSystemFontProvider(U'金'));

        // 生僻字不在预烘焙子集中，必须触发底层 SystemFontProvider 离屏拼车光栅化
        E2E_ASSERT_FALSE(fontSys.isGlyphInPrebakedSubset(U'饕'));
        E2E_ASSERT_TRUE(fontSys.queriesSystemFontProvider(U'饕'));

        const std::vector<char32_t> mixedUiText = {U'金', U'饕', U'餮', 0x1F389u};
        E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(mixedUiText, 22, "@system-ui"));
        E2E_ASSERT_GE(fontSys.getLastBatchCodepointCount(), 2u);

        CachedUiGlyph prebakedGlyph{};
        CachedUiGlyph fallbackGlyph{};
        CachedUiGlyph emojiGlyph{};
        E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph("@system-ui", U'金', 22, 4, false, prebakedGlyph));
        E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph("@system-ui", U'饕', 22, 4, false, fallbackGlyph));
        E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph("@system-ui", 0x1F389u, 22, 4, false, emojiGlyph));

        E2E_ASSERT_GT(fallbackGlyph.metric.advanceX, 0.0f);
        E2E_ASSERT_TRUE(fallbackGlyph.format == TextureFormat::R8_UNORM);
        E2E_ASSERT_TRUE(emojiGlyph.format == TextureFormat::R8G8B8A8_UNORM);
        E2E_ASSERT_NE(fallbackGlyph.pageId, emojiGlyph.pageId);

        fontSys.term();
        return true;
    }

    // =========================================================================
    // 6. FEAT_59: R8/RGBA8 双格式动态图集 LRU 淘汰 × 内存告警代数递增同步 MQRmlFontEngine
    // =========================================================================
    E2E_TEST(Tier3, FEAT_59, Pair_FontHidUi_DualFormatGlyphAtlasLruEvictionAndMemoryWarningSyncsRmlEngine)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));
        E2E_ASSERT_STATUS_OK(fontSys.initGpuResources(&wgl2Ctx));

        GlyphAtlasManager& atlas = fontSys.getAtlasManager();
        E2E_ASSERT_TRUE(GlyphAtlasManager::getGlyphFormat(U'剑') == TextureFormat::R8_UNORM);
        E2E_ASSERT_TRUE(GlyphAtlasManager::getGlyphFormat(0x1F680u) == TextureFormat::R8G8B8A8_UNORM);

        atlas.setCurrentFrame(10);
        uint32_t r8PageId = 0;
        AtlasAllocation r8Alloc{};
        E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(U'剑', 24, 24, r8PageId, r8Alloc));

        atlas.setCurrentFrame(25);
        uint32_t rgbaPageId = 0;
        AtlasAllocation rgbaAlloc{};
        E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(0x1F680u, 28, 28, rgbaPageId, rgbaAlloc));

        E2E_ASSERT_NE(r8PageId, rgbaPageId);
        E2E_ASSERT_EQ(atlas.getPageCount(), 2u);
        E2E_ASSERT_TRUE(atlas.getAtlasPageFormat(r8PageId) == TextureFormat::R8_UNORM);
        E2E_ASSERT_TRUE(atlas.getAtlasPageFormat(rgbaPageId) == TextureFormat::R8G8B8A8_UNORM);

        atlas.setCurrentFrame(40);
        E2E_ASSERT_TRUE(atlas.evictLruPage());
        E2E_ASSERT_EQ(atlas.getPageCount(), 1u);
        E2E_ASSERT_TRUE(atlas.getAtlasPageTexture(r8PageId) == nullptr);
        E2E_ASSERT_TRUE(atlas.getAtlasPageTexture(rgbaPageId) != nullptr);

        MQRmlFontEngine rmlFont(&fontSys);
        rmlFont.Initialize();
        (void)rmlFont.LoadFontFace("@system-ui", true);
        const int genBefore = rmlFont.getGeneration();
        const int widthBefore = rmlFont.getStringWidth("图集重建验证", 18);
        E2E_ASSERT_GT(widthBefore, 0);

        const uint32_t atlasGenBefore = fontSys.getAtlasGeneration();
        fontSys.onMemoryWarning();
        E2E_ASSERT_GT(fontSys.getAtlasGeneration(), atlasGenBefore);

        // 图集清空后重新测宽应自动递增代数并无崩溃重填缺失字形
        const int widthAfter = rmlFont.getStringWidth("图集重建验证", 18);
        E2E_ASSERT_GT(widthAfter, 0);
        E2E_ASSERT_GT(rmlFont.getGeneration(), genBefore);

        rmlFont.Shutdown();
        fontSys.term();
        return true;
    }

    // =========================================================================
    // 7. FEAT_47: RmlUiComposite 剥离内置字体/Debugger × 生成 UI|RmlUi 绘制桶接入 RenderGraphMatcher
    // =========================================================================
    E2E_TEST(Tier3, FEAT_47, Pair_FontHidUi_RmlUiCompositeStripsDefaultFontAndDebuggerEmitsUiBucket)
    {
        RmlUiComposite uiComp;
        uiComp.initFontSubsystem();

        E2E_ASSERT_TRUE(uiComp.isCustomFontEngineActive());
        E2E_ASSERT_FALSE(uiComp.isDefaultFontEngineActive());
        E2E_ASSERT_FALSE(uiComp.isDebuggerEnabled());
        E2E_ASSERT_TRUE(uiComp.isBackbufferTarget());
        E2E_ASSERT_FALSE(uiComp.allocatesOffscreenRt());

        const std::string rmlMarkup =
            "<rml><head><title>HUD</title></head>"
            "<body><div id=\"score_label\">SCORE: 100</div></body></rml>";
        E2E_ASSERT_STATUS_OK(uiComp.loadDocumentFromMemory(rmlMarkup, "hud_doc"));
        E2E_ASSERT_TRUE(uiComp.isDocumentVisible("hud_doc"));
        E2E_ASSERT_EQ(uiComp.getActiveDocumentCount(), 1u);
        E2E_ASSERT_TRUE(uiComp.setElementInnerText("score_label", "SCORE: 8880"));

        const RmlGeneratedDrawBucket uiBucket = uiComp.generateDrawBucket(7u);
        E2E_ASSERT_EQ(uiBucket.boundTechId, 7u);
        E2E_ASSERT_TRUE(uiBucket.hasInputTag("UI"));
        E2E_ASSERT_TRUE(uiBucket.hasInputTag("RmlUi"));

        PipelineBindingDesc uiPipe{};
        uiPipe.pipelineId = 4701u;
        uiPipe.ownerTechId = 7u;
        uiPipe.name = "RmlUi_BackbufferAlphaBlend";
        uiPipe.requiredPassTags = PassTags::UI;
        uiPipe.requiredInputTags = InputTags::UI | InputTags::RmlUi;
        uiPipe.requiresDepthWrite = false;

        RenderGraphMatcher matcher;
        matcher.registerPipeline(uiPipe);
        const std::vector<DrawBucket> frameBuckets = {uiBucket};
        const auto execItems = matcher.evaluatePassExecutionItems(PassTags::UI, frameBuckets);
        E2E_ASSERT_EQ(execItems.size(), 1u);
        E2E_ASSERT_EQ(execItems[0].pipelineId, 4701u);

        uiComp.onUnload();
        return true;
    }

    // =========================================================================
    // 8. FEAT_48: MQRmlFontEngine @system-ui 零大字库 I/O × DPR 物理字号量化与基线符号翻转
    // =========================================================================
    E2E_TEST(Tier3, FEAT_48, Pair_FontHidUi_MQRmlFontEngineSystemUiDprQuantizeAndBaselineSignFlip)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));
        E2E_ASSERT_STATUS_OK(fontSys.initGpuResources(&wgl2Ctx));

        MQRmlFileInterface fileInterface;
        MQRmlFontEngine fontEngine(&fontSys);
        fontEngine.Initialize();

        E2E_ASSERT_TRUE(fontEngine.LoadFontFace("@system-ui", true));
        E2E_ASSERT_FALSE(fileInterface.hasQueriedPath("msyh"));
        E2E_ASSERT_FALSE(fileInterface.hasQueriedPath(".ttf"));
        E2E_ASSERT_FALSE(fileInterface.hasQueriedPath(".otf"));

        fontEngine.setDevicePixelRatio(2.0f);
        E2E_ASSERT_NEAR(fontEngine.getDevicePixelRatio(), 2.0f, 1e-4f);
        E2E_ASSERT_EQ(MQRmlFontEngine::quantizePhysicalFontSize(16, 2.0f), static_cast<uint16_t>(32));

        const Rml::FontFaceHandle faceHandle = fontEngine.getFontFaceHandle("@system-ui", 16);
        E2E_ASSERT_NE(faceHandle, static_cast<Rml::FontFaceHandle>(0));

        const Rml::FontMetrics& rmlMetrics = fontEngine.GetFontMetrics(faceHandle);
        const FCT::SAI::FontMetrics signedMetrics = fontEngine.getSignedFontMetrics(faceHandle);
        // RmlUi 要求 descent 为正标量，而底层引擎签名字体度量的 descent 为负值或反号约定
        E2E_ASSERT_GT(rmlMetrics.ascent, 0.0f);
        E2E_ASSERT_GE(rmlMetrics.descent, 0.0f);
        E2E_ASSERT_LE(signedMetrics.descent, 0.0f);
        E2E_ASSERT_GE(rmlMetrics.line_spacing, rmlMetrics.ascent + rmlMetrics.descent - 0.5f);

        const int mixedWidth = fontEngine.getStringWidth("抖音小游戏🎉", 16);
        E2E_ASSERT_GT(mixedWidth, 0);

        const std::string r8Uri = MQRmlFontEngine::buildAtlasPageUri(0u, false);
        const std::string emojiUri = MQRmlFontEngine::buildAtlasPageUri(1u, true);
        E2E_ASSERT_TRUE(r8Uri.find("@mq_glyph_atlas/0") != std::string::npos);
        E2E_ASSERT_TRUE(emojiUri.find("@mq_glyph_atlas/1") != std::string::npos);

        fontEngine.Shutdown();
        fontSys.term();
        return true;
    }

    // =========================================================================
    // 9. FEAT_49: MQRmlRenderInterface 直出 Backbuffer × Scissor 裁剪与未编译句柄安全跳过
    // =========================================================================
    E2E_TEST(Tier3, FEAT_49, Pair_FontHidUi_MQRmlRenderInterfaceBackbufferDirectScissorAndUncompiledGuard)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        MQRmlRenderInterface renderInterface;
        renderInterface.bindRhiContext(&wgl2Ctx);

        E2E_ASSERT_TRUE(renderInterface.isBackbufferTarget());
        E2E_ASSERT_FALSE(renderInterface.allocatesOffscreenRt());

        const MQRmlPipelineRenderState state = renderInterface.getPipelineRenderState();
        E2E_ASSERT_FALSE(state.depthWriteEnabled);
        E2E_ASSERT_FALSE(state.depthTestEnabled);
        E2E_ASSERT_TRUE(state.blendEnabled);

        std::vector<Rml::Vertex> quadVerts;
        std::vector<int> quadIndices;
        T3UiHid_BuildUnitUiQuad(quadVerts, quadIndices);

        const Rml::CompiledGeometryHandle validGeom = renderInterface.compileGeometry(quadVerts, quadIndices);
        E2E_ASSERT_NE(validGeom, static_cast<Rml::CompiledGeometryHandle>(0));
        E2E_ASSERT_TRUE(renderInterface.isGeometryCompiled(validGeom));

        renderInterface.enableScissorRegion(true);
        renderInterface.setScissorRegion(24, 36, 320, 180);
        E2E_ASSERT_TRUE(renderInterface.isScissorEnabled());
        E2E_ASSERT_EQ(renderInterface.getScissorRegion().width, 320);

        renderInterface.renderGeometry(validGeom, 12.0f, 18.0f, 2u);
        E2E_ASSERT_EQ(renderInterface.getRecordedCommandCount(), 1u);
        E2E_ASSERT_EQ(renderInterface.getSkippedDrawCommandCount(), 0u);

        // 非法未编译几何句柄必须被渲染后端安全拦截并累加跳过计数，绝不崩溃
        renderInterface.renderGeometry(static_cast<Rml::CompiledGeometryHandle>(999999u), 0.0f, 0.0f, 2u);
        E2E_ASSERT_EQ(renderInterface.getRecordedCommandCount(), 1u);
        E2E_ASSERT_EQ(renderInterface.getSkippedDrawCommandCount(), 1u);

        const RenderGraphAssetDesc miniGraph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
        E2E_ASSERT_FALSE(miniGraph.isDirectToBackbuffer());
        E2E_ASSERT_EQ(miniGraph.countOffscreenColorImages(), 1u);

        const DrawBucket aggBucket = renderInterface.buildAggregateDrawBucket(1u);
        E2E_ASSERT_TRUE((aggBucket.inputTagsMask & InputTags::UI) != 0ULL);
        E2E_ASSERT_TRUE((aggBucket.inputTagsMask & InputTags::RmlUi) != 0ULL);

        renderInterface.ReleaseGeometry(validGeom);
        E2E_ASSERT_FALSE(renderInterface.isGeometryCompiled(validGeom));
        return true;
    }

    // =========================================================================
    // 10. FEAT_50: VirtualTextureRouter 零拷贝虚拟 URI 路由 (@mq_glyph_atlas / @mq_shared_canvas / 异步占位热升级)
    // =========================================================================
    E2E_TEST(Tier3, FEAT_50, Pair_FontHidUi_VirtualTextureRouterZeroCopyGlyphAtlasSharedCanvasAndAsyncSwap)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));
        E2E_ASSERT_STATUS_OK(fontSys.initGpuResources(&wgl2Ctx));
        const std::vector<char32_t> seedChars = {U'榜', U'单'};
        E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(seedChars, 20, "@system-ui"));
        FCT::Image* expectedAtlasImg = fontSys.getAtlasPageTexture(0u);
        E2E_ASSERT_TRUE(expectedAtlasImg != nullptr);

        VirtualTextureRouter router;
        router.bindFontSystem(&fontSys);
        router.setPlaceholderTextureHandle(2u);

        Rml::Vector2i atlasDims{0, 0};
        const Rml::TextureHandle glyphTexHandle = router.loadTexture(atlasDims, "@mq_glyph_atlas/0");
        E2E_ASSERT_NE(glyphTexHandle, static_cast<Rml::TextureHandle>(0));
        E2E_ASSERT_EQ(router.resolveBoundGpuImage(glyphTexHandle), expectedAtlasImg);

        OpenDataLeaderboardController leaderboard(&wgl2Ctx, &router);
        const Rml::TextureHandle rankCanvasHandle = leaderboard.initializeSharedCanvas(512u, 512u);
        E2E_ASSERT_NE(rankCanvasHandle, static_cast<Rml::TextureHandle>(0));

        Rml::Vector2i rankDims{0, 0};
        const Rml::TextureHandle routedRankHandle = router.loadTexture(rankDims, "@mq_shared_canvas/rank");
        E2E_ASSERT_EQ(routedRankHandle, rankCanvasHandle);
        E2E_ASSERT_EQ(rankDims.x, 512);
        E2E_ASSERT_EQ(router.resolveBoundGpuImage(routedRankHandle), leaderboard.getSharedCanvasImage());

        // 未就绪常规贴图先瞬时返回 1x1 占位句柄 2，异步落盘通知后原地切换至真实句柄
        Rml::Vector2i bannerDims{0, 0};
        const Rml::TextureHandle pendingHandle = router.loadTexture(bannerDims, "Textures/UI/ActivityBanner.astc");
        E2E_ASSERT_EQ(pendingHandle, static_cast<Rml::TextureHandle>(2u));

        router.notifyAssetLoaded("Textures/UI/ActivityBanner.astc", 77u, Rml::Vector2i(512, 256));
        const Rml::TextureHandle readyHandle = router.loadTexture(bannerDims, "Textures/UI/ActivityBanner.astc");
        E2E_ASSERT_EQ(readyHandle, static_cast<Rml::TextureHandle>(77u));
        E2E_ASSERT_EQ(bannerDims.x, 512);
        E2E_ASSERT_EQ(bannerDims.y, 256);

        fontSys.term();
        return true;
    }

    // =========================================================================
    // 11. FEAT_51: VirtualTouchController 左拇指摇杆 + 右拇指技能双指并发直注 HID 且零场景穿透
    // =========================================================================
    E2E_TEST(Tier3, FEAT_51, Pair_FontHidUi_VirtualTouchDualFingerJoystickAndButtonDirectHidNoSceneLeak)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());
        (void)hid.registerAction("Move", EInputActionValueType::Axis2D, EInputAccumulationPolicy::Cumulative);
        (void)hid.registerAction("CastSkill", EInputActionValueType::Digital, EInputAccumulationPolicy::MaxMagnitude);

        VirtualTouchController touchCtrl(&hid);
        touchCtrl.bindJoystick("Move", 150.0f, 500.0f, 80.0f);
        touchCtrl.bindButton("CastSkill", 620.0f, 460.0f, 100.0f, 100.0f);

        // 左拇指 (pointer 0) 按压摇杆并向右上方推动，右拇指 (pointer 1) 同时点击技能按钮
        E2E_ASSERT_TRUE(touchCtrl.onTouchDown(0, 150.0f, 500.0f));
        E2E_ASSERT_TRUE(touchCtrl.onTouchMove(0, 210.0f, 460.0f));
        E2E_ASSERT_TRUE(touchCtrl.onTouchDown(1, 660.0f, 500.0f));

        E2E_ASSERT_EQ(touchCtrl.getActivePointerCount(), 2u);
        E2E_ASSERT_EQ(touchCtrl.getConsumedPointerCount(), 2u);
        E2E_ASSERT_TRUE(touchCtrl.isPointerConsumedByUi(0));
        E2E_ASSERT_TRUE(touchCtrl.isPointerConsumedByUi(1));
        E2E_ASSERT_TRUE(touchCtrl.getButtonState("CastSkill"));

        const FCT::Vec2 moveVec = hid.getActionValue("Move").get<FCT::Vec2>();
        E2E_ASSERT_GT(moveVec.x, 0.5f);
        E2E_ASSERT_TRUE(hid.getActionValue("CastSkill").get<bool>());

        // 被 UI 控件消化的双指触控绝不泄漏到底层 3D 场景相机滑动源
        E2E_ASSERT_NEAR(hid.getTouchSwipeDelta().x, 0.0f, 1e-5f);
        E2E_ASSERT_NEAR(hid.getTouchSwipeDelta().y, 0.0f, 1e-5f);

        (void)touchCtrl.onTouchUp(0, 210.0f, 460.0f);
        (void)touchCtrl.onTouchUp(1, 660.0f, 500.0f);
        E2E_ASSERT_FALSE(touchCtrl.getButtonState("CastSkill"));

        // 点击空白场景区域 (pointer 2) 应返回未消化并向 HIDSubsystem 转发相机滑动增量
        E2E_ASSERT_FALSE(touchCtrl.onTouchDown(2, 400.0f, 220.0f));
        E2E_ASSERT_FALSE(touchCtrl.onTouchMove(2, 475.0f, 240.0f));
        E2E_ASSERT_FALSE(touchCtrl.isPointerConsumedByUi(2));
        E2E_ASSERT_GT(hid.getTouchSwipeDelta().x, 0.0f);
        (void)touchCtrl.onTouchUp(2, 475.0f, 240.0f);

        hid.term();
        return true;
    }

    // =========================================================================
    // 12. FEAT_52: 异形屏安全区与抖音右上角胶囊避让 CSS 变量注入 × 旋转重排响应
    // =========================================================================
    E2E_TEST(Tier3, FEAT_52, Pair_FontHidUi_CapsuleCssVariableInjectorDprSafeAreaAndWindowResizeRelayout)
    {
        DouyinMiniGame::DouyinSystemSafeArea sysArea{};
        sysArea.windowWidth = 390.0f;
        sysArea.windowHeight = 844.0f;
        sysArea.pixelRatio = 2.0f;
        sysArea.safeLeft = 0.0f;
        sysArea.safeTop = 44.0f;
        sysArea.safeRight = 390.0f;
        sysArea.safeBottom = 810.0f;

        DouyinMiniGame::DouyinCapsuleRect capsuleRect{};
        capsuleRect.left = 291.0f;
        capsuleRect.top = 48.0f;
        capsuleRect.right = 378.0f;
        capsuleRect.bottom = 80.0f;
        capsuleRect.width = 87.0f;
        capsuleRect.height = 32.0f;

        const auto sanitized = DouyinMiniGame::CapsuleAvoidanceBridge::computeSanitizedMetrics(sysArea, capsuleRect);
        E2E_ASSERT_GT(sanitized.capsuleAvoidTopPx, 0.0f);
        E2E_ASSERT_GT(sanitized.capsuleAvoidRightPx, 0.0f);

        CapsuleCssVariableInjector injector;
        injector.setDpr(sysArea.pixelRatio);
        injector.updateSafeArea(44.0f, 34.0f, 0.0f, 0.0f);
        injector.updateCapsule(
            capsuleRect.top,
            capsuleRect.right,
            capsuleRect.width,
            capsuleRect.height,
            sysArea.windowWidth);
        injector.onWindowResize(390, 844);

        E2E_ASSERT_GE(injector.getAvoidTop(), 48.0f);
        E2E_ASSERT_GE(injector.getAvoidTop() + capsuleRect.height, 80.0f);
        E2E_ASSERT_GE(injector.getAvoidRight(), 99.0f);
        E2E_ASSERT_EQ(injector.getCssVariable("--mq-device-pixel-ratio"), "2.0");
        E2E_ASSERT_EQ(injector.getCssVariable("--mq-safe-area-top"), "44px");
        E2E_ASSERT_FALSE(injector.getCssVariable("--mq-capsule-avoid-top").empty());
        E2E_ASSERT_FALSE(injector.getCssVariable("--mq-capsule-avoid-right").empty());
        E2E_ASSERT_EQ(injector.getResizeCount(), 1u);

        // 模拟横竖屏翻转触发窗口重排更新
        injector.onWindowResize(844, 390);
        E2E_ASSERT_EQ(injector.getResizeCount(), 2u);
        E2E_ASSERT_EQ(injector.getCssVariable("--mq-window-width"), "844px");
        E2E_ASSERT_EQ(injector.getCssVariable("--mq-window-height"), "390px");
        return true;
    }

    // =========================================================================
    // 13. FEAT_54: 首帧 Splash 幂等卸载跨插件接力 × 开放数据域排行榜隐藏 0 FPS 休眠与滚动局部上传
    // =========================================================================
    E2E_TEST(Tier3, FEAT_54, Pair_FontHidUi_SplashIdempotentDismissAndLeaderboardZeroFpsSleepVsScrollUpload)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3UiHid_InitHeadlessWgl2(wgl2Ctx);

        DouyinMiniGame::SplashRelayCoordinator douyinSplash(nullptr);
        GameHtmlUI::SplashRelayCoordinator uiSplash;
        uint32_t splashRelayCount = 0;
        uiSplash.setDismissCallback(T3UiHid_SplashBridgeRecorder{&splashRelayCount, &douyinSplash});

        E2E_ASSERT_FALSE(uiSplash.isSplashDismissed());
        E2E_ASSERT_FALSE(douyinSplash.isSplashDismissed());

        // 连续三次渲染完成通知必须严格保持幂等，仅触发一次宿主封面卸载
        uiSplash.notifyFirstFrameRendered();
        uiSplash.notifyFirstFrameRendered();
        uiSplash.notifyFirstFrameRendered();
        E2E_ASSERT_TRUE(uiSplash.isSplashDismissed());
        E2E_ASSERT_TRUE(douyinSplash.isSplashDismissed());
        E2E_ASSERT_EQ(uiSplash.getDismissCount(), 1u);
        E2E_ASSERT_EQ(splashRelayCount, 1u);

        VirtualTextureRouter router;
        OpenDataLeaderboardController leaderboard(&wgl2Ctx, &router);
        uint32_t uploadCount = 0;
        uint32_t uploadedW = 0;
        uint32_t uploadedH = 0;
        leaderboard.setSubRegionUploadObserver(
            T3UiHid_SubRegionUploadRecorder{&uploadCount, &uploadedW, &uploadedH});

        const Rml::TextureHandle handle = leaderboard.initializeSharedCanvas(512u, 512u);
        E2E_ASSERT_NE(handle, static_cast<Rml::TextureHandle>(0));

        // 弹窗默认隐藏时必须保持 0 FPS 零 GPU 拷贝休眠
        E2E_ASSERT_FALSE(leaderboard.isVisible());
        E2E_ASSERT_EQ(leaderboard.getTargetRefreshRateFps(), 0u);
        for (int i = 0; i < 5; ++i)
        {
            leaderboard.onFrameUpdate();
        }
        E2E_ASSERT_EQ(leaderboard.getRefreshCount(), 0u);
        E2E_ASSERT_EQ(uploadCount, 0u);

        // 打开排行榜弹窗并触发滚动时，激活 60 FPS 按需子区域上传
        leaderboard.setVisible(true);
        E2E_ASSERT_EQ(leaderboard.getTargetRefreshRateFps(), 60u);
        leaderboard.onFrameUpdate();
        leaderboard.onScroll(0.0f, 48.0f);
        E2E_ASSERT_EQ(leaderboard.getRefreshCount(), 2u);
        E2E_ASSERT_EQ(uploadCount, 2u);
        E2E_ASSERT_EQ(uploadedW, 512u);
        E2E_ASSERT_EQ(uploadedH, 512u);
        E2E_ASSERT_NEAR(leaderboard.getScrollOffset().y, 48.0f, 1e-4f);
        return true;
    }

    // =========================================================================
    // 14. FEAT_60: 四维强类型 InputAction 注册查表 × 具名动作事件绑定与解绑闭环
    // =========================================================================
    E2E_TEST(Tier3, FEAT_60, Pair_FontHidUi_StrongTypedInputActionsDispatchNamedEventCallbacksInHid)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());

        const InputActionHandle hJump = hid.registerAction("IA_Jump", EInputActionValueType::Digital);
        const InputActionHandle hThrottle = hid.registerAction("IA_Throttle", EInputActionValueType::Axis1D);
        const InputActionHandle hMove = hid.registerAction("IA_Move", EInputActionValueType::Axis2D);
        const InputActionHandle hDrone = hid.registerAction("IA_Drone3D", EInputActionValueType::Axis3D);

        E2E_ASSERT_NE(hJump, 0u);
        E2E_ASSERT_NE(hThrottle, 0u);
        E2E_ASSERT_NE(hMove, 0u);
        E2E_ASSERT_NE(hDrone, 0u);

        E2E_ASSERT_TRUE(hid.findAction(hJump)->getValueType() == EInputActionValueType::Digital);
        E2E_ASSERT_TRUE(hid.findAction(hThrottle)->getValueType() == EInputActionValueType::Axis1D);
        E2E_ASSERT_TRUE(hid.findAction(hMove)->getValueType() == EInputActionValueType::Axis2D);
        E2E_ASSERT_TRUE(hid.findAction(hDrone)->getValueType() == EInputActionValueType::Axis3D);

        uint32_t eventCount = 0;
        InputValue capturedVal;
        ETriggerState capturedState = ETriggerState::None;
        const uint64_t cbId = hid.bindActionEvent(
            hDrone,
            ETriggerState::Triggered,
            T3UiHid_ActionEventRecorder{&eventCount, &capturedVal, &capturedState});

        E2E_ASSERT_STATUS_OK(hid.injectInputForAction(hDrone, InputValue(FCT::Vec3f{0.25f, 0.5f, -0.75f})));
        E2E_ASSERT_EQ(eventCount, 1u);
        E2E_ASSERT_TRUE(capturedState == ETriggerState::Triggered);
        E2E_ASSERT_NEAR(capturedVal.get<FCT::Vec3f>().z, -0.75f, 1e-4f);

        hid.unbindActionEvent(cbId);
        E2E_ASSERT_STATUS_OK(hid.injectInputForAction(hDrone, InputValue(FCT::Vec3f{1.0f, 0.0f, 0.0f})));
        E2E_ASSERT_EQ(eventCount, 1u);

        hid.term();
        return true;
    }

    // =========================================================================
    // 15. FEAT_61: InputValue 多源融合策略 (Cumulative vs MaxMagnitude) × NaN/Inf 净化防护
    // =========================================================================
    E2E_TEST(Tier3, FEAT_61, Pair_FontHidUi_InputValueAccumulationPoliciesDimensionPromotionAndNanGuard)
    {
        const float nanVal = std::numeric_limits<float>::quiet_NaN();
        const float infVal = std::numeric_limits<float>::infinity();
        const InputValue sanitized2D(nanVal, infVal);
        E2E_ASSERT_TRUE(std::isfinite(sanitized2D.get<FCT::Vec2>().x));
        E2E_ASSERT_TRUE(std::isfinite(sanitized2D.get<FCT::Vec2>().y));
        E2E_ASSERT_NEAR(sanitized2D.magnitude(), 0.0f, 1e-6f);

        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());

        const InputActionHandle hSteerMax = hid.registerAction(
            "Steer_MaxMag", EInputActionValueType::Axis1D, EInputAccumulationPolicy::MaxMagnitude);
        const InputActionHandle hMoveSum = hid.registerAction(
            "Move_Cumulative", EInputActionValueType::Axis2D, EInputAccumulationPolicy::Cumulative);

        auto imc = makeUnique<InputMappingContext>("IMC_DualSource", 0);
        ActionBindingMapping padSteer{};
        padSteer.inputSourceKey = "Pad_LeftStickX";
        padSteer.actionName = "Steer_MaxMag";
        padSteer.consumesInput = false;
        imc->addBinding(std::move(padSteer));

        ActionBindingMapping keySteer{};
        keySteer.inputSourceKey = "Key_SteerLeft";
        keySteer.actionName = "Steer_MaxMag";
        keySteer.consumesInput = false;
        imc->addBinding(std::move(keySteer));

        ActionBindingMapping moveX{};
        moveX.inputSourceKey = "Virtual_Right";
        moveX.actionName = "Move_Cumulative";
        moveX.consumesInput = false;
        imc->addBinding(std::move(moveX));

        ActionBindingMapping moveY{};
        moveY.inputSourceKey = "Virtual_Forward";
        moveY.actionName = "Move_Cumulative";
        moveY.modifiers.push_back(makeUnique<InputModifierSwizzleAxis>(ESwizzleAxisOrder::YXZ));
        imc->addBinding(std::move(moveY));

        hid.addMappingContext(std::move(imc), 0);

        hid.setSourceInputValue("Pad_LeftStickX", InputValue(0.35f));
        hid.setSourceInputValue("Key_SteerLeft", InputValue(-0.90f));
        hid.setSourceInputValue("Virtual_Right", InputValue(1.0f));
        hid.setSourceInputValue("Virtual_Forward", InputValue(1.0f));
        hid.update(1.0f / 60.0f);

        // MaxMagnitude 保留绝对模长峰值 -0.90f，绝不叠加溢出；Cumulative 执行正交分量向量求和
        E2E_ASSERT_NEAR(hid.getActionValue(hSteerMax).get<float>(), -0.90f, 1e-4f);
        const FCT::Vec2 fusedMove = hid.getActionValue(hMoveSum).get<FCT::Vec2>();
        E2E_ASSERT_NEAR(fusedMove.x, 1.0f, 1e-4f);
        E2E_ASSERT_NEAR(fusedMove.y, 1.0f, 1e-4f);

        hid.term();
        return true;
    }

    // =========================================================================
    // 16. FEAT_62: 顺序修饰器链 (DeadZone -> Swizzle -> Negate -> Normalize2D 单位圆钳制 -> DeltaTimeScale)
    // =========================================================================
    E2E_TEST(Tier3, FEAT_62, Pair_FontHidUi_SequentialModifierChainDeadZoneSwizzleNegateNormalizeAndDtScale)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());

        auto moveAction = makeUnique<InputAction>(
            "WASD_Normalized", 0, EInputActionValueType::Axis2D, EInputAccumulationPolicy::Cumulative);
        moveAction->addModifier(makeUnique<InputModifierNormalize2D>());
        hid.registerAction(std::move(moveAction));

        auto lookAction = makeUnique<InputAction>(
            "Look_FrameIndependent", 0, EInputActionValueType::Axis2D, EInputAccumulationPolicy::Cumulative);
        lookAction->addModifier(makeUnique<InputModifierScalar>(2.0f));
        lookAction->addModifier(makeUnique<InputModifierDeltaTimeScale>());
        hid.registerAction(std::move(lookAction));

        auto imc = makeUnique<InputMappingContext>("IMC_CombatMove", 0);

        ActionBindingMapping bindW{};
        bindW.inputSourceKey = "Src_W";
        bindW.actionName = "WASD_Normalized";
        bindW.modifiers.push_back(makeUnique<InputModifierDeadZone>(0.20f));
        bindW.modifiers.push_back(makeUnique<InputModifierSwizzleAxis>(ESwizzleAxisOrder::YXZ));
        imc->addBinding(std::move(bindW));

        ActionBindingMapping bindA{};
        bindA.inputSourceKey = "Src_A";
        bindA.actionName = "WASD_Normalized";
        bindA.modifiers.push_back(makeUnique<InputModifierDeadZone>(0.20f));
        bindA.modifiers.push_back(makeUnique<InputModifierNegate>(true, false, false));
        imc->addBinding(std::move(bindA));

        ActionBindingMapping bindLook{};
        bindLook.inputSourceKey = "Src_LookRate";
        bindLook.actionName = "Look_FrameIndependent";
        imc->addBinding(std::move(bindLook));

        hid.addMappingContext(std::move(imc), 0);

        // 1) 低于 0.20 死区阈值的摇杆漂移噪声过滤为零向量，且零向量过 Normalize2D 安全无 NaN
        hid.setSourceInputValue("Src_W", InputValue(0.12f));
        hid.setSourceInputValue("Src_A", InputValue(0.05f));
        hid.setSourceInputValue("Src_LookRate", InputValue(FCT::Vec2{90.0f, -45.0f}));
        hid.update(0.02f);

        const FCT::Vec2 deadVec = hid.getActionValue("WASD_Normalized").get<FCT::Vec2>();
        E2E_ASSERT_TRUE(std::isfinite(deadVec.x) && std::isfinite(deadVec.y));
        E2E_ASSERT_NEAR(deadVec.x, 0.0f, 1e-5f);
        E2E_ASSERT_NEAR(deadVec.y, 0.0f, 1e-5f);

        // Look: 90 * Scalar(2.0) * dt(0.02) = 3.6
        const FCT::Vec2 lookDelta = hid.getActionValue("Look_FrameIndependent").get<FCT::Vec2>();
        E2E_ASSERT_NEAR(lookDelta.x, 3.6f, 1e-4f);
        E2E_ASSERT_NEAR(lookDelta.y, -1.8f, 1e-4f);

        // 2) W+A 斜向双键同时满推，原始模长 sqrt(2) 被 Normalize2D 严格钳制至单位圆模长 1.0
        hid.setSourceInputValue("Src_W", InputValue(1.0f));
        hid.setSourceInputValue("Src_A", InputValue(1.0f));
        hid.update(0.02f);

        const InputValue diagVal = hid.getActionValue("WASD_Normalized");
        E2E_ASSERT_NEAR(diagVal.magnitude(), 1.0f, 1e-4f);
        E2E_ASSERT_NEAR(diagVal.get<FCT::Vec2>().x, -0.70710678f, 1e-3f);
        E2E_ASSERT_NEAR(diagVal.get<FCT::Vec2>().y, 0.70710678f, 1e-3f);

        hid.term();
        return true;
    }

    // =========================================================================
    // 17. FEAT_63: 五态触发器状态机 (Released/Tap/Hold/HoldAndRelease) × HID 组合键 ChordedAction 门控
    // =========================================================================
    E2E_TEST(Tier3, FEAT_63, Pair_FontHidUi_FiveStateInputTriggersTapTimeoutHoldReleaseAndChordedGate)
    {
        // Released: Started -> Ongoing -> Triggered -> None
        InputTriggerReleased trigReleased;
        E2E_ASSERT_TRUE(trigReleased.update(InputValue(true), 0.016f) == ETriggerState::Started);
        E2E_ASSERT_TRUE(trigReleased.update(InputValue(true), 0.016f) == ETriggerState::Ongoing);
        E2E_ASSERT_TRUE(trigReleased.update(InputValue(false), 0.016f) == ETriggerState::Triggered);
        E2E_ASSERT_TRUE(trigReleased.update(InputValue(false), 0.016f) == ETriggerState::None);

        // Tap (0.20s): 短按释放 -> Triggered；按住超阈值 -> 立即 Canceled
        InputTriggerTap trigTap(0.20f);
        E2E_ASSERT_TRUE(trigTap.update(InputValue(true), 0.10f) == ETriggerState::Ongoing);
        E2E_ASSERT_TRUE(trigTap.update(InputValue(false), 0.02f) == ETriggerState::Triggered);
        E2E_ASSERT_TRUE(trigTap.update(InputValue(true), 0.25f) == ETriggerState::Canceled);
        E2E_ASSERT_TRUE(trigTap.update(InputValue(false), 0.02f) == ETriggerState::Canceled);

        // HoldAndRelease (0.50s): 蓄力不足释放 -> Canceled；蓄力达标释放 -> Triggered
        InputTriggerHoldAndRelease trigCharge(0.50f);
        E2E_ASSERT_TRUE(trigCharge.update(InputValue(true), 0.30f) == ETriggerState::Ongoing);
        E2E_ASSERT_TRUE(trigCharge.update(InputValue(false), 0.02f) == ETriggerState::Canceled);
        E2E_ASSERT_TRUE(trigCharge.update(InputValue(true), 0.55f) == ETriggerState::Ongoing);
        E2E_ASSERT_TRUE(trigCharge.update(InputValue(false), 0.02f) == ETriggerState::Triggered);

        // ChordedAction 在真实 HIDSubsystem 中验证先决动作门控 (Shift + G 释放终极技能)
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());
        const InputActionHandle hModShift = hid.registerAction("Mod_Shift", EInputActionValueType::Digital);
        const InputActionHandle hUltSkill = hid.registerAction("Ultimate_Skill", EInputActionValueType::Digital);

        auto imc = makeUnique<InputMappingContext>("IMC_ChordSkill", 0);
        ActionBindingMapping shiftBind{};
        shiftBind.inputSourceKey = "Key_Shift";
        shiftBind.actionHandle = hModShift;
        shiftBind.triggers.push_back(makeUnique<InputTriggerDown>());
        imc->addBinding(std::move(shiftBind));

        ActionBindingMapping ultBind{};
        ultBind.inputSourceKey = "Key_G";
        ultBind.actionHandle = hUltSkill;
        ultBind.triggers.push_back(makeUnique<InputTriggerChordedAction>(hModShift, "Mod_Shift"));
        imc->addBinding(std::move(ultBind));
        hid.addMappingContext(std::move(imc), 0);

        // 未按住 Shift 时单按 G 绝不触发终极技能
        hid.setSourceInputValue("Key_Shift", InputValue(false));
        hid.setSourceInputValue("Key_G", InputValue(true));
        hid.update(0.016f);
        E2E_ASSERT_TRUE(hid.getActionTriggerState(hUltSkill) == ETriggerState::None);

        // 先激活 Shift 先决动作，再按 G 立即触发 Triggered
        hid.setSourceInputValue("Key_Shift", InputValue(true));
        hid.setSourceInputValue("Key_G", InputValue(false));
        hid.update(0.016f);
        hid.setSourceInputValue("Key_G", InputValue(true));
        hid.update(0.016f);
        E2E_ASSERT_TRUE(hid.getActionTriggerState(hUltSkill) == ETriggerState::Triggered);

        hid.term();
        return true;
    }

    // =========================================================================
    // 18. FEAT_64: TOML 驱动的优先级 IMC 上下文栈 × 模态弹窗消费阻断与弹出恢复
    // =========================================================================
    E2E_TEST(Tier3, FEAT_64, Pair_FontHidUi_PriorityImcTomlStackModalConsumesSharedKeyAndPopRestoresGameplay)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());

        const InputActionHandle hJump = hid.registerAction("Jump", EInputActionValueType::Digital);
        const InputActionHandle hInteract = hid.registerAction("Interact", EInputActionValueType::Digital);
        const InputActionHandle hConfirm = hid.registerAction("ConfirmModal", EInputActionValueType::Digital);

        const std::string gameplayToml = R"(
[context]
name = "IMC_Gameplay"
priority = 0

[[bindings]]
source = "Key_Space"
action = "Jump"
consumes_input = true
triggers = ["Pressed"]

[[bindings]]
source = "Key_E"
action = "Interact"
consumes_input = true
triggers = ["Down"]
)";

        const std::string modalToml = R"(
[context]
name = "IMC_ModalPopup"
priority = 100

[[bindings]]
source = "Key_Space"
action = "ConfirmModal"
consumes_input = true
triggers = ["Pressed"]
)";

        auto gameplayImc = makeUnique<InputMappingContext>();
        E2E_ASSERT_STATUS_OK(gameplayImc->loadFromToml(gameplayToml));
        auto modalImc = makeUnique<InputMappingContext>();
        E2E_ASSERT_STATUS_OK(modalImc->loadFromToml(modalToml));

        hid.addMappingContext(std::move(gameplayImc), 0);
        hid.addMappingContext(std::move(modalImc), 100);

        const auto resolvedSpace = hid.resolveKeyInput("Key_Space");
        E2E_ASSERT_EQ(resolvedSpace.contextName, "IMC_ModalPopup");
        E2E_ASSERT_EQ(resolvedSpace.actionName, "ConfirmModal");

        // 弹窗在栈顶时：Key_Space 被 IMC_ModalPopup 消费，底层 Jump 不得误触发；未拦截的 Key_E 正常穿透至 Interact
        hid.setSourceInputValue("Key_Space", InputValue(true));
        hid.setSourceInputValue("Key_E", InputValue(true));
        hid.update(0.016f);

        E2E_ASSERT_TRUE(hid.getActionTriggerState(hConfirm) == ETriggerState::Triggered);
        E2E_ASSERT_TRUE(hid.getActionTriggerState(hJump) == ETriggerState::None);
        E2E_ASSERT_TRUE(hid.getActionTriggerState(hInteract) == ETriggerState::Triggered);

        // 关闭弹窗移除高优上下文后，重新按下 Key_Space 立即恢复角色跳跃
        hid.setSourceInputValue("Key_Space", InputValue(false));
        hid.update(0.016f);
        hid.removeMappingContext("IMC_ModalPopup");
        E2E_ASSERT_FALSE(hid.hasActiveContext("IMC_ModalPopup"));

        hid.setSourceInputValue("Key_Space", InputValue(true));
        hid.update(0.016f);
        E2E_ASSERT_TRUE(hid.getActionTriggerState(hJump) == ETriggerState::Triggered);

        hid.term();
        return true;
    }

    // =========================================================================
    // 19. FEAT_65: 多指触控槽位隔离：UI 消费触点零泄漏 vs 场景双指捏合缩放虚拟源驱动
    // =========================================================================
    E2E_TEST(Tier3, FEAT_65, Pair_FontHidUi_MultiTouchIsolationBlocksUiConsumedPointersAndTracksPinch)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());

        const InputActionHandle hCameraZoom = hid.registerAction("CameraZoom", EInputActionValueType::Axis1D);
        auto camImc = makeUnique<InputMappingContext>("IMC_TouchCamera", 0);
        ActionBindingMapping zoomBind{};
        zoomBind.inputSourceKey = "TouchPinchDelta1D";
        zoomBind.actionHandle = hCameraZoom;
        camImc->addBinding(std::move(zoomBind));
        hid.addMappingContext(std::move(camImc), 0);

        // Pointer 0 被标记为 UI 消费，剧烈拖拽也不得产生任何滑动或捏合增量
        hid.onTouchDown(0u, 80.0f, 500.0f, true);
        hid.onTouchMove(0u, 220.0f, 420.0f, true);
        E2E_ASSERT_NEAR(hid.getTouchSwipeDeltaForPointer(0u).x, 0.0f, 1e-5f);
        E2E_ASSERT_NEAR(hid.getTouchSwipeDelta().x, 0.0f, 1e-5f);
        E2E_ASSERT_FALSE(hid.isTouchPinchActive());

        // Pointer 1 与 Pointer 2 落在 3D 场景空白区并向外张开，构成双指放大捏合手势
        hid.onTouchDown(1u, 300.0f, 300.0f, false);
        hid.onTouchDown(2u, 500.0f, 300.0f, false);
        E2E_ASSERT_TRUE(hid.isTouchPinchActive());

        hid.onTouchMove(1u, 240.0f, 300.0f, false);
        hid.onTouchMove(2u, 560.0f, 300.0f, false);
        E2E_ASSERT_GT(hid.getTouchPinchDelta(), 0.0f);

        hid.update(0.016f);
        E2E_ASSERT_GT(hid.getActionValue(hCameraZoom).get<float>(), 0.0f);

        hid.resetTouches();
        E2E_ASSERT_FALSE(hid.isTouchPinchActive());
        E2E_ASSERT_NEAR(hid.getTouchPinchDelta(), 0.0f, 1e-5f);

        hid.term();
        return true;
    }

    // =========================================================================
    // 20. FEAT_66: 无头语义动作注入与 TimestampedAction 序列跨实例比特级确定性回放
    // =========================================================================
    E2E_TEST(Tier3, FEAT_66, Pair_FontHidUi_DeterministicHeadlessTimestampedActionReplayBitExactAcrossRuns)
    {
        HIDSubsystem hidRunA;
        HIDSubsystem hidRunB;
        T3UiHid_ReplayRig rigA{};
        T3UiHid_ReplayRig rigB{};

        E2E_ASSERT_STATUS_OK(T3UiHid_SetupReplaySubsystem(hidRunA, rigA));
        E2E_ASSERT_STATUS_OK(T3UiHid_SetupReplaySubsystem(hidRunB, rigB));

        const auto sequenceA = T3UiHid_BuildDeterministicSequence(rigA);
        const auto sequenceB = T3UiHid_BuildDeterministicSequence(rigB);

        uint32_t processedA = 0;
        uint32_t processedB = 0;
        E2E_ASSERT_STATUS_OK(hidRunA.replayActionSequence(sequenceA, processedA));
        E2E_ASSERT_STATUS_OK(hidRunB.replayActionSequence(sequenceB, processedB));

        E2E_ASSERT_EQ(processedA, 6u);
        E2E_ASSERT_EQ(processedB, 6u);
        E2E_ASSERT_EQ(rigA.moveEvents, rigB.moveEvents);
        E2E_ASSERT_EQ(rigA.fireEvents, rigB.fireEvents);
        E2E_ASSERT_EQ(rigA.chargeEvents, rigB.chargeEvents);
        E2E_ASSERT_GT(rigA.fireEvents, 0u);
        E2E_ASSERT_GT(rigA.chargeEvents, 0u);

        // 两次独立回放的归一化移动向量与最终状态必须逐比特一致
        const FCT::Vec2 vecA = rigA.lastMoveVal.get<FCT::Vec2>();
        const FCT::Vec2 vecB = rigB.lastMoveVal.get<FCT::Vec2>();
        E2E_ASSERT_EQ(std::memcmp(&vecA, &vecB, sizeof(FCT::Vec2)), 0);
        E2E_ASSERT_NEAR(rigA.lastMoveVal.magnitude(), 1.0f, 1e-4f);
        E2E_ASSERT_TRUE(hidRunA.getActionTriggerState(rigA.holdChargeHandle) ==
                        hidRunB.getActionTriggerState(rigB.holdChargeHandle));

        hidRunA.term();
        hidRunB.term();
        return true;
    }

    // =========================================================================
    // 21. FEAT_73: 离线渲染图管线变体裁剪 × RmlUiComposite 显隐驱动动态空载 UI Pass 旁路
    // =========================================================================
    E2E_TEST(Tier3, FEAT_73, Pair_FontHidUi_OfflinePipelineVariantStripAndDynamicEmptyUiPassBypass)
    {
        const std::string techToml = R"(
[[pipelines]]
id = 7301
name = "ForwardPlus_LitOpaque"
required_pass_tags = ["Geometry", "Opaque", "ForwardPlus"]
required_input_tags = ["Mesh", "Opaque"]
order = 10

[[pipelines]]
id = 7302
name = "Desktop_DeferredGBuffer"
required_pass_tags = ["DeferredGBuffer"]
required_input_tags = ["Mesh", "Opaque"]
order = 10

[[pipelines]]
id = 7303
name = "Desktop_RayTracingShadow"
required_pass_tags = ["RayTracing"]
required_input_tags = ["Mesh", "CastShadow"]
order = 5

[[pipelines]]
id = 7304
name = "MiniGame_RmlUiOverlay"
required_pass_tags = ["UI"]
required_input_tags = ["UI", "RmlUi"]
depth_write = false
order = 100
)";

        auto parsedPipesOr = RenderGraphMatcher::LoadTechFromToml(techToml, 1u);
        E2E_ASSERT_STATUS_OK(parsedPipesOr);
        E2E_ASSERT_EQ(parsedPipesOr->size(), 4u);

        const RenderGraphAssetDesc miniGraph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
        const auto retainedPipes = RenderGraphMatcher::StripUnusedPipelinesForGraph(miniGraph, *parsedPipesOr);
        // DeferredGBuffer 与 RayTracing 变体在小游戏 Forward+ 图谱中无匹配 Pass，离线直接剥离 50% 冗余变体
        E2E_ASSERT_EQ(retainedPipes.size(), 2u);

        RenderGraphMatcher matcher;
        for (const auto& p : *parsedPipesOr)
        {
            matcher.registerPipeline(p);
        }
        const size_t strippedCount = matcher.stripUnusedPipelinesForGraph(miniGraph);
        E2E_ASSERT_EQ(strippedCount, 2u);
        E2E_ASSERT_EQ(matcher.getPipelines().size(), 2u);

        RmlUiComposite uiComp;
        uiComp.initFontSubsystem();
        E2E_ASSERT_STATUS_OK(uiComp.loadDocumentFromMemory("<rml><body><div id='dlg'>Pause</div></body></rml>", "pause_dlg"));
        E2E_ASSERT_STATUS_OK(uiComp.hideDocument("pause_dlg"));
        E2E_ASSERT_FALSE(uiComp.isDocumentVisible("pause_dlg"));

        const DrawBucket sceneOpaqueBucket(1u, InputTags::Mesh | InputTags::Opaque | InputTags::Viewport, 32u);

        // 当 UI 文档隐藏不提交 UI DrawBucket 时，UI Pass 处于空载状态被动态旁路跳过
        const std::vector<DrawBucket> sceneOnlyBuckets = {sceneOpaqueBucket};
        const auto activeWithoutUi = matcher.evaluateActivePassNames(miniGraph, sceneOnlyBuckets);
        bool hasUiPassWhenHidden = false;
        for (const std::string& passName : activeWithoutUi)
        {
            const auto* passDesc = miniGraph.findPass(passName);
            if (passDesc && (passDesc->tagMask & PassTags::UI) != 0ULL)
            {
                hasUiPassWhenHidden = true;
            }
        }
        E2E_ASSERT_FALSE(hasUiPassWhenHidden);

        // 显示 UI 文档并提交 RmlGeneratedDrawBucket 后，UI Pass 动态恢复激活执行
        E2E_ASSERT_STATUS_OK(uiComp.showDocument("pause_dlg"));
        const RmlGeneratedDrawBucket uiBucket = uiComp.generateDrawBucket(1u);
        const std::vector<DrawBucket> sceneWithUiBuckets = {sceneOpaqueBucket, uiBucket};
        const auto activeWithUi = matcher.evaluateActivePassNames(miniGraph, sceneWithUiBuckets);
        bool hasUiPassWhenVisible = false;
        for (const std::string& passName : activeWithUi)
        {
            const auto* passDesc = miniGraph.findPass(passName);
            if (passDesc && (passDesc->tagMask & PassTags::UI) != 0ULL)
            {
                hasUiPassWhenVisible = true;
            }
        }
        E2E_ASSERT_TRUE(hasUiPassWhenVisible);

        uiComp.onUnload();
        return true;
    }
} // namespace MQEngine::Testing
