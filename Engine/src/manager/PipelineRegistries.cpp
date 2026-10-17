#include "PipelineRegistries.h"
#include "../core/engine.h"
#include "../core/UniformSlots.h"
#include "../core/VertexLayouts.h"
#include "../data/Component.h"
#include "../data/Camera.h"
#include "../core/EngineGlobal.h"
#include "../system/MatrixCacheSystem.h"
#include "../system/ShininessSystem.h"
#include "../data/ResourceLoader.h"

#include "g_engineShaderObjectPixel.h"
#include "g_engineShaderObjectVertex.h"
#include "g_engineShaderShadowVertex.h"
#include "g_engineShaderDiffuseObjectPixel.h"
#include "g_engineShaderDiffuseObjectVertex.h"
#include "g_engineShaderNormalMapObjectPixel.h"
#include "g_engineShaderNormalMapObjectVertex.h"
#include "g_engineShaderPBRVertex.h"
#include "g_engineShaderPBRPixel.h"
#include "g_engineShaderSkyboxVertex.h"
#include "g_engineShaderSkyboxPixel.h"
#include "g_engineShaderToneMappingVertex.h"
#include "g_engineShaderToneMappingPixel.h"

namespace MQEngine {

namespace {

std::string LoadResourceString(const unsigned char* resource, size_t size) {
    return std::string(reinterpret_cast<const char*>(resource), size);
}

void executeUniversalMeshDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<StaticMeshInstance>(context.entity)) {
        const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
        if (meshInstance.mesh != nullptr) {
            if (g_engineGlobal.matrixCacheSystem) {
                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
            }
            context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
        }
    }
}

void executeDiffuseMeshDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<StaticMeshInstance>(context.entity)) {
        const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
        if (context.registry.all_of<DiffuseTextureComponent>(context.entity)) {
            const auto& diffuseTexture = context.registry.get<DiffuseTextureComponent>(context.entity);
            if (diffuseTexture.texture && diffuseTexture.visible) {
                context.layout->bindTexture("diffuseTexture", diffuseTexture.texture);
                if (meshInstance.mesh && g_engineGlobal.matrixCacheSystem) {
                    g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                    context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                }
            }
        }
    }
}

void executeNormalMapMeshDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<StaticMeshInstance>(context.entity)) {
        const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
        if (context.registry.all_of<DiffuseTextureComponent, NormalTextureComponent>(context.entity)) {
            const auto& diffuseTexture = context.registry.get<DiffuseTextureComponent>(context.entity);
            const auto& normalMap = context.registry.get<NormalTextureComponent>(context.entity);
            if (diffuseTexture.texture && diffuseTexture.visible && normalMap.texture && normalMap.visible) {
                context.layout->bindTexture("diffuseTexture", diffuseTexture.texture);
                context.layout->bindTexture("normalTexture", normalMap.texture);
                if (g_engineGlobal.shininessSystem) {
                    g_engineGlobal.shininessSystem->bindShininess(&context.registry, context.entity, context.layout);
                }
                if (meshInstance.mesh && g_engineGlobal.matrixCacheSystem) {
                    g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                    context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                }
            }
        }
    }
}

void executePBRMeshDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<StaticMeshInstance>(context.entity)) {
        const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
        if (context.registry.all_of<AlbedoTextureComponent, NormalTextureComponent, OrmTextureComponent>(context.entity)) {
            const auto& albedo = context.registry.get<AlbedoTextureComponent>(context.entity);
            const auto& normal = context.registry.get<NormalTextureComponent>(context.entity);
            const auto& orm = context.registry.get<OrmTextureComponent>(context.entity);
            if (albedo.texture && albedo.visible && normal.texture && normal.visible && orm.texture && orm.visible) {
                context.layout->bindTexture("albedoTexture", albedo.texture);
                context.layout->bindTexture("normalTexture", normal.texture);
                context.layout->bindTexture("ormTexture", orm.texture);
                if (meshInstance.mesh && g_engineGlobal.matrixCacheSystem) {
                    g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                    context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                }
            }
        }
    }
}

void executeSkyboxDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<CacheSkyboxComponent>(context.entity)) {
        const auto& skyboxComp = context.registry.get<CacheSkyboxComponent>(context.entity);
        if (skyboxComp.texture && skyboxComp.visible) {
            context.layout->bindTexture("skyboxTexture", skyboxComp.texture);
            if (g_engineGlobal.matrixCacheSystem) {
                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
            }
            auto mesh = Engine::getInstance().getSkyboxMesh();
            if (mesh) {
                context.layout->drawMesh(context.cmdBuf, mesh);
            }
        }
    }
}

void executeToneMappingDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<CameraComponent>(context.entity)) {
        const auto& cameraComp = context.registry.get<CameraComponent>(context.entity);
        if (cameraComp.active) {
            auto mesh = Engine::getInstance().getFullScreenMesh();
            if (mesh) {
                context.layout->drawMesh(context.cmdBuf, mesh);
            }
        }
    }
}

} // namespace

// --- ComponentRegistry ---

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry s_instance;
    return s_instance;
}

ComponentRegistry::ComponentRegistry() {
    registerAllStandardComponents();
}

void ComponentRegistry::registerComponent(const std::string& name, entt::type_info info) {
    m_components.insert_or_assign(name, info);
}

StatusOr<entt::type_info> ComponentRegistry::getTypeInfo(const std::string& name) const {
    auto it = m_components.find(name);
    if (it != m_components.end()) {
        return it->second;
    }
    return NotFoundError(StrCat("未注册的组件类型: ", name));
}

entt::id_type ComponentRegistry::getTypeId(const std::string& name) const {
    auto it = m_components.find(name);
    if (it != m_components.end()) {
        return it->second.hash();
    }
    return entt::id_type{};
}

bool ComponentRegistry::hasComponent(const std::string& name) const {
    return m_components.find(name) != m_components.end();
}

void ComponentRegistry::registerAllStandardComponents() {
    registerComponent<StaticMeshInstance>("StaticMeshInstance");
    registerComponent<PositionComponent>("PositionComponent");
    registerComponent<PositionComponent>("Transform");
    registerComponent<RotationComponent>("RotationComponent");
    registerComponent<ScaleComponent>("ScaleComponent");
    registerComponent<CameraComponent>("CameraComponent");
    registerComponent<CameraComponent>("Camera");
    registerComponent<DiffuseTextureComponent>("DiffuseTextureComponent");
    registerComponent<NormalTextureComponent>("NormalTextureComponent");
    registerComponent<AlbedoTextureComponent>("AlbedoTextureComponent");
    registerComponent<OrmTextureComponent>("OrmTextureComponent");
    registerComponent<EmissiveTextureComponent>("EmissiveTextureComponent");
    registerComponent<CacheSkyboxComponent>("CacheSkyboxComponent");
    registerComponent<SkyboxComponent>("SkyboxComponent");
    registerComponent<DirectionalLightComponent>("DirectionalLightComponent");
    registerComponent<ShininessComponent>("ShininessComponent");
    registerComponent<TickerScriptComponent>("TickerScriptComponent");
    registerComponent<OnStartScriptComponent>("OnStartScriptComponent");
    registerComponent<NameTag>("NameTag");
}

// --- UniformSlotRegistry ---

UniformSlotRegistry& UniformSlotRegistry::instance() {
    static UniformSlotRegistry s_instance;
    return s_instance;
}

UniformSlotRegistry::UniformSlotRegistry() {
    registerAllStandardSlots();
}

void UniformSlotRegistry::registerSlot(const std::string& name, const FCT::UniformSlot& slot) {
    m_slots[name] = slot;
}

StatusOr<FCT::UniformSlot> UniformSlotRegistry::getSlot(const std::string& name) const {
    auto it = m_slots.find(name);
    if (it != m_slots.end()) {
        return it->second;
    }
    return NotFoundError(StrCat("未注册的 UniformSlot: ", name));
}

bool UniformSlotRegistry::hasSlot(const std::string& name) const {
    return m_slots.find(name) != m_slots.end();
}

void UniformSlotRegistry::registerAllStandardSlots() {
    registerSlot("ModelUniform", ModelUniformSlot);
    registerSlot("ModelUniformSlot", ModelUniformSlot);
    registerSlot("CameraUniform", CameraUniformSlot);
    registerSlot("CameraUniformSlot", CameraUniformSlot);
    registerSlot("DirectionalLightUniform", DirectionalLightUniformSlot);
    registerSlot("DirectionalLightUniformSlot", DirectionalLightUniformSlot);
    registerSlot("PointLightUniform", PointLightUniformSlot);
    registerSlot("PointLightUniformSlot", PointLightUniformSlot);
    registerSlot("ViewPosUniform", ViewPosUniformSlot);
    registerSlot("ShadowUniform", ShadowUniformSlot);
    registerSlot("ShininessUniform", ShininessUniformSlot);
}

// --- EntityCallbackRegistry ---

EntityCallbackRegistry& EntityCallbackRegistry::instance() {
    static EntityCallbackRegistry s_instance;
    return s_instance;
}

EntityCallbackRegistry::EntityCallbackRegistry() {
    registerAllStandardCallbacks();
}

void EntityCallbackRegistry::registerCallback(const std::string& name, EntityOperationCallback callback) {
    m_callbacks[name] = std::move(callback);
}

StatusOr<EntityOperationCallback> EntityCallbackRegistry::getCallback(const std::string& name) const {
    auto it = m_callbacks.find(name);
    if (it != m_callbacks.end()) {
        return it->second;
    }
    return NotFoundError(StrCat("未注册的 EntityCallback: ", name));
}

bool EntityCallbackRegistry::hasCallback(const std::string& name) const {
    return m_callbacks.find(name) != m_callbacks.end();
}

void EntityCallbackRegistry::registerAllStandardCallbacks() {
    registerCallback("UniversalMeshDrawCallback", &executeUniversalMeshDraw);
    registerCallback("universalEntityCallback", &executeUniversalMeshDraw);
    registerCallback("DiffuseMeshDrawCallback", &executeDiffuseMeshDraw);
    registerCallback("NormalMapMeshDrawCallback", &executeNormalMapMeshDraw);
    registerCallback("PBRMeshDrawCallback", &executePBRMeshDraw);
    registerCallback("SkyboxDrawCallback", &executeSkyboxDraw);
    registerCallback("ToneMappingDrawCallback", &executeToneMappingDraw);
    registerCallback("FullScreenQuadDrawCallback", &executeToneMappingDraw);
}

// --- LayoutRegistry ---

LayoutRegistry& LayoutRegistry::instance() {
    static LayoutRegistry s_instance;
    return s_instance;
}

LayoutRegistry::LayoutRegistry() {
    registerAllStandardLayouts();
}

void LayoutRegistry::registerVertexLayout(const std::string& name, const FCT::VertexLayout& layout) {
    m_vertexLayouts[name] = layout;
}

StatusOr<FCT::VertexLayout> LayoutRegistry::getVertexLayout(const std::string& name) const {
    auto it = m_vertexLayouts.find(name);
    if (it != m_vertexLayouts.end()) {
        return it->second;
    }
    return NotFoundError(StrCat("未注册的 VertexLayout: ", name));
}

void LayoutRegistry::registerPixelLayout(const std::string& name, const FCT::PixelLayout& layout) {
    m_pixelLayouts[name] = layout;
}

StatusOr<FCT::PixelLayout> LayoutRegistry::getPixelLayout(const std::string& name) const {
    auto it = m_pixelLayouts.find(name);
    if (it != m_pixelLayouts.end()) {
        return it->second;
    }
    return NotFoundError(StrCat("未注册的 PixelLayout: ", name));
}

void LayoutRegistry::registerAllStandardLayouts() {
    registerVertexLayout("StandardMeshVertexLayout", StandardMeshVertexLayout);

    FCT::PixelLayout standardPixelLayout = {
        FCT::VertexElement{FCT::VtxType::Custom, "worldPos", FCT::Format::R32G32B32A32_SFLOAT},
        FCT::VertexElement{FCT::VtxType::Custom, "shadowPos", FCT::Format::R32G32B32A32_SFLOAT},
        StandardMeshVertexLayout
    };
    registerPixelLayout("StandardPixelLayout", standardPixelLayout);
}

// --- ShaderSourceRegistry ---

ShaderSourceRegistry& ShaderSourceRegistry::instance() {
    static ShaderSourceRegistry s_instance;
    return s_instance;
}

ShaderSourceRegistry::ShaderSourceRegistry() {
    registerAllStandardShaders();
}

void ShaderSourceRegistry::registerShader(const std::string& name, const std::string& source) {
    m_shaders[name] = source;
}

StatusOr<std::string> ShaderSourceRegistry::getShader(const std::string& name) const {
    auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        return it->second;
    }
    if (g_engineGlobal.resourceLoader != nullptr) {
        auto fileResult = g_engineGlobal.resourceLoader->loadFile(name);
        if (fileResult.ok()) {
            return fileResult.value();
        }
    }
    return NotFoundError(StrCat("未找到着色器资源: ", name));
}

bool ShaderSourceRegistry::hasShader(const std::string& name) const {
    return m_shaders.find(name) != m_shaders.end();
}

void ShaderSourceRegistry::registerAllStandardShaders() {
    registerShader("ObjectVertex", LoadResourceString(g_engineShaderObjectVertex, g_engineShaderObjectVertexSize));
    registerShader("ObjectPixel", LoadResourceString(g_engineShaderObjectPixel, g_engineShaderObjectPixelSize));
    registerShader("ShadowVertex", LoadResourceString(g_engineShaderShadowVertex, g_engineShaderShadowVertexSize));
    registerShader("DiffuseObjectVertex", LoadResourceString(g_engineShaderDiffuseObjectVertex, g_engineShaderDiffuseObjectVertexSize));
    registerShader("DiffuseObjectPixel", LoadResourceString(g_engineShaderDiffuseObjectPixel, g_engineShaderDiffuseObjectPixelSize));
    registerShader("NormalMapObjectVertex", LoadResourceString(g_engineShaderNormalMapObjectVertex, g_engineShaderNormalMapObjectVertexSize));
    registerShader("NormalMapObjectPixel", LoadResourceString(g_engineShaderNormalMapObjectPixel, g_engineShaderNormalMapObjectPixelSize));
    registerShader("PBRVertex", LoadResourceString(g_engineShaderPBRVertex, g_engineShaderPBRVertexSize));
    registerShader("PBRPixel", LoadResourceString(g_engineShaderPBRPixel, g_engineShaderPBRPixelSize));
    registerShader("SkyboxVertex", LoadResourceString(g_engineShaderSkyboxVertex, g_engineShaderSkyboxVertexSize));
    registerShader("SkyboxPixel", LoadResourceString(g_engineShaderSkyboxPixel, g_engineShaderSkyboxPixelSize));
    registerShader("ToneMappingVertex", LoadResourceString(g_engineShaderToneMappingVertex, g_engineShaderToneMappingVertexSize));
    registerShader("ToneMappingPixel", LoadResourceString(g_engineShaderToneMappingPixel, g_engineShaderToneMappingPixelSize));
}

void InitializePipelineRegistries() {
    ComponentRegistry::instance().registerAllStandardComponents();
    UniformSlotRegistry::instance().registerAllStandardSlots();
    EntityCallbackRegistry::instance().registerAllStandardCallbacks();
    LayoutRegistry::instance().registerAllStandardLayouts();
    ShaderSourceRegistry::instance().registerAllStandardShaders();
}

} // namespace MQEngine
