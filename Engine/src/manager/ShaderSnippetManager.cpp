//
// Created by MaaBlock on 2025/11/29.
//

#include "ShaderSnippetManager.h"
#include <ctre.hpp>
#include <filesystem>
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "../core/EngineGlobal.h"
#include "../data/DataManager.h"
#include "../data/ResourceLoader.h"

namespace MQEngine {
    static constexpr auto param_pattern = ctll::fixed_string{ R"([,;\s]*(?:(inout|in|out)\s+)?([\w]+(?:<[^>]+>)?)\s+(\w+))" };

    static constexpr auto main_sig_pattern = ctll::fixed_string{ R"((?:void\s+)?\bmain\s*\(\s*([^)]*)\s*\))" };

    StatusOr<std::vector<ShaderParameter>> ShaderSnippetManager::parseParameters(std::string_view source)
    {
        std::vector<ShaderParameter> parameters;

        auto mainMatch = ctre::search<main_sig_pattern>(source);
        if (!mainMatch)
        {
            return NotFoundError("没找到main函数");
        }

        std::string_view paramList = mainMatch.get<1>().to_view();

        for (auto match : ctre::search_all<param_pattern>(paramList))
        {
            ShaderParameter param;

            if (match.get<1>())
            {
                std::string_view dirStr = match.get<1>().to_view();
                if (dirStr == "out")
                {
                    param.direction = ParamDirection::Out;
                }
                else if (dirStr == "inout")
                {
                    param.direction = ParamDirection::InOut;
                }
                else
                {
                    param.direction = ParamDirection::In;
                }
            }
            else
            {
                param.direction = ParamDirection::In;
            }

            param.type = match.get<2>().to_string();

            param.name = match.get<3>().to_string();

            parameters.push_back(param);
        }

        return parameters;
    }

    ShaderSnippetManager::ShaderSnippetManager() = default;
    ShaderSnippetManager::~ShaderSnippetManager() = default;

    Status ShaderSnippetManager::loadSnippetFromResource()
    {
        auto rl = g_engineGlobal.resourceLoader;
        const std::string snippetDir = "./res/snippets/";
        
        Status ensureRes = rl->ensureDir(snippetDir);
        CHECK_STATUS(ensureRes);
        
        // 1. 加载所有.meta as 幽灵片段
        auto filesOr = rl->readDir(snippetDir);
        CHECK_STATUS(filesOr);
        auto files = std::move(filesOr.value());

        for (const auto& file : files) {
            if (file->extension() == ".meta") {
                auto contentOr = file->readContent();
                if (!contentOr.ok()) continue;

                try {
                    SnippetMeta meta;
                    std::stringstream ss(contentOr.value());
                    boost::archive::text_iarchive ia(ss);
                    ia >> meta;

                    Snippet ghost;
                    ghost.uuid = meta.uuid;
                    ghost.name = file->stem();
                    ghost.sourceHash = meta.sourceHash;
                    ghost.source = "";
                    ghost.filePath = ""; 
                    
                    if (m_snippets.get<ByUuid>().find(meta.uuid) == m_snippets.get<ByUuid>().end()) {
                         m_snippets.insert(std::move(ghost));
                    }
                } catch (...) {
                    spdlog::warn("Failed to parse meta file: {}", file->stem());
                }
            }
        }

        //遍历所有meta，加载对应的snippet
        for (auto it = m_snippets.begin(); it != m_snippets.end(); ) {
            const auto& s = *it;
            std::string name = s.name;
            ++it; 

            if (s.source.empty()) {
                loadSnippet(name); 
            }
        }

        //加载所有未加载的 hlsl（即无对应meta的）
        for (const auto& file : files) {
            if (file->extension() == ".hlsl") {
                std::string name = file->stem();
                const Snippet* s = getSnippetByName(name);
                if (s && !s->source.empty())  //已有对应meta
                    continue;

                loadSnippet(name);
            }
        }

        return OkStatus();
    }

    Status ShaderSnippetManager::loadSnippet(const std::string& snippetName)
    {
        const std::string snippetDir = "./res/snippets/";
        std::string hlslPath = snippetDir + snippetName + ".hlsl";
        std::string metaPath = snippetDir + snippetName + ".meta";
        auto rl = g_engineGlobal.resourceLoader;

        auto sourceOr = rl->loadFile(hlslPath);
        if (!sourceOr.ok()) {
             if (IsNotFound(sourceOr.status())) {
                 return NotFoundError(StrCat("Snippet file not found: ",snippetName));
             }
             return sourceOr.status();
        }
        
        std::string source = sourceOr.value();
        size_t sourceHash = std::hash<std::string>{}(source);

        if (hasMetaByName(snippetName))
        {
            const Snippet* s = getSnippetByName(snippetName);
            if (s->sourceHash != sourceHash)
            {
                //有meta，hash不匹配，触发update
                Status st = registerSnippet(s->uuid, snippetName, source, false, hlslPath);
                if (!st.ok()) return st;

                trigger(SnippetEvent::Update{ snippetName, s->uuid });

                SnippetMeta meta;
                meta.uuid = s->uuid;
                meta.sourceHash = sourceHash;

                std::stringstream ss;
                boost::archive::text_oarchive oa(ss);
                oa << meta;
                rl->saveFile(metaPath, ss.str());
            }
            else
            {
                if (s->source.empty()) {
                     registerSnippet(s->uuid, snippetName, source, false, hlslPath);
                }
            }
        }
        else
        {
            std::string uuid;
            bool isRename = false;
            std::string oldName;

            if (hasMetaByHash(sourceHash))
            {
                auto& hashIdx = m_snippets.get<ByHash>();
                auto range = hashIdx.equal_range(sourceHash);

                for (auto it = range.first; it != range.second; ++it)
                {
                    if (it->source.empty())
                    {
                        uuid = it->uuid;
                        oldName = it->name;
                        isRename = true;
                        break;
                    }
                }
            }

            if (isRename)
            {

                //无meta，有hash匹配且source为空的meta，触发rename
                Status st = registerSnippet(uuid, snippetName, source, false, hlslPath);
                CHECK_STATUS(st);

                trigger(SnippetEvent::Rename{ oldName, snippetName, uuid });

                SnippetMeta meta;
                meta.uuid = uuid;
                meta.sourceHash = sourceHash;

                std::stringstream ss;
                boost::archive::text_oarchive oa(ss);
                oa << meta;
                rl->saveFile(metaPath, ss.str());

                std::string oldMetaPath = snippetDir + oldName + ".meta";
                Status unlinkStatus = rl->unlink(oldMetaPath);
                if (!unlinkStatus.ok()) {
                    spdlog::warn("Failed to unlink old meta file '{}': {}", oldMetaPath, unlinkStatus.message());
                }
            }
            else
            {
                //无meta，触发create
                boost::uuids::random_generator gen;
                boost::uuids::uuid u = gen();
                uuid = boost::uuids::to_string(u);

                Status st = registerSnippet(uuid, snippetName, source, false, hlslPath);
                CHECK_STATUS(st);

                trigger(SnippetEvent::Create{ snippetName, uuid });

                SnippetMeta meta;
                meta.uuid = uuid;
                meta.sourceHash = sourceHash;

                std::stringstream ss;
                boost::archive::text_oarchive oa(ss);
                oa << meta;
                rl->saveFile(metaPath, ss.str());
            }
        }
        return OkStatus();
    }

    Status ShaderSnippetManager::unloadSnippet(const std::string& snippetName)
    {
        auto& index = m_snippets.get<ByName>();
        auto it = index.find(snippetName);
        if (it != index.end())
        {
            index.modify(it, [](Snippet& s) {
                s.source = "";
            });
            spdlog::info("Unloaded snippet source: {}", snippetName);
        }
        return OkStatus();
    }

    Status ShaderSnippetManager::registerSnippet(const std::string& uuid, const std::string& snippetName, const std::string& snippetSource) {
        return registerSnippet(uuid, snippetName, snippetSource, true, "");
    }

    const Snippet* ShaderSnippetManager::getSnippetByUuid(const std::string& uuid) const
    {
        const auto& index = m_snippets.get<ByUuid>();
        auto it = index.find(uuid);
        if (it != index.end())
        {
            return &(*it);
        }
        return nullptr;
    }
    const SnippetContainer& ShaderSnippetManager::getSnippets() const { return m_snippets; }
    
    const Snippet* ShaderSnippetManager::getSnippetByName(const std::string& snippetName) const {
        const auto& index = m_snippets.get<ByName>();
        auto it = index.find(snippetName);
        if (it != index.end()) {
            return &(*it);
        }
        return nullptr;
    }

    Status ShaderSnippetManager::registerSnippet(const std::string& uuid, const std::string& snippetName,
                                                        const std::string& snippetSource, bool isEmbed, const std::string& filePath)
    {
        auto paramsOr = parseParameters(snippetSource);
        CHECK_STATUS(paramsOr);

        const auto& params = paramsOr.value();

        Snippet snippet;
        snippet.uuid = uuid;
        snippet.name = snippetName;
        snippet.sourceHash = std::hash<std::string>{}(snippetSource);
        snippet.source = snippetSource;
        snippet.isEmbed = isEmbed;
        snippet.filePath = filePath;

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

        auto& index = m_snippets.get<ByUuid>();
        auto it = index.find(uuid);
        if (it != index.end()) {
             auto& nameIdx = m_snippets.get<ByName>();
             auto nameIt = nameIdx.find(snippetName);
             if (nameIt != nameIdx.end() && nameIt->uuid != uuid) {
                 nameIdx.erase(nameIt);
             }

             index.replace(it, std::move(snippet));
        } else {
             auto& nameIdx = m_snippets.get<ByName>();
             auto nameIt = nameIdx.find(snippetName);
             if (nameIt != nameIdx.end()) {
                 nameIdx.erase(nameIt);
             }
             m_snippets.insert(std::move(snippet));
        }
        
        spdlog::info("成功注册着色器片段: {}", snippetName);

        return OkStatus();
    }

    bool ShaderSnippetManager::hasMetaByUuid(const std::string& uuid) const {
        return m_snippets.get<ByUuid>().find(uuid) != m_snippets.get<ByUuid>().end();
    }

    bool ShaderSnippetManager::hasMetaByName(const std::string& name) const {
        return m_snippets.get<ByName>().find(name) != m_snippets.get<ByName>().end();
    }

    bool ShaderSnippetManager::hasMetaByHash(size_t hash) const {
        return m_snippets.get<ByHash>().find(hash) != m_snippets.get<ByHash>().end();
    }

} // MQEngine