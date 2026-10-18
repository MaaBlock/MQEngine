#ifndef MQENGINE_IMGUI_PLUGIN_H
#define MQENGINE_IMGUI_PLUGIN_H

#include "thirdparty/thirdparty.h"
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifdef BUILD_IMGUI_PLUGIN
        #define IMGUI_PLUGIN_API __declspec(dllexport)
    #else
        #define IMGUI_PLUGIN_API __declspec(dllimport)
    #endif
#else
    #define IMGUI_PLUGIN_API
#endif

namespace MQEngine {

/**
 * @brief 独立的 ImGui 底层渲染与上下文生命周期插件
 */
class IMGUI_PLUGIN_API ImguiPlugin : public IPlugin {
public:
    using UiRenderCallback = std::function<void()>;
    using KeepImageCallback = std::function<void(FCT::RenderGraph*)>;

    ImguiPlugin();
    ~ImguiPlugin() override;

    Status onLoad(PluginContext& ctx) override;
    Status onInit(PluginContext& ctx) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

    std::string getName() const override { return "ImguiPlugin"; }
    std::string getVersion() const override { return "1.0.0"; }

    virtual FCT::ImguiContext* getImguiContext() const { return m_imguiCtx; }
    virtual FCT::ImguiModule* getImguiModule() { return &m_imguiModule; }
    virtual ImGuiContext* getRawImGuiContext() const;
    virtual void getImGuiAllocators(ImGuiMemAllocFunc* outAllocFunc,
                                    ImGuiMemFreeFunc* outFreeFunc,
                                    void** outUserData) const;
    virtual ImNodesContext* getRawImNodesContext() const { return m_imnodesCtx; }

    /**
     * @brief 设置 ImguiPass 清屏颜色
     */
    virtual void setClearColor(const FCT::Vec4& clearColor) { m_clearColor = clearColor; }

    /**
     * @brief 注册每帧 UI 绘制回调
     */
    virtual void registerUiCallback(const std::string& name, UiRenderCallback callback);

    /**
     * @brief 注销指定名称的 UI 绘制回调
     */
    virtual void unregisterUiCallback(const std::string& name);

    /**
     * @brief 注册 RenderGraph 编译完成后的 KeepImage 回调（用于采样渲染目标贴图等）
     */
    virtual void registerKeepImageCallback(KeepImageCallback callback);

    /**
     * @brief 应用默认深色主题样式
     */
    virtual void applyDefaultDarkTheme();

    /**
     * @brief 跨 DLL 边界同步调用方模块内的静态 ImGuiContext、内存分配器与 ImNodesContext
     */
    template <typename = void>
    void syncCallerContext() const {
        ImGuiMemAllocFunc allocFn = nullptr;
        ImGuiMemFreeFunc freeFn = nullptr;
        void* userData = nullptr;
        getImGuiAllocators(&allocFn, &freeFn, &userData);
        if (allocFn && freeFn) {
            ImGui::SetAllocatorFunctions(allocFn, freeFn, userData);
        }
        if (ImGuiContext* rawCtx = getRawImGuiContext()) {
            ImGui::SetCurrentContext(rawCtx);
        }
        if (ImNodesContext* nodesCtx = getRawImNodesContext()) {
            ImNodes::SetCurrentContext(nodesCtx);
            if (ImGuiContext* rawCtx = getRawImGuiContext()) {
                ImNodes::SetImGuiContext(rawCtx);
            }
        }
    }

private:
    struct NamedUiCallback {
        std::string name;
        UiRenderCallback callback;
    };

    void setupRenderCallbacks();

private:
    FCT::ImguiModule m_imguiModule;
    FCT::ImguiContext* m_imguiCtx = nullptr;
    ImNodesContext* m_imnodesCtx = nullptr;
    FCT::Window* m_wnd = nullptr;
    FCT::Context* m_rhiContext = nullptr;
    Engine* m_engine = nullptr;
    FCT::Vec4 m_clearColor = FCT::Vec4(0.06f, 0.06f, 0.06f, 1.0f);
    std::vector<NamedUiCallback> m_uiCallbacks;
    std::vector<KeepImageCallback> m_keepImageCallbacks;
    bool m_callbacksRegistered = false;
};

} // namespace MQEngine

extern "C" IMGUI_PLUGIN_API MQEngine::IPlugin* CreatePlugin();

#endif // MQENGINE_IMGUI_PLUGIN_H
