#include "Tiles3dPlugin.h"

namespace MQEngine {

using absl::FailedPreconditionError;

namespace {

class Tiles3dRuntimeComposite : public Runtime::Composite {
public:
    explicit Tiles3dRuntimeComposite(Tiles3dPlugin* owner) : m_owner(owner) {}

    void registerInfo() override {
        compositeRegisterInfo = Runtime::CompositeRegisterInfo{
            .name = "Tiles3dRuntimeComposite",
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
    Tiles3dPlugin* m_owner = nullptr;
};

} // namespace

Tiles3dPlugin::Tiles3dPlugin() {
    emplaceComposite<Tiles3dRuntimeComposite>(this);
}

Tiles3dPlugin::~Tiles3dPlugin() {
    onUnload();
}

Status Tiles3dPlugin::onLoad(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    m_wnd = m_engine ? m_engine->platform->getWindow() : nullptr;
    m_rhiContext = m_engine ? m_engine->platform->getContext() : nullptr;
    m_dataManager = m_engine ? m_engine->scene->getDataManager() : g_engineGlobal.dataManager;
    m_resourceLoader = m_engine ? m_engine->resource->getResourceLoader() : g_engineGlobal.resourceLoader;
    m_systemManager = m_engine ? m_engine->host->getSystemManager() : nullptr;

    if (!m_rhiContext || !m_dataManager) {
        return FailedPreconditionError("Tiles3dPlugin::onLoad: RHI Context 或 DataManager 为空");
    }

    if (m_engine) {
        if (auto* cr = m_engine->foundation->getComponentReflection()) {
            cr->registerComponent<CesiumGeoreference>("CesiumGeoreference");
            cr->registerComponent<Cesium3DTileset>("Cesium3DTileset");
            cr->registerComponent<CesiumTilePrimitiveComponent>("CesiumTilePrimitiveComponent");
        }
    }

    return OkStatus();
}

Status Tiles3dPlugin::onInit(Runtime::Engine* engine) {
    if (!m_rhiContext || !m_dataManager) {
        CHECK_STATUS(onLoad(engine));
    }
    return OkStatus();
}

Status Tiles3dPlugin::initRuntime(Runtime::Engine* engine) {
    if (!m_rhiContext || !m_dataManager) {
        CHECK_STATUS(onLoad(engine));
    }

    if (!m_tilesetSystem) {
        m_tilesetSystem = makeUnique<Cesium3DTilesetSystem>(
            m_rhiContext,
            m_dataManager,
            m_resourceLoader,
            m_wnd);
    }

    if (!m_systemRegistered && m_systemManager) {
        m_systemManager->requestAddSystem("Cesium3DTilesetSystem", m_tilesetSystem.get());
        m_systemManager->flushRequests();
        m_systemRegistered = true;
    }

    ScriptSystem* ss = m_engine ? m_engine->script->getScriptSystem() : g_engineGlobal.scriptSystem;
    if (ss) {
        ss->registerScriptService("CesiumGeoreferenceService", m_tilesetSystem.get());
        ss->addTypeScriptDeclaration(R"TS(export interface ScriptBehaviour {
    getPositionEus(trunkName?: string): Vec3;
    setPositionEus(eusPos: Vec3, trunkName?: string): boolean;
    getPositionEcef(trunkName?: string): Vec3;
    setPositionEcef(ecefPos: Vec3, trunkName?: string): boolean;
    getCartographic(trunkName?: string): Vec3;
    setCartographic(longitude: number, latitude: number, height: number, trunkName?: string): boolean;
})TS");
    }

    FCT::JS::NodeEnvironment* nodeEnv = ss ? ss->getNodeEnvironment() : nullptr;
    if (nodeEnv) {
        nodeEnv->excuteScript(R"JS(
            globalThis.__mqScriptBehaviourExtensions = globalThis.__mqScriptBehaviourExtensions || {};
            const geoExts = {
                getPositionEus(trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.CesiumGeoreferenceService) return this.getWorldPosition(trunkName);
                    return globalThis.MQEngine.CesiumGeoreferenceService.engineToEus(trunkName, this.getWorldPosition(trunkName));
                },
                setPositionEus(eusPos, trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.CesiumGeoreferenceService || !eusPos) return false;
                    const enginePos = globalThis.MQEngine.CesiumGeoreferenceService.eusToEngine(trunkName, eusPos);
                    return this.setWorldPosition(enginePos, trunkName);
                },
                getPositionEcef(trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.CesiumGeoreferenceService) return { x: 0, y: 0, z: 0 };
                    return globalThis.MQEngine.CesiumGeoreferenceService.engineToEcef(trunkName, this.getWorldPosition(trunkName));
                },
                setPositionEcef(ecefPos, trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.CesiumGeoreferenceService || !ecefPos) return false;
                    const enginePos = globalThis.MQEngine.CesiumGeoreferenceService.ecefToEngine(trunkName, ecefPos);
                    return this.setWorldPosition(enginePos, trunkName);
                },
                getCartographic(trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.CesiumGeoreferenceService) return { x: 0, y: 0, z: 0 };
                    return globalThis.MQEngine.CesiumGeoreferenceService.engineToCartographic(trunkName, this.getWorldPosition(trunkName));
                },
                setCartographic(longitude, latitude, height, trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.CesiumGeoreferenceService) return false;
                    const enginePos = globalThis.MQEngine.CesiumGeoreferenceService.cartographicToEngine(trunkName, longitude, latitude, height);
                    return this.setWorldPosition(enginePos, trunkName);
                }
            };
            Object.assign(globalThis.__mqScriptBehaviourExtensions, geoExts);
            if (globalThis.ScriptBehaviour && globalThis.ScriptBehaviour.prototype) {
                Object.assign(globalThis.ScriptBehaviour.prototype, geoExts);
            }
        )JS");
    }

    return OkStatus();
}

void Tiles3dPlugin::unloadRuntime() {
    if (m_systemRegistered && m_systemManager) {
        m_systemManager->requestRemoveSystem("Cesium3DTilesetSystem");
        m_systemManager->flushRequests();
        m_systemRegistered = false;
    }

    if (m_tilesetSystem) {
        m_tilesetSystem->shutdown();
        m_tilesetSystem.reset();
    }
}

void Tiles3dPlugin::onLogicTick(float /*dt*/) {
}

void Tiles3dPlugin::onRenderTick() {
}

void Tiles3dPlugin::onUnload() {
    unloadRuntime();

    if (m_engine) {
        if (auto* cr = m_engine->foundation->getComponentReflection()) {
            cr->unregisterComponent("CesiumGeoreference");
            cr->unregisterComponent("Cesium3DTileset");
            cr->unregisterComponent("CesiumTilePrimitiveComponent");
        }
    }
}

} // namespace MQEngine

extern "C" TILES3D_PLUGIN_API MQEngine::Runtime::IPlugin* CreatePlugin() {
    return new MQEngine::Tiles3dPlugin();
}
