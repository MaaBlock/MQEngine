/**
 * @file DouyinScriptAndWasmBuilder.h
 * @brief Step 3 TypeScript/Blueprint 脚本汇聚打包与 Step 4 Emscripten Wasm 裁剪构建编排器（FEAT-03）
 */
#ifndef MQENGINE_DOUYIN_SCRIPT_AND_WASM_BUILDER_H
#define MQENGINE_DOUYIN_SCRIPT_AND_WASM_BUILDER_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinPackagerConfig.h"

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief Step 3 脚本打包产物报告
     */
    struct ScriptBundleReport
    {
        uint32_t sourceTsFileCount{0};
        uint32_t sourceBlueprintCount{0};
        std::string bundledJsCode;
    };

    /**
     * @brief Step 4 Wasm 引擎构建与裁剪产物报告
     */
    struct WasmBuildReport
    {
        bool compiledViaEmsdkToolchain{false};
        std::vector<std::string> compileFlags;
        std::string compilerPathUsed;
        std::vector<uint8_t> wasmBinary;
    };

    /**
     * @brief 抖音小游戏脚本打包器与 Wasm 裁剪链接器
     */
    class DOUYIN_PACKAGER_CLI_API DouyinScriptAndWasmBuilder
    {
    public:
        static std::string transpileTypeScriptLight(std::string_view tsSource);

        static StatusOr<ScriptBundleReport> bundleProjectScripts(
            ResourceLoader* loader,
            const std::string& projectDir,
            const DouyinPackagerConfig& config);

        static std::vector<std::string> buildEmscriptenCompileFlags(
            const DouyinPackagerConfig& config);

        static std::vector<uint8_t> emitDeterministicSimdEngineWasm();

        static StatusOr<WasmBuildReport> buildEngineWasm(
            ResourceLoader* loader,
            const DouyinPackagerConfig& config,
            const std::string& scratchDir);
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_SCRIPT_AND_WASM_BUILDER_H
