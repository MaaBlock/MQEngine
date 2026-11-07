#include "PipelineRegistries.h"
#include "../../runtime/core/Engine.h"
#include "../core/UniformSlots.h"
#include "../core/VertexLayouts.h"
#include "../../scene/data/Component.h"
#include "../data/Camera.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../system/MatrixCacheSystem.h"
#include "../system/ShininessSystem.h"
#include "../system/TextureCacheSystem.h"
#include "../../animation/system/SkeletalAnimationSystem.h"
#include "../system/GpuDrivenSystem.h"
#include "../../resource/core/ResourceLoader.h"

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
#include "g_engineShaderDeferredLightingVertex.h"
#include "g_engineShaderDeferredLightingPixel.h"

namespace MQEngine {

namespace {

std::string LoadResourceString(const unsigned char* resource, size_t size) {
    return std::string(reinterpret_cast<const char*>(resource), size);
}

template<typename T>
FCT::Image* resolveComponentTexture(const T* comp, FCT::Image* fallback) {
    if (comp && comp->texture && comp->visible)
        return comp->texture;
    return fallback;
}

void drawStaticMeshGpuDrivenOrDirect(const EntityRenderContext& context, FCT::StaticMesh<uint32_t>* mesh) {
    void* indirectBuffer = nullptr;
    size_t byteOffset = 0;
    if (g_engineGlobal.gpuDrivenSystem &&
        g_engineGlobal.gpuDrivenSystem->tryGetEntityIndirectDraw(context.registry, context.entity, indirectBuffer, byteOffset)) {
        context.layout->drawMeshIndirect(context.cmdBuf, mesh, indirectBuffer, byteOffset, 1);
        return;
    }
    context.layout->drawMesh(context.cmdBuf, mesh);
}

void executeUniversalMeshDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<StaticMeshInstance>(context.entity)) {
        const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
        if (meshInstance.mesh != nullptr) {
            if (g_engineGlobal.matrixCacheSystem) {
                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
            }
            if (g_engineGlobal.skeletalAnimationSystem) {
                g_engineGlobal.skeletalAnimationSystem->bindBoneMatrices(&context.registry, context.entity, context.layout);
            }
            context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
        }
    }
}

void executeDiffuseMeshDraw(const EntityRenderContext& context) {
    if (!context.registry.all_of<StaticMeshInstance>(context.entity))
        return;
    const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
    if (!meshInstance.mesh || !g_engineGlobal.matrixCacheSystem)
        return;

    FCT::Image* defaultWhite = g_engineGlobal.textureRenderSystem
        ? g_engineGlobal.textureRenderSystem->getDefaultWhiteTexture()
        : nullptr;
    const auto* diffuseComp = context.registry.try_get<DiffuseTextureComponent>(context.entity);
    FCT::Image* diffuseImg = resolveComponentTexture(diffuseComp, defaultWhite);
    if (!diffuseImg)
        return;

    context.layout->bindTexture("diffuseTexture", diffuseImg);
    g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
    if (g_engineGlobal.skeletalAnimationSystem) {
        g_engineGlobal.skeletalAnimationSystem->bindBoneMatrices(&context.registry, context.entity, context.layout);
    }
    drawStaticMeshGpuDrivenOrDirect(context, meshInstance.mesh);
}

void executeNormalMapMeshDraw(const EntityRenderContext& context) {
    if (!context.registry.all_of<StaticMeshInstance>(context.entity))
        return;
    const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
    if (!meshInstance.mesh || !g_engineGlobal.matrixCacheSystem)
        return;

    FCT::Image* defaultWhite = g_engineGlobal.textureRenderSystem
        ? g_engineGlobal.textureRenderSystem->getDefaultWhiteTexture()
        : nullptr;
    FCT::Image* defaultNormal = g_engineGlobal.textureRenderSystem
        ? g_engineGlobal.textureRenderSystem->getDefaultNormalTexture()
        : nullptr;

    const auto* diffuseComp = context.registry.try_get<DiffuseTextureComponent>(context.entity);
    const auto* normalComp = context.registry.try_get<NormalTextureComponent>(context.entity);
    FCT::Image* diffuseImg = resolveComponentTexture(diffuseComp, defaultWhite);
    FCT::Image* normalImg = resolveComponentTexture(normalComp, defaultNormal);
    if (!diffuseImg || !normalImg)
        return;

    context.layout->bindTexture("diffuseTexture", diffuseImg);
    context.layout->bindTexture("normalTexture", normalImg);
    if (g_engineGlobal.shininessSystem)
        g_engineGlobal.shininessSystem->bindShininess(&context.registry, context.entity, context.layout);
    g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
    if (g_engineGlobal.skeletalAnimationSystem) {
        g_engineGlobal.skeletalAnimationSystem->bindBoneMatrices(&context.registry, context.entity, context.layout);
    }
    drawStaticMeshGpuDrivenOrDirect(context, meshInstance.mesh);
}

void executePBRMeshDraw(const EntityRenderContext& context) {
    if (!context.registry.all_of<StaticMeshInstance>(context.entity))
        return;
    const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
    if (!meshInstance.mesh || !g_engineGlobal.matrixCacheSystem)
        return;

    FCT::Image* defaultWhite = g_engineGlobal.textureRenderSystem
        ? g_engineGlobal.textureRenderSystem->getDefaultWhiteTexture()
        : nullptr;
    FCT::Image* defaultNormal = g_engineGlobal.textureRenderSystem
        ? g_engineGlobal.textureRenderSystem->getDefaultNormalTexture()
        : nullptr;
    FCT::Image* defaultOrm = g_engineGlobal.textureRenderSystem
        ? g_engineGlobal.textureRenderSystem->getDefaultOrmTexture()
        : nullptr;

    const auto* albedoComp = context.registry.try_get<AlbedoTextureComponent>(context.entity);
    const auto* diffuseComp = context.registry.try_get<DiffuseTextureComponent>(context.entity);
    const auto* normalComp = context.registry.try_get<NormalTextureComponent>(context.entity);
    const auto* ormComp = context.registry.try_get<OrmTextureComponent>(context.entity);

    FCT::Image* albedoImg = albedoComp
        ? resolveComponentTexture(albedoComp, defaultWhite)
        : resolveComponentTexture(diffuseComp, defaultWhite);
    FCT::Image* normalImg = resolveComponentTexture(normalComp, defaultNormal);
    FCT::Image* ormImg = resolveComponentTexture(ormComp, defaultOrm);
    if (!albedoImg || !normalImg || !ormImg)
        return;

    context.layout->bindTexture("albedoTexture", albedoImg);
    context.layout->bindTexture("normalTexture", normalImg);
    context.layout->bindTexture("ormTexture", ormImg);
    g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
    if (g_engineGlobal.skeletalAnimationSystem) {
        g_engineGlobal.skeletalAnimationSystem->bindBoneMatrices(&context.registry, context.entity, context.layout);
    }
    drawStaticMeshGpuDrivenOrDirect(context, meshInstance.mesh);
}

void executeSkyboxDraw(const EntityRenderContext& context) {
    if (context.registry.all_of<CacheSkyboxComponent>(context.entity)) {
        const auto& skyboxComp = context.registry.get<CacheSkyboxComponent>(context.entity);
        if (skyboxComp.texture && skyboxComp.visible) {
            context.layout->bindTexture("skyboxTexture", skyboxComp.texture);
            if (g_engineGlobal.matrixCacheSystem) {
                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
            }
            auto mesh = Runtime::Engine::getInstance()->renderer->getSkyboxMesh();
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
            auto mesh = Runtime::Engine::getInstance()->renderer->getFullScreenMesh();
            if (mesh) {
                context.layout->drawMesh(context.cmdBuf, mesh);
            }
        }
    }
}

std::string ExtractShortComponentName(std::string_view rawName) {
    auto colonPos = rawName.rfind("::");
    if (colonPos != std::string_view::npos) {
        return std::string(rawName.substr(colonPos + 2));
    }
    constexpr std::string_view structPrefix = "struct ";
    constexpr std::string_view classPrefix = "class ";
    if (rawName.substr(0, structPrefix.size()) == structPrefix) {
        rawName.remove_prefix(structPrefix.size());
    } else if (rawName.substr(0, classPrefix.size()) == classPrefix) {
        rawName.remove_prefix(classPrefix.size());
    }
    return std::string(rawName);
}

template <typename Tuple, size_t... Is>
void RegisterComponentsFromTupleImpl(ComponentRegistry& registry, std::index_sequence<Is...>) {
    (([&]() {
        using CompType = std::tuple_element_t<Is, Tuple>;
        std::string shortName = ExtractShortComponentName(entt::type_id<CompType>().name());
        registry.registerComponent<CompType>(shortName);
    }()), ...);
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

void ComponentRegistry::unregisterComponent(const std::string& name) {
    m_components.erase(name);
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
    // 基于 AllComponentsList 编译期自动注册所有引擎组件，消除手写字符串映射表
    RegisterComponentsFromTupleImpl<AllComponentsList>(
        *this,
        std::make_index_sequence<std::tuple_size_v<AllComponentsList>>{});
    // 兼容旧版配置别名
    registerComponent<PositionComponent>("Transform");
    registerComponent<CameraComponent>("Camera");
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

void UniformSlotRegistry::unregisterSlot(const std::string& name) {
    m_slots.erase(name);
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
    const FCT::UniformSlot standardSlots[] = {
        ModelUniformSlot,
        CameraUniformSlot,
        DirectionalLightUniformSlot,
        PointLightUniformSlot,
        ViewPosUniformSlot,
        ShadowUniformSlot,
        ShininessUniformSlot,
        PBRMaterialUniformSlot,
        BoneUniformSlot
    };
    for (const auto& slot : standardSlots) {
        std::string baseName = slot.getName();
        registerSlot(baseName, slot);
        registerSlot(baseName + "Slot", slot);
    }
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

void EntityCallbackRegistry::unregisterCallback(const std::string& name) {
    m_callbacks.erase(name);
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
    registerCallback("DeferredLightingDrawCallback", &executeToneMappingDraw);
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
    registerPixelLayout("StandardPixelLayout", StandardPixelLayout);
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

void ShaderSourceRegistry::unregisterShader(const std::string& name) {
    m_shaders.erase(name);
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
    registerShader("DeferredLightingVertex", LoadResourceString(g_engineShaderDeferredLightingVertex, g_engineShaderDeferredLightingVertexSize));
    registerShader("DeferredLightingPixel", LoadResourceString(g_engineShaderDeferredLightingPixel, g_engineShaderDeferredLightingPixelSize));
}

void InitializePipelineRegistries() {
    ComponentRegistry::instance().registerAllStandardComponents();
    UniformSlotRegistry::instance().registerAllStandardSlots();
    EntityCallbackRegistry::instance().registerAllStandardCallbacks();
    LayoutRegistry::instance().registerAllStandardLayouts();
    ShaderSourceRegistry::instance().registerAllStandardShaders();
}

} // namespace MQEngine
