#ifndef MQENGINE_RUNTIME_INTERFACE_APPLICATION_H
#define MQENGINE_RUNTIME_INTERFACE_APPLICATION_H

#include <string>
#include <vector>
#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/runtime/classdef.h>

namespace MQEngine
{
    enum class RenderTarget
    {
        Window,     // 渲染到窗口
        Texture     // 渲染到离屏纹理
    };

    struct PlatformRequire
    {
        RenderTarget target = RenderTarget::Window;
        const char* windowTitle = "MQEngine";
    };

    /**
     * @brief 宿主应用注册信息（纯粹的信息与 Tags 注入契约，不包含 isEditor / loadUserCode 或解析回调）
     */
    struct ApplicationRegisterInfo
    {
        PlatformRequire platformRequire;
        const char* defaultProjectPath = nullptr;
        Local local = Local::zh_CN;
        std::vector<std::string> tags;
    };

    namespace Runtime
    {
        class ENGINE_API Application
        {
        public:
            virtual ~Application() = default;

            ApplicationRegisterInfo applicationRegisterInfo;

            virtual void registerInfo()
            {
                registerInfoImpl();
            }

            virtual void init() {}
            virtual void logicTick() {}

        protected:
            void registerInfoImpl();
            Engine* m_engine = nullptr;
        };
    } // namespace Runtime
} // namespace MQEngine

#endif // MQENGINE_RUNTIME_INTERFACE_APPLICATION_H
