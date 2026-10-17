#ifndef ANDROID_APPLICATION_H
#define ANDROID_APPLICATION_H

#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    class AndroidApplication : public Application
    {
    public:
        RenderConfig renderConfig() const override
        {
            return RenderConfig{
                .target = RenderTarget::Window,
                .windowTitle = "NexusEngine Android",
            };
        }

        void init() override;
        void logicTick() override;

    private:
        void setupFallbackCamera();
        void loadInitialScene();
        std::string resolveInitialSceneUuid() const;
        bool activateSceneCameras();

    private:
        int m_frame = 0;
        bool m_sceneLoaded = false;
        entt::registry m_registry;
        entt::entity m_tempCameraEntity = entt::null;
    };
}

#endif // ANDROID_APPLICATION_H
