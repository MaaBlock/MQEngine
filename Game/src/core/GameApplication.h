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
                .windowTitle = "MQEngine SandBox",
            };
        }

        void init() override;
        void logicTick() override;
    };
} // MQEngine

#endif //GAMEAPPLICATION_H
