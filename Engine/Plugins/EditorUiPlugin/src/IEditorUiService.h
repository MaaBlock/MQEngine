#ifndef MQENGINE_IEDITOR_UI_SERVICE_H
#define MQENGINE_IEDITOR_UI_SERVICE_H

#include "EditorUiPluginApi.h"
#include "thirdparty/thirdparty.h"
#include <functional>
#include <string>
#include <vector>

namespace MQEngine {

/**
 * @brief 编辑器主 DockSpace 标准停靠槽位枚举
 */
enum class EditorDockSlot {
    Center,
    Left,
    LeftBottom,
    Right,
    RightBottom,
    BottomLeft,
    BottomRight
};

/**
 * @brief 动态停靠窗口注册描述项
 */
struct EditorDockWindowDesc {
    std::string windowTitle;
    EditorDockSlot slot = EditorDockSlot::Center;
    bool focusOnReset = false;
    std::string ownerComposite;
};

/**
 * @brief 顶栏主菜单动态扩展动作描述项
 */
struct EditorMenuActionDesc {
    std::string menuCategory;
    std::string itemLabel;
    std::string shortcut;
    std::function<void()> callback;
    std::string ownerComposite;
};

/**
 * @brief 编辑器 UI 基础服务接口（由 EditorUiPlugin 注册至 PluginContext，供下游编辑器插件线调用）
 */
class EDITOR_UI_PLUGIN_API IEditorUiService {
public:
    using PanelRenderCallback = std::function<void()>;

    virtual ~IEditorUiService() = default;

    /**
     * @brief 获取底层 ImguiPlugin 实例
     */
    virtual ImguiPlugin* getImguiPlugin() const = 0;

    /**
     * @brief 获取 FCT::ImguiContext 指针
     */
    virtual FCT::ImguiContext* getImguiContext() const = 0;

    /**
     * @brief 向编辑器主 DockSpace 注册窗口停靠槽位（重置布局或首次启动时自动停靠到指定区域）
     */
    virtual void registerDockWindow(const std::string& windowTitle,
                                    EditorDockSlot slot,
                                    bool focusOnReset = false,
                                    const std::string& ownerComposite = "") = 0;

    /**
     * @brief 向编辑器顶栏主菜单动态注册菜单项
     */
    virtual void registerMenuAction(const std::string& menuCategory,
                                    const std::string& itemLabel,
                                    const std::string& shortcut,
                                    std::function<void()> callback,
                                    const std::string& ownerComposite = "") = 0;

    /**
     * @brief 注册编辑器面板每帧绘制回调
     */
    virtual void registerPanelCallback(const std::string& panelName,
                                       PanelRenderCallback callback,
                                       const std::string& ownerComposite = "") = 0;

    /**
     * @brief 注销指定面板绘制回调
     */
    virtual void unregisterPanelCallback(const std::string& panelName) = 0;

    /**
     * @brief 按归属 Composite 名称一键解绑其注册的所有停靠窗口、菜单项与面板回调（支持下游插件安全热重载）
     */
    virtual void unregisterAllByOwner(const std::string& ownerComposite) = 0;

    /**
     * @brief 请求在下一帧重置编辑器 DockSpace 布局
     */
    virtual void requestResetLayout() = 0;

    /**
     * @brief 创建并更新编辑器主 DockSpace（若需要重置布局则自动调用 applyDockSpaceLayout(ImGuiID)）
     */
    virtual void applyDockSpaceLayout(const char* dockspaceName = "MQEngine") = 0;

    /**
     * @brief 应用指定 DockSpace 节点的默认或动态注册布局
     */
    virtual void applyDockSpaceLayout(ImGuiID dockspaceId) = 0;

    /**
     * @brief 在重置布局后按注册顺序聚焦默认 Tab 并保存 Ini 配置
     */
    virtual void applyDefaultTabFocusIfNeeded() = 0;

    /**
     * @brief 保存当前活动场景
     */
    virtual Status saveActiveScene() = 0;

    /**
     * @brief 保存当前工程配置（project.mq 与 project.setting）
     */
    virtual Status saveProjectSettings() = 0;

    /**
     * @brief 获取所有已注册的动态停靠窗口描述
     */
    virtual const std::vector<EditorDockWindowDesc>& getRegisteredDockWindows() const = 0;

    /**
     * @brief 获取所有已注册的扩展菜单动作
     */
    virtual const std::vector<EditorMenuActionDesc>& getRegisteredMenuActions() const = 0;
};

} // namespace MQEngine

#endif // MQENGINE_IEDITOR_UI_SERVICE_H
