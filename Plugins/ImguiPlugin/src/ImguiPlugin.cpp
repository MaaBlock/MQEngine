#include "ImguiPlugin.h"
#include <spdlog/spdlog.h>
#include <algorithm>

using namespace FCT;

namespace MQEngine {

using absl::FailedPreconditionError;

ImguiPlugin::ImguiPlugin() = default;

ImguiPlugin::~ImguiPlugin() {
    onUnload();
}

Status ImguiPlugin::onLoad(PluginContext& ctx) {
    m_engine = ctx.engine ? ctx.engine : Engine::s_instance;
    m_wnd = ctx.wnd ? ctx.wnd : (m_engine ? m_engine->getWindow() : nullptr);
    m_rhiContext = ctx.rhiContext ? ctx.rhiContext : (m_engine ? m_engine->getContext() : nullptr);

    if (!m_wnd || !m_rhiContext) {
        return FailedPreconditionError("ImguiPlugin::onLoad: Window 或 RHI Context 为空");
    }

    if (!m_imguiCtx) {
        m_imguiCtx = m_imguiModule.createContext(m_wnd, m_rhiContext);
        m_imguiCtx->attachPass("ImguiPass");
        m_imguiCtx->create(ImguiContextCreateFlag::Docking);
    }

    if (!m_imnodesCtx) {
        m_imnodesCtx = ImNodes::CreateContext();
    }

    applyDefaultDarkTheme();
    ImNodes::StyleColorsDark();
    return OkStatus();
}

Status ImguiPlugin::onInit(PluginContext& ctx) {
    if (!m_imguiCtx) {
        CHECK_STATUS(onLoad(ctx));
    }

    if (!m_callbacksRegistered) {
        setupRenderCallbacks();
        m_callbacksRegistered = true;
    }
    return OkStatus();
}

void ImguiPlugin::setupRenderCallbacks() {
    if (!m_engine || !m_engine->getApplication()) {
        return;
    }

    auto& dispatcher = m_engine->getApplication()->renderCallBackDispatcher;

    dispatcher.subscribe<RenderCallBack::SettingUpPass>(
        [this](const RenderCallBack::SettingUpPass& event) {
            auto graph = event.graph;
            if (!graph || !m_wnd) {
                return;
            }
            graph->addPass(
                "ImguiPass",
                EnablePassClear(ClearType::color | ClearType::depthStencil, Vec4(m_clearColor)),
                Target("mainWindowColor", m_wnd),
                Texture("SceneColorTarget"),
                Texture("PosTarget"),
                Texture("RetTarget"),
                DepthStencil("mainWindowDS", m_wnd));
        });

    dispatcher.subscribe<RenderCallBack::KeepImage>(
        [this](const RenderCallBack::KeepImage& event) {
            if (m_imguiCtx) {
                m_imguiCtx->enableChinese();
            }
            for (const auto& cb : m_keepImageCallbacks) {
                if (cb) {
                    cb(event.graph);
                }
            }
        });

    dispatcher.subscribe<RenderCallBack::SettingSync>(
        [this](const RenderCallBack::SettingSync& event) {
            event.graph["syncImgui"] = {
                [this]() {
                    if (m_imguiCtx) {
                        m_imguiCtx->swapBuffer();
                    }
                },
                {},
                {}
            };
        });

    dispatcher.subscribe<RenderCallBack::SubscribePass>(
        [this](const RenderCallBack::SubscribePass& event) {
            if (!event.graph) {
                return;
            }
            event.graph->subscribe("ImguiPass", [this](PassSubmitEvent env) {
                if (m_imguiCtx) {
                    m_imguiCtx->submit(env.cmdBuf);
                }
            });
        });
}

void ImguiPlugin::onLogicTick(float dt) {
    (void)dt;
    if (!m_imguiCtx || m_uiCallbacks.empty()) {
        return;
    }

    m_imguiCtx->push([this]() {
        for (const auto& item : m_uiCallbacks) {
            if (item.callback) {
                item.callback();
            }
        }
    });
}

void ImguiPlugin::onRenderTick() {
}

void ImguiPlugin::onUnload() {
    m_uiCallbacks.clear();
    m_keepImageCallbacks.clear();
    if (m_imnodesCtx) {
        ImNodes::DestroyContext(m_imnodesCtx);
        m_imnodesCtx = nullptr;
    }
    m_imguiCtx = nullptr;
}

ImGuiContext* ImguiPlugin::getRawImGuiContext() const {
    return ImGui::GetCurrentContext();
}

void ImguiPlugin::getImGuiAllocators(ImGuiMemAllocFunc* outAllocFunc,
                                     ImGuiMemFreeFunc* outFreeFunc,
                                     void** outUserData) const {
    ImGui::GetAllocatorFunctions(outAllocFunc, outFreeFunc, outUserData);
}

void ImguiPlugin::registerUiCallback(const std::string& name, UiRenderCallback callback) {
    if (!callback) {
        return;
    }
    for (auto& item : m_uiCallbacks) {
        if (item.name == name) {
            item.callback = std::move(callback);
            return;
        }
    }
    m_uiCallbacks.push_back(NamedUiCallback{ name, std::move(callback) });
}

void ImguiPlugin::unregisterUiCallback(const std::string& name) {
    m_uiCallbacks.erase(
        std::remove_if(m_uiCallbacks.begin(), m_uiCallbacks.end(),
                       [&](const NamedUiCallback& item) { return item.name == name; }),
        m_uiCallbacks.end());
}

void ImguiPlugin::registerKeepImageCallback(KeepImageCallback callback) {
    if (callback) {
        m_keepImageCallbacks.push_back(std::move(callback));
    }
}

void ImguiPlugin::applyDefaultDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.07f, 0.07f, 0.07f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.25f, 0.88f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.07f, 0.07f, 0.07f, 0.75f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button]                 = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator]              = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    style.WindowPadding                     = ImVec2(6.00f, 4.00f);
    style.FramePadding                      = ImVec2(6.00f, 4.00f);
    style.CellPadding                       = ImVec2(6.00f, 6.00f);
    style.ItemSpacing                       = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing                  = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding                 = ImVec2(0.00f, 0.00f);
    style.IndentSpacing                     = 25;
    style.ScrollbarSize                     = 15;
    style.GrabMinSize                       = 10;
    style.WindowBorderSize                  = 1;
    style.ChildBorderSize                   = 1;
    style.PopupBorderSize                   = 1;
    style.FrameBorderSize                   = 1;
    style.TabBorderSize                     = 1;
    style.WindowRounding                    = 6.0f;
    style.ChildRounding                     = 6.0f;
    style.FrameRounding                     = 4.0f;
    style.PopupRounding                     = 4.0f;
    style.ScrollbarRounding                 = 9;
    style.GrabRounding                      = 4.0f;
    style.LogSliderDeadzone                 = 4;
    style.TabRounding                       = 4.0f;
}

} // namespace MQEngine

extern "C" IMGUI_PLUGIN_API MQEngine::IPlugin* CreatePlugin() {
    return new MQEngine::ImguiPlugin();
}
