#include "thirdparty/thirdparty.h"
using namespace FCT;
#include "../../Engine/src/headers.h"

namespace MQEngine
{
    class EditorApplication : public Application
    {
    public:
        RenderConfig renderConfig() const override
        {
            return RenderConfig{
                .target = RenderTarget::Texture,
                .windowTitle = "MQEngine Editor",
            };
        }
    private:

    };
}
using namespace MQEngine;

int main() {
    EditorApplication application;
    EngineScope engineScope(&application);
    Engine& engine = getEngine();
    engine.loop();
    return 0;
}
