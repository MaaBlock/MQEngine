/**
 * @file HtmlUiTypes.h
 * @brief HTML UI 设计器 DOM 节点结构、CSS 盒模型/Flex/Grid 布局结构与对齐辅助线定义
 */
#ifndef MQENGINE_GAME_HTML_UI_EDITOR_TYPES_H
#define MQENGINE_GAME_HTML_UI_EDITOR_TYPES_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace GameHtmlUI
    {
        /**
         * @brief 八向缩放控制手柄枚举
         */
        enum class ResizeHandle : uint8_t
        {
            None,
            TopLeft,
            Top,
            TopRight,
            Right,
            BottomRight,
            Bottom,
            BottomLeft,
            Left
        };

        /**
         * @brief CSS 标准盒模型（Margin / Border / Padding）
         */
        struct HtmlUiBoxModel
        {
            float marginTop = 0.0f;
            float marginRight = 0.0f;
            float marginBottom = 0.0f;
            float marginLeft = 0.0f;

            float borderTop = 0.0f;
            float borderRight = 0.0f;
            float borderBottom = 0.0f;
            float borderLeft = 0.0f;

            float paddingTop = 0.0f;
            float paddingRight = 0.0f;
            float paddingBottom = 0.0f;
            float paddingLeft = 0.0f;
        };

        /**
         * @brief CSS Flex / Grid 容器布局扩展属性
         */
        struct HtmlUiFlexGridLayout
        {
            std::string displayMode = "block";     // block, flex, inline-block
            std::string flexDirection = "column";  // row, column
            std::string justifyContent = "start";  // start, center, space-between
            std::string alignItems = "stretch";    // start, center, stretch
            float gap = 4.0f;
        };

        /**
         * @brief 编辑器端可视化 DOM 节点描述结构
         */
        struct HtmlUiElement
        {
            uint32_t id = 0;
            std::string tag = "div";
            std::string domId;
            bool emitDomIdAttr = false;
            std::string classes;
            std::string styleAttr;
            std::string srcAttr;
            std::string extraAttrs;
            std::string innerText;
            int depth = 0;
            uint32_t parentId = 0;
            std::vector<uint32_t> childIds;

            // 1280x720 逻辑设计画布绝对坐标与尺寸
            float x = 0.0f;
            float y = 0.0f;
            float width = 200.0f;
            float height = 32.0f;

            // CSS 定位与显式约束标记
            std::string positionMode = "static";
            std::string textAlign = "left";
            float fontSize = 13.0f;
            float cssLeft = 0.0f;
            float cssTop = 0.0f;
            float cssRight = 0.0f;
            float cssBottom = 0.0f;
            bool hasExplicitLeft = false;
            bool hasExplicitTop = false;
            bool hasExplicitRight = false;
            bool hasExplicitBottom = false;
            bool hasExplicitWidth = false;
            bool hasExplicitHeight = false;
            bool layoutModifiedByUser = false;

            // CSS 盒模型与弹性布局
            HtmlUiBoxModel boxModel;
            HtmlUiFlexGridLayout flexGrid;

            // 视觉样式
            ImU32 bgColor = IM_COL32(0, 0, 0, 0);
            ImU32 textColor = IM_COL32(234, 242, 255, 255);
            ImU32 borderColor = IM_COL32(88, 152, 235, 120);
            float borderRadius = 0.0f;
            bool isSelected = false;
        };

        /**
         * @brief 画布智能对齐吸附参考线
         */
        struct AlignmentGuide
        {
            bool isVertical = false;
            float position = 0.0f;
            float start = 0.0f;
            float end = 0.0f;
        };

        /**
         * @brief 撤销/重做命令历史快照结构
         */
        struct HtmlUiStateSnapshot
        {
            std::string commandName;
            std::vector<HtmlUiElement> elements;
        };

    } // namespace GameHtmlUI
} // namespace MQEngine

#endif // MQENGINE_GAME_HTML_UI_EDITOR_TYPES_H
