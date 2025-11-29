//
// Created by MaaBlock on 2025/11/29.
//

#include "ShaderSnippetManager.h"
#include <ctre.hpp>

namespace MQEngine {
    static constexpr auto param_pattern = ctll::fixed_string{ R"([,;\s]*(?:(inout|in|out)\s+)?([\w]+(?:<[^>]+>)?)\s+(\w+))" };

    static constexpr auto main_sig_pattern = ctll::fixed_string{ R"((?:void\s+)?\bmain\s*\(\s*([^)]*)\s*\))" };

    StatusOr<std::vector<ShaderParameter>> ShaderSnippetManager::parseParameters(std::string_view source) {
        std::vector<ShaderParameter> parameters;

        auto mainMatch = ctre::search<main_sig_pattern>(source);
        if (!mainMatch) {
            return NotFoundError("没找到main函数");
        }

        std::string_view paramList = mainMatch.get<1>().to_view();

        for (auto match : ctre::search_all<param_pattern>(paramList)) {
            ShaderParameter param;

            if (match.get<1>()) {
                std::string_view dirStr = match.get<1>().to_view();
                if (dirStr == "out") {
                    param.direction = ParamDirection::Out;
                } else if (dirStr == "inout") {
                    param.direction = ParamDirection::InOut;
                } else {
                    param.direction = ParamDirection::In;
                }
            } else {
                param.direction = ParamDirection::In;
            }

            param.type = match.get<2>().to_string();

            param.name = match.get<3>().to_string();

            parameters.push_back(param);
        }

        return parameters;
    }

    Status ShaderSnippetManager::registerSnippet(const std::string& uuid, const std::string& snippetName, const std::string& snippetSource) {
        auto paramsOr = parseParameters(snippetSource);
        CHECK_STATUS(paramsOr);

        const auto& params = paramsOr.value();
        
        Snippet snippet;
        snippet.uuid = uuid;
        snippet.name = snippetName;
        snippet.source = snippetSource;

        for (const auto& param : params) {
            switch (param.direction) {
                case ParamDirection::In:
                    spdlog::debug("成功从着色器片段{}中解析出类型为{}的输入参数:{}",snippetName,param.type, param.name);
                    snippet.inputs.push_back(param);
                    break;
                case ParamDirection::Out:
                    spdlog::debug("成功从着色器片段{}中解析出类型为{}的输出参数:{}",snippetName,param.type, param.name);
                    snippet.outputs.push_back(param);
                    break;
                case ParamDirection::InOut:
                    spdlog::debug("成功从着色器片段{}中解析出类型为{}的InOut参数:{}",snippetName,param.type, param.name);
                    snippet.inouts.push_back(param);
                    break;
            }
        }

        m_snippets[snippetName] = std::move(snippet);
        spdlog::info("成功注册着色器片段: {}", snippetName);
        return OkStatus();
    }

    const Snippet* ShaderSnippetManager::getSnippet(const std::string& snippetName) const {
        auto it = m_snippets.find(snippetName);
        if (it != m_snippets.end()) {
            return &it->second;
        }
        return nullptr;
    }

} // MQEngine