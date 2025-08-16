#ifndef APPLICATION_H
#define APPLICATION_H
namespace MQEngine {
    enum class RenderTarget {
        Window,     // 渲染到窗口
        Texture     // 渲染到纹理
    };

    struct RenderConfig {
        RenderTarget target = RenderTarget::Window;
        const char* windowTitle;
    };

    class Application {
    public:
        virtual RenderConfig renderConfig() const = 0;
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly> renderCallBackDispatcher;
    };
}
#endif //APPLICATION_H
