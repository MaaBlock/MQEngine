#include "UserCode.h"

namespace MQEngine {

UserRotateSystem::UserRotateSystem(DataManager* dataManager)
    : m_dataManager(dataManager) {
}

void UserRotateSystem::updateLogic() {
    if (!m_dataManager || !g_engineGlobal.isRunning) {
        return;
    }

    auto registries = m_dataManager->currentRegistries();
    for (auto* registry : registries) {
        if (!registry) {
            continue;
        }
        auto view = registry->view<UserRotateComponent, RotationComponent>();
        for (auto entity : view) {
            const auto& rotateComp = view.get<UserRotateComponent>(entity);
            if (!rotateComp.enabled) {
                continue;
            }
            auto& rot = view.get<RotationComponent>(entity);
            rot.rotation.x += rotateComp.axis.x * rotateComp.speed * m_deltaTime;
            rot.rotation.y += rotateComp.axis.y * rotateComp.speed * m_deltaTime;
            rot.rotation.z += rotateComp.axis.z * rotateComp.speed * m_deltaTime;
        }
    }
}

void UserRotateSystem::updateRender() {
}

namespace {

class UserRotateComposite : public Runtime::Composite {
public:
    explicit UserRotateComposite(UserCodeModule* owner) : m_owner(owner) {}

    void registerInfo() override {
        compositeRegisterInfo = Runtime::CompositeRegisterInfo{
            .name = "UserRotateComposite",
            .requiredTags = {"Runtime"},
        };
    }

    Status onInit(Runtime::Engine* engine) override {
        return m_owner ? m_owner->initRuntime(engine) : OkStatus();
    }

    void onLogicTick(float dt) override {
        if (m_owner) {
            m_owner->tickRuntime(dt);
        }
    }

    void onUnload() override {
        if (m_owner) {
            m_owner->unloadRuntime();
        }
    }

private:
    UserCodeModule* m_owner = nullptr;
};

} // namespace

UserCodeModule::UserCodeModule() {
    emplaceComposite<UserRotateComposite>(this);
}

UserCodeModule::~UserCodeModule() {
    onUnload();
}

Status UserCodeModule::onLoad(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();

    if (m_engine) {
        if (auto* cr = m_engine->foundation->getComponentReflection()) {
            cr->registerComponent<UserRotateComponent>();
        }
    }

    spdlog::info("UserCodeModule::onLoad 完成");
    return OkStatus();
}

Status UserCodeModule::onInit(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();

    if (m_engine) {
        auto imguiPlugin = m_engine->host->getPluginManager()->getPluginAs<ImguiPlugin>("ImguiPlugin");
        if (imguiPlugin) {
            spdlog::info("UserCodeModule::onInit 已连接底层插件 ImguiPlugin (v{})", imguiPlugin->getVersion());
        }
    }

    return OkStatus();
}

Status UserCodeModule::initRuntime(Runtime::Engine* engine) {
    if (engine) {
        m_engine = engine;
    }
    if (m_engine && !m_systemRegistered) {
        DataManager* dm = m_engine->scene->getDataManager();
        SystemManager* sm = m_engine->host->getSystemManager();
        if (dm && sm) {
            m_rotateSystem = makeUnique<UserRotateSystem>(dm);
            sm->requestAddSystem("UserRotateSystem", m_rotateSystem.get(), {}, {"MatrixCacheSystem"});
            sm->flushRequests();
            m_systemRegistered = true;
        }
    }
    return OkStatus();
}

void UserCodeModule::tickRuntime(float dt) {
    if (m_rotateSystem) {
        m_rotateSystem->setDeltaTime(dt);
    }
}

void UserCodeModule::unloadRuntime() {
    if (m_systemRegistered && m_engine) {
        if (auto* sm = m_engine->host->getSystemManager()) {
            sm->requestRemoveSystem("UserRotateSystem");
            sm->flushRequests();
        }
        m_systemRegistered = false;
    }
    m_rotateSystem.reset();
}

void UserCodeModule::onLogicTick(float /*dt*/) {
}

void UserCodeModule::onRenderTick() {
}

void UserCodeModule::onUnload() {
    unloadRuntime();
    if (m_engine) {
        if (auto* cr = m_engine->foundation->getComponentReflection()) {
            cr->unregisterComponent("UserRotateComponent");
        }
    }
}

} // namespace MQEngine

extern "C" USER_CODE_API MQEngine::Runtime::NativeCode* CreateUserCode() {
    return new MQEngine::UserCodeModule();
}
