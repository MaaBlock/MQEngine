/**
 * @file RenderGraphEditorSerializer.cpp
 * @brief RenderGraph .pgraph TOML 格式序列化/反序列化与 FCT::RenderGraph C++ 代码自动生成实现
 */
#include "RenderGraphEditorComposite.h"

#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/thirdparty/TomlBackend.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <sstream>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        void RenderGraphEditorComposite::saveToFile(const std::string& filename)
        {
            std::string path = filename;
            if (path.find(".pgraph") == std::string::npos)
            {
                path += ".pgraph";
            }

            std::ostringstream ss;
            ss << "# MQEngine RenderGraph .pgraph file (TOML format)\n";
            ss << "[graph]\n";
            ss << "version = \"2.0\"\n";
            ss << "next_node_id = " << m_nextNodeId << "\n";
            ss << "link_id = " << m_linkId << "\n\n";

            for (auto& [id, pass] : m_passes)
            {
                if (m_imnodesCtx)
                {
                    ImVec2 pos = ImNodes::GetNodeGridSpacePos(pass.id);
                    pass.pos[0] = pos.x;
                    pass.pos[1] = pos.y;
                }

                ss << "[[passes]]\n";
                ss << "id = " << pass.id << "\n";
                ss << "name = \"" << pass.name << "\"\n";
                ss << "pos_x = " << pass.pos[0] << "\n";
                ss << "pos_y = " << pass.pos[1] << "\n";
                ss << "enable_clear = " << (pass.enableClear ? "true" : "false") << "\n";
                ss << "enable_clear_target = " << (pass.enableClearTarget ? "true" : "false") << "\n";
                ss << "enable_clear_depth = " << (pass.enableClearDepth ? "true" : "false") << "\n";
                ss << "enable_clear_stencil = " << (pass.enableClearStencil ? "true" : "false") << "\n";
                ss << "clear_color = [" << pass.clearColor[0] << ", " << pass.clearColor[1] << ", "
                   << pass.clearColor[2] << ", " << pass.clearColor[3] << "]\n";
                ss << "clear_depth = " << pass.clearDepth << "\n";
                ss << "clear_stencil = " << pass.clearStencil << "\n";
                ss << "texture_pin_index = " << pass.texturePinIndex << "\n";
                ss << "texture_pins = [";
                for (size_t p = 0; p < pass.texturePins.size(); ++p)
                {
                    if (p > 0) ss << ", ";
                    ss << pass.texturePins[p];
                }
                ss << "]\n\n";

                for (int i = 0; i < 9; ++i)
                {
                    const auto& tgt = pass.targetDesc[i];
                    ss << "[[passes.targets]]\n";
                    ss << "index = " << i << "\n";
                    ss << "enabled = " << (tgt.enabled ? "true" : "false") << "\n";
                    ss << "is_window = " << (tgt.isWindow ? "true" : "false") << "\n";
                    ss << "use_custom_format = " << (tgt.useCustomFormat ? "true" : "false") << "\n";
                    ss << "format = \"" << tgt.format << "\"\n";
                    ss << "use_custom_size = " << (tgt.useCustomSize ? "true" : "false") << "\n";
                    ss << "custom_width = " << tgt.customWidth << "\n";
                    ss << "custom_height = " << tgt.customHeight << "\n";
                }

                const auto& ds = pass.depthStencilDesc;
                ss << "[passes.depth_stencil]\n";
                ss << "enabled = " << (ds.enabled ? "true" : "false") << "\n";
                ss << "is_window = " << (ds.isWindow ? "true" : "false") << "\n";
                ss << "use_custom_format = " << (ds.useCustomFormat ? "true" : "false") << "\n";
                ss << "format = \"" << ds.format << "\"\n";
                ss << "use_custom_size = " << (ds.useCustomSize ? "true" : "false") << "\n";
                ss << "custom_width = " << ds.customWidth << "\n";
                ss << "custom_height = " << ds.customHeight << "\n\n";
            }

            for (auto& [id, image] : m_images)
            {
                if (m_imnodesCtx)
                {
                    ImVec2 pos = ImNodes::GetNodeGridSpacePos(image.id);
                    image.pos[0] = pos.x;
                    image.pos[1] = pos.y;
                }

                ss << "[[images]]\n";
                ss << "id = " << image.id << "\n";
                ss << "name = \"" << image.name << "\"\n";
                ss << "format = \"" << image.format << "\"\n";
                ss << "width = " << image.width << "\n";
                ss << "height = " << image.height << "\n";
                ss << "is_aliased = " << (image.isAliased ? "true" : "false") << "\n";
                ss << "pos_x = " << image.pos[0] << "\n";
                ss << "pos_y = " << image.pos[1] << "\n\n";
            }

            for (const auto& [pinId, link] : m_passOutputlinks)
            {
                ss << "[[output_links]]\n";
                ss << "id = " << link.id << "\n";
                ss << "start_pin_id = " << link.startPinId << "\n";
                ss << "end_pin_id = " << link.endPinId << "\n";
            }

            for (const auto& [pinId, link] : m_passInputLinks)
            {
                ss << "[[input_links]]\n";
                ss << "id = " << link.id << "\n";
                ss << "start_pin_id = " << link.startPinId << "\n";
                ss << "end_pin_id = " << link.endPinId << "\n";
            }

            auto* res = Resource::Resource::InstancePtr();
            if (res && res->getResourceLoader())
            {
                auto* loader = res->getResourceLoader();
                (void)loader->ensureDir("Assets/RenderGraph");
                Status st = loader->saveFile(path, ss.str());
                if (st.ok() && m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "RenderGraph",
                        tr("图表已成功保存至 ", "Saved graph to ") + path,
                        EditorBase::LogEntry::Level::Info);
                }
            }
        }

        void RenderGraphEditorComposite::loadFromFile(const std::string& filename)
        {
            std::string path = filename;
            if (path.find(".pgraph") == std::string::npos)
            {
                path += ".pgraph";
            }

            auto* res = Resource::Resource::InstancePtr();
            if (!res || !res->getResourceLoader())
                return;

            auto* loader = res->getResourceLoader();
            auto contentOr = loader->loadFile(path);
            if (!contentOr.ok())
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "RenderGraph",
                        std::string(tr("读取图表失败: ", "Failed to load graph: ")) + std::string(contentOr.status().message()),
                        EditorBase::LogEntry::Level::Warn);
                }
                return;
            }

            toml::parse_result parsed = toml::parse(contentOr.value());
            if (!parsed)
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "RenderGraph",
                        tr("解析 TOML .pgraph 文件失败", "Failed to parse TOML .pgraph file"),
                        EditorBase::LogEntry::Level::Error);
                }
                return;
            }

            clearGraph();

            const auto& tbl = parsed.table();

            if (auto graphTbl = tbl["graph"].as_table())
            {
                m_nextNodeId = graphTbl->get("next_node_id")->value_or(0);
                m_linkId = graphTbl->get("link_id")->value_or(0);
            }

            if (auto passesArr = tbl["passes"].as_array())
            {
                for (const auto& item : *passesArr)
                {
                    if (auto pTbl = item.as_table())
                    {
                        PassNode pass;
                        pass.id = pTbl->get("id")->value_or(0);
                        pass.name = pTbl->get("name")->value_or("");
                        pass.pos[0] = static_cast<float>(pTbl->get("pos_x")->value_or(100.0));
                        pass.pos[1] = static_cast<float>(pTbl->get("pos_y")->value_or(100.0));
                        pass.enableClear = pTbl->get("enable_clear")->value_or(false);
                        pass.enableClearTarget = pTbl->get("enable_clear_target")->value_or(false);
                        pass.enableClearDepth = pTbl->get("enable_clear_depth")->value_or(false);
                        pass.enableClearStencil = pTbl->get("enable_clear_stencil")->value_or(false);
                        pass.clearDepth = static_cast<float>(pTbl->get("clear_depth")->value_or(1.0));
                        pass.clearStencil = pTbl->get("clear_stencil")->value_or(0);
                        pass.texturePinIndex = pTbl->get("texture_pin_index")->value_or(0);

                        if (auto ccArr = pTbl->get("clear_color")->as_array())
                        {
                            for (size_t c = 0; c < 4 && c < ccArr->size(); ++c)
                            {
                                pass.clearColor[c] = static_cast<float>(ccArr->get(c)->value_or(0.0));
                            }
                        }

                        if (auto tpArr = pTbl->get("texture_pins")->as_array())
                        {
                            for (const auto& pinVal : *tpArr)
                            {
                                int pinId = pinVal.value_or(0);
                                pass.texturePins.push_back(pinId);
                                PinInfo pinInfo;
                                pinInfo.nodeId = pass.id;
                                pinInfo.pinType = "texture";
                                pinInfo.index = static_cast<int>(pass.texturePins.size());
                                m_pinInfoMap[pinId] = pinInfo;
                            }
                        }

                        if (auto tgtsArr = pTbl->get("targets")->as_array())
                        {
                            for (const auto& tgtItem : *tgtsArr)
                            {
                                if (auto tgtTbl = tgtItem.as_table())
                                {
                                    int idx = tgtTbl->get("index")->value_or(0);
                                    if (idx >= 0 && idx < 9)
                                    {
                                        auto& tgt = pass.targetDesc[idx];
                                        tgt.enabled = tgtTbl->get("enabled")->value_or(false);
                                        tgt.isWindow = tgtTbl->get("is_window")->value_or(false);
                                        tgt.useCustomFormat = tgtTbl->get("use_custom_format")->value_or(false);
                                        tgt.format = tgtTbl->get("format")->value_or("R8G8B8A8_UNORM");
                                        tgt.useCustomSize = tgtTbl->get("use_custom_size")->value_or(false);
                                        tgt.customWidth = tgtTbl->get("custom_width")->value_or(1920);
                                        tgt.customHeight = tgtTbl->get("custom_height")->value_or(1080);
                                    }
                                }
                            }
                        }

                        if (auto dsTbl = pTbl->get("depth_stencil")->as_table())
                        {
                            auto& ds = pass.depthStencilDesc;
                            ds.enabled = dsTbl->get("enabled")->value_or(false);
                            ds.isWindow = dsTbl->get("is_window")->value_or(false);
                            ds.useCustomFormat = dsTbl->get("use_custom_format")->value_or(false);
                            ds.format = dsTbl->get("format")->value_or("D32_SFLOAT");
                            ds.useCustomSize = dsTbl->get("use_custom_size")->value_or(false);
                            ds.customWidth = dsTbl->get("custom_width")->value_or(1920);
                            ds.customHeight = dsTbl->get("custom_height")->value_or(1080);
                        }

                        for (int i = 0; i < 9; ++i)
                        {
                            generatePinId(pass.id, "target", i);
                        }
                        generatePinId(pass.id, "depth", 0);

                        m_passes[pass.id] = pass;
                        if (m_imnodesCtx)
                        {
                            ImNodes::SetNodeGridSpacePos(pass.id, ImVec2(pass.pos[0], pass.pos[1]));
                        }
                    }
                }
            }

            if (auto imagesArr = tbl["images"].as_array())
            {
                for (const auto& item : *imagesArr)
                {
                    if (auto imgTbl = item.as_table())
                    {
                        ImageNode img;
                        img.id = imgTbl->get("id")->value_or(0);
                        img.name = imgTbl->get("name")->value_or("");
                        img.format = imgTbl->get("format")->value_or("R8G8B8A8_UNORM");
                        img.width = imgTbl->get("width")->value_or(1920);
                        img.height = imgTbl->get("height")->value_or(1080);
                        img.isAliased = imgTbl->get("is_aliased")->value_or(false);
                        img.pos[0] = static_cast<float>(imgTbl->get("pos_x")->value_or(350.0));
                        img.pos[1] = static_cast<float>(imgTbl->get("pos_y")->value_or(100.0));

                        generatePinId(img.id, "texture", 0);
                        generatePinId(img.id, "target", 0);
                        generatePinId(img.id, "depth", 0);

                        m_images[img.id] = img;
                        if (m_imnodesCtx)
                        {
                            ImNodes::SetNodeGridSpacePos(img.id, ImVec2(img.pos[0], img.pos[1]));
                        }
                    }
                }
            }

            if (auto outLinksArr = tbl["output_links"].as_array())
            {
                for (const auto& item : *outLinksArr)
                {
                    if (auto lTbl = item.as_table())
                    {
                        LinkInfo info;
                        info.id = lTbl->get("id")->value_or(0);
                        info.startPinId = lTbl->get("start_pin_id")->value_or(0);
                        info.endPinId = lTbl->get("end_pin_id")->value_or(0);
                        m_passOutputlinks[info.startPinId] = info;
                    }
                }
            }

            if (auto inLinksArr = tbl["input_links"].as_array())
            {
                for (const auto& item : *inLinksArr)
                {
                    if (auto lTbl = item.as_table())
                    {
                        LinkInfo info;
                        info.id = lTbl->get("id")->value_or(0);
                        info.startPinId = lTbl->get("start_pin_id")->value_or(0);
                        info.endPinId = lTbl->get("end_pin_id")->value_or(0);
                        m_passInputLinks[info.endPinId] = info;
                    }
                }
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "RenderGraph",
                    tr("图表已成功从文件加载", "Loaded graph successfully from file"),
                    EditorBase::LogEntry::Level::Info);
            }
        }

        std::string RenderGraphEditorComposite::generatePassCode(const PassNode& pass)
        {
            std::string code;
            code += "graph->addPass(\n";
            code += "    \"" + pass.name + "\",\n";

            for (int texturePin : pass.texturePins)
            {
                if (m_passInputLinks.count(texturePin))
                {
                    auto linkInfo = m_passInputLinks.at(texturePin);
                    if (m_pinInfoMap.count(linkInfo.startPinId))
                    {
                        auto startPinInfo = m_pinInfoMap.at(linkInfo.startPinId);
                        if (m_images.count(startPinInfo.nodeId))
                        {
                            auto& sourceImage = m_images.at(startPinInfo.nodeId);
                            code += "    FCT::Texture(\"" + sourceImage.name + "\"),\n";
                        }
                    }
                }
            }

            if (pass.enableClear && (pass.enableClearDepth || pass.enableClearStencil || pass.enableClearTarget))
            {
                std::string clearType;
                std::vector<std::string> clearTypes;
                if (pass.enableClearTarget) clearTypes.push_back("FCT::ClearType::color");
                if (pass.enableClearDepth) clearTypes.push_back("FCT::ClearType::depth");
                if (pass.enableClearStencil) clearTypes.push_back("FCT::ClearType::stencil");

                if (clearTypes.size() == 1)
                {
                    clearType = clearTypes[0];
                }
                else if (clearTypes.size() > 1)
                {
                    clearType = clearTypes[0];
                    for (size_t i = 1; i < clearTypes.size(); ++i)
                    {
                        clearType += " | " + clearTypes[i];
                    }
                }

                code += "    FCT::EnablePassClear(" + clearType;
                bool needColorParam = pass.enableClearTarget &&
                    !(pass.clearColor[0] == 0.0f && pass.clearColor[1] == 0.0f &&
                      pass.clearColor[2] == 0.0f && pass.clearColor[3] == 1.0f);
                bool needDepthParam = pass.enableClearDepth && (pass.clearDepth != 1.0f);
                bool needStencilParam = pass.enableClearStencil && (pass.clearStencil != 0);

                if (needColorParam)
                {
                    code += ",\n        FCT::Vec4(" + std::to_string(pass.clearColor[0]) + "f, "
                            + std::to_string(pass.clearColor[1]) + "f, "
                            + std::to_string(pass.clearColor[2]) + "f, "
                            + std::to_string(pass.clearColor[3]) + "f)";
                }
                if (needDepthParam)
                {
                    code += ", " + std::to_string(pass.clearDepth) + "f";
                }
                if (needStencilParam)
                {
                    code += ", " + std::to_string(pass.clearStencil);
                }
                code += "),\n";
            }

            for (int i = 0; i < 9; ++i)
            {
                uint32_t targetPinId = pass.targetPinId(i);
                if (m_passOutputlinks.count(targetPinId))
                {
                    auto linkInfo = m_passOutputlinks.at(targetPinId);
                    if (m_pinInfoMap.count(linkInfo.endPinId))
                    {
                        auto endPinInfo = m_pinInfoMap.at(linkInfo.endPinId);
                        if (m_images.count(endPinInfo.nodeId))
                        {
                            auto& targetImage = m_images.at(endPinInfo.nodeId);
                            const auto& targetConfig = pass.targetDesc[i];
                            code += "    FCT::Target(\"" + targetImage.name + "\"";
                            if (targetConfig.enabled)
                            {
                                if (targetConfig.useCustomSize)
                                {
                                    code += ", " + std::to_string(targetConfig.customWidth)
                                            + ", " + std::to_string(targetConfig.customHeight);
                                }
                                if (targetConfig.useCustomFormat)
                                {
                                    code += ", FCT::Format::" + targetConfig.format;
                                }
                            }
                            code += "),\n";
                        }
                    }
                }
            }

            uint32_t depthPinId = pass.depthStencilPinId();
            if (m_passOutputlinks.count(depthPinId))
            {
                auto linkInfo = m_passOutputlinks.at(depthPinId);
                if (m_pinInfoMap.count(linkInfo.endPinId))
                {
                    auto endPinInfo = m_pinInfoMap.at(linkInfo.endPinId);
                    if (m_images.count(endPinInfo.nodeId))
                    {
                        auto& depthImage = m_images.at(endPinInfo.nodeId);
                        const auto& depthConfig = pass.depthStencilDesc;
                        code += "    FCT::DepthStencil(\"" + depthImage.name + "\"";
                        if (depthConfig.enabled)
                        {
                            if (depthConfig.useCustomSize)
                            {
                                code += ", " + std::to_string(depthConfig.customWidth)
                                        + ", " + std::to_string(depthConfig.customHeight);
                            }
                            if (depthConfig.useCustomFormat)
                            {
                                code += ", FCT::Format::" + depthConfig.format;
                            }
                        }
                        code += ")\n";
                    }
                }
            }

            if (code.back() == '\n' && code.size() >= 2 && code[code.length() - 2] == ',')
            {
                code = code.substr(0, code.length() - 2) + "\n";
            }
            code += ");\n\n";
            return code;
        }

        std::string RenderGraphEditorComposite::generatorCode()
        {
            std::string code;
            code += "// Auto-generated by MQEngine RenderGraphEditorComposite\n";
            code += "// Use with FCT::RenderGraph* graph\n\n";
            for (const auto& [passId, pass] : m_passes)
            {
                code += generatePassCode(pass);
            }
            return code;
        }

    } // namespace MQEditor
} // namespace MQEngine
