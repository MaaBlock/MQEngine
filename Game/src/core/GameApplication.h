#ifndef GAMEAPPLICATION_H
#define GAMEAPPLICATION_H
#include "../thirdparty/thirdparty.h"
#include <Engine/headers.h>
namespace MQEngine {
    class GameApplication : public Application {
    public:
        RenderConfig renderConfig() const override
        {
            return RenderConfig{
                .target = RenderTarget::Window,
                .windowTitle = "NexusEngine SandBox",
            };
        }

        void init() override;
        void logicTick() override;
    private:
        int m_frame = 0;
        bool m_isFirstFrame = true;
        entt::registry m_registry;
        entt::entity m_tempCameraEntity = entt::null;
    };
} // MQEngine

#endif //GAMEAPPLICATION_H
