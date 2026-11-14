/**
 * @file BlueprintEditorTypes.h
 * @brief 编辑器端蓝图节点、连线与快照 Undo/Redo 数据结构定义
 */
#ifndef MQENGINE_BLUEPRINT_EDITOR_TYPES_H
#define MQENGINE_BLUEPRINT_EDITOR_TYPES_H

#include "../../BlueprintApi.h"
#include "../BlueprintRuntimeComposite/BlueprintRuntimeTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <string>
#include <vector>

namespace MQEngine
{
    namespace Blueprint
    {
        /**
         * @brief 编辑器端蓝图节点扩展定义
         */
        struct EditorNode
        {
            uint32_t id = 0;
            std::string title;
            std::string opCode;
            FCT::Vec2 pos{100.0f, 100.0f};
            FCT::Vec2 size{180.0f, 80.0f};
            std::vector<RuntimePin> inputs;
            std::vector<RuntimePin> outputs;
            bool isSelected = false;
        };

        /**
         * @brief 编辑器端蓝图引脚连线定义
         */
        struct EditorLink
        {
            uint32_t id = 0;
            uint32_t startPinId = 0;
            uint32_t endPinId = 0;
            bool isSelected = false;
        };

        /**
         * @brief 蓝图图表状态快照（用于 Undo/Redo 恢复）
         */
        struct GraphSnapshot
        {
            std::vector<EditorNode> nodes;
            std::vector<EditorLink> links;
        };

    } // namespace Blueprint
} // namespace MQEngine

#endif // MQENGINE_BLUEPRINT_EDITOR_TYPES_H
