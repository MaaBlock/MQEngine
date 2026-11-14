/**
 * @file BlueprintRuntimeTypes.h
 * @brief 蓝图运行时引脚变体值、节点描述符、连线与运行时执行图数据结构定义
 */
#ifndef MQENGINE_BLUEPRINT_RUNTIME_TYPES_H
#define MQENGINE_BLUEPRINT_RUNTIME_TYPES_H

#include "../../BlueprintApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <cstdint>

namespace MQEngine
{
    namespace Blueprint
    {
        /**
         * @brief 蓝图引脚数据类型枚举
         */
        enum class PinDataType : uint8_t
        {
            Execution,
            Bool,
            Int,
            Float,
            String,
            Vector3,
            Entity
        };

        /**
         * @brief 蓝图引脚多态变体值封装
         */
        struct PinValue
        {
            PinDataType type = PinDataType::Float;
            std::variant<bool, int32_t, float, std::string, FCT::Vec3<float>, uint32_t> data{0.0f};

            bool asBool() const noexcept
            {
                if (const auto* val = std::get_if<bool>(&data)) return *val;
                if (const auto* val = std::get_if<float>(&data)) return *val != 0.0f;
                if (const auto* val = std::get_if<int32_t>(&data)) return *val != 0;
                return false;
            }

            float asFloat() const noexcept
            {
                if (const auto* val = std::get_if<float>(&data)) return *val;
                if (const auto* val = std::get_if<int32_t>(&data)) return static_cast<float>(*val);
                if (const auto* val = std::get_if<bool>(&data)) return *val ? 1.0f : 0.0f;
                return 0.0f;
            }

            int32_t asInt() const noexcept
            {
                if (const auto* val = std::get_if<int32_t>(&data)) return *val;
                if (const auto* val = std::get_if<float>(&data)) return static_cast<int32_t>(*val);
                if (const auto* val = std::get_if<bool>(&data)) return *val ? 1 : 0;
                return 0;
            }

            std::string asString() const
            {
                if (const auto* val = std::get_if<std::string>(&data)) return *val;
                if (const auto* val = std::get_if<float>(&data)) return std::to_string(*val);
                if (const auto* val = std::get_if<int32_t>(&data)) return std::to_string(*val);
                if (const auto* val = std::get_if<bool>(&data)) return *val ? "true" : "false";
                return "";
            }

            FCT::Vec3<float> asVector3() const noexcept
            {
                if (const auto* val = std::get_if<FCT::Vec3<float>>(&data)) return *val;
                return FCT::Vec3<float>(0.0f, 0.0f, 0.0f);
            }
        };

        /**
         * @brief 蓝图运行时节点引脚描述符
         */
        struct RuntimePin
        {
            uint32_t id = 0;
            std::string name;
            PinDataType dataType = PinDataType::Execution;
            bool isOutput = false;
            PinValue defaultValue;
        };

        /**
         * @brief 蓝图运行时节点描述符
         */
        struct RuntimeNode
        {
            uint32_t id = 0;
            std::string title;
            std::string opCode;
            std::vector<RuntimePin> inputs;
            std::vector<RuntimePin> outputs;
            float posX = 0.0f;
            float posY = 0.0f;
            std::string extraProperty; // 附加元信息：如 elementId、componentType、比较操作符等
        };

        /**
         * @brief 蓝图运行时引脚连线描述符
         */
        struct RuntimeLink
        {
            uint32_t id = 0;
            uint32_t startPinId = 0; // 输出引脚 ID
            uint32_t endPinId = 0;   // 输入引脚 ID
        };

        /**
         * @brief 蓝图运行时执行图实例
         */
        struct RuntimeGraph
        {
            std::string name;
            std::vector<RuntimeNode> nodes;
            std::vector<RuntimeLink> links;
            std::unordered_map<std::string, PinValue> variables;
        };

        /**
         * @brief PIE 运行时调试执行信号脉冲探针
         */
        struct ExecutionPulseProbe
        {
            uint32_t nodeId = 0;
            uint32_t pinId = 0;
            float timestampSec = 0.0f;
            std::string debugMessage;
        };

    } // namespace Blueprint
} // namespace MQEngine

#endif // MQENGINE_BLUEPRINT_RUNTIME_TYPES_H
