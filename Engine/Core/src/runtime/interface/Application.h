#ifndef APPLICATION_H
#define APPLICATION_H
#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../../base/EngineCLIOption.h"
#include "../classdef.h"

namespace MQEngine {
    enum class RenderTarget {
        Window,     // 渲染到窗口
        Texture     // 渲染到纹理
    };
    struct PlatformRequire
    {
        RenderTarget target = RenderTarget::Window;
        const char* windowTitle = "MQEngine";
    };

    struct ApplicationRegisterInfo {
        PlatformRequire platformRequire;
        const char* defaultProjectPath = nullptr;
        Local local = Local::zh_CN;
        std::vector<std::string> tags;
        std::vector<CLIOptionDesc> cliOptions;
    };
    namespace RenderCallBack
    {
        struct SettingUpPass
        {
            RenderGraph* graph;
        };
        struct SubscribePass
        {
            RenderGraph* graph;
        };
        struct SettingSync
        {
            TokenGraph<std::string, FCT::SyncTicker>& graph;
        };
        struct KeepImage
        {
            RenderGraph* graph;
        };
        struct WindowClose
        {
        };
    }

    namespace Runtime {
        class ENGINE_API Application {
        public:
            virtual ~Application() = default;

            ApplicationRegisterInfo applicationRegisterInfo;

            virtual void registerInfo()
            {
                registerInfoImpl();
                /*
                 * 子类todo： 填写applicationRegisterInfo
                 */
            }
            virtual void init() = 0;
            /*
             * @brief 逻辑 tick
             */
            virtual void logicTick() = 0;
            FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly> renderCallBackDispatcher;
        protected:
            void registerInfoImpl();
            Engine* m_engine = nullptr;
        };
    } // namespace Runtime
}
#endif //APPLICATION_H
