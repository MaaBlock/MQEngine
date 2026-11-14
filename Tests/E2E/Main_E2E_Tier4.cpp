#include "Common/E2ECommon.h"
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>

using namespace MQEngine::Testing;

E2E_TEST(Tier4, FEAT_57, Smoke_Tier4_Scenario_S1_WorkloadBootstrap) {
    E2ETestBase fixture;
    fixture.setUp();

    std::vector<MQEngine::Domain::Renderer::GpuInstanceRow> instances(1000);
    for (uint32_t i = 0; i < 1000; ++i) {
        instances[i].worldSphereBounds = FCT::Vec4{static_cast<float>(i), 0.0f, static_cast<float>(i * 2), 2.5f};
        instances[i].meshSlot = i % 8;
        instances[i].materialSlot = i % 16;
        instances[i].entityId = i + 1;
        instances[i].flags = 1u;
    }

    auto start = std::chrono::steady_clock::now();
    std::string buffer(reinterpret_cast<const char*>(instances.data()), instances.size() * sizeof(MQEngine::Domain::Renderer::GpuInstanceRow));
    bool writeOk = fixture.writeSandboxFile("instances_1000.bin", buffer);
    E2E_ASSERT_TRUE(writeOk);

    uint32_t texelX = 0;
    uint32_t texelY = 0;
    MQEngine::Domain::Renderer::GPUScene::TexelCoordFromSlot(999, 10, texelX, texelY);
    E2E_ASSERT_LT(texelX, MQEngine::Domain::Renderer::GPUScene::DataTextureWidth);

    auto end = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    E2E_ASSERT_LE(elapsedMs, 250);

    fixture.tearDown();
    return true;
}

E2E_TEST(Tier4, FEAT_55, Smoke_Tier4_Scenario_S2_FontPolicyBaseline) {
    using namespace MQEngine::Resource::Font;

    FontSystem fontSys;
    E2E_ASSERT_STATUS_OK(fontSys.init(FontBackendMode::MiniGame_Hybrid));

    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("-9999 暴击"), FontTrack::InGameBaked);
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("每日签到奖励领取"), FontTrack::UiRuntime);
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(U'永'), TextureFormat::R8_UNORM);
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(0x1F525u), TextureFormat::R8G8B8A8_UNORM);

    fontSys.term();
    return true;
}

int main(int argc, char** argv) {
    return E2ETestRegistry::instance().run(TestTier::Tier4, argc, argv);
}
