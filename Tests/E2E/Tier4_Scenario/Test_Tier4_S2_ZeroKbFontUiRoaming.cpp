/**
 * @file Test_Tier4_S2_ZeroKbFontUiRoaming.cpp
 * @brief Tier 4 真实负载综合场景 S2：0KB 中文字库策略、双格式动态字形图集、MQRmlFontEngine 与内存告警重建漫游
 */
#include "Common/E2ECommon.h"
#include "GameHtmlUI.h"

#include <Font/Composites/FontRuntimeComposite/FontSystem.h>
#include <Font/Composites/FontRuntimeComposite/GlyphAtlasManager.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>

#include <string>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        using namespace MQEngine::Resource::Font;
        using namespace MQEngine::GameHtmlUI;
        using namespace MQEngine::Domain::Renderer;
    } // namespace

    // S2-01: MiniGame_Hybrid 双轨路由：战斗跳字走 InGameBaked，开放社交中文/Emoji 走 UiRuntime 零 FreeType
    E2E_TEST(Tier4, FEAT_55, S2_01_ZeroKbChineseFontHybridTrackRouting)
    {
        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));
        E2E_ASSERT_EQ(fontSys.getBackendMode(), FontBackendMode::MiniGame_Hybrid);
        E2E_ASSERT_FALSE(fontSys.isFreeTypeBackendActive());

        E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("+8888 暴击"), FontTrack::InGameBaked);
        E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("闪避"), FontTrack::InGameBaked);
        E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("公会招募：今晚八点跨服团战！"), FontTrack::UiRuntime);
        E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("赛季段位奖励\xF0\x9F\x8F\x86"), FontTrack::UiRuntime);

        fontSys.term();
        return true;
    }

    // S2-02: 预烘焙常用字子集优先命中与生僻字/Emoji 批量拼车回退 SystemFontProvider
    E2E_TEST(Tier4, FEAT_58, S2_02_PrebakedSubsetHitAndBatchSystemFontFallback)
    {
        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));

        const char32_t prebakedUiChars[] = {U'确', U'认', U'取', U'消'};
        fontSys.registerPrebakedSubset(prebakedUiChars);

        E2E_ASSERT_TRUE(fontSys.isGlyphInPrebakedSubset(U'确'));
        E2E_ASSERT_FALSE(fontSys.queriesSystemFontProvider(U'确'));
        E2E_ASSERT_TRUE(fontSys.queriesSystemFontProvider(U'饕'));
        E2E_ASSERT_TRUE(fontSys.queriesSystemFontProvider(0x1F525u));

        const char32_t mixedSentence[] = {U'A', U'确', U'饕', U'餮', 0x1F525u};
        E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(mixedSentence, 20u, "@system-ui", 4u, false));

        CachedUiGlyph hanziGlyph{};
        CachedUiGlyph emojiGlyph{};
        E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph("@system-ui", U'饕', 20u, 4u, false, hanziGlyph));
        E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph("@system-ui", 0x1F525u, 20u, 4u, false, emojiGlyph));
        E2E_ASSERT_EQ(hanziGlyph.format, TextureFormat::R8_UNORM);
        E2E_ASSERT_EQ(emojiGlyph.format, TextureFormat::R8G8B8A8_UNORM);

        fontSys.term();
        return true;
    }

    // S2-03: GlyphAtlasManager 双格式页（R8_UNORM 单通道灰阶 + RGBA8_UNORM 彩色 Emoji）同屏共存隔离
    E2E_TEST(Tier4, FEAT_59, S2_03_DualFormatGlyphAtlasPagesCoexistence)
    {
        GlyphAtlasManager atlasMgr;
        E2E_ASSERT_STATUS_OK(atlasMgr.initGpuResources(nullptr));

        uint32_t r8PageId = 0xFFFFFFFFu;
        AtlasAllocation r8Alloc{};
        E2E_ASSERT_STATUS_OK(atlasMgr.allocateGlyph(U'永', 24u, 24u, r8PageId, r8Alloc));
        E2E_ASSERT_TRUE(r8Alloc.success);

        uint32_t rgbaPageId = 0xFFFFFFFFu;
        AtlasAllocation rgbaAlloc{};
        E2E_ASSERT_STATUS_OK(atlasMgr.allocateGlyph(0x1F680u, 32u, 32u, rgbaPageId, rgbaAlloc));
        E2E_ASSERT_TRUE(rgbaAlloc.success);

        E2E_ASSERT_NE(r8PageId, rgbaPageId);
        E2E_ASSERT_EQ(atlasMgr.getAtlasPageFormat(r8PageId), TextureFormat::R8_UNORM);
        E2E_ASSERT_EQ(atlasMgr.getAtlasPageFormat(rgbaPageId), TextureFormat::R8G8B8A8_UNORM);
        E2E_ASSERT_GE(atlasMgr.getPageCount(), 2u);

        std::vector<uint8_t> r8Pixels(24u * 24u, 200u);
        std::vector<uint8_t> rgbaPixels(32u * 32u * 4u, 255u);
        E2E_ASSERT_STATUS_OK(atlasMgr.uploadGlyphBitmap(r8PageId, r8Alloc, r8Pixels.data(), r8Pixels.size()));
        E2E_ASSERT_STATUS_OK(atlasMgr.uploadGlyphBitmap(rgbaPageId, rgbaAlloc, rgbaPixels.data(), rgbaPixels.size()));

        atlasMgr.releaseGpuResources();
        return true;
    }

    // S2-04: MQRmlFontEngine DPR 字号量化、带符号/无符号基线度量契约与多国混排网格发射
    E2E_TEST(Tier4, FEAT_48, S2_04_MQRmlFontEngineDprQuantizationAndMeshGeneration)
    {
        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));

        MQRmlFontEngine rmlFont(&fontSys);
        rmlFont.Initialize();
        rmlFont.setDevicePixelRatio(2.0f);
        E2E_ASSERT_EQ(MQRmlFontEngine::quantizePhysicalFontSize(16, 2.0f), 32u);

        const Rml::FontFaceHandle faceHandle = rmlFont.getFontFaceHandle("@system-ui", 16);
        E2E_ASSERT_NE(faceHandle, 0u);

        const Rml::FontMetrics& rmlMetrics = rmlFont.GetFontMetrics(faceHandle);
        E2E_ASSERT_GT(rmlMetrics.ascent, 0.0f);
        E2E_ASSERT_GT(rmlMetrics.descent, 0.0f);
        E2E_ASSERT_GE(rmlMetrics.line_spacing, rmlMetrics.ascent + rmlMetrics.descent);

        const FCT::SAI::FontMetrics signedMetrics = rmlFont.getSignedFontMetrics(faceHandle);
        E2E_ASSERT_LT(signedMetrics.descent, 0.0f);

        const std::string mixedText = "Lv.99 巅峰王者\xF0\x9F\x94\xA5";
        const int stringWidth = rmlFont.GetStringWidth(faceHandle, mixedText);
        E2E_ASSERT_GT(stringWidth, 0);

        std::vector<Rml::Mesh> generatedMeshes;
        const int genAdvance = rmlFont.GenerateString(
            nullptr,
            faceHandle,
            mixedText,
            Rml::Vector2f{12.0f, 24.0f},
            Rml::Colourb{255, 255, 255, 255},
            generatedMeshes);
        E2E_ASSERT_GT(genAdvance, 0);
        E2E_ASSERT_FALSE(generatedMeshes.empty());

        rmlFont.Shutdown();
        fontSys.term();
        return true;
    }

    // S2-05: VirtualTextureRouter 拦截 @mq_glyph_atlas/<pageId>、@mq_shared_canvas/rank 与缺失贴图占位符热替换
    E2E_TEST(Tier4, FEAT_50, S2_05_VirtualTextureRouterZeroCopyAndHotSwap)
    {
        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));

        VirtualTextureRouter router;
        router.bindFontSystem(&fontSys);
        router.registerFontAtlasPage(0u, 1001u, TextureFormat::R8_UNORM, Rml::Vector2i{1024, 1024});
        router.registerSharedCanvas("rank", 2002u, Rml::Vector2i{512, 768}, nullptr);

        Rml::Vector2i atlasDim{0, 0};
        const Rml::TextureHandle hAtlas = router.loadTexture(atlasDim, "@mq_glyph_atlas/0");
        E2E_ASSERT_EQ(hAtlas, 1001u);
        E2E_ASSERT_EQ(atlasDim.x, 1024);
        E2E_ASSERT_EQ(atlasDim.y, 1024);

        Rml::Vector2i rankDim{0, 0};
        const Rml::TextureHandle hRank = router.loadTexture(rankDim, "@mq_shared_canvas/rank");
        E2E_ASSERT_EQ(hRank, 2002u);
        E2E_ASSERT_EQ(rankDim.x, 512);
        E2E_ASSERT_EQ(rankDim.y, 768);

        Rml::Vector2i pendingDim{0, 0};
        const Rml::TextureHandle hPending = router.loadTexture(pendingDim, "ui/shop_banner_cdn.astc");
        E2E_ASSERT_EQ(hPending, router.getPlaceholderTextureHandle());

        router.notifyAssetLoaded("ui/shop_banner_cdn.astc", 3003u, Rml::Vector2i{640, 256});
        const Rml::TextureHandle hLoaded = router.loadTexture(pendingDim, "ui/shop_banner_cdn.astc");
        E2E_ASSERT_EQ(hLoaded, 3003u);
        E2E_ASSERT_EQ(pendingDim.x, 640);

        fontSys.term();
        return true;
    }

    // S2-06: MQRmlRenderInterface 编译几何体、Scissor 裁剪与直出主屏 Backbuffer DrawBucket 管道对齐
    E2E_TEST(Tier4, FEAT_49, S2_06_MQRmlRenderInterfaceDirectBackbufferDrawBucket)
    {
        MQRmlRenderInterface renderInterface;
        E2E_ASSERT_TRUE(renderInterface.isBackbufferTarget());
        E2E_ASSERT_FALSE(renderInterface.allocatesOffscreenRt());

        const auto state = renderInterface.getPipelineRenderState();
        E2E_ASSERT_FALSE(state.depthWriteEnabled);
        E2E_ASSERT_FALSE(state.depthTestEnabled);
        E2E_ASSERT_TRUE(state.blendEnabled);

        std::vector<Rml::Vertex> quadVerts(4);
        quadVerts[0].position = Rml::Vector2f{0.0f, 0.0f};
        quadVerts[1].position = Rml::Vector2f{200.0f, 0.0f};
        quadVerts[2].position = Rml::Vector2f{200.0f, 80.0f};
        quadVerts[3].position = Rml::Vector2f{0.0f, 80.0f};
        std::vector<int> quadIndices = {0, 1, 2, 0, 2, 3};

        const Rml::CompiledGeometryHandle geomHandle = renderInterface.compileGeometry(quadVerts, quadIndices);
        E2E_ASSERT_NE(geomHandle, 0u);
        E2E_ASSERT_TRUE(renderInterface.isGeometryCompiled(geomHandle));

        renderInterface.enableScissorRegion(true);
        renderInterface.setScissorRegion(16, 32, 360, 640);
        renderInterface.renderGeometry(geomHandle, 10.0f, 20.0f, 1001u);
        E2E_ASSERT_EQ(renderInterface.getRecordedCommandCount(), 1u);

        renderInterface.renderGeometry(999999u, 0.0f, 0.0f, 1001u);
        E2E_ASSERT_EQ(renderInterface.getSkippedDrawCommandCount(), 1u);

        const DrawBucket uiBucket = renderInterface.buildAggregateDrawBucket(1u);
        E2E_ASSERT_NE(uiBucket.inputTagsMask & InputTags::UI, 0ULL);
        E2E_ASSERT_NE(uiBucket.inputTagsMask & InputTags::RmlUi, 0ULL);

        renderInterface.ReleaseGeometry(geomHandle);
        E2E_ASSERT_FALSE(renderInterface.isGeometryCompiled(geomHandle));
        return true;
    }

    // S2-07: RmlUiFileInterface 无原始中文字库请求审计（0 .ttf/.otf/.ttc 访问）
    E2E_TEST(Tier4, FEAT_47, S2_07_MQRmlFileInterfaceZeroRawFontFileAudit)
    {
        E2ETestBase sandbox;
        sandbox.setUp();
        const auto rmlPath = sandbox.getSandboxFilePath("hud_main.rml");
        const bool wrote = sandbox.writeSandboxFile(
            "hud_main.rml",
            "<rml><head><style>body { font-family: @system-ui; }</style></head><body>背包商城</body></rml>");
        E2E_ASSERT_TRUE(wrote);

        MQRmlFileInterface fileInterface;
        Rml::String content;
        const bool loaded = fileInterface.LoadFile(rmlPath.string(), content);
        E2E_ASSERT_TRUE(loaded);
        E2E_ASSERT_TRUE(fileInterface.hasQueriedPath("hud_main.rml"));
        E2E_ASSERT_FALSE(fileInterface.hasQueriedPath(".ttf"));
        E2E_ASSERT_FALSE(fileInterface.hasQueriedPath(".otf"));
        E2E_ASSERT_FALSE(fileInterface.hasQueriedPath("msyh.ttc"));

        sandbox.tearDown();
        return true;
    }

    // S2-08: 多面板漫游触发 onMemoryWarning 冷门页 LRU 淘汰、Generation 递增与当前可见界面无黑块重建
    E2E_TEST(Tier4, FEAT_59, S2_08_UiPanelRoamingMemoryWarningEvictionAndRebuild)
    {
        FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));

        MQRmlFontEngine rmlFont(&fontSys);
        rmlFont.Initialize();
        const Rml::FontFaceHandle face = rmlFont.getFontFaceHandle("@system-ui", 18);
        E2E_ASSERT_NE(face, 0u);

        fontSys.getAtlasManager().setCurrentFrame(10u);
        E2E_ASSERT_GT(rmlFont.GetStringWidth(face, "主城聊天频道：组队刷深渊副本"), 0);

        fontSys.getAtlasManager().setCurrentFrame(20u);
        E2E_ASSERT_GT(rmlFont.GetStringWidth(face, "限时祈愿卡池：传说武器概率提升\xF0\x9F\x8E\x89"), 0);

        const uint32_t genBefore = fontSys.getAtlasGeneration();
        const int rmlVerBefore = rmlFont.GetVersion(face);

        fontSys.onMemoryWarning();
        rmlFont.notifyAtlasInvalidated();

        E2E_ASSERT_GT(fontSys.getAtlasGeneration(), genBefore);
        E2E_ASSERT_GT(rmlFont.GetVersion(face), rmlVerBefore);

        std::vector<Rml::Mesh> rebuiltMeshes;
        const int rebuiltWidth = rmlFont.GenerateString(
            nullptr,
            face,
            "战斗结算：完美通关 S+",
            Rml::Vector2f{0.0f, 0.0f},
            Rml::Colourb{255, 240, 128, 255},
            rebuiltMeshes);
        E2E_ASSERT_GT(rebuiltWidth, 0);
        E2E_ASSERT_FALSE(rebuiltMeshes.empty());

        rmlFont.Shutdown();
        fontSys.term();
        return true;
    }
} // namespace MQEngine::Testing
