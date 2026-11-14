#include "Common/E2ECommon.h"
#include <FCT/Context/WGL2_Context.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/asset/loader/AssetManifest.h>

using namespace MQEngine::Testing;

E2E_TEST(All, FEAT_INFRA, Smoke_All_ConsolidatedHarness) {
    FCT::WGL2_Context ctx(nullptr);
    E2E_ASSERT_FALSE(ctx.isBindlessSupported());
    E2E_ASSERT_EQ(ctx.getUniformBufferOffsetAlignment(), 256u);

    auto preset = MQEngine::Domain::Renderer::RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    E2E_ASSERT_FALSE(preset.passes.empty());
    E2E_ASSERT_TRUE(preset.isAcyclic());

    MQEngine::Asset::AssetManifest manifest;
    manifest.setCdnBaseUrl("https://cdn.example.com/minigame");
    E2E_ASSERT_EQ(manifest.getCdnBaseUrl(), "https://cdn.example.com/minigame");
    return true;
}

int main(int argc, char** argv) {
    return E2ETestRegistry::instance().run(TestTier::All, argc, argv);
}
