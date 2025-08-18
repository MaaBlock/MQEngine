//
// Created by Administrator on 2025/8/16.
//

#ifndef EDITORAPPLICATION_H
#define EDITORAPPLICATION_H
#include "../thirdparty/thirdparty.h"
#include <Engine/headers.h>
#include "../imgui/UiManager.h"
#include "./Global.h"
namespace MQEngine
{

    class EditorApplication : public Application
    {
    public:
        EditorApplication()
        {
        }
        RenderConfig renderConfig() const override
        {
            return RenderConfig{
                .target = RenderTarget::Texture,
                .windowTitle = "MQEngine Editor",
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
    };
}


#endif //EDITORAPPLICATION_H
