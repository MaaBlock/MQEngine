/**
 * @file CliEditorTypes.h
 * @brief CliEditor 渐进式披露协议数据结构与 JSON 辅助工具
 */
#ifndef MQENGINE_PLUGIN_CLIEDITOR_CLIEDITORTYPES_H
#define MQENGINE_PLUGIN_CLIEDITOR_CLIEDITORTYPES_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace CliEditor
    {
        /**
         * @brief CLI 命令参数 Schema 描述（用于 Level 2 渐进式披露与参数校验）
         */
        struct CliParamSpec
        {
            std::string name;
            std::string type = "string";
            bool required = false;
            std::string defaultValue;
            std::string description;
        };

        /**
         * @brief CLI 命令执行返回结构（统一支持人类可读输出与 AI 结构化 JSON 输出）
         */
        struct CliExecutionResult
        {
            bool ok = true;
            std::string command;
            std::string message;
            std::string jsonPayload = "{}";
        };

        /**
         * @brief 单条渐进式 CLI 命令完整契约
         */
        struct CliCommandSpec
        {
            using HandlerFn = std::function<CliExecutionResult(const std::unordered_map<std::string, std::string>&)>;

            std::string domain;
            std::string action;
            std::string ownerComponent;
            std::string summary;
            std::string details;
            std::string example;
            std::vector<CliParamSpec> params;
            HandlerFn handler;

            std::string fullName() const
            {
                return domain + "." + action;
            }
        };

        /**
         * @brief 顶层能力域描述（用于 Level 0 渐进式披露）
         */
        struct CliDomainSpec
        {
            std::string name;
            std::string summary;
            std::string ownerModule;
        };

        inline std::string escapeJson(std::string_view input)
        {
            std::string out;
            out.reserve(input.size() + 8);
            for (char c : input)
            {
                switch (c)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
                }
            }
            return out;
        }

        inline bool parseVec3Arg(std::string_view str, FCT::Vec3<>& outVec)
        {
            std::string s(str);
            std::stringstream ss(s);
            std::string item;
            float vals[3] = {outVec.x, outVec.y, outVec.z};
            int idx = 0;
            while (std::getline(ss, item, ',') && idx < 3)
            {
                float v = 0.0f;
                if (absl::SimpleAtof(absl::string_view(item.data(), item.size()), &v))
                {
                    vals[idx] = v;
                }
                ++idx;
            }
            if (idx > 0)
            {
                outVec = FCT::Vec3<>(vals[0], vals[1], vals[2]);
                return true;
            }
            return false;
        }

    } // namespace CliEditor
} // namespace MQEngine

#endif // MQENGINE_PLUGIN_CLIEDITOR_CLIEDITORTYPES_H
