#ifndef GAMEAPPLICATION_H
#define GAMEAPPLICATION_H

#include "../thirdparty/thirdparty.h"
#include <Engine/headers.h>

namespace MQEngine {

    /**
     * @brief 独立游戏发布宿主应用程序：极简信息与标签注入壳
     */
    class GameApplication : public Runtime::Application {
    public:
        void registerInfo() override
        {
            registerInfoImpl();
            applicationRegisterInfo = ApplicationRegisterInfo{
                .platformRequire = {
                    .target = RenderTarget::Window,
                    .windowTitle = "MQEngine Game",
                },
                .defaultProjectPath = "UserCode/Projects/DemoProject/project.mq",
                .tags = {"Runtime", "Game"},
            };
        }

        void init() override;
        void logicTick() override;
    };

} // namespace MQEngine

#endif // GAMEAPPLICATION_H
