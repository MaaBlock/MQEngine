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
    namespace RenderCallBack
    {
        struct SettingUpPass
        {
            FCT::RenderGraph* graph;
        };
        struct SubscribePass
        {
            FCT::RenderGraph* graph;
        };
        struct SettingSync
        {
            FCT::TokenGraph<std::string, FCT::SyncTicker>& graph;
        };
        struct KeepImage
        {
            FCT::RenderGraph* graph;
        };
    }

    class Application {
    public:
        struct Global
        {
            FCT::Context* ctx;
            FCT::Window* wnd;
        } global;
        virtual RenderConfig renderConfig() const = 0;
        virtual void logicTicker() = 0;
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly> renderCallBackDispatcher;
    };
}
#endif //APPLICATION_H
