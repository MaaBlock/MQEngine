#ifndef MQENGINE_GAMEAPPLICATION_H
#define MQENGINE_GAMEAPPLICATION_H

#include <MQEngine/runtime/runtime.h>

namespace MQEngine
{
    /**
     * @brief Game 宿主应用纯壳：仅负责注入 Runtime 与 Game 标签，不包含业务逻辑
     */
    class GameApplication : public Runtime::Application
    {
    public:
        void registerInfo() override
        {
            registerInfoImpl();
            applicationRegisterInfo.platformRequire.target = RenderTarget::Window;
            applicationRegisterInfo.platformRequire.windowTitle = "MQEngine Game";
            applicationRegisterInfo.local = Local::zh_CN;
            applicationRegisterInfo.tags = {"Runtime", "Game"};
        }

        void init() override;
        void logicTick() override;
    };
} // namespace MQEngine

#endif // MQENGINE_GAMEAPPLICATION_H
