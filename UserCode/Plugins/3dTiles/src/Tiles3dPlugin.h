#ifndef MQENGINE_TILES3D_PLUGIN_H
#define MQENGINE_TILES3D_PLUGIN_H

#include "thirdparty/thirdparty.h"
#include "CesiumComponents.h"
#include "Cesium3DTilesetSystem.h"

namespace MQEngine {

/**
 * @brief 3D Tiles 地理空间流式渲染插件，基于 Cesium Native 集成瓦片集调度与渲染
 */
class TILES3D_PLUGIN_API Tiles3dPlugin : public Runtime::IPlugin {
public:
    Tiles3dPlugin();
    ~Tiles3dPlugin() override;

    Status onLoad(Runtime::Engine* engine) override;
    Status onInit(Runtime::Engine* engine) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

    std::string getName() const override { return "3dTiles"; }
    std::string getVersion() const override { return "1.0.0"; }

    Cesium3DTilesetSystem* getTilesetSystem() const { return m_tilesetSystem.get(); }

    Status initRuntime(Runtime::Engine* engine);
    void unloadRuntime();

private:
    Runtime::Engine* m_engine = nullptr;
    FCT::Window* m_wnd = nullptr;
    FCT::Context* m_rhiContext = nullptr;
    DataManager* m_dataManager = nullptr;
    ResourceLoader* m_resourceLoader = nullptr;
    SystemManager* m_systemManager = nullptr;

    UniquePtr<Cesium3DTilesetSystem> m_tilesetSystem;
    bool m_systemRegistered = false;
};

} // namespace MQEngine

extern "C" TILES3D_PLUGIN_API MQEngine::Runtime::IPlugin* CreatePlugin();

#endif // MQENGINE_TILES3D_PLUGIN_H
