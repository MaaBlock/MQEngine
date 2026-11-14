/**
 * @file DouyinPackageExporter.cpp
 * @brief Step 5 MainPackage + RemoteCDN 导出、首屏极速 Splash game.js 生成及 10MB/4MB 配额门禁实现
 */
#include "DouyinPackageExporter.h"
#include <MQEngine/resource/core/DiskResourceLoader.h>

#include <cstdio>

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        std::string toLowerCopy(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return out;
        }

        std::string escapeJsonString(std::string_view s)
        {
            std::string out;
            out.reserve(s.size() + 8);
            for (char c : s)
            {
                switch (c)
                {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:   out.push_back(c); break;
                }
            }
            return out;
        }
    } // namespace

    std::string DouyinPackageExporter::generateGameJson(const DouyinPackagerConfig& config)
    {
        const std::string orient = (config.orientation == "portrait") ? "portrait" : "landscape";
        std::string json = "{\n";
        json += "  \"deviceOrientation\": \"" + orient + "\",\n";
        json += "  \"showStatusBar\": false,\n";
        json += "  \"networkTimeout\": {\n";
        json += "    \"request\": 10000,\n";
        json += "    \"connectSocket\": 10000,\n";
        json += "    \"uploadFile\": 30000,\n";
        json += "    \"downloadFile\": 30000\n";
        json += "  },\n";
        json += "  \"subpackages\": []\n";
        json += "}\n";
        return json;
    }

    std::string DouyinPackageExporter::generateProjectConfigJson(const DouyinPackagerConfig& config)
    {
        std::string json = "{\n";
        json += "  \"appid\": \"" + escapeJsonString(config.appId) + "\",\n";
        json += "  \"projectname\": \"" + escapeJsonString(config.projectName) + "\",\n";
        json += "  \"compileType\": \"game\",\n";
        json += "  \"miniprogramRoot\": \"./\",\n";
        json += "  \"setting\": {\n";
        json += "    \"urlCheck\": false,\n";
        json += "    \"es6\": false,\n";
        json += "    \"minified\": true,\n";
        json += "    \"enhance\": true\n";
        json += "  }\n";
        json += "}\n";
        return json;
    }

    std::string DouyinPackageExporter::generateTtAdapterJs(const DouyinPackagerConfig& config)
    {
        char dprBuf[32];
        std::snprintf(dprBuf, sizeof(dprBuf), "%.2f", static_cast<double>(config.devicePixelRatioCap));

        std::string js;
        js += "(function(g){\n";
        js += "  \"use strict\";\n";
        js += "  if (typeof tt === \"undefined\" || !tt) { return; }\n";
        js += "  var sys = tt.getSystemInfoSync ? tt.getSystemInfoSync() : {};\n";
        js += "  var rawDpr = (sys && (sys.pixelRatio || sys.devicePixelRatio)) ? (sys.pixelRatio || sys.devicePixelRatio) : 2.0;\n";
        js += "  var cappedDpr = Math.min(Math.max(rawDpr, 1.0), " + std::string(dprBuf) + ");\n";
        js += "  var capsule = tt.getMenuButtonBoundingClientRect ? tt.getMenuButtonBoundingClientRect() : { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 };\n";
        js += "  g.__MQ_TT_ADAPTER__ = {\n";
        js += "    dpr: cappedDpr,\n";
        js += "    windowWidth: (sys && sys.windowWidth) ? sys.windowWidth : 390,\n";
        js += "    windowHeight: (sys && sys.windowHeight) ? sys.windowHeight : 844,\n";
        js += "    safeArea: (sys && sys.safeArea) ? sys.safeArea : { left: 0, top: 0, right: 390, bottom: 844 },\n";
        js += "    capsuleRect: capsule,\n";
        js += "    cdnBaseUrl: \"" + escapeJsonString(config.cdnBaseUrl) + "\"\n";
        js += "  };\n";
        js += "})(typeof globalThis !== \"undefined\" ? globalThis : this);\n";
        return js;
    }

    std::string DouyinPackageExporter::generateBootSplashGameJs(const DouyinPackagerConfig& config)
    {
        char dprBuf[32];
        std::snprintf(dprBuf, sizeof(dprBuf), "%.2f", static_cast<double>(config.devicePixelRatioCap));

        // Note: FEAT-54 极速首屏接力脚本：同步阶段 (<50ms) 立即创建主屏 Canvas + WebGL2 上下文并绘制首帧 Splash 进度条，
        //       随后非阻塞读取并编译 engine.wasm + shaders_wgl2.bin + asset_manifest.bin，在引擎首帧就绪后平滑卸载 Splash。
        std::string js;
        js += "(function(){\n";
        js += "  \"use strict\";\n";
        js += "  var bootStartMs = Date.now();\n";
        js += "  var rootScope = (typeof globalThis !== \"undefined\") ? globalThis : this;\n";
        js += "  if (typeof require === \"function\") {\n";
        js += "    require(\"./tt-adapter.js\");\n";
        js += "  }\n";
        js += "\n";
        js += "  var sys = (typeof tt !== \"undefined\" && tt.getSystemInfoSync) ? tt.getSystemInfoSync() : {};\n";
        js += "  var rawDpr = (sys && (sys.pixelRatio || sys.devicePixelRatio)) ? (sys.pixelRatio || sys.devicePixelRatio) : 2.0;\n";
        js += "  var dpr = Math.min(Math.max(rawDpr, 1.0), " + std::string(dprBuf) + ");\n";
        js += "  var winW = (sys && sys.windowWidth) ? sys.windowWidth : 390;\n";
        js += "  var winH = (sys && sys.windowHeight) ? sys.windowHeight : 844;\n";
        js += "\n";
        js += "  var mainCanvas = tt.createCanvas();\n";
        js += "  mainCanvas.width = Math.max(1, Math.floor(winW * dpr));\n";
        js += "  mainCanvas.height = Math.max(1, Math.floor(winH * dpr));\n";
        js += "  var gl = mainCanvas.getContext(\"webgl2\", {\n";
        js += "    alpha: false,\n";
        js += "    antialias: false,\n";
        js += "    depth: true,\n";
        js += "    stencil: true,\n";
        js += "    powerPreference: \"high-performance\"\n";
        js += "  });\n";
        js += "\n";
        js += "  var splashState = {\n";
        js += "    active: true,\n";
        js += "    progress: 0.15,\n";
        js += "    firstPaintMs: 0,\n";
        js += "    wasmReady: false,\n";
        js += "    manifestReady: false,\n";
        js += "    shadersReady: false,\n";
        js += "    program: null,\n";
        js += "    vbo: null\n";
        js += "  };\n";
        js += "\n";
        js += "  function initSplashPipeline(ctx) {\n";
        js += "    if (!ctx) { return; }\n";
        js += "    var vsSrc = \"#version 300 es\\nlayout(location=0) in vec2 aPos;\\nvoid main(){ gl_Position = vec4(aPos, 0.0, 1.0); }\";\n";
        js += "    var fsSrc = \"#version 300 es\\nprecision mediump float;\\nout vec4 oColor;\\nvoid main(){ oColor = vec4(0.12, 0.78, 0.96, 1.0); }\";\n";
        js += "    var vs = ctx.createShader(0x8B31);\n";
        js += "    ctx.shaderSource(vs, vsSrc);\n";
        js += "    ctx.compileShader(vs);\n";
        js += "    var fs = ctx.createShader(0x8B30);\n";
        js += "    ctx.shaderSource(fs, fsSrc);\n";
        js += "    ctx.compileShader(fs);\n";
        js += "    var prog = ctx.createProgram();\n";
        js += "    ctx.attachShader(prog, vs);\n";
        js += "    ctx.attachShader(prog, fs);\n";
        js += "    ctx.linkProgram(prog);\n";
        js += "    splashState.program = prog;\n";
        js += "    splashState.vbo = ctx.createBuffer();\n";
        js += "  }\n";
        js += "\n";
        js += "  function renderSplashFrame(ctx, ratio) {\n";
        js += "    if (!ctx || !splashState.active) { return; }\n";
        js += "    var clamped = Math.max(0.05, Math.min(1.0, ratio));\n";
        js += "    ctx.viewport(0, 0, mainCanvas.width, mainCanvas.height);\n";
        js += "    ctx.clearColor(0.05, 0.07, 0.12, 1.0);\n";
        js += "    ctx.clear(0x00004000 | 0x00000100);\n";
        js += "    if (splashState.program && splashState.vbo) {\n";
        js += "      var left = -0.7;\n";
        js += "      var right = left + 1.4 * clamped;\n";
        js += "      var quad = new Float32Array([left, -0.68, right, -0.68, left, -0.62, right, -0.62]);\n";
        js += "      ctx.useProgram(splashState.program);\n";
        js += "      ctx.bindBuffer(0x8892, splashState.vbo);\n";
        js += "      ctx.bufferData(0x8892, quad, 0x88E8);\n";
        js += "      ctx.drawArrays(0x0005, 0, 4);\n";
        js += "    }\n";
        js += "  }\n";
        js += "\n";
        js += "  initSplashPipeline(gl);\n";
        js += "  renderSplashFrame(gl, splashState.progress);\n";
        js += "  splashState.firstPaintMs = Date.now() - bootStartMs;\n";
        js += "\n";
        js += "  function dismissSplashAndHandOff(wasmInstance) {\n";
        js += "    if (!splashState.active) { return; }\n";
        js += "    renderSplashFrame(gl, 1.0);\n";
        js += "    splashState.active = false;\n";
        js += "    splashState.program = null;\n";
        js += "    splashState.vbo = null;\n";
        js += "    if (typeof require === \"function\") {\n";
        js += "      require(\"./game_logic.js\");\n";
        js += "    }\n";
        js += "    if (wasmInstance && wasmInstance.exports) {\n";
        js += "      if (typeof wasmInstance.exports.mq_engine_init === \"function\") {\n";
        js += "        wasmInstance.exports.mq_engine_init(mainCanvas.width, mainCanvas.height);\n";
        js += "      }\n";
        js += "      if (typeof wasmInstance.exports.mq_engine_tick === \"function\") {\n";
        js += "        wasmInstance.exports.mq_engine_tick(0.016667);\n";
        js += "      }\n";
        js += "    }\n";
        js += "  }\n";
        js += "\n";
        js += "  rootScope.__MQ_SPLASH_RELAY__ = {\n";
        js += "    getFirstPaintMs: function() { return splashState.firstPaintMs; },\n";
        js += "    isSplashActive: function() { return splashState.active; },\n";
        js += "    dismiss: function() { dismissSplashAndHandOff(null); }\n";
        js += "  };\n";
        js += "\n";
        js += "  var fsm = (typeof tt !== \"undefined\" && tt.getFileSystemManager) ? tt.getFileSystemManager() : null;\n";
        js += "  if (fsm && typeof fsm.readFile === \"function\") {\n";
        js += "    fsm.readFile({\n";
        js += "      filePath: \"/pkg/asset_manifest.bin\",\n";
        js += "      success: function() {\n";
        js += "        splashState.manifestReady = true;\n";
        js += "        splashState.progress = Math.max(splashState.progress, 0.35);\n";
        js += "        renderSplashFrame(gl, splashState.progress);\n";
        js += "      }\n";
        js += "    });\n";
        js += "    fsm.readFile({\n";
        js += "      filePath: \"/pkg/shaders_wgl2.bin\",\n";
        js += "      success: function() {\n";
        js += "        splashState.shadersReady = true;\n";
        js += "        splashState.progress = Math.max(splashState.progress, 0.55);\n";
        js += "        renderSplashFrame(gl, splashState.progress);\n";
        js += "      }\n";
        js += "    });\n";
        js += "    fsm.readFile({\n";
        js += "      filePath: \"/pkg/engine.wasm\",\n";
        js += "      success: function(res) {\n";
        js += "        var raw = res && res.data ? res.data : res;\n";
        js += "        var bytes = (raw instanceof Uint8Array) ? raw : new Uint8Array(raw);\n";
        js += "        splashState.progress = 0.80;\n";
        js += "        renderSplashFrame(gl, splashState.progress);\n";
        js += "        WebAssembly.instantiate(bytes, {}).then(function(instantiated) {\n";
        js += "          splashState.wasmReady = true;\n";
        js += "          var inst = instantiated.instance ? instantiated.instance : instantiated;\n";
        js += "          dismissSplashAndHandOff(inst);\n";
        js += "        }, function(err) {\n";
        js += "          console.error(\"[MQEngine] engine.wasm instantiate failed:\", err);\n";
        js += "        });\n";
        js += "      },\n";
        js += "      fail: function(err) {\n";
        js += "        console.error(\"[MQEngine] read /pkg/engine.wasm failed:\", err);\n";
        js += "      }\n";
        js += "    });\n";
        js += "  }\n";
        js += "})();\n";
        return js;
    }

    bool DouyinPackageExporter::isProhibitedMainPackageFilename(std::string_view fileName) noexcept
    {
        const std::string lower = toLowerCopy(fileName);

        static constexpr const char* kBannedExts[] = {
            ".ttf", ".otf", ".ttc", ".woff", ".woff2", ".eot",
            ".fbx", ".gltf", ".glb", ".obj",
            ".hlsl", ".glsl", ".vert", ".frag", ".spv",
            ".hdr", ".exr", ".tga", ".psd",
            ".pdb", ".map", ".d.ts", ".dll", ".exe", ".lib"
        };
        for (const char* ext : kBannedExts)
        {
            if (lower.ends_with(ext))
            {
                return true;
            }
        }

        if (!lower.ends_with(".png") && !lower.ends_with(".jpg"))
        {
            static constexpr const char* kBannedSubstrings[] = {
                "imgui", "editor", "unittest", "test"
            };
            for (const char* sub : kBannedSubstrings)
            {
                if (lower.find(sub) != std::string::npos)
                {
                    return true;
                }
            }
        }
        return false;
    }

    Status DouyinPackageExporter::validateStepPreconditions(const DouyinPipelineStepState& stepState)
    {
        if (!stepState.step1ShadersDone)
        {
            return FailedPreconditionError("Step 5 导出被拦截：尚未完成 Step 1 着色器变体裁剪与转译");
        }
        if (!stepState.step2AssetsDone)
        {
            return FailedPreconditionError("Step 5 导出被拦截：尚未完成 Step 2 资产烘焙与寻址清单生成");
        }
        if (!stepState.step3ScriptsDone)
        {
            return FailedPreconditionError("Step 5 导出被拦截：尚未完成 Step 3 脚本汇聚打包");
        }
        if (!stepState.step4WasmDone)
        {
            return FailedPreconditionError("Step 5 导出被拦截：尚未完成 Step 4 Wasm 引擎构建");
        }
        return OkStatus();
    }

    StatusOr<PackageExportReport> DouyinPackageExporter::exportPackages(
        ResourceLoader* loader,
        const std::string& outDir,
        const DouyinPackagerConfig& config,
        const DouyinPipelineStepState& stepState,
        const ShaderCookReport& shaderReport,
        const AssetCookReport& assetReport,
        const ScriptBundleReport& scriptReport,
        const WasmBuildReport& wasmReport)
    {
        CHECK_STATUS(DouyinConfigManager::validate(config));
        CHECK_STATUS(validateStepPreconditions(stepState));

        if (outDir.empty())
        {
            return InvalidArgumentError("输出目录 outDir 不能为空");
        }
        if (wasmReport.wasmBinary.size() < 8)
        {
            return InvalidArgumentError("engine.wasm 二进制长度不合法");
        }
        if (shaderReport.shaderPackBinary.empty())
        {
            return InvalidArgumentError("shaders_wgl2.bin 不能为空");
        }
        if (assetReport.manifestBinary.empty())
        {
            return InvalidArgumentError("asset_manifest.bin 不能为空");
        }

        const std::string gameJson = generateGameJson(config);
        const std::string projectConfigJson = generateProjectConfigJson(config);
        const std::string adapterJs = generateTtAdapterJs(config);
        const std::string gameJs = generateBootSplashGameJs(config);

        const uint64_t codeBytes =
            wasmReport.wasmBinary.size() +
            gameJs.size() +
            adapterJs.size() +
            scriptReport.bundledJsCode.size();

        uint64_t totalMainBytes =
            codeBytes +
            gameJson.size() +
            projectConfigJson.size() +
            shaderReport.shaderPackBinary.size() +
            assetReport.manifestBinary.size();

        for (const auto& item : assetReport.items)
        {
            if (item.bundleType == Asset::BundleType::FirstPackage)
            {
                if (isProhibitedMainPackageFilename(item.packageRelPath))
                {
                    return PermissionDeniedError("首包包含违规扩展名或桌面残留关键字文件: " + item.packageRelPath);
                }
                totalMainBytes += item.payload.size();
            }
        }

        const uint64_t maxCodeBytes = static_cast<uint64_t>(config.codePackageMaxMB * 1024.0f * 1024.0f);
        const uint64_t maxMainBytes = static_cast<uint64_t>(config.mainPackageMaxMB * 1024.0f * 1024.0f);
        if (codeBytes > maxCodeBytes)
        {
            return ResourceExhaustedError("代码首包 (.wasm + .js) 超出配额红线 (" +
                                          std::to_string(codeBytes) + " > " + std::to_string(maxCodeBytes) + " bytes)");
        }
        if (totalMainBytes > maxMainBytes)
        {
            return ResourceExhaustedError("MainPackage 首包总大小超出配额红线 (" +
                                          std::to_string(totalMainBytes) + " > " + std::to_string(maxMainBytes) + " bytes)");
        }

        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        const std::string mainPkgDir = SAI::Path::join(outDir, "MainPackage");
        const std::string remoteCdnDir = SAI::Path::join(outDir, "RemoteCDN");

        // 清理旧目录残留文件，确保重复构建无脏文件污染
        if (SAI::FileSystem::exists(mainPkgDir))
        {
            (void)SAI::FileSystem::removeAll(mainPkgDir);
        }
        if (SAI::FileSystem::exists(remoteCdnDir))
        {
            (void)SAI::FileSystem::removeAll(remoteCdnDir);
        }

        CHECK_STATUS(activeLoader->ensureDir(mainPkgDir));
        CHECK_STATUS(activeLoader->ensureDir(remoteCdnDir));

        CHECK_STATUS(activeLoader->saveFile(SAI::Path::join(mainPkgDir, "game.json"), gameJson));
        CHECK_STATUS(activeLoader->saveFile(SAI::Path::join(mainPkgDir, "project.config.json"), projectConfigJson));
        CHECK_STATUS(activeLoader->saveFile(SAI::Path::join(mainPkgDir, "tt-adapter.js"), adapterJs));
        CHECK_STATUS(activeLoader->saveFile(SAI::Path::join(mainPkgDir, "game.js"), gameJs));
        CHECK_STATUS(activeLoader->saveFile(SAI::Path::join(mainPkgDir, "game_logic.js"), scriptReport.bundledJsCode));

        CHECK_STATUS(SAI::FileSystem::writeBinaryFile(
            SAI::Path::join(mainPkgDir, "engine.wasm"),
            wasmReport.wasmBinary.data(),
            wasmReport.wasmBinary.size(),
            true));
        CHECK_STATUS(SAI::FileSystem::writeBinaryFile(
            SAI::Path::join(mainPkgDir, "shaders_wgl2.bin"),
            shaderReport.shaderPackBinary.data(),
            shaderReport.shaderPackBinary.size(),
            true));
        CHECK_STATUS(SAI::FileSystem::writeBinaryFile(
            SAI::Path::join(mainPkgDir, "asset_manifest.bin"),
            assetReport.manifestBinary.data(),
            assetReport.manifestBinary.size(),
            true));

        PackageExportReport report;
        report.mainPackageDir = mainPkgDir;
        report.remoteCdnDir = remoteCdnDir;
        report.mainPackageRawBytes = totalMainBytes;
        report.codeRawBytes = codeBytes;
        report.mainPackageFileCount = 8u;

        for (const auto& item : assetReport.items)
        {
            if (item.bundleType == Asset::BundleType::FirstPackage)
            {
                const std::string dstPath = SAI::Path::join(mainPkgDir, item.packageRelPath);
                CHECK_STATUS(SAI::FileSystem::writeBinaryFile(
                    dstPath,
                    item.payload.data(),
                    item.payload.size(),
                    true));
                ++report.mainPackageFileCount;
            }
            else
            {
                const std::string cdnFilePath = SAI::Path::join(remoteCdnDir, item.sha256Hex + ".bin");
                CHECK_STATUS(SAI::FileSystem::writeBinaryFile(
                    cdnFilePath,
                    item.payload.data(),
                    item.payload.size(),
                    true));
                report.remoteCdnRawBytes += item.payload.size();
                ++report.remoteCdnFileCount;
            }
        }

        return report;
    }
} // namespace MQEngine::DouyinMiniGame
