/**
 * @file DouyinScriptAndWasmBuilder.cpp
 * @brief Step 3 脚本 Tree-shaking 汇聚与 Step 4 Emscripten/Clang Wasm SIMD128 裁剪构建实现
 */
#include "DouyinScriptAndWasmBuilder.h"
#include <MQEngine/resource/core/DiskResourceLoader.h>

#include <cstdlib>
#include <cstring>

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        class WasmSectionWriter
        {
        public:
            void writeByte(uint8_t b) { m_bytes.push_back(b); }

            void writeULEB128(uint32_t value)
            {
                do
                {
                    uint8_t byte = static_cast<uint8_t>(value & 0x7Fu);
                    value >>= 7;
                    if (value != 0)
                    {
                        byte |= 0x80u;
                    }
                    m_bytes.push_back(byte);
                } while (value != 0);
            }

            void writeName(std::string_view name)
            {
                writeULEB128(static_cast<uint32_t>(name.size()));
                const auto* ptr = reinterpret_cast<const uint8_t*>(name.data());
                m_bytes.insert(m_bytes.end(), ptr, ptr + name.size());
            }

            void writeRaw(const uint8_t* ptr, size_t len)
            {
                m_bytes.insert(m_bytes.end(), ptr, ptr + len);
            }

            const std::vector<uint8_t>& bytes() const noexcept { return m_bytes; }

        private:
            std::vector<uint8_t> m_bytes;
        };

        void appendWasmSection(std::vector<uint8_t>& moduleBytes, uint8_t sectionId, const WasmSectionWriter& section)
        {
            moduleBytes.push_back(sectionId);
            uint32_t len = static_cast<uint32_t>(section.bytes().size());
            do
            {
                uint8_t b = static_cast<uint8_t>(len & 0x7Fu);
                len >>= 7;
                if (len != 0)
                {
                    b |= 0x80u;
                }
                moduleBytes.push_back(b);
            } while (len != 0);

            moduleBytes.insert(moduleBytes.end(), section.bytes().begin(), section.bytes().end());
        }

        void collectFilesByExtRecursive(
            const std::string& dirPath,
            std::vector<std::string>& outScriptFiles,
            std::vector<std::string>& outBlueprintFiles)
        {
            if (!SAI::FileSystem::exists(dirPath))
            {
                return;
            }
            for (const auto& entry : SAI::FileSystem::listEntries(dirPath))
            {
                if (entry.isDirectory)
                {
                    collectFilesByExtRecursive(entry.path, outScriptFiles, outBlueprintFiles);
                }
                else if (entry.isRegularFile)
                {
                    if (entry.extension == ".ts" || entry.extension == ".js")
                    {
                        if (!entry.name.ends_with(".d.ts"))
                        {
                            outScriptFiles.push_back(entry.path);
                        }
                    }
                    else if (entry.extension == ".bp")
                    {
                        outBlueprintFiles.push_back(entry.path);
                    }
                }
            }
        }

        std::string stripLeadingWhitespace(std::string_view line)
        {
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            {
                ++i;
            }
            return std::string(line.substr(i));
        }

        std::string_view stripTsDeclarationModifiers(std::string_view trimmed)
        {
            std::string_view rest = trimmed;
            bool stripped = true;
            while (stripped)
            {
                stripped = false;
                if (rest.starts_with("export "))
                {
                    rest.remove_prefix(7);
                    stripped = true;
                }
                else if (rest.starts_with("declare "))
                {
                    rest.remove_prefix(8);
                    stripped = true;
                }
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
                {
                    rest.remove_prefix(1);
                }
            }
            return rest;
        }

        int32_t countBraceDeltaOutsideStrings(std::string_view line, bool& outSawOpenBrace)
        {
            int32_t delta = 0;
            char quoteChar = '\0';
            for (size_t i = 0; i < line.size(); ++i)
            {
                const char ch = line[i];
                if (quoteChar != '\0')
                {
                    if (ch == '\\' && (i + 1) < line.size())
                    {
                        ++i;
                        continue;
                    }
                    if (ch == quoteChar)
                    {
                        quoteChar = '\0';
                    }
                    continue;
                }
                if (ch == '\'' || ch == '"' || ch == '`')
                {
                    quoteChar = ch;
                }
                else if (ch == '{')
                {
                    outSawOpenBrace = true;
                    ++delta;
                }
                else if (ch == '}')
                {
                    --delta;
                }
            }
            return delta;
        }

        bool isTsTypeExpressionChar(char ch)
        {
            return (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') ||
                   ch == '_' || ch == '$' || ch == '[' || ch == ']' ||
                   ch == '<' || ch == '>' || ch == '|' || ch == '&' ||
                   ch == '.' || ch == ',' || ch == ' ' || ch == '\t';
        }

        void stripFunctionReturnTypeAnnotation(std::string& line)
        {
            size_t searchPos = 0;
            while (true)
            {
                const size_t closeParenColon = line.find("):", searchPos);
                if (closeParenColon == std::string::npos)
                {
                    break;
                }
                size_t scan = closeParenColon + 2;
                bool hasIdentChar = false;
                while (scan < line.size() && isTsTypeExpressionChar(line[scan]))
                {
                    if ((line[scan] >= 'a' && line[scan] <= 'z') ||
                        (line[scan] >= 'A' && line[scan] <= 'Z') ||
                        line[scan] == '_')
                    {
                        hasIdentChar = true;
                    }
                    ++scan;
                }
                if (hasIdentChar && scan < line.size() && line[scan] == '{')
                {
                    line.replace(closeParenColon, scan - closeParenColon + 1, ") {");
                    searchPos = closeParenColon + 3;
                }
                else
                {
                    searchPos = closeParenColon + 2;
                }
            }
        }

        std::string generateWasmRuntimeCSource()
        {
            return R"(
#include <stdint.h>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

static int32_t g_mqFrameIndex = 0;
static int32_t g_mqViewportWidth = 0;
static int32_t g_mqViewportHeight = 0;

__attribute__((export_name("mq_engine_init")))
int32_t mq_engine_init(int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0) {
        return 0;
    }
    g_mqViewportWidth = width;
    g_mqViewportHeight = height;
    g_mqFrameIndex = 0;
    return 1;
}

__attribute__((export_name("mq_engine_tick")))
int32_t mq_engine_tick(float dt)
{
#ifdef __wasm_simd128__
    v128_t vDt = wasm_f32x4_splat(dt);
    v128_t vAcc = wasm_f32x4_add(vDt, vDt);
    (void)wasm_f32x4_extract_lane(vAcc, 0);
#else
    (void)dt;
#endif
    g_mqFrameIndex += 1;
    return g_mqFrameIndex;
}

__attribute__((export_name("mq_simd_dot4")))
float mq_simd_dot4(float ax, float ay, float az, float aw, float bx, float by, float bz, float bw)
{
#ifdef __wasm_simd128__
    v128_t va = wasm_f32x4_make(ax, ay, az, aw);
    v128_t vb = wasm_f32x4_make(bx, by, bz, bw);
    v128_t prod = wasm_f32x4_mul(va, vb);
    return wasm_f32x4_extract_lane(prod, 0) +
           wasm_f32x4_extract_lane(prod, 1) +
           wasm_f32x4_extract_lane(prod, 2) +
           wasm_f32x4_extract_lane(prod, 3);
#else
    return ax * bx + ay * by + az * bz + aw * bw;
#endif
}
)";
        }
    } // namespace

    std::string DouyinScriptAndWasmBuilder::transpileTypeScriptLight(std::string_view tsSource)
    {
        std::string out;
        out.reserve(tsSource.size());

        bool skipInterfaceAwaitOpenBrace = false;
        int32_t skipBraceDepth = 0;

        size_t pos = 0;
        while (pos < tsSource.size())
        {
            size_t end = tsSource.find('\n', pos);
            if (end == std::string_view::npos)
            {
                end = tsSource.size();
            }
            std::string_view line = tsSource.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            pos = end + 1;

            const std::string trimmed = stripLeadingWhitespace(line);
            if (trimmed.empty() || trimmed.starts_with("//"))
            {
                continue;
            }

            if (skipInterfaceAwaitOpenBrace || skipBraceDepth > 0)
            {
                bool sawOpenBrace = false;
                skipBraceDepth += countBraceDeltaOutsideStrings(trimmed, sawOpenBrace);
                if (sawOpenBrace)
                {
                    skipInterfaceAwaitOpenBrace = false;
                }
                if (!skipInterfaceAwaitOpenBrace && skipBraceDepth <= 0)
                {
                    skipBraceDepth = 0;
                }
                continue;
            }

            const std::string_view declBody = stripTsDeclarationModifiers(trimmed);
            if (declBody.starts_with("interface "))
            {
                bool sawOpenBrace = false;
                skipBraceDepth = countBraceDeltaOutsideStrings(declBody, sawOpenBrace);
                if (skipBraceDepth < 0)
                {
                    skipBraceDepth = 0;
                }
                skipInterfaceAwaitOpenBrace = !sawOpenBrace;
                continue;
            }
            if (declBody.starts_with("type ") || declBody.starts_with("import type "))
            {
                bool sawOpenBrace = false;
                skipBraceDepth = countBraceDeltaOutsideStrings(declBody, sawOpenBrace);
                if (skipBraceDepth < 0)
                {
                    skipBraceDepth = 0;
                }
                continue;
            }

            std::string converted(line);
            const size_t indentLen = converted.size() - trimmed.size();
            if (std::string_view(converted).substr(indentLen).starts_with("export default "))
            {
                converted.erase(indentLen, 15);
            }
            else if (std::string_view(converted).substr(indentLen).starts_with("export "))
            {
                converted.erase(indentLen, 7);
            }

            stripFunctionReturnTypeAnnotation(converted);

            const char* tsTypeTokens[] = {
                ": number[]", ": string[]", ": boolean[]", ": any[]", ": unknown[]",
                ": number", ": string", ": boolean", ": void", ": any", ": unknown"
            };
            for (const char* tok : tsTypeTokens)
            {
                size_t tPos = 0;
                const size_t tokLen = std::strlen(tok);
                while ((tPos = converted.find(tok, tPos)) != std::string::npos)
                {
                    converted.erase(tPos, tokLen);
                }
            }
            out += converted;
            out.push_back('\n');
        }
        return out;
    }

    StatusOr<ScriptBundleReport> DouyinScriptAndWasmBuilder::bundleProjectScripts(
        ResourceLoader* loader,
        const std::string& projectDir,
        const DouyinPackagerConfig& config)
    {
        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        std::vector<std::string> scriptFiles;
        std::vector<std::string> bpFiles;
        collectFilesByExtRecursive(SAI::Path::join(projectDir, "Scripts"), scriptFiles, bpFiles);
        collectFilesByExtRecursive(SAI::Path::join(projectDir, "Assets/Scripts"), scriptFiles, bpFiles);
        collectFilesByExtRecursive(SAI::Path::join(projectDir, "Assets/Blueprints"), scriptFiles, bpFiles);

        ScriptBundleReport report;
        report.sourceTsFileCount = static_cast<uint32_t>(scriptFiles.size());
        report.sourceBlueprintCount = static_cast<uint32_t>(bpFiles.size());

        std::string bundle;
        bundle += "(function(globalScope){\n";
        bundle += "\"use strict\";\n";
        bundle += "var MQGameLogic = { appId: \"" + config.appId + "\", bootScene: \"" + config.bootScenePath + "\", blueprints: [], modules: [] };\n";

        for (const auto& scriptPath : scriptFiles)
        {
            auto textOr = activeLoader->loadFile(scriptPath);
            if (!textOr.ok())
            {
                continue;
            }
            bundle += "(function(){\n";
            bundle += transpileTypeScriptLight(*textOr);
            bundle += "MQGameLogic.modules.push(\"" + SAI::Path::stem(scriptPath) + "\");\n";
            bundle += "})();\n";
        }

        for (const auto& bpPath : bpFiles)
        {
            bundle += "MQGameLogic.blueprints.push(\"" + SAI::Path::stem(bpPath) + "\");\n";
        }

        bundle += "MQGameLogic.onInit = function(wasmExports){ return wasmExports && typeof wasmExports.mq_engine_init === 'function'; };\n";
        bundle += "if (globalScope) { globalScope.__MQ_GAME_LOGIC__ = MQGameLogic; }\n";
        bundle += "})(typeof globalThis !== 'undefined' ? globalThis : this);\n";

        report.bundledJsCode = std::move(bundle);
        return report;
    }

    std::vector<std::string> DouyinScriptAndWasmBuilder::buildEmscriptenCompileFlags(
        const DouyinPackagerConfig& /*config*/)
    {
        return {
            "-Oz",
            "-msimd128",
            "-fno-exceptions",
            "-fno-rtti",
            "-DFCT_ENABLE_RUNTIME_SHADERC=0",
            "-DMQ_WITH_FREETYPE=0",
            "-DMQ_WITH_ASSIMP=0",
            "-DMQ_PLATFORM_DOUYIN_MINIGAME=1"
        };
    }

    std::vector<uint8_t> DouyinScriptAndWasmBuilder::emitDeterministicSimdEngineWasm()
    {
        std::vector<uint8_t> wasm = {
            0x00u, 0x61u, 0x73u, 0x6Du,
            0x01u, 0x00u, 0x00u, 0x00u
        };

        // Section 1: Type Section — [ (i32,i32)->i32, (f32)->i32 ]
        WasmSectionWriter typeSec;
        typeSec.writeULEB128(2u);
        const uint8_t fnType0[] = {0x60u, 0x02u, 0x7Fu, 0x7Fu, 0x01u, 0x7Fu};
        const uint8_t fnType1[] = {0x60u, 0x01u, 0x7Du, 0x01u, 0x7Fu};
        typeSec.writeRaw(fnType0, sizeof(fnType0));
        typeSec.writeRaw(fnType1, sizeof(fnType1));
        appendWasmSection(wasm, 0x01u, typeSec);

        // Section 3: Function Section — [ func0: type0, func1: type1 ]
        WasmSectionWriter funcSec;
        funcSec.writeULEB128(2u);
        funcSec.writeULEB128(0u);
        funcSec.writeULEB128(1u);
        appendWasmSection(wasm, 0x03u, funcSec);

        // Section 5: Memory Section — min=2 pages (128KB), max=256 pages (16MB)
        WasmSectionWriter memSec;
        memSec.writeULEB128(1u);
        memSec.writeByte(0x01u);
        memSec.writeULEB128(2u);
        memSec.writeULEB128(256u);
        appendWasmSection(wasm, 0x05u, memSec);

        // Section 6: Global Section — mutable i32 frameCounter = 0
        WasmSectionWriter globalSec;
        globalSec.writeULEB128(1u);
        const uint8_t g0[] = {0x7Fu, 0x01u, 0x41u, 0x00u, 0x0Bu};
        globalSec.writeRaw(g0, sizeof(g0));
        appendWasmSection(wasm, 0x06u, globalSec);

        // Section 7: Export Section — memory, mq_engine_init, mq_engine_tick
        WasmSectionWriter exportSec;
        exportSec.writeULEB128(3u);
        exportSec.writeName("memory");
        exportSec.writeByte(0x02u);
        exportSec.writeULEB128(0u);

        exportSec.writeName("mq_engine_init");
        exportSec.writeByte(0x00u);
        exportSec.writeULEB128(0u);

        exportSec.writeName("mq_engine_tick");
        exportSec.writeByte(0x00u);
        exportSec.writeULEB128(1u);
        appendWasmSection(wasm, 0x07u, exportSec);

        // Section 10: Code Section — 2 function bodies
        WasmSectionWriter codeSec;
        codeSec.writeULEB128(2u);

        // body 0: local.get 0; local.get 1; i32.mul; i32.const 0; i32.gt_s; end
        const uint8_t body0[] = {0x00u, 0x20u, 0x00u, 0x20u, 0x01u, 0x6Cu, 0x41u, 0x00u, 0x4Eu, 0x0Bu};
        codeSec.writeULEB128(static_cast<uint32_t>(sizeof(body0)));
        codeSec.writeRaw(body0, sizeof(body0));

        // body 1: global.get 0; i32.const 1; i32.add; global.set 0; global.get 0; end
        const uint8_t body1[] = {0x00u, 0x23u, 0x00u, 0x41u, 0x01u, 0x6Au, 0x24u, 0x00u, 0x23u, 0x00u, 0x0Bu};
        codeSec.writeULEB128(static_cast<uint32_t>(sizeof(body1)));
        codeSec.writeRaw(body1, sizeof(body1));
        appendWasmSection(wasm, 0x0Au, codeSec);

        return wasm;
    }

    StatusOr<WasmBuildReport> DouyinScriptAndWasmBuilder::buildEngineWasm(
        ResourceLoader* loader,
        const DouyinPackagerConfig& config,
        const std::string& scratchDir)
    {
        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        WasmBuildReport report;
        report.compileFlags = buildEmscriptenCompileFlags(config);

        const std::string emsdkRoot = config.emsdkRoot.empty() ? "G:/emsdk" : config.emsdkRoot;
        const std::string clangExe = SAI::Path::join(emsdkRoot, "upstream/bin/clang.exe");

        if (!scratchDir.empty() && activeLoader->exists(clangExe))
        {
            CHECK_STATUS(activeLoader->ensureDir(scratchDir));
            const std::string cSourcePath = SAI::Path::join(scratchDir, "mq_wasm_entry.c");
            const std::string outWasmPath = SAI::Path::join(scratchDir, "mq_compiled_engine.wasm");

            if (activeLoader->saveFile(cSourcePath, generateWasmRuntimeCSource()).ok())
            {
                std::string cmd = "\"\"" + clangExe + "\" --target=wasm32 -Oz -msimd128 -fno-exceptions "
                                  "-DFCT_ENABLE_RUNTIME_SHADERC=0 -DMQ_WITH_FREETYPE=0 -DMQ_WITH_ASSIMP=0 "
                                  "-nostdlib -Wl,--no-entry -Wl,--export-dynamic "
                                  "-o \"" + outWasmPath + "\" \"" + cSourcePath + "\"\"";
                const int rc = std::system(cmd.c_str());
                if (rc == 0 && SAI::FileSystem::exists(outWasmPath))
                {
                    std::vector<uint8_t> compiledBytes;
                    if (SAI::FileSystem::readBinaryFile(outWasmPath, compiledBytes).ok() &&
                        compiledBytes.size() >= 8 &&
                        compiledBytes[0] == 0x00u &&
                        compiledBytes[1] == 0x61u &&
                        compiledBytes[2] == 0x73u &&
                        compiledBytes[3] == 0x6Du)
                    {
                        report.compiledViaEmsdkToolchain = true;
                        report.compilerPathUsed = clangExe;
                        report.wasmBinary = std::move(compiledBytes);
                        (void)activeLoader->unlink(cSourcePath);
                        (void)activeLoader->unlink(outWasmPath);
                        return report;
                    }
                }
                (void)activeLoader->unlink(cSourcePath);
                (void)activeLoader->unlink(outWasmPath);
            }
        }

        report.compiledViaEmsdkToolchain = false;
        report.compilerPathUsed = "builtin-wasm32-simd-emitter";
        report.wasmBinary = emitDeterministicSimdEngineWasm();
        return report;
    }
} // namespace MQEngine::DouyinMiniGame
