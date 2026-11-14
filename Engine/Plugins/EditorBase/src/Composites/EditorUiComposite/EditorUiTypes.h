/**
 * @file EditorUiTypes.h
 * @brief 编辑器 UI 停靠槽位枚举、停靠窗口条目、菜单项条目、浮动通知与工作区预设类型定义
 */
#ifndef MQENGINE_EDITOR_BASE_EDITOR_UI_TYPES_H
#define MQENGINE_EDITOR_BASE_EDITOR_UI_TYPES_H

#include "UiSignals.h"
#include "ToastNotification.h"

#include <cstdint>
#include <functional>
#include <string>

namespace MQEngine
{
    namespace EditorBase
    {
        /**
         * @brief 编辑器主 DockSpace 标准停靠槽位枚举
         */
        enum class EditorDockSlot : uint8_t
        {
            Center = 0, // 主视口区域
            Left,       // 左侧层级大纲树
            Right,      // 右侧实体属性检视器
            Bottom,     // 底部资产浏览器 / 控制台日志 / 性能剖析
            Top         // 顶部工具栏槽位
        };

        /**
         * @brief 编辑器内置工作区布局预设枚举
         */
        enum class EditorWorkspacePreset : int32_t
        {
            Standard = 0,     // 标准默认工作区
            SceneFocus = 1,   // 场景专注工作区
            ShaderAndRdg = 2, // 着色器与管线工作区
            HtmlUiDesign = 3  // HTML UI 设计器工作区
        };

        /**
         * @brief 动态注册的 Dock 停靠面板描述条目
         */
        struct DockWindowEntry
        {
            std::string name;
            EditorDockSlot slot = EditorDockSlot::Center;
            std::function<void()> renderCallback;
            bool isOpen = true;
        };

        /**
         * @brief 动态注册的顶部主菜单栏命令条目
         */
        struct MenuActionEntry
        {
            std::string category;
            std::string item;
            std::string shortcut;
            std::function<void()> callback;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_EDITOR_UI_TYPES_H
