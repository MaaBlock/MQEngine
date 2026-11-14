/**
 * @file Plugin.cpp
 * @brief 插件包逻辑容器实现
 */
#include "Plugin.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <MQEngine/thirdparty/TomlBackend.h>

namespace MQEngine
{
    namespace Asset
    {
        PluginPackage::PluginPackage(PluginMetadata metadata)
            : m_metadata(std::move(metadata))
        {
        }

        PluginPackage::~PluginPackage() = default;

        PluginPackage::PluginPackage(PluginPackage&& other) noexcept
            : m_metadata(std::move(other.m_metadata))
            , m_factories(std::move(other.m_factories))
        {
        }

        PluginPackage& PluginPackage::operator=(PluginPackage&& other) noexcept
        {
            if (this != &other)
            {
                m_metadata = std::move(other.m_metadata);
                m_factories = std::move(other.m_factories);
            }
            return *this;
        }

        void PluginPackage::registerCompositeFactory(const std::string& compositeName, CompositeFactory factory)
        {
            assert(!compositeName.empty() && "Composite 名称不可为空");
            assert(factory != nullptr && "Composite 工厂函数不可为空");
            m_factories[compositeName] = std::move(factory);
        }

        std::vector<UniquePtr<Composite>> PluginPackage::createComposites() const
        {
            std::vector<UniquePtr<Composite>> result;
            result.reserve(m_factories.size());

            for (const auto& [name, factory] : m_factories)
            {
                if (factory)
                {
                    auto comp = factory();
                    if (comp)
                    {
                        result.push_back(std::move(comp));
                    }
                }
            }
            return result;
        }

        StatusOr<PluginMetadata> PluginPackage::parseMetadataFromJson(const std::string& jsonContent)
        {
            if (jsonContent.empty())
            {
                return InvalidArgumentError("Plugin 元数据 JSON 内容为空");
            }

            boost::system::error_code ec;
            auto parsed = boost::json::parse(jsonContent, ec);
            if (ec)
            {
                return InvalidArgumentError(StrCat("解析 plugin.mq 失败: ", ec.message()));
            }

            if (!parsed.is_object())
            {
                return InvalidArgumentError("plugin.mq 根节点必须为 JSON 对象");
            }

            const auto& obj = parsed.as_object();
            PluginMetadata meta;

            if (auto it = obj.find("name"); it != obj.end() && it->value().is_string())
            {
                meta.name = it->value().as_string().c_str();
            }
            if (auto it = obj.find("version"); it != obj.end() && it->value().is_string())
            {
                meta.version = it->value().as_string().c_str();
            }
            if (auto it = obj.find("author"); it != obj.end() && it->value().is_string())
            {
                meta.author = it->value().as_string().c_str();
            }
            if (auto it = obj.find("description"); it != obj.end() && it->value().is_string())
            {
                meta.description = it->value().as_string().c_str();
            }
            if (auto it = obj.find("signature"); it != obj.end() && it->value().is_string())
            {
                meta.signature = it->value().as_string().c_str();
            }
            if (auto it = obj.find("license"); it != obj.end() && it->value().is_string())
            {
                meta.license = it->value().as_string().c_str();
            }

            if (auto it = obj.find("dependencies"); it != obj.end() && it->value().is_array())
            {
                for (const auto& dep : it->value().as_array())
                {
                    if (dep.is_string())
                    {
                        meta.dependencies.push_back(dep.as_string().c_str());
                    }
                }
            }

            if (auto it = obj.find("tags"); it != obj.end() && it->value().is_array())
            {
                for (const auto& tag : it->value().as_array())
                {
                    if (tag.is_string())
                    {
                        meta.tags.push_back(tag.as_string().c_str());
                    }
                }
            }

            if (auto it = obj.find("composites"); it != obj.end() && it->value().is_array())
            {
                for (const auto& comp : it->value().as_array())
                {
                    if (comp.is_string())
                    {
                        std::string compName = comp.as_string().c_str();
                        meta.composites.push_back(compName);
                        CompositeManifest cm;
                        cm.name = compName;
                        cm.pluginName = meta.name;
                        cm.tags = meta.tags;
                        meta.compositeManifests.push_back(std::move(cm));
                    }
                    else if (comp.is_object())
                    {
                        const auto& compObj = comp.as_object();
                        CompositeManifest cm;
                        cm.pluginName = meta.name;
                        if (auto nit = compObj.find("name"); nit != compObj.end() && nit->value().is_string())
                        {
                            cm.name = nit->value().as_string().c_str();
                        }
                        if (auto descIt = compObj.find("description"); descIt != compObj.end() && descIt->value().is_string())
                        {
                            cm.description = descIt->value().as_string().c_str();
                        }
                        if (auto tit = compObj.find("tags"); tit != compObj.end() && tit->value().is_array())
                        {
                            for (const auto& t : tit->value().as_array())
                            {
                                if (t.is_string())
                                {
                                    cm.tags.push_back(t.as_string().c_str());
                                }
                            }
                        }
                        if (auto dit = compObj.find("dependencies"); dit != compObj.end() && dit->value().is_array())
                        {
                            for (const auto& d : dit->value().as_array())
                            {
                                if (d.is_string())
                                {
                                    cm.dependencies.push_back(d.as_string().c_str());
                                }
                            }
                        }
                        if (auto rit = compObj.find("responsibilities"); rit != compObj.end() && rit->value().is_array())
                        {
                            for (const auto& r : rit->value().as_array())
                            {
                                if (r.is_string())
                                {
                                    cm.responsibilities.push_back(r.as_string().c_str());
                                }
                            }
                        }
                        if (auto wit = compObj.find("dock_windows"); wit != compObj.end() && wit->value().is_array())
                        {
                            for (const auto& w : wit->value().as_array())
                            {
                                if (w.is_string())
                                {
                                    cm.dockWindows.push_back(w.as_string().c_str());
                                }
                            }
                        }
                        if (auto ngit = compObj.find("non_goals"); ngit != compObj.end() && ngit->value().is_array())
                        {
                            for (const auto& ng : ngit->value().as_array())
                            {
                                if (ng.is_string())
                                {
                                    cm.nonGoals.push_back(ng.as_string().c_str());
                                }
                            }
                        }
                        if (!cm.name.empty())
                        {
                            meta.composites.push_back(cm.name);
                            meta.compositeManifests.push_back(std::move(cm));
                        }
                    }
                }
            }

            if (meta.name.empty())
            {
                return InvalidArgumentError("Plugin 元数据缺少必填字段: name");
            }

            return meta;
        }

        StatusOr<PluginMetadata> PluginPackage::parseMetadataFromToml(const std::string& tomlContent)
        {
            if (tomlContent.empty())
            {
                return InvalidArgumentError("Plugin 元数据 TOML 内容为空");
            }

            toml::parse_result res = toml::parse(tomlContent);
            if (!res)
            {
                const auto& err = res.error();
                return InvalidArgumentError(StrCat("解析 plugin.mq (TOML) 失败: ", err.description()));
            }

            const auto& tbl = res.table();
            PluginMetadata meta;

            if (auto v = tbl["name"].value<std::string_view>()) meta.name = std::string(*v);
            if (auto v = tbl["version"].value<std::string_view>()) meta.version = std::string(*v);
            if (auto v = tbl["author"].value<std::string_view>()) meta.author = std::string(*v);
            if (auto v = tbl["description"].value<std::string_view>()) meta.description = std::string(*v);
            if (auto v = tbl["license"].value<std::string_view>()) meta.license = std::string(*v);
            if (auto v = tbl["signature"].value<std::string_view>()) meta.signature = std::string(*v);

            if (auto arr = tbl["dependencies"].as_array())
            {
                for (const auto& elem : *arr)
                {
                    if (auto s = elem.value<std::string_view>())
                    {
                        meta.dependencies.emplace_back(*s);
                    }
                }
            }

            if (auto arr = tbl["tags"].as_array())
            {
                for (const auto& elem : *arr)
                {
                    if (auto s = elem.value<std::string_view>())
                    {
                        meta.tags.emplace_back(*s);
                    }
                }
            }

            if (auto arr = tbl["composites"].as_array())
            {
                for (const auto& elem : *arr)
                {
                    if (const auto* compTbl = elem.as_table())
                    {
                        CompositeManifest cm;
                        cm.pluginName = meta.name;
                        if (auto s = (*compTbl)["name"].value<std::string_view>())
                        {
                            cm.name = std::string(*s);
                        }
                        if (auto desc = (*compTbl)["description"].value<std::string_view>())
                        {
                            cm.description = std::string(*desc);
                        }
                        if (auto tarr = (*compTbl)["tags"].as_array())
                        {
                            for (const auto& telem : *tarr)
                            {
                                if (auto ts = telem.value<std::string_view>())
                                {
                                    cm.tags.emplace_back(*ts);
                                }
                            }
                        }
                        if (auto darr = (*compTbl)["dependencies"].as_array())
                        {
                            for (const auto& delem : *darr)
                            {
                                if (auto ds = delem.value<std::string_view>())
                                {
                                    cm.dependencies.emplace_back(*ds);
                                }
                            }
                        }
                        if (auto rarr = (*compTbl)["responsibilities"].as_array())
                        {
                            for (const auto& relem : *rarr)
                            {
                                if (auto rs = relem.value<std::string_view>())
                                {
                                    cm.responsibilities.emplace_back(*rs);
                                }
                            }
                        }
                        if (auto warr = (*compTbl)["dock_windows"].as_array())
                        {
                            for (const auto& welem : *warr)
                            {
                                if (auto ws = welem.value<std::string_view>())
                                {
                                    cm.dockWindows.emplace_back(*ws);
                                }
                            }
                        }
                        if (auto ngarr = (*compTbl)["non_goals"].as_array())
                        {
                            for (const auto& ngelem : *ngarr)
                            {
                                if (auto ngs = ngelem.value<std::string_view>())
                                {
                                    cm.nonGoals.emplace_back(*ngs);
                                }
                            }
                        }
                        if (!cm.name.empty())
                        {
                            meta.composites.push_back(cm.name);
                            meta.compositeManifests.push_back(std::move(cm));
                        }
                    }
                }
            }

            if (meta.name.empty())
            {
                return InvalidArgumentError("Plugin 元数据缺少必填字段: name");
            }

            return meta;
        }

        StatusOr<PluginMetadata> PluginPackage::parseMetadata(const std::string& content)
        {
            if (content.empty())
            {
                return InvalidArgumentError("Plugin 元数据内容为空");
            }

            // 优先尝试 TOML 格式解析 (Doc 04 规范)
            auto tomlResult = parseMetadataFromToml(content);
            if (tomlResult.ok())
            {
                return tomlResult;
            }

            // 回退尝试 JSON 格式解析
            auto jsonResult = parseMetadataFromJson(content);
            if (jsonResult.ok())
            {
                return jsonResult;
            }

            return tomlResult.status();
        }

    } // namespace Asset
} // namespace MQEngine
