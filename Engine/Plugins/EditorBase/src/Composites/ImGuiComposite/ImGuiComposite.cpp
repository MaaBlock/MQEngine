/**
 * @file ImGuiComposite.cpp
 * @brief ImGui 渲染底座与上下文生命周期 Composite 实现
 */
#include "ImGuiComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/host/core/Host.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/host/ui/UIHost.h>

#include <algorithm>

namespace MQEngine
{
    namespace EditorBase
    {
        ImGuiComposite::ImGuiComposite() = default;

        ImGuiComposite::~ImGuiComposite()
        {
            onUnload();
        }

        Status ImGuiComposite::onLoad(Runtime::Engine* engine)
        {
            ZoneScoped();
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* hostModule = Host::Host::InstancePtr();
            if (hostModule)
            {
                m_window = hostModule->uiHost.getWindow();
                m_rhiContext = hostModule->renderHost.getContext();
            }

            if (!m_imguiCtx && m_window && m_rhiContext)
            {
                m_imguiCtx = m_imguiModule.createContext(m_window, m_rhiContext);
                if (m_imguiCtx)
                {
                    m_imguiCtx->attachPass("ImguiPass");
                    m_imguiCtx->create(FCT::ImguiContextCreateFlag::Docking);
                }
            }

            if (ImGui::GetCurrentContext() != nullptr)
            {
                if (!m_imnodesCtx)
                {
                    m_imnodesCtx = ImNodes::CreateContext();
                    ImNodes::StyleColorsDark();
                }

                m_vulkanBridge.updateDpiScaling(m_window);
                applyDefaultDarkTheme();
            }
            return OkStatus();
        }

        Status ImGuiComposite::onInit(Runtime::Engine* engine)
        {
            ZoneScoped();
            CHECK_STATUS(Asset::Composite::onInit(engine));

            if (!m_callbacksRegistered)
            {
                setupRenderCallbacks();
                m_callbacksRegistered = true;
            }

            return OkStatus();
        }

        void ImGuiComposite::onTick(float deltaTime)
        {
            ZoneScoped();
            (void)deltaTime;

            if (m_imguiCtx)
            {
                // 将所有注册的 UI 回调打包投递至 FCT::ImguiContext 的绘制队列
                m_imguiCtx->push([this]() {
                    for (const auto& entry : m_uiCallbacks)
                    {
                        if (entry.callback)
                        {
                            entry.callback();
                        }
                    }
                });
            }
        }

        void ImGuiComposite::onUnload()
        {
            ZoneScoped();
            if (m_callbacksRegistered)
            {
                if (auto* domain = Domain::Domain::InstancePtr())
                {
                    domain->renderer.unregisterRenderGraphExtension("ImGuiComposite");
                }
                else if (m_engine)
                {
                    m_engine->domain.renderer.unregisterRenderGraphExtension("ImGuiComposite");
                }
                m_callbacksRegistered = false;
            }

            if (m_imnodesCtx)
            {
                ImNodes::DestroyContext(m_imnodesCtx);
                m_imnodesCtx = nullptr;
            }

            if (m_imguiCtx)
            {
                m_imguiCtx->unsubscribePassPipe();
                FCT_DELETE(m_imguiCtx);
                m_imguiCtx = nullptr;
            }

            // 彻底移除 FCT_IMGUI 写入宿主 Context syncTickers 的节点，避免 FreeLibrary 后残留跨 DLL 函数包装虚表
            FCT::Context* activeRhiCtx = nullptr;
            if (auto* hostModule = Host::Host::InstancePtr())
            {
                activeRhiCtx = hostModule->renderHost.getContext();
            }
            if (activeRhiCtx)
            {
                activeRhiCtx->syncTickers().removeNode(FCT::ImguiTicker::RecreateDescriptorSync);
                (void)activeRhiCtx->syncTickers().update();
            }

            m_vulkanBridge.clearTextureDescriptorCache();
            m_window = nullptr;
            m_rhiContext = nullptr;
            m_uiCallbacks.clear();
            m_keepImageCallbacks.clear();

            Asset::Composite::onUnload();
        }

        ImGuiContext* ImGuiComposite::getRawImGuiContext() const noexcept
        {
            return ImGui::GetCurrentContext();
        }

        void ImGuiComposite::getImGuiAllocators(
            ImGuiMemAllocFunc* outAllocFunc,
            ImGuiMemFreeFunc* outFreeFunc,
            void** outUserData) const
        {
            ImGui::GetAllocatorFunctions(outAllocFunc, outFreeFunc, outUserData);
        }

        void ImGuiComposite::registerUiCallback(const std::string& name, UiRenderCallback callback)
        {
            assert(!name.empty() && "UI 绘制回调名称不可为空");
            assert(callback != nullptr && "UI 绘制回调函数不可为空");

            auto it = std::find_if(m_uiCallbacks.begin(), m_uiCallbacks.end(),
                                   [&name](const NamedUiCallback& item) { return item.name == name; });
            if (it != m_uiCallbacks.end())
            {
                it->callback = std::move(callback);
            }
            else
            {
                m_uiCallbacks.push_back(NamedUiCallback{name, std::move(callback)});
            }
        }

        void ImGuiComposite::unregisterUiCallback(const std::string& name)
        {
            auto it = std::remove_if(m_uiCallbacks.begin(), m_uiCallbacks.end(),
                                     [&name](const NamedUiCallback& item) { return item.name == name; });
            m_uiCallbacks.erase(it, m_uiCallbacks.end());
        }

        void ImGuiComposite::registerKeepImageCallback(KeepImageCallback callback)
        {
            if (!callback)
            {
                return;
            }
            if (auto* domain = Domain::Domain::InstancePtr())
            {
                if (domain->renderer.isInitialized() && domain->renderer.getRenderGraph())
                {
                    callback(domain->renderer.getRenderGraph());
                }
            }
            m_keepImageCallbacks.push_back(std::move(callback));
        }

        void ImGuiComposite::setupRenderCallbacks()
        {
            Domain::Renderer::RendererSubsystem* renderer = nullptr;
            if (auto* domain = Domain::Domain::InstancePtr())
            {
                renderer = &domain->renderer;
            }
            else if (m_engine)
            {
                renderer = &m_engine->domain.renderer;
            }
            if (!renderer)
            {
                return;
            }

            Domain::Renderer::RenderGraphExtensionDesc ext;
            ext.name = "ImGuiComposite";

            ext.onSetupPass = [this](FCT::RenderGraph* graph) {
                if (!graph || !m_window)
                {
                    return;
                }
                graph->addPass(
                    "ImguiPass",
                    FCT::EnablePassClear(FCT::ClearType::color | FCT::ClearType::depthStencil, FCT::Vec4(m_clearColor)),
                    FCT::Target("mainWindowColor", m_window),
                    FCT::Texture("SceneColorTarget"),
                    FCT::Texture("PosTarget"),
                    FCT::Texture("RetTarget"),
                    FCT::DepthStencil("mainWindowDS", m_window));
            };

            ext.onPostCompile = [this](FCT::RenderGraph* graph) {
                m_vulkanBridge.buildFontsAndEnableChinese(m_imguiCtx);
                for (const auto& cb : m_keepImageCallbacks)
                {
                    if (cb)
                    {
                        cb(graph);
                    }
                }
            };

            ext.syncNodes.push_back(Domain::Renderer::RenderSyncNodeDesc{
                .name = "syncImgui",
                .callback = [this]() {
                    if (m_imguiCtx)
                    {
                        m_imguiCtx->swapBuffer();
                    }
                },
                .predecessors = {},
                .successors = {}
            });

            ext.passSubscribers.push_back(Domain::Renderer::NamedPassSubscription{
                .passName = "ImguiPass",
                .callback = [this](FCT::PassSubmitEvent env) {
                    if (m_imguiCtx)
                    {
                        m_imguiCtx->submit(env.cmdBuf);
                    }
                }
            });

            renderer->registerRenderGraphExtension(std::move(ext));
        }

        void ImGuiComposite::applyDefaultDarkTheme()
        {
            m_vulkanBridge.applyDarkTheme(m_vulkanBridge.getDpiScale());
        }

    } // namespace EditorBase
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::EditorBase::ImGuiComposite)
