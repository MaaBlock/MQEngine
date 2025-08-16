//
// Created by Administrator on 2025/8/16.
//

#ifndef EDITORAPPLICATION_H
#define EDITORAPPLICATION_H
#include "../thirdparty/thirdparty.h"
#include <Engine/headers.h>
namespace MQEngine
{
    class EditorApplication : public Application
    {
    public:
        EditorApplication()
        {
            initRenderCallBack();
        }
        RenderConfig renderConfig() const override
        {
            return RenderConfig{
                .target = RenderTarget::Texture,
                .windowTitle = "MQEngine Editor",
            };
        }
        void initRenderCallBack();
    private:

    };
}


#endif //EDITORAPPLICATION_H
