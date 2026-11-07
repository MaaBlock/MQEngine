#include "HomeLbmPlugin.h"
#include <FCT_Base/Reflect/ReflectMeta.h>
#include "HomeLBM_reflection.gen.h"

namespace MQEngine {

static_assert(FCT::Reflect::HasStaticTypeMeta<LbmSimulationMode>, "LbmSimulationMode 必须由 mq_reflect 生成静态反射");
static_assert(FCT::Reflect::HasStaticTypeMeta<LbmComputeBackendMode>, "LbmComputeBackendMode 必须由 mq_reflect 生成静态反射");
static_assert(FCT::Reflect::HasStaticTypeMeta<LbmVisualizationMode>, "LbmVisualizationMode 必须由 mq_reflect 生成静态反射");
static_assert(FCT::Reflect::HasStaticTypeMeta<LbmBoundaryType>, "LbmBoundaryType 必须由 mq_reflect 生成静态反射");
static_assert(FCT::Reflect::HasStaticTypeMeta<LbmDomainComponent>, "LbmDomainComponent 必须由 mq_reflect 生成静态反射");
static_assert(FCT::Reflect::HasStaticTypeMeta<LbmEmitterComponent>, "LbmEmitterComponent 必须由 mq_reflect 生成静态反射");
static_assert(FCT::Reflect::HasStaticTypeMeta<LbmObstacleComponent>, "LbmObstacleComponent 必须由 mq_reflect 生成静态反射");
static_assert(FCT::Reflect::HasStaticTypeMeta<HomeLbmService>, "HomeLbmService 必须由 mq_reflect 生成静态反射");

namespace {

class HomeLbmRuntimeComposite : public Runtime::Composite {
public:
    explicit HomeLbmRuntimeComposite(HomeLbmPlugin* owner) : m_owner(owner) {}

    void registerInfo() override {
        compositeRegisterInfo = Runtime::CompositeRegisterInfo{
            .name = "HomeLbmRuntimeComposite",
            .requiredTags = {"Runtime"},
        };
    }

    Status onInit(Runtime::Engine* engine) override {
        return m_owner ? m_owner->initRuntime(engine) : OkStatus();
    }

    void onUnload() override {
        if (m_owner) {
            m_owner->unloadRuntime();
        }
    }

private:
    HomeLbmPlugin* m_owner = nullptr;
};

class HomeLbmEditorComposite : public Runtime::Composite {
public:
    explicit HomeLbmEditorComposite(HomeLbmPlugin* owner) : m_owner(owner) {}

    void registerInfo() override {
        compositeRegisterInfo = Runtime::CompositeRegisterInfo{
            .name = "HomeLbmEditorComposite",
            .requiredTags = {"Editor", "ImGui"},
            .predecessors = {"ImguiComposite"},
        };
    }

    Status onInit(Runtime::Engine* engine) override {
        return m_owner ? m_owner->initEditorUi(engine) : OkStatus();
    }

    void onUnload() override {
        if (m_owner) {
            m_owner->unloadEditorUi();
        }
    }

private:
    HomeLbmPlugin* m_owner = nullptr;
};

} // namespace

HomeLbmPlugin::HomeLbmPlugin() {
    emplaceComposite<HomeLbmRuntimeComposite>(this);
    emplaceComposite<HomeLbmEditorComposite>(this);
}

HomeLbmPlugin::~HomeLbmPlugin() {
    onUnload();
}

Status HomeLbmPlugin::onLoad(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    m_rhiContext = m_engine ? m_engine->platform->getContext() : nullptr;
    m_dataManager = m_engine ? m_engine->scene->getDataManager() : g_engineGlobal.dataManager;
    m_systemManager = m_engine ? m_engine->host->getSystemManager() : nullptr;
    m_componentReflection = m_engine ? m_engine->foundation->getComponentReflection() : nullptr;

    if (m_componentReflection) {
        m_componentReflection->registerComponent<LbmDomainComponent>("LbmDomainComponent");
        m_componentReflection->registerComponent<LbmEmitterComponent>("LbmEmitterComponent");
        m_componentReflection->registerComponent<LbmObstacleComponent>("LbmObstacleComponent");
    }

    if (!m_system) {
        m_system = makeUnique<HomeLbmSystem>(m_rhiContext, m_dataManager);
    }
    if (!m_service) {
        m_service = makeUnique<HomeLbmService>(m_system.get());
    } else {
        m_service->bindSystem(m_system.get());
    }
    if (!m_editorUi) {
        m_editorUi = makeUnique<HomeLbmEditorUi>(m_system.get());
    }

    m_loaded = true;
    return OkStatus();
}

Status HomeLbmPlugin::onInit(Runtime::Engine* engine) {
    if (!m_loaded) {
        CHECK_STATUS(onLoad(engine));
    }
    return OkStatus();
}

Status HomeLbmPlugin::initRuntime(Runtime::Engine* engine) {
    if (!m_loaded) {
        CHECK_STATUS(onLoad(engine));
    }
    CHECK_STATUS(m_system->initialize());

    if (!m_systemRegistered && m_systemManager) {
        m_systemManager->requestAddSystem("HomeLbmSystem", m_system.get());
        m_systemManager->flushRequests();
        m_systemRegistered = true;
    }

    if (m_engine) {
        if (ScriptSystem* ss = m_engine->script->getScriptSystem()) {
            ss->registerScriptService("HomeLbmService", m_service.get());
        }
    }
    return OkStatus();
}

void HomeLbmPlugin::unloadRuntime() {
    if (m_systemRegistered && m_systemManager) {
        m_systemManager->requestRemoveSystem("HomeLbmSystem");
        m_systemManager->flushRequests();
        m_systemRegistered = false;
    }
}

Status HomeLbmPlugin::initEditorUi(Runtime::Engine* engine) {
    if (engine) {
        m_engine = engine;
    }
    tryBindImguiCallback();
    return OkStatus();
}

void HomeLbmPlugin::unloadEditorUi() {
    if (m_uiRegistered && m_engine) {
        if (auto imguiPlugin = m_engine->host->getPluginManager()->getPluginAs<ImguiPlugin>("ImguiPlugin")) {
            imguiPlugin->unregisterUiCallback("HomeLbmEditorUi");
        }
        m_uiRegistered = false;
    }
}

void HomeLbmPlugin::tryBindImguiCallback() {
    if (m_uiRegistered || !m_engine) {
        return;
    }
    auto* pm = m_engine->host->getPluginManager();
    ImguiPlugin* rawImgui = pm->getService<ImguiPlugin>();
    if (!rawImgui) {
        if (auto imguiPlugin = pm->getPluginAs<ImguiPlugin>("ImguiPlugin")) {
            rawImgui = imguiPlugin.get();
        }
    }
    if (!rawImgui) {
        return;
    }

    rawImgui->registerUiCallback("HomeLbmEditorUi", [this, rawImgui]() {
        if (!m_editorUi || !rawImgui) {
            return;
        }
        rawImgui->syncCallerContext();
        m_editorUi->render();
    });
    m_uiRegistered = true;
}

void HomeLbmPlugin::onLogicTick(float /*dt*/) {
    if (!m_uiRegistered && m_engine && m_engine->host->getPluginManager()->hasTag("Editor")) {
        tryBindImguiCallback();
    }
}

void HomeLbmPlugin::onRenderTick() {}

void HomeLbmPlugin::onUnload() {
    unloadEditorUi();
    unloadRuntime();

    if (m_componentReflection) {
        m_componentReflection->unregisterComponent("LbmDomainComponent");
        m_componentReflection->unregisterComponent("LbmEmitterComponent");
        m_componentReflection->unregisterComponent("LbmObstacleComponent");
    }

    m_editorUi.reset();
    if (m_service) {
        m_service->bindSystem(nullptr);
        m_service.reset();
    }
    if (m_system) {
        m_system->shutdown();
        m_system.reset();
    }
    m_loaded = false;
}

} // namespace MQEngine

extern "C" HOME_LBM_API MQEngine::Runtime::IPlugin* CreatePlugin() {
    return new MQEngine::HomeLbmPlugin();
}
