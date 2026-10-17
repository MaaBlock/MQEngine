#include "PipelineManager.h"
#include "../core/EngineGlobal.h"
#include "../data/ResourceLoader.h"
#include "../core/engine.h"
#include "../core/VertexLayouts.h"
#include "../manager/SystemManager.h"
#include <boost/json/src.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cstdint>

// MSVC CRT 兼容垫片：解决 vcpkg 第三方库 (SPIRV-Tools/glslang) 在不同 MSVC 小版本间的矢量化 STL 符号差异
#if defined(_MSC_VER) && (_MSC_VER < 1940)
extern "C" {

const void* __stdcall __std_find_last_trivial_1(const void* _First, const void* _Last, uint8_t _Val) noexcept {
    const uint8_t* first = static_cast<const uint8_t*>(_First);
    const uint8_t* last = static_cast<const uint8_t*>(_Last);
    while (last > first) {
        --last;
        if (*last == _Val) return last;
    }
    return static_cast<const uint8_t*>(_Last);
}

const void* __stdcall __std_search_1(
    const void* _First1, const void* _Last1, const void* _First2, size_t _Count2) noexcept {
    const uint8_t* first1 = static_cast<const uint8_t*>(_First1);
    const uint8_t* last1 = static_cast<const uint8_t*>(_Last1);
    const uint8_t* first2 = static_cast<const uint8_t*>(_First2);
    if (_Count2 == 0) return first1;
    if (static_cast<size_t>(last1 - first1) < _Count2) return last1;
    const uint8_t* searchEnd = last1 - _Count2 + 1;
    for (const uint8_t* it = first1; it < searchEnd; ++it) {
        if (std::memcmp(it, first2, _Count2) == 0) return it;
    }
    return last1;
}

void* __stdcall __std_remove_8(void* _First, void* _Last, uint64_t _Val) noexcept {
    uint64_t* first = static_cast<uint64_t*>(_First);
    uint64_t* last = static_cast<uint64_t*>(_Last);
    uint64_t* result = first;
    for (; first != last; ++first) {
        if (*first != _Val) {
            *result = *first;
            ++result;
        }
    }
    return result;
}

uint32_t __stdcall __std_max_4u(const void* _First, const void* _Last) noexcept {
    const uint32_t* first = static_cast<const uint32_t*>(_First);
    const uint32_t* last = static_cast<const uint32_t*>(_Last);
    if (first == last) return 0;
    uint32_t maxVal = *first;
    for (++first; first != last; ++first) {
        if (*first > maxVal) maxVal = *first;
    }
    return maxVal;
}

size_t __stdcall __std_mismatch_4(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const uint32_t* first1 = static_cast<const uint32_t*>(_First1);
    const uint32_t* first2 = static_cast<const uint32_t*>(_First2);
    for (size_t i = 0; i < _Count; ++i) {
        if (first1[i] != first2[i]) return i;
    }
    return _Count;
}

} // extern "C"
#endif

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

PipelineManager::PipelineManager() = default;

StatusOr<PipelineConfig> PipelineManager::loadPipeline(const std::string& path) {
    ZoneScoped();
    if (!g_engineGlobal.resourceLoader) {
        return InternalError("ResourceLoader 实例未初始化");
    }

    // 候选路径尝试列表，保证在不同运行目录下均能正确读取
    std::vector<std::string> candidatePaths = {
        path,
        "./" + path,
        "MQEngine/" + path,
        "../" + path,
        "../../" + path,
        "../../../" + path,
        "../../../../" + path,
        "Res/pipelines/default_pipeline.json",
        "MQEngine/Res/pipelines/default_pipeline.json",
        "res/pipelines/default_pipeline.json",
        "MQEngine/res/pipelines/default_pipeline.json",
        "../Res/pipelines/default_pipeline.json",
        "../../Res/pipelines/default_pipeline.json",
        "../../../Res/pipelines/default_pipeline.json",
        "../../../../Res/pipelines/default_pipeline.json"
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
        return NotFoundError(StrCat("无法通过 ResourceLoader 读取管线配置文件: ", path));
    }

    try {
        json::value jv = json::parse(content);
        auto configRes = parsePipelineConfig(jv);
        CHECK_STATUS(configRes);
        m_currentConfig = configRes.value();
        return m_currentConfig;
    } catch (const std::exception& e) {
        return InvalidArgumentError(StrCat("JSON 解析失败: ", e.what()));
    }
}

Status PipelineManager::buildRenderGraph(const PipelineConfig& config,
                                         FCT::RenderGraph* graph,
                                         FCT::Window* wnd,
                                         RenderTarget targetType) {
    ZoneScoped();
    if (!graph) {
        return InvalidArgumentError("RenderGraph 指针为空");
    }

    for (const auto& pass : config.passes) {
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
                    fctTarget.format = tgt.format.empty() ? Format::R8G8B8A8_SRGB : StringToFormat(tgt.format.c_str());
                    fctTarget.width = tgt.width > 0 ? tgt.width : 1024;
                    fctTarget.height = tgt.height > 0 ? tgt.height : 768;
                    fctTarget.hasFixedSize = true;
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
                    fctDs.format = Format::D32_SFLOAT;
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

        graph->addPassDesc(passDesc);
    }

    m_isGraphBuilt = true;
    return OkStatus();
}

Status PipelineManager::buildTechManager(const PipelineConfig& config, TechManager* techMgr) {
    ZoneScoped();
    if (!techMgr) {
        return InvalidArgumentError("TechManager 指针为空");
    }

    InitializePipelineRegistries();

    for (const auto& techDesc : config.techniques) {
        Tech tech;
        tech.setName(techDesc.name);
        tech.setPassName(techDesc.pass);

        // 顶点布局
        auto vlRes = LayoutRegistry::instance().getVertexLayout(techDesc.vertexLayout);
        if (vlRes.ok()) {
            tech.addVertexLayout(vlRes.value());
        } else {
            tech.addVertexLayout(StandardMeshVertexLayout);
        }

        // 像素布局
        auto plRes = LayoutRegistry::instance().getPixelLayout(techDesc.pixelLayout);
        if (plRes.ok()) {
            tech.setPixelLayout(plRes.value());
        } else {
            auto defaultPl = LayoutRegistry::instance().getPixelLayout("StandardPixelLayout");
            if (defaultPl.ok()) {
                tech.setPixelLayout(defaultPl.value());
            }
        }

        // 顶点着色器
        if (!techDesc.vertexShader.empty()) {
            auto vsRes = ShaderSourceRegistry::instance().getShader(techDesc.vertexShader);
            if (vsRes.ok()) {
                tech.setVertexShaderSource(vsRes.value());
            } else {
                return vsRes.status();
            }
        }

        // 像素着色器
        if (!techDesc.pixelShader.empty()) {
            auto psRes = ShaderSourceRegistry::instance().getShader(techDesc.pixelShader);
            if (psRes.ok()) {
                tech.setPixelShaderSource(psRes.value());
            } else {
                return psRes.status();
            }
        }

        // Uniform 槽位
        for (const auto& slotName : techDesc.uniformSlots) {
            auto slotRes = UniformSlotRegistry::instance().getSlot(slotName);
            if (slotRes.ok()) {
                tech.addUniformSlot(slotRes.value());
            } else {
                spdlog::warn("Technique {} 找不到指定 UniformSlot: {}", techDesc.name, slotName);
            }
        }

        // Sampler 槽位
        for (const auto& samplerName : techDesc.samplerSlots) {
            tech.addSamplerSlot(SamplerSlot(samplerName.c_str()));
        }

        // Texture 槽位
        for (const auto& texName : techDesc.textureSlots) {
            if (texName == "skyboxTexture") {
                tech.addTextureSlot(TextureSlot{texName, TextureType::TextureCube});
            } else {
                tech.addTextureSlot(TextureSlot{texName, TextureType::Texture2D});
            }
        }

        // 组件过滤器
        ComponentFilter filter;
        for (const auto& compName : techDesc.componentFilter.include) {
            auto infoRes = ComponentRegistry::instance().getTypeInfo(compName);
            if (infoRes.ok()) {
                filter.include_types.push_back(infoRes.value());
            } else {
                spdlog::warn("Technique {} 组件包含过滤器找不到类型: {}", techDesc.name, compName);
            }
        }
        for (const auto& compName : techDesc.componentFilter.exclude) {
            auto infoRes = ComponentRegistry::instance().getTypeInfo(compName);
            if (infoRes.ok()) {
                filter.exclude_types.push_back(infoRes.value());
            } else {
                spdlog::warn("Technique {} 组件排除过滤器找不到类型: {}", techDesc.name, compName);
            }
        }
        tech.setComponentFilter(filter);

        // 实体操作绘制回调
        if (!techDesc.entityCallback.empty()) {
            auto cbRes = EntityCallbackRegistry::instance().getCallback(techDesc.entityCallback);
            if (cbRes.ok()) {
                tech.setEntityOperationCallback(cbRes.value());
            } else {
                spdlog::warn("Technique {} 找不到指定 EntityCallback: {}", techDesc.name, techDesc.entityCallback);
            }
        }

        // 绑定关联系统 (如 CameraSystem, LightingSystem, TextureSamplerSystem)
        if (g_engineGlobal.systemManager) {
            for (const auto& sysName : techDesc.bindedSystems) {
                auto* binded = dynamic_cast<BindedSystem*>(g_engineGlobal.systemManager->findSystem(sysName));
                if (binded) {
                    tech.addBindedSystem(binded);
                } else {
                    spdlog::warn("Technique {} 找不到绑定系统: {}", techDesc.name, sysName);
                }
            }
        }

        auto ret = techMgr->addTech(techDesc.pass, std::move(tech));
        CHECK_STATUS(ret);
    }

    return OkStatus();
}

Status PipelineManager::loadAndApplyPipeline(const std::string& path,
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
