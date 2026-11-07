#include "MainMenuBarUiModule.h"
#include <tinyfiledialogs.h>

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    void MainMenuBarUiModule::onRender(UiContext& ctx) {
        handleGlobalShortcuts(ctx);
        if (ImGui::BeginMainMenuBar()) {
            renderFileMenu(ctx);
            renderEditMenu(ctx);
            renderPluginHotReloadMenu(ctx);
            renderLayoutMenu(ctx);
            renderPlayControlMenuItem(ctx);
            ImGui::EndMainMenuBar();
        }
        renderDebugMemoryWindow(ctx);
    }

    void MainMenuBarUiModule::renderFileMenu(UiContext& ctx) {
        if (!ImGui::BeginMenu(TEXT("文件"))) {
            return;
        }
        if (ImGui::MenuItem(TEXT("保存项目设置"), "Ctrl+S")) {
            ctx.emit(SaveProjectSettingSignal{});
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT("导入 Godot 场景 (.tscn) / 脚本 (.gd)..."))) {
            const char* filterPatterns[] = { "*.tscn", "*.gd" };
            const char* selectedPath = tinyfd_openFileDialog(
                TEXT("选择 Godot 场景或 GDScript 脚本"),
                "",
                2,
                filterPatterns,
                TEXT("Godot 文件 (*.tscn, *.gd)"),
                0);
            if (selectedPath && selectedPath[0] != '\0') {
                ctx.emit(ImportGodotSceneSignal{ .filePath = selectedPath });
            }
        }
        if (ImGui::MenuItem(TEXT("导入内置 Godot 示例场景"))) {
            ctx.emit(ImportGodotSceneSignal{ .filePath = "./Assets/GodotSamples/sample_godot_scene.tscn" });
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT("退出"), "Alt+F4")) {
            ctx.emit(WindowCloseSaveSignal{});
        }
        ImGui::EndMenu();
    }

    void MainMenuBarUiModule::handleGlobalShortcuts(UiContext& ctx) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) {
            return;
        }

        if (io.KeyCtrl && !io.KeyAlt) {
            // 重做快捷键：Ctrl+Shift+Z 或 Ctrl+Y
            if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                ctx.emit(RedoCommandSignal{});
            } else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                ctx.emit(RedoCommandSignal{});
            }
            // 撤销快捷键：Ctrl+Z (非 Shift)
            else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                ctx.emit(UndoCommandSignal{});
            }
            // 一键热重载快捷键：Ctrl+R
            else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                ctx.emit(HotReloadAllSignal{});
            }
        }
    }

    void MainMenuBarUiModule::renderEditMenu(UiContext& ctx) {
        if (!ImGui::BeginMenu(TEXT("编辑"))) {
            return;
        }

        const auto& menuState = ctx.state<MainMenuViewState>();

        std::string undoLabel = (menuState.canUndo && !menuState.undoCommandName.empty())
            ? (std::string(TEXT("撤销 ")) + menuState.undoCommandName)
            : std::string(TEXT("撤销"));
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, menuState.canUndo)) {
            ctx.emit(UndoCommandSignal{});
        }

        std::string redoLabel = (menuState.canRedo && !menuState.redoCommandName.empty())
            ? (std::string(TEXT("重做 ")) + menuState.redoCommandName)
            : std::string(TEXT("重做"));
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, menuState.canRedo)) {
            ctx.emit(RedoCommandSignal{});
        }

        ImGui::EndMenu();
    }

    void MainMenuBarUiModule::renderPluginHotReloadMenu(UiContext& ctx) {
        if (!ImGui::BeginMenu(TEXT("插件与热更新"))) {
            return;
        }

        const auto& menuState = ctx.state<MainMenuViewState>();

        if (ImGui::MenuItem(TEXT("一键热重载全部 (UserCode / 插件 / 脚本)"), "Ctrl+R")) {
            ctx.emit(HotReloadAllSignal{});
        }

        if (ImGui::MenuItem(TEXT("热重载 UserCode (C++ DLL)"))) {
            ctx.emit(ReloadUserCodeSignal{ .moduleName = "UserCode" });
        }

        bool autoWatch = menuState.autoHotReloadEnabled;
        if (ImGui::MenuItem(TEXT("自动监控 DLL/脚本变更并热重载"), nullptr, autoWatch)) {
            ctx.emit(SetAutoHotReloadSignal{ .enabled = !autoWatch });
        }

        ImGui::Separator();

        if (ImGui::MenuItem(TEXT("热加载外部插件 (.dll / .ts / .js / .py)..."))) {
            const char* filterPatterns[] = { "*.dll", "*.ts", "*.js", "*.py", "plugin.mq", "plugin.json" };
            const char* selectedPath = tinyfd_openFileDialog(
                TEXT("选择要热加载的插件文件"),
                "",
                6,
                filterPatterns,
                TEXT("插件文件 (*.dll, *.ts, *.js, *.py, plugin.mq)"),
                0);
            if (selectedPath && selectedPath[0] != '\0') {
                ctx.emit(LoadPluginSignal{ .pluginIdent = selectedPath });
            }
        }

        if (!menuState.userCodes.empty()) {
            ImGui::Separator();
            for (const auto& uc : menuState.userCodes) {
                std::string label = "UserCode [" + uc.name + "] (" +
                    (uc.isLoaded ? std::string(TEXT("已加载")) : std::string(TEXT("未加载"))) +
                    ", 重载:" + std::to_string(uc.reloadCount) + ")";
                if (ImGui::BeginMenu(label.c_str())) {
                    if (ImGui::MenuItem(TEXT("热重载模块"))) {
                        ctx.emit(ReloadUserCodeSignal{ .moduleName = uc.name });
                    }
                    ImGui::EndMenu();
                }
            }
        }

        if (!menuState.plugins.empty()) {
            ImGui::Separator();
            for (const auto& plugin : menuState.plugins) {
                std::string label = std::string(TEXT("插件 [")) + plugin.name + "] (" +
                    (plugin.isLoaded ? std::string(TEXT("已激活")) : std::string(TEXT("已卸载"))) +
                    ", 重载:" + std::to_string(plugin.reloadCount) + ")";
                if (ImGui::BeginMenu(label.c_str())) {
                    if (ImGui::MenuItem(TEXT("热重载插件"))) {
                        ctx.emit(ReloadPluginSignal{ .pluginName = plugin.name });
                    }
                    if (plugin.isLoaded && plugin.name != "ImguiPlugin") {
                        if (ImGui::MenuItem(TEXT("热卸载插件"))) {
                            ctx.emit(UnloadPluginSignal{ .pluginName = plugin.name });
                        }
                    } else if (!plugin.isLoaded) {
                        if (ImGui::MenuItem(TEXT("重新加载插件"))) {
                            ctx.emit(LoadPluginSignal{ .pluginIdent = plugin.name });
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        }

        ImGui::EndMenu();
    }

    void MainMenuBarUiModule::renderLayoutMenu(UiContext& ctx) {
        if (!ImGui::BeginMenu(TEXT("布局"))) {
            return;
        }
        if (ImGui::MenuItem(TEXT("重置默认排版"))) {
            ctx.emit(RequestResetLayoutSignal{});
        }
        ImGui::EndMenu();
    }

    void MainMenuBarUiModule::renderPlayControlMenuItem(UiContext& ctx) {
        const auto& menuState = ctx.state<MainMenuViewState>();
        if (menuState.isEngineRunning) {
            if (ImGui::MenuItem(TEXT("停止运行"))) {
                ctx.emit(SetEngineRunningSignal{ .running = false });
            }
        } else {
            if (ImGui::MenuItem(TEXT("开始运行"))) {
                ctx.emit(SetEngineRunningSignal{ .running = true });
            }
        }
    }

    void MainMenuBarUiModule::renderDebugMemoryWindow(UiContext& ctx) {
#ifdef FCT_DEBUG
        if (ImGui::Begin(TEXT("编辑器内存分析"))) {
            if (ImGui::Button(TEXT("输出对象"))) {
                ctx.emit(DumpDebugObjectsSignal{});
            }
            if (ImGui::Button(TEXT("记录并输出距离上次记录新增"))) {
                ctx.emit(RecordDebugObjectDiffSignal{});
            }
        }
        ImGui::End();
#else
        (void)ctx;
#endif
    }

} // namespace MQEngine
