#ifndef MQENGINE_HOMELBM_PLUGIN_H
#define MQENGINE_HOMELBM_PLUGIN_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmComponents.h"
#include "HomeLbmSystem.h"
#include "HomeLbmService.h"
#include "HomeLbmEditorUi.h"

namespace MQEngine {

/**
 * @brief 自适应多级块结构 HOME-LBM & HOME-FREE 流体模拟与空间建模插件入口
 */
class HOME_LBM_API HomeLbmPlugin : public Runtime::IPlugin {
public:
    HomeLbmPlugin();
    ~HomeLbmPlugin() override;

    Status onLoad(Runtime::Engine* engine) override;
    Status onInit(Runtime::Engine* engine) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

    std::string getName() const override { return "HomeLBM"; }
    std::string getVersion() const override { return "1.0.0"; }

    HomeLbmSystem* getSystem() const noexcept { return m_system.get(); }
    HomeLbmService* getService() const noexcept { return m_service.get(); }
    HomeLbmEditorUi* getEditorUi() const noexcept { return m_editorUi.get(); }

    Status initRuntime(Runtime::Engine* engine);
    void unloadRuntime();
    Status initEditorUi(Runtime::Engine* engine);
    void unloadEditorUi();

private:
    void tryBindImguiCallback();

private:
    Runtime::Engine* m_engine = nullptr;
    FCT::Context* m_rhiContext = nullptr;
    DataManager* m_dataManager = nullptr;
    SystemManager* m_systemManager = nullptr;
    ComponentReflection* m_componentReflection = nullptr;

    UniquePtr<HomeLbmSystem> m_system;
    UniquePtr<HomeLbmService> m_service;
    UniquePtr<HomeLbmEditorUi> m_editorUi;

    bool m_loaded = false;
    bool m_systemRegistered = false;
    bool m_uiRegistered = false;
};

} // namespace MQEngine

extern "C" HOME_LBM_API MQEngine::Runtime::IPlugin* CreatePlugin();

#endif // MQENGINE_HOMELBM_PLUGIN_H
