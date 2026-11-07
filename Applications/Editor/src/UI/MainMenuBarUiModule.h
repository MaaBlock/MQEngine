#ifndef EDITOR_MAIN_MENU_BAR_UI_MODULE_H
#define EDITOR_MAIN_MENU_BAR_UI_MODULE_H

#include "IUiModule.h"

namespace MQEngine {

    /**
     * @brief 主菜单栏与调试工具UI模块，仅负责顶栏菜单绘制与操作信号发射
     */
    class MainMenuBarUiModule : public IUiModule {
    public:
        MainMenuBarUiModule() = default;
        ~MainMenuBarUiModule() override = default;

        std::string_view getModuleName() const override {
            return "MainMenuBarUiModule";
        }

        void onRender(UiContext& ctx) override;

    private:
        void renderFileMenu(UiContext& ctx);
        void renderEditMenu(UiContext& ctx);
        void renderPluginHotReloadMenu(UiContext& ctx);
        void renderLayoutMenu(UiContext& ctx);
        void renderPlayControlMenuItem(UiContext& ctx);
        void renderDebugMemoryWindow(UiContext& ctx);
        void handleGlobalShortcuts(UiContext& ctx);
    };

} // namespace MQEngine

#endif // EDITOR_MAIN_MENU_BAR_UI_MODULE_H
