/**
 * @file ImGuiVulkanBridge.cpp
 * @brief ImGui Vulkan 字体构建、现代深色主题样式、DPI 缩放与纹理描述符缓存桥接器实现
 */
#include "ImGuiVulkanBridge.h"

#include <algorithm>

namespace MQEngine
{
    namespace EditorBase
    {
        void ImGuiVulkanBridge::buildFontsAndEnableChinese(FCT::ImguiContext* imguiCtx)
        {
            if (!imguiCtx)
            {
                return;
            }
            imguiCtx->enableChinese();
            m_fontsInitialized = true;
        }

        void ImGuiVulkanBridge::updateDpiScaling(FCT::Window* window)
        {
            float targetScale = 1.0f;
            if (window)
            {
                const int width = window->getWidth();
                if (width >= 3840)
                {
                    targetScale = 1.25f;
                }
            }
            m_dpiScale = std::clamp(targetScale, 0.75f, 2.5f);
        }

        void ImGuiVulkanBridge::applyDarkTheme(float dpiScale)
        {
            m_dpiScale = std::clamp(dpiScale, 0.75f, 2.5f);

            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 5.0f;
            style.FrameRounding = 4.0f;
            style.PopupRounding = 4.0f;
            style.ScrollbarRounding = 4.0f;
            style.GrabRounding = 4.0f;
            style.TabRounding = 4.0f;

            ImVec4* colors = style.Colors;
            colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.30f, 0.38f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.26f, 0.33f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.32f, 0.40f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.28f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.28f, 0.34f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.30f, 0.36f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.24f, 0.29f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);
            colors[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.45f, 0.70f, 0.70f);
        }

        void ImGuiVulkanBridge::cacheTextureDescriptor(const std::string& textureName, void* descriptorHandle)
        {
            if (!textureName.empty())
            {
                m_textureDescriptorCache[textureName] = descriptorHandle;
            }
        }

        void* ImGuiVulkanBridge::getCachedTextureDescriptor(const std::string& textureName) const noexcept
        {
            auto it = m_textureDescriptorCache.find(textureName);
            return (it != m_textureDescriptorCache.end()) ? it->second : nullptr;
        }

        void ImGuiVulkanBridge::removeCachedTextureDescriptor(const std::string& textureName)
        {
            m_textureDescriptorCache.erase(textureName);
        }

        void ImGuiVulkanBridge::clearTextureDescriptorCache() noexcept
        {
            m_textureDescriptorCache.clear();
            m_fontsInitialized = false;
        }

    } // namespace EditorBase
} // namespace MQEngine
