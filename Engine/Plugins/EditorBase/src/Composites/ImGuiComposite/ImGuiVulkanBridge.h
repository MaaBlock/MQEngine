/**
 * @file ImGuiVulkanBridge.h
 * @brief ImGui Vulkan 字体构建、现代深色主题样式、DPI 缩放与纹理描述符缓存桥接器
 */
#ifndef MQENGINE_EDITOR_BASE_IMGUI_VULKAN_BRIDGE_H
#define MQENGINE_EDITOR_BASE_IMGUI_VULKAN_BRIDGE_H

#include "../../EditorBaseApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <string>
#include <unordered_map>

namespace MQEngine
{
    namespace EditorBase
    {
        /**
         * @brief ImGui 与底层 Vulkan/RHI 上下文的视觉与资源桥接器：
         *        负责中文字体集启用、DPI 缩放计算、现代深色主题样式配置以及纹理描述符缓存
         */
        class IMGUI_COMPOSITE_API ImGuiVulkanBridge
        {
        public:
            ImGuiVulkanBridge() = default;
            ~ImGuiVulkanBridge() = default;

            ImGuiVulkanBridge(const ImGuiVulkanBridge&) = delete;
            ImGuiVulkanBridge& operator=(const ImGuiVulkanBridge&) = delete;

            /**
             * @brief 构建并启用中文字体图集
             */
            void buildFontsAndEnableChinese(FCT::ImguiContext* imguiCtx);

            /**
             * @brief 应用编辑器默认现代深色主题配色与圆角样式
             */
            void applyDarkTheme(float dpiScale = 1.0f);

            /**
             * @brief 根据宿主窗口尺寸更新 DPI 缩放系数并应用至 ImGuiStyle
             */
            void updateDpiScaling(FCT::Window* window);

            float getDpiScale() const noexcept { return m_dpiScale; }

            /**
             * @brief 缓存与查询命名纹理的 ImGui 描述符句柄
             */
            void cacheTextureDescriptor(const std::string& textureName, void* descriptorHandle);
            void* getCachedTextureDescriptor(const std::string& textureName) const noexcept;
            void removeCachedTextureDescriptor(const std::string& textureName);
            void clearTextureDescriptorCache() noexcept;

        private:
            float m_dpiScale = 1.0f;
            bool m_fontsInitialized = false;
            std::unordered_map<std::string, void*> m_textureDescriptorCache;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_IMGUI_VULKAN_BRIDGE_H
