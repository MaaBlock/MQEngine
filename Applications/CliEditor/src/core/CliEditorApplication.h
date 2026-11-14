/**
 * @file CliEditorApplication.h
 * @brief CliEditor 宿主应用纯壳：仅负责注入 Runtime 与 CliEditor 标签，具体渐进式 CLI 编辑器逻辑全部位于 Engine/Plugins/CliEditor 插件
 */
#ifndef MQENGINE_CLIEDITORAPPLICATION_H
#define MQENGINE_CLIEDITORAPPLICATION_H

#include <MQEngine/runtime/runtime.h>

namespace MQEngine
{
    class CliEditorApplication : public Runtime::Application
    {
    public:
        void registerInfo() override
        {
            registerInfoImpl();
            applicationRegisterInfo.platformRequire.target = RenderTarget::Texture;
            applicationRegisterInfo.platformRequire.windowTitle = "MQEngine CliEditor";
            applicationRegisterInfo.local = Local::zh_CN;
            applicationRegisterInfo.tags = {"Runtime", "CliEditor", "Cli", "Cook"};
        }

        void init() override;
        void logicTick() override;
    };
} // namespace MQEngine

#endif // MQENGINE_CLIEDITORAPPLICATION_H
