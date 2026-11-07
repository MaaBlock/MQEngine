#ifndef EDITORAPPLICATION_H
#define EDITORAPPLICATION_H
#include "../thirdparty/thirdparty.h"
#include <Engine/headers.h>
#include "../imgui/UiManager.h"
#include "./Global.h"
namespace MQEngine
{

    class UndoManager;

    class EditorApplication : public Runtime::Application
    {
    public:
        EditorApplication();
        ~EditorApplication() override;
        void registerInfo() override
        {
            registerInfoImpl();
            applicationRegisterInfo = ApplicationRegisterInfo{
                .platformRequire = {
                    .target = RenderTarget::Texture,
                    .windowTitle = "NexusEngine Editor",
                },
                .defaultProjectPath = "UserCode/Projects/DemoProject/project.mq",
                .tags = {"Runtime", "Editor", "ImGui"},
            };
        }
        void init() override;
        void settingRenderCallBack();
        void logicTick() override;
        void settingUpImgui();
        void imguiLogicTick();
    private:
        FCT::ImguiContext* m_imguiCtx;
        FCT::ImguiModule imguiModule;
        UiManager uiManager;
        entt::registry* m_registry;
        UniquePtr<UndoManager> m_undoManager;
    };
}


#endif //EDITORAPPLICATION_H
