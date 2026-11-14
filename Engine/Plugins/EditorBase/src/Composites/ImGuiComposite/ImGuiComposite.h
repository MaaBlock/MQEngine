/**
 * @file ImGuiComposite.h
 * @brief ImGui 渲染底座与上下文生命周期 Composite
 */
#ifndef MQENGINE_EDITOR_BASE_IMGUI_COMPOSITE_H
#define MQENGINE_EDITOR_BASE_IMGUI_COMPOSITE_H

#include "../../EditorBaseApi.h"
#include "ImGuiVulkanBridge.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>
#include <imnodes.h>

#include <functional>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        /**
         * @brief ImGui 底层渲染与多后端对接 Composite：
         *        负责初始化 FCT::ImguiModule、配置 Docking 与中文字体、ImNodes 上下文以及跨 DLL 边界上下文同步
         */
        class IMGUI_COMPOSITE_API ImGuiComposite : public Asset::Composite
        {
        public:
            using UiRenderCallback = std::function<void()>;
            using KeepImageCallback = std::function<void(FCT::RenderGraph*)>;

            ImGuiComposite();
            ~ImGuiComposite() override;

            ImGuiComposite(const ImGuiComposite&) = delete;
            ImGuiComposite& operator=(const ImGuiComposite&) = delete;

            // ==================== Composite 接口实现 ====================

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "ImGuiComposite"; }
            std::string getPluginName() const override { return "EditorBase"; }
            std::vector<std::string> getDependencies() const override { return {}; }
            std::vector<std::string> getRequiredTags() const override { return {"Runtime", "ImGui"}; }

            // ==================== 上下文与跨 DLL 同步 ====================

            FCT::ImguiContext* getImguiContext() const noexcept { return m_imguiCtx; }
            ImGuiContext* getRawImGuiContext() const noexcept;
            ImNodesContext* getRawImNodesContext() const noexcept { return m_imnodesCtx; }
            ImGuiVulkanBridge& getVulkanBridge() noexcept { return m_vulkanBridge; }
            const ImGuiVulkanBridge& getVulkanBridge() const noexcept { return m_vulkanBridge; }

            void getImGuiAllocators(
                ImGuiMemAllocFunc* outAllocFunc,
                ImGuiMemFreeFunc* outFreeFunc,
                void** outUserData) const;

            /**
             * @brief 跨 DLL 边界同步调用方模块内的静态 ImGuiContext、内存分配器与 ImNodesContext
             */
            template <typename = void>
            void syncCallerContext() const
            {
                ImGuiMemAllocFunc allocFn = nullptr;
                ImGuiMemFreeFunc freeFn = nullptr;
                void* userData = nullptr;
                getImGuiAllocators(&allocFn, &freeFn, &userData);
                if (allocFn && freeFn)
                {
                    ImGui::SetAllocatorFunctions(allocFn, freeFn, userData);
                }
                if (ImGuiContext* rawCtx = getRawImGuiContext())
                {
                    ImGui::SetCurrentContext(rawCtx);
                }
                if (ImNodesContext* nodesCtx = getRawImNodesContext())
                {
                    ImNodes::SetCurrentContext(nodesCtx);
                    if (ImGuiContext* rawCtx = getRawImGuiContext())
                    {
                        ImNodes::SetImGuiContext(rawCtx);
                    }
                }
            }

            // ==================== 回调与排版驱动 ====================

            void registerUiCallback(const std::string& name, UiRenderCallback callback);
            void unregisterUiCallback(const std::string& name);
            void registerKeepImageCallback(KeepImageCallback callback);

            void applyDefaultDarkTheme();
            void setClearColor(const FCT::Vec4& clearColor) { m_clearColor = clearColor; }

        private:
            struct NamedUiCallback
            {
                std::string name;
                UiRenderCallback callback;
            };

            void setupRenderCallbacks();

        private:
            FCT::ImguiModule m_imguiModule;
            FCT::ImguiContext* m_imguiCtx = nullptr;
            ImNodesContext* m_imnodesCtx = nullptr;
            FCT::Window* m_window = nullptr;
            FCT::Context* m_rhiContext = nullptr;
            ImGuiVulkanBridge m_vulkanBridge;

            FCT::Vec4 m_clearColor = FCT::Vec4(0.06f, 0.06f, 0.06f, 1.0f);
            std::vector<NamedUiCallback> m_uiCallbacks;
            std::vector<KeepImageCallback> m_keepImageCallbacks;
            bool m_callbacksRegistered = false;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_IMGUI_COMPOSITE_H
