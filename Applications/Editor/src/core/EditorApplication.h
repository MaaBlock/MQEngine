#ifndef MQENGINE_EDITORAPPLICATION_H
#define MQENGINE_EDITORAPPLICATION_H

#include <MQEngine/runtime/runtime.h>

namespace MQEngine
{
    /**
     * @brief Editor 宿主应用纯壳：仅负责注入 Runtime、Editor、ImGui 标签，不包含具体编辑器业务逻辑
     */
    class EditorApplication : public Runtime::Application
    {
    public:
        void registerInfo() override
        {
            registerInfoImpl();
            applicationRegisterInfo.platformRequire.target = RenderTarget::Texture;
            applicationRegisterInfo.platformRequire.windowTitle = "MQEngine Editor";
            applicationRegisterInfo.local = Local::zh_CN;
            applicationRegisterInfo.tags = {"Runtime", "Editor", "ImGui", "Cli", "Cook"};
        }

        void init() override;
        void logicTick() override;
    };
} // namespace MQEngine

#endif // MQENGINE_EDITORAPPLICATION_H
