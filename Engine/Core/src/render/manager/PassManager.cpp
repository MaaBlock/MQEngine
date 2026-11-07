#include <boost/json/src.hpp>
#include "PassManager.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../resource/core/ResourceLoader.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/Runtime.h"
#include "../core/VertexLayouts.h"
#include "../../runtime/manager/SystemManager.h"
#include <spdlog/spdlog.h>

using namespace FCT;
namespace json = boost::json;

namespace MQEngine {

namespace {

std::string jsonGetString(const json::object& obj, boost::core::string_view key, const std::string& def = "") {
    auto it = obj.find(key);
    if (it != obj.end() && it->value().is_string()) {
        return std::string(it->value().as_string());
    }
    return def;
}

uint32_t jsonGetUint(const json::object& obj, boost::core::string_view key, uint32_t def = 0) {
    auto it = obj.find(key);
    if (it != obj.end() && it->value().is_number()) {
        return static_cast<uint32_t>(it->value().to_number<int64_t>());
    }
    return def;
}

float jsonGetFloat(const json::object& obj, boost::core::string_view key, float def = 0.0f) {
    auto it = obj.find(key);
    if (it != obj.end() && it->value().is_number()) {
        return static_cast<float>(it->value().to_number<double>());
    }
    return def;
}

bool jsonGetBool(const json::object& obj, boost::core::string_view key, bool def = false) {
    auto it = obj.find(key);
    if (it != obj.end() && it->value().is_bool()) {
        return it->value().as_bool();
    }
    return def;
}

std::vector<std::string> jsonGetStringArray(const json::object& obj, boost::core::string_view key) {
    std::vector<std::string> res;
    auto it = obj.find(key);
    if (it != obj.end() && it->value().is_array()) {
        for (const auto& item : it->value().as_array()) {
            if (item.is_string()) {
                res.push_back(std::string(item.as_string()));
            }
        }
    }
    return res;
}

PipelineClearDesc parseClearDesc(const json::object& obj) {
    PipelineClearDesc clear;
    clear.types = jsonGetStringArray(obj, "types");
    clear.depth = jsonGetFloat(obj, "depth", 1.0f);
    clear.stencil = static_cast<uint8_t>(jsonGetUint(obj, "stencil", 0));
    auto itColor = obj.find("color");
    if (itColor != obj.end() && itColor->value().is_array()) {
        const auto& arr = itColor->value().as_array();
        float r = arr.size() > 0 && arr[0].is_number() ? static_cast<float>(arr[0].to_number<double>()) : 0.0f;
        float g = arr.size() > 1 && arr[1].is_number() ? static_cast<float>(arr[1].to_number<double>()) : 0.0f;
        float b = arr.size() > 2 && arr[2].is_number() ? static_cast<float>(arr[2].to_number<double>()) : 0.0f;
        float a = arr.size() > 3 && arr[3].is_number() ? static_cast<float>(arr[3].to_number<double>()) : 1.0f;
        clear.color = Vec4(r, g, b, a);
    }
    return clear;
}

PipelineTargetDesc parseTargetDesc(const json::object& obj) {
    PipelineTargetDesc target;
    target.name = jsonGetString(obj, "name");
    target.format = jsonGetString(obj, "format");
    target.width = jsonGetUint(obj, "width", 0);
    target.height = jsonGetUint(obj, "height", 0);
    target.inheritWindow = jsonGetBool(obj, "inheritWindow", false);
    return target;
}

PipelineDepthStencilDesc parseDepthStencilDesc(const json::object& obj) {
    PipelineDepthStencilDesc ds;
    ds.name = jsonGetString(obj, "name");
    ds.format = jsonGetString(obj, "format", "D32_SFLOAT");
    ds.width = jsonGetUint(obj, "width", 0);
    ds.height = jsonGetUint(obj, "height", 0);
    ds.inheritWindow = jsonGetBool(obj, "inheritWindow", false);
    return ds;
}

PipelineTextureDesc parseTextureDesc(const json::value& val) {
    PipelineTextureDesc tex;
    if (val.is_string()) {
        tex.name = std::string(val.as_string());
    } else if (val.is_object()) {
        const auto& obj = val.as_object();
        tex.name = jsonGetString(obj, "name");
        tex.stages = jsonGetStringArray(obj, "stages");
        tex.format = jsonGetString(obj, "format");
        tex.width = jsonGetUint(obj, "width", 0);
        tex.height = jsonGetUint(obj, "height", 0);
    }
    return tex;
}

PipelinePassDesc parsePassDesc(const json::object& obj) {
    PipelinePassDesc pass;
    pass.name = jsonGetString(obj, "name");
    pass.type = jsonGetString(obj, "type", "raster");

    auto itClear = obj.find("clear");
    if (itClear != obj.end() && itClear->value().is_object()) {
        pass.clear = parseClearDesc(itClear->value().as_object());
    }

    auto itInputs = obj.find("inputTextures");
    if (itInputs == obj.end()) itInputs = obj.find("textures");
    if (itInputs != obj.end() && itInputs->value().is_array()) {
        for (const auto& item : itInputs->value().as_array()) {
            pass.inputTextures.push_back(parseTextureDesc(item));
        }
    }

    auto itTargets = obj.find("targets");
    if (itTargets != obj.end() && itTargets->value().is_array()) {
        for (const auto& item : itTargets->value().as_array()) {
            if (item.is_object()) {
                pass.targets.push_back(parseTargetDesc(item.as_object()));
            }
        }
    } else {
        auto itTarget = obj.find("target");
        if (itTarget != obj.end() && itTarget->value().is_object()) {
            pass.targets.push_back(parseTargetDesc(itTarget->value().as_object()));
        }
    }

    auto itDSList = obj.find("depthStencils");
    if (itDSList != obj.end() && itDSList->value().is_array()) {
        for (const auto& item : itDSList->value().as_array()) {
            if (item.is_object()) {
                pass.depthStencils.push_back(parseDepthStencilDesc(item.as_object()));
            }
        }
    } else {
        auto itDS = obj.find("depthStencil");
        if (itDS != obj.end() && itDS->value().is_object()) {
            pass.depthStencils.push_back(parseDepthStencilDesc(itDS->value().as_object()));
        }
    }

    return pass;
}

PipelineTechDesc parseTechDesc(const json::object& obj) {
    PipelineTechDesc tech;
    tech.name = jsonGetString(obj, "name");
    tech.pass = jsonGetString(obj, "pass");
    tech.vertexShader = jsonGetString(obj, "vertexShader");
    tech.pixelShader = jsonGetString(obj, "pixelShader");
    tech.vertexLayout = jsonGetString(obj, "vertexLayout", "StandardMeshVertexLayout");
    tech.pixelLayout = jsonGetString(obj, "pixelLayout", "StandardPixelLayout");
    tech.uniformSlots = jsonGetStringArray(obj, "uniformSlots");
    tech.samplerSlots = jsonGetStringArray(obj, "samplerSlots");
    tech.textureSlots = jsonGetStringArray(obj, "textureSlots");
    tech.bindedSystems = jsonGetStringArray(obj, "bindedSystems");
    tech.entityCallback = jsonGetString(obj, "entityCallback", "UniversalMeshDrawCallback");

    auto itFilter = obj.find("componentFilter");
    if (itFilter != obj.end() && itFilter->value().is_object()) {
        const auto& filterObj = itFilter->value().as_object();
        tech.componentFilter.include = jsonGetStringArray(filterObj, "include");
        tech.componentFilter.exclude = jsonGetStringArray(filterObj, "exclude");
    }

    return tech;
}

StatusOr<PipelineConfig> parsePipelineConfig(const json::value& jv) {
    if (!jv.is_object()) {
        return InvalidArgumentError("Pipeline 配置根节点必须为 JSON 对象");
    }
    const auto& obj = jv.as_object();
    PipelineConfig config;
    config.name = jsonGetString(obj, "name");
    config.version = jsonGetString(obj, "version", "1.0");
    config.defaultTarget = jsonGetString(obj, "defaultTarget", "window");

    auto itPasses = obj.find("passes");
    if (itPasses != obj.end() && itPasses->value().is_array()) {
        for (const auto& item : itPasses->value().as_array()) {
            if (item.is_object()) {
                config.passes.push_back(parsePassDesc(item.as_object()));
            }
        }
    }

    auto itTechs = obj.find("techniques");
    if (itTechs != obj.end() && itTechs->value().is_array()) {
        for (const auto& item : itTechs->value().as_array()) {
            if (item.is_object()) {
                config.techniques.push_back(parseTechDesc(item.as_object()));
            }
        }
    }

    return config;
}

} // namespace

PassManager::PassManager() {
    m_requestQueue.subscribe<Request::LoadPipeline>([this](Request::LoadPipeline& req) {
        auto res = loadPipeline(req.path);
        CHECK_STATUS(res);
        m_graphDirty = true;
        m_techsDirty = true;
        return OkStatus();
    });

    m_requestQueue.subscribe<Request::SetConfig>([this](Request::SetConfig& req) {
        m_currentConfig = req.config;
        m_graphDirty = true;
        m_techsDirty = true;
        return OkStatus();
    });

    m_requestQueue.subscribe<Request::AddPassDesc>([this](Request::AddPassDesc& req) {
        m_currentConfig.passes.push_back(req.pass);
        m_graphDirty = true;
        return OkStatus();
    });

    m_requestQueue.subscribe<Request::AddCustomPass>([this](Request::AddCustomPass& req) {
        if (!req.callback) {
            return OkStatus();
        }
        for (auto& item : m_customPassCallbacks) {
            if (item.name == req.name) {
                item.callback = std::move(req.callback);
                m_graphDirty = true;
                return OkStatus();
            }
        }
        m_customPassCallbacks.push_back(NamedCustomPass{req.name, std::move(req.callback)});
        m_graphDirty = true;
        return OkStatus();
    });

    m_requestQueue.subscribe<Request::KeepImage>([this](Request::KeepImage& req) {
        if (req.callback) {
            m_keepImageCallbacks.push_back(std::move(req.callback));
            m_graphDirty = true;
        }
        return OkStatus();
    });

    m_requestQueue.subscribe<Request::SubscribePass>([this](Request::SubscribePass& req) {
        if (req.callback) {
            m_subscribePassCallbacks.push_back(NamedPassSubscription{req.passName, std::move(req.callback)});
            m_graphDirty = true;
        }
        return OkStatus();
    });
}

void PassManager::requestLoadPipeline(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.enqueue(Request::LoadPipeline{path});
}

void PassManager::requestSetConfig(const PipelineConfig& config) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.enqueue(Request::SetConfig{config});
}

void PassManager::requestAddPassDesc(const PipelinePassDesc& pass) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.enqueue(Request::AddPassDesc{pass});
}

void PassManager::requestAddCustomPass(const std::string& name, CustomPassCallback callback) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.enqueue(Request::AddCustomPass{name, std::move(callback)});
}

void PassManager::requestKeepImage(KeepImageCallback callback) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.enqueue(Request::KeepImage{std::move(callback)});
}

void PassManager::requestSubscribePass(const std::string& passName, PassSubmitCallback callback) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.enqueue(Request::SubscribePass{passName, std::move(callback)});
}

void PassManager::syncTicker() {
    ZoneScoped();
    bool needCompile = false;
    bool needBuildTechs = false;
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        while (!m_requestQueue.empty()) {
            auto st = m_requestQueue.processNext();
            if (!st.ok()) {
                spdlog::error("PassManager::syncTicker 处理请求失败: {}", st.message());
            }
        }
        if (m_graphDirty || !m_isGraphCompiled) {
            needCompile = true;
            m_graphDirty = false;
        }
        if (m_techsDirty) {
            needBuildTechs = true;
            m_techsDirty = false;
        }
    }

    if (needCompile) {
        compileAndBindGraph();
    }

    if (needBuildTechs) {
        auto* engine = Runtime::Engine::getInstance();
        TechManager* techMgr = (engine && engine->effect) ? engine->effect->getTechManager() : g_engineGlobal.techManager;
        if (techMgr) {
            techMgr->requestBuildFromPipeline(m_currentConfig);
        }
    }
}

void PassManager::compileAndBindGraph() {
    auto* engine = Runtime::Engine::getInstance();
    if (!engine || !engine->platform) {
        return;
    }
    FCT::Context* ctx = engine->platform->getContext();
    if (!ctx) {
        return;
    }
    FCT::Window* wnd = engine->platform->getWindow();
    auto* graph = ctx->getModule<FCT::RenderGraph>();
    if (!graph) {
        return;
    }
    auto* application = engine->application;
    RenderTarget targetType = application
        ? application->applicationRegisterInfo.platformRequire.target
        : RenderTarget::Window;

    if (!m_isGraphBuilt && !m_currentConfig.passes.empty()) {
        auto buildRes = buildRenderGraph(m_currentConfig, graph, wnd, targetType);
        if (!buildRes.ok()) {
            spdlog::error("PassManager: 构建 RenderGraph 失败: {}", buildRes.message());
        }
    }

    for (const auto& item : m_customPassCallbacks) {
        if (item.callback) {
            item.callback(graph);
        }
    }

    if (application) {
        RenderCallBack::SettingUpPass callback;
        callback.graph = graph;
        application->renderCallBackDispatcher.trigger(callback);
    }

    graph->compile();
    m_isGraphCompiled = true;

    for (const auto& cb : m_keepImageCallbacks) {
        if (cb) {
            cb(graph);
        }
    }
    if (application) {
        RenderCallBack::KeepImage callback;
        callback.graph = graph;
        application->renderCallBackDispatcher.trigger(callback);
    }

    for (const auto& sub : m_subscribePassCallbacks) {
        if (sub.callback) {
            graph->subscribe(sub.passName, sub.callback);
        }
    }
    if (application) {
        RenderCallBack::SubscribePass callback;
        callback.graph = graph;
        application->renderCallBackDispatcher.trigger(callback);
    }
}

StatusOr<PipelineConfig> PassManager::loadPipeline(const std::string& path) {
    ZoneScoped();
    if (!g_engineGlobal.resourceLoader) {
        return InternalError("ResourceLoader 实例未初始化");
    }

    std::vector<std::string> candidatePaths = {
        path,
        "./" + path,
        "MQEngine/" + path,
        "../" + path,
        "../../" + path,
        "../../../" + path,
        "../../../../" + path,
        "Assets/Pipelines/default_pipeline.json",
        "MQEngine/Assets/Pipelines/default_pipeline.json",
        "../Assets/Pipelines/default_pipeline.json",
        "../../Assets/Pipelines/default_pipeline.json",
        "../../../Assets/Pipelines/default_pipeline.json",
        "../../../../Assets/Pipelines/default_pipeline.json"
    };

    std::string content;
    bool loaded = false;
    for (const auto& candidate : candidatePaths) {
        auto loadRes = g_engineGlobal.resourceLoader->loadFile(candidate);
        if (loadRes.ok()) {
            content = std::move(loadRes.value());
            loaded = true;
            break;
        }
    }

    if (!loaded) {
        spdlog::warn("PassManager: 未找到外部管线配置文件 [{}]，自动回退至内置 C++ 默认延迟渲染管线", path);
        m_currentConfig = createDefaultDeferredPipelineConfig();
        return m_currentConfig;
    }

    boost::system::error_code ec;
    json::value jv = json::parse(content, ec);
    if (ec) {
        return InvalidArgumentError(StrCat("JSON 解析失败: ", ec.message()));
    }
    auto configRes = parsePipelineConfig(jv);
    CHECK_STATUS(configRes);
    m_currentConfig = configRes.value();
    return m_currentConfig;
}

FCT::PassDesc PassManager::buildPassDesc(const PipelinePassDesc& pass,
                                         FCT::Window* wnd,
                                         RenderTarget targetType) const {
    PassDesc passDesc(pass.name);

    if (pass.type == "compute") {
        passDesc.type = PassType::Compute;
    } else if (pass.type == "external" || pass.type == "external_interop") {
        passDesc.type = PassType::ExternalInterop;
    } else {
        passDesc.type = PassType::Rasterization;
    }

    ClearTypes clearTypes;
    for (const auto& t : pass.clear.types) {
        if (t == "color") clearTypes |= ClearType::color;
        else if (t == "depth") clearTypes |= ClearType::depth;
        else if (t == "stencil") clearTypes |= ClearType::stencil;
        else if (t == "depthStencil" || t == "depth_stencil") clearTypes |= ClearType::depthStencil;
    }
    EnablePassClear clearDesc;
    clearDesc.types = clearTypes;
    clearDesc.color = pass.clear.color;
    clearDesc.depth = pass.clear.depth;
    clearDesc.stencil = pass.clear.stencil;
    passDesc.clear = clearDesc;

    for (const auto& tex : pass.inputTextures) {
        Texture fctTex(tex.name);
        ShaderStages stages;
        for (const auto& s : tex.stages) {
            if (s == "fragment") stages |= ShaderStage::Fragment;
            else if (s == "vertex") stages |= ShaderStage::Vertex;
            else if (s == "compute") stages |= ShaderStage::Compute;
            else if (s == "storage" || s == "uav") {
                stages |= ShaderStage::Compute;
                fctTex.isStorage = true;
            }
        }
        if (stages) {
            fctTex.stages = stages;
        }
        if (!tex.format.empty()) {
            fctTex.format = StringToFormat(tex.format.c_str());
        }
        if (tex.width > 0 && tex.height > 0) {
            fctTex.size = TextureSize::Absolute(tex.width, tex.height);
        }
        passDesc.textures.push_back(fctTex);
    }

    for (const auto& tgt : pass.targets) {
        Target fctTarget;
        fctTarget.name = tgt.name;
        if (tgt.inheritWindow) {
            if (targetType == RenderTarget::Window) {
                fctTarget.wnd = wnd;
                fctTarget.isWindow = true;
            } else if (targetType == RenderTarget::Texture) {
                if (m_externalTarget) {
                    fctTarget = Target(tgt.name, m_externalTarget);
                } else {
                    fctTarget.format = tgt.format.empty() ? Format::R8G8B8A8_SRGB : StringToFormat(tgt.format.c_str());
                    fctTarget.width = tgt.width > 0 ? tgt.width : 1024;
                    fctTarget.height = tgt.height > 0 ? tgt.height : 768;
                    fctTarget.hasFixedSize = true;
                }
            }
        } else {
            if (!tgt.format.empty()) {
                fctTarget.format = StringToFormat(tgt.format.c_str());
            }
            if (tgt.width > 0 && tgt.height > 0) {
                fctTarget.width = tgt.width;
                fctTarget.height = tgt.height;
                fctTarget.hasFixedSize = true;
            }
        }
        passDesc.targets.push_back(fctTarget);
    }

    for (const auto& ds : pass.depthStencils) {
        DepthStencil fctDs;
        fctDs.name = ds.name;
        fctDs.format = ds.format.empty() ? Format::D32_SFLOAT : StringToFormat(ds.format.c_str());
        if (ds.inheritWindow) {
            if (targetType == RenderTarget::Window) {
                fctDs.wnd = wnd;
                fctDs.isWindow = true;
            } else if (targetType == RenderTarget::Texture) {
                if (m_externalDepthStencil) {
                    fctDs = DepthStencil(ds.name, m_externalDepthStencil);
                } else {
                    fctDs.format = Format::D32_SFLOAT;
                }
            }
        } else {
            if (ds.width > 0 && ds.height > 0) {
                fctDs.width = ds.width;
                fctDs.height = ds.height;
                fctDs.hasFixedSize = true;
            }
        }
        passDesc.depthStencils.push_back(fctDs);
    }

    return passDesc;
}

Status PassManager::buildRenderGraph(const PipelineConfig& config,
                                     FCT::RenderGraph* graph,
                                     FCT::Window* wnd,
                                     RenderTarget targetType) {
    ZoneScoped();
    if (!graph) {
        return InvalidArgumentError("RenderGraph 指针为空");
    }

    for (const auto& pass : config.passes) {
        graph->addPassDesc(buildPassDesc(pass, wnd, targetType));
    }

    m_isGraphBuilt = true;
    return OkStatus();
}

StatusOr<Tech> PassManager::buildTech(const PipelineTechDesc& techDesc) const {
    TechManager tempMgr;
    return tempMgr.buildTech(techDesc);
}

Status PassManager::buildTechManager(const PipelineConfig& config, TechManager* techMgr) {
    ZoneScoped();
    if (!techMgr) {
        return InvalidArgumentError("TechManager 指针为空");
    }
    return techMgr->buildFromPipeline(config);
}

PipelineConfig PassManager::createDefaultDeferredPipelineConfig() {
    PipelineConfig cfg;
    cfg.name = "DefaultDeferredPipeline";
    cfg.version = "2.0";
    cfg.defaultTarget = "window";

    PipelinePassDesc objectPass;
    objectPass.name = "ObjectPass";
    objectPass.clear.types = {"color", "depth", "stencil"};
    objectPass.clear.color = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    objectPass.targets = {
        {"RetTarget", "R16G16B16A16_SFLOAT", 0, 0, false},
        {"PosTarget", "R32G32B32A32_SFLOAT", 0, 0, false},
        {"NormalTarget", "R16G16B16A16_SFLOAT", 0, 0, false},
        {"OrmTarget", "R8G8B8A8_UNORM", 0, 0, false}
    };
    objectPass.depthStencils = {{"Depth", "D32_SFLOAT", 0, 0, false}};
    cfg.passes.push_back(objectPass);

    PipelinePassDesc lightingPass;
    lightingPass.name = "LightingPass";
    lightingPass.clear.types = {"color"};
    lightingPass.clear.color = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    lightingPass.inputTextures = {
        {"RetTarget", {"fragment"}, "", 0, 0},
        {"PosTarget", {"fragment"}, "", 0, 0},
        {"NormalTarget", {"fragment"}, "", 0, 0},
        {"OrmTarget", {"fragment"}, "", 0, 0}
    };
    lightingPass.targets = {{"SceneHDRColor", "R16G16B16A16_SFLOAT", 0, 0, false}};
    cfg.passes.push_back(lightingPass);

    PipelinePassDesc skyboxPass;
    skyboxPass.name = "SkyboxPass";
    skyboxPass.targets = {{"SceneHDRColor", "R16G16B16A16_SFLOAT", 0, 0, false}};
    skyboxPass.depthStencils = {{"Depth", "D32_SFLOAT", 0, 0, false}};
    cfg.passes.push_back(skyboxPass);

    PipelinePassDesc toneMappingPass;
    toneMappingPass.name = "ToneMappingPass";
    toneMappingPass.clear.types = {"color"};
    toneMappingPass.clear.color = Vec4(0.2f, 0.2f, 0.2f, 1.0f);
    toneMappingPass.inputTextures = {{"SceneHDRColor", {"fragment"}, "", 0, 0}};
    toneMappingPass.targets = {{"SwapchainTarget", "", 0, 0, true}};
    cfg.passes.push_back(toneMappingPass);

    return cfg;
}

Status PassManager::applyDefaultDeferredPipeline(FCT::RenderGraph* graph,
                                                 TechManager* techMgr,
                                                 FCT::Window* wnd,
                                                 RenderTarget targetType) {
    m_currentConfig = createDefaultDeferredPipelineConfig();
    if (graph) {
        CHECK_STATUS(buildRenderGraph(m_currentConfig, graph, wnd, targetType));
    }
    if (techMgr && !m_currentConfig.techniques.empty()) {
        CHECK_STATUS(buildTechManager(m_currentConfig, techMgr));
    }
    return OkStatus();
}

Status PassManager::loadAndApplyPipeline(const std::string& path,
                                         FCT::RenderGraph* graph,
                                         TechManager* techMgr,
                                         FCT::Window* wnd,
                                         RenderTarget targetType) {
    ZoneScoped();
    auto configRes = loadPipeline(path);
    CHECK_STATUS(configRes);
    const auto& config = configRes.value();

    if (graph) {
        auto graphStatus = buildRenderGraph(config, graph, wnd, targetType);
        CHECK_STATUS(graphStatus);
    }

    if (techMgr) {
        auto techStatus = buildTechManager(config, techMgr);
        CHECK_STATUS(techStatus);
    }

    return OkStatus();
}

} // namespace MQEngine
