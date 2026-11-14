/**
 * @file RenderGraphEditorNodes.cpp
 * @brief RenderGraph 节点/引脚动态管理、自动增长纹理引脚、DAG 拓扑层级自动布局与 FCT::PassDesc 双向转换实现
 */
#include "RenderGraphEditorComposite.h"

#include <MQEngine/host/ui/UIHost.h>

#include <FCT/Context/Format.h>
#include <FCT/Context/RenderGraph_PassDesc.h>
#include <FCT/Context/RenderGraph_EnablePassClear.h>
#include <FCT/Context/RenderGraph_Texture.h>
#include <FCT/Context/RenderGraph_Target.h>
#include <FCT/Context/RenderGraph_DepthStencil.h>
#include <FCT/Context/ClearTypes.h>
#include <FCT/UI/Window.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        int RenderGraphEditorComposite::generatePinId(int nodeId, const std::string& pinType, int index)
        {
            std::hash<std::string> hasher;
            std::string pinString = std::to_string(nodeId) + "_" + pinType + "_" + std::to_string(index);
            int pinId = static_cast<int>(static_cast<uint32_t>(hasher(pinString)) & 0x7FFFFFFF);

            PinInfo info;
            info.nodeId = static_cast<uint32_t>(nodeId);
            info.pinType = pinType;
            info.index = index;
            m_pinInfoMap[pinId] = info;

            return pinId;
        }

        int RenderGraphEditorComposite::findImageNode(const std::string& name) const
        {
            for (const auto& [id, image] : m_images)
            {
                if (image.name == name)
                    return id;
            }
            return -1;
        }

        int RenderGraphEditorComposite::findPassNode(const std::string& name) const
        {
            for (const auto& [id, pass] : m_passes)
            {
                if (pass.name == name)
                    return id;
            }
            return -1;
        }

        int RenderGraphEditorComposite::newPassNode(const std::string& name)
        {
            PassNode newPass;
            newPass.id = getNextNodeId();
            newPass.name = (findPassNode(name) == -1) ? name : (name + " " + std::to_string(newPass.id));
            newPass.enableClear = false;
            newPass.enableClearDepth = false;
            newPass.enableClearStencil = false;
            newPass.enableClearTarget = true;

            m_passes[newPass.id] = newPass;
            newTexturePin(m_passes[newPass.id]);
            for (int i = 0; i < 9; ++i)
            {
                generatePinId(newPass.id, "target", i);
            }
            generatePinId(newPass.id, "depth", 0);
            return newPass.id;
        }

        void RenderGraphEditorComposite::newTexturePin(PassNode& pass)
        {
            int id = generatePinId(pass.id, "texture", static_cast<int>(++pass.texturePinIndex));
            pass.texturePins.push_back(id);
        }

        int RenderGraphEditorComposite::newImageNode(const std::string& name)
        {
            ImageNode newImage;
            newImage.id = getNextNodeId();
            newImage.name = (findImageNode(name) == -1) ? name : (name + " " + std::to_string(newImage.id));

            m_images[newImage.id] = newImage;
            generatePinId(newImage.id, "texture", 0);
            generatePinId(newImage.id, "target", 0);
            generatePinId(newImage.id, "depth", 0);
            return newImage.id;
        }

        void RenderGraphEditorComposite::addTextureLink(int imageId, int passId)
        {
            if (m_images.count(imageId) && m_passes.count(passId))
            {
                auto& passes = m_passes[passId];
                auto& image = m_images[imageId];
                addLink(image.texturePinId(), passes.lastTexturePinId());
            }
        }

        void RenderGraphEditorComposite::addTargetLink(int passId, int index, int imageId)
        {
            if (m_passes.count(passId) && m_images.count(imageId))
            {
                auto& pass = m_passes[passId];
                auto& image = m_images[imageId];
                addLink(pass.targetPinId(index), image.targetPinId());
            }
        }

        void RenderGraphEditorComposite::addDepthStencilLink(int passId, int imageId)
        {
            if (m_passes.count(passId) && m_images.count(imageId))
            {
                addLink(m_passes[passId].depthStencilPinId(), m_images[imageId].depthStencilPinId());
            }
        }

        void RenderGraphEditorComposite::addLink(int startHash, int endHash)
        {
            if (!m_pinInfoMap.count(startHash) || !m_pinInfoMap.count(endHash))
                return;

            auto startPin = m_pinInfoMap[startHash];
            auto endPin = m_pinInfoMap[endHash];

            // 1. Pass 输出目标连向 Image 输入
            if (m_passes.count(startPin.nodeId))
            {
                if (startPin.pinType == endPin.pinType)
                {
                    LinkInfo info;
                    info.id = getNextLinkId();
                    info.startPinId = startHash;
                    info.endPinId = endHash;
                    m_passOutputlinks[startHash] = info;
                }
            }
            // 2. Image 输出纹理连向 Pass 输入纹理 (自动增长 Texture 引脚)
            else if (m_images.count(startPin.nodeId))
            {
                if (startPin.pinType == endPin.pinType)
                {
                    if (!m_passInputLinks.count(endHash))
                    {
                        newTexturePin(m_passes[endPin.nodeId]);
                    }
                    LinkInfo info;
                    info.id = getNextLinkId();
                    info.startPinId = startHash;
                    info.endPinId = endHash;
                    m_passInputLinks[endHash] = info;
                }
            }
        }

        void RenderGraphEditorComposite::removePassPin(int pinHash)
        {
            m_passOutputlinks.erase(pinHash);
            m_passInputLinks.erase(pinHash);
            m_pinInfoMap.erase(pinHash);
        }

        void RenderGraphEditorComposite::removeImagePin(int pinHash)
        {
            std::vector<int> needRemoveOutputs;
            for (const auto& [pinId, link] : m_passOutputlinks)
            {
                if (link.endPinId == pinHash)
                {
                    needRemoveOutputs.push_back(pinId);
                }
            }
            for (int pid : needRemoveOutputs)
            {
                m_passOutputlinks.erase(pid);
            }

            std::vector<int> needRemoveInputs;
            for (const auto& [pinId, link] : m_passInputLinks)
            {
                if (link.startPinId == pinHash)
                {
                    needRemoveInputs.push_back(pinId);
                }
            }
            for (int pid : needRemoveInputs)
            {
                if (m_pinInfoMap.count(pid))
                {
                    auto info = m_pinInfoMap[pid];
                    if (m_passes.count(info.nodeId))
                    {
                        m_passes[info.nodeId].removeTexturePin(pid);
                    }
                    m_pinInfoMap.erase(pid);
                }
                m_passInputLinks.erase(pid);
            }

            m_pinInfoMap.erase(pinHash);
        }

        void RenderGraphEditorComposite::deletePass(int contextMenuNodeId)
        {
            if (!m_passes.count(contextMenuNodeId))
                return;

            auto pass = m_passes[contextMenuNodeId];
            for (int i = 0; i < 9; ++i)
            {
                removePassPin(pass.targetPinId(i));
            }
            removePassPin(pass.depthStencilPinId());

            for (int pin : pass.texturePins)
            {
                removePassPin(pin);
            }
            m_passes.erase(contextMenuNodeId);
        }

        void RenderGraphEditorComposite::deleteImage(int contextMenuNodeId)
        {
            if (!m_images.count(contextMenuNodeId))
                return;

            auto image = m_images[contextMenuNodeId];
            removeImagePin(image.targetPinId());
            removeImagePin(image.depthStencilPinId());
            removeImagePin(image.texturePinId());
            m_images.erase(contextMenuNodeId);
        }

        void RenderGraphEditorComposite::deleteNode(int contextMenuNodeId)
        {
            if (m_passes.count(contextMenuNodeId))
            {
                deletePass(contextMenuNodeId);
            }
            if (m_images.count(contextMenuNodeId))
            {
                deleteImage(contextMenuNodeId);
            }
        }

        void RenderGraphEditorComposite::clearGraph()
        {
            m_passes.clear();
            m_images.clear();
            m_pinInfoMap.clear();
            m_passOutputlinks.clear();
            m_passInputLinks.clear();
            m_nextNodeId = 0;
            m_linkId = 0;
            m_generatedCode.clear();
            m_selectedNodeId = -1;
        }

        void RenderGraphEditorComposite::createGraphFromPassDescs(const std::vector<FCT::PassDesc>& passDescs)
        {
            clearGraph();

            std::map<std::string, int> imageNameToId;

            // 1. 扫描所有 PassDesc 中引用的纹理与目标，创建 ImageNode
            for (const auto& passDesc : passDescs)
            {
                for (const auto& texture : passDesc.textures)
                {
                    if (imageNameToId.find(texture.name) == imageNameToId.end())
                    {
                        int imageId = newImageNode(texture.name);
                        imageNameToId[texture.name] = imageId;
                    }
                }
                for (const auto& target : passDesc.targets)
                {
                    if (imageNameToId.find(target.name) == imageNameToId.end())
                    {
                        int imageId = newImageNode(target.name);
                        imageNameToId[target.name] = imageId;
                        if (target.format != FCT::Format::UNDEFINED)
                        {
                            m_images[imageId].format = FCT::FormatToString(target.format);
                        }
                        if (target.hasFixedSize)
                        {
                            m_images[imageId].width = target.width;
                            m_images[imageId].height = target.height;
                        }
                    }
                }
                for (const auto& depthStencil : passDesc.depthStencils)
                {
                    if (imageNameToId.find(depthStencil.name) == imageNameToId.end())
                    {
                        int imageId = newImageNode(depthStencil.name);
                        imageNameToId[depthStencil.name] = imageId;
                        if (depthStencil.format != FCT::Format::UNDEFINED)
                        {
                            m_images[imageId].format = FCT::FormatToString(depthStencil.format);
                        }
                        if (depthStencil.hasFixedSize)
                        {
                            m_images[imageId].width = depthStencil.width;
                            m_images[imageId].height = depthStencil.height;
                        }
                    }
                }
            }

            // 2. 创建 PassNode 并建立连线
            for (const auto& passDesc : passDescs)
            {
                int passId = newPassNode(passDesc.name);
                auto& pass = m_passes[passId];

                if (passDesc.clear.types != 0)
                {
                    pass.enableClear = true;
                    if (passDesc.clear.types & FCT::ClearType::color)
                    {
                        pass.enableClearTarget = true;
                        pass.clearColor[0] = passDesc.clear.color.x;
                        pass.clearColor[1] = passDesc.clear.color.y;
                        pass.clearColor[2] = passDesc.clear.color.z;
                        pass.clearColor[3] = passDesc.clear.color.w;
                    }
                    if (passDesc.clear.types & FCT::ClearType::depth)
                    {
                        pass.enableClearDepth = true;
                        pass.clearDepth = passDesc.clear.depth;
                    }
                    if (passDesc.clear.types & FCT::ClearType::stencil)
                    {
                        pass.enableClearStencil = true;
                        pass.clearStencil = passDesc.clear.stencil;
                    }
                }

                for (size_t i = 0; i < passDesc.targets.size() && i < 9; ++i)
                {
                    const auto& target = passDesc.targets[i];
                    auto& targetConfig = pass.targetDesc[i];
                    targetConfig.enabled = true;
                    targetConfig.isWindow = target.isWindow;
                    if (target.hasFixedSize)
                    {
                        targetConfig.useCustomSize = true;
                        targetConfig.customWidth = target.width;
                        targetConfig.customHeight = target.height;
                    }
                    if (target.format != FCT::Format::UNDEFINED)
                    {
                        targetConfig.useCustomFormat = true;
                        targetConfig.format = FCT::FormatToString(target.format);
                    }
                }

                if (!passDesc.depthStencils.empty())
                {
                    const auto& depthStencil = passDesc.depthStencils[0];
                    auto& depthConfig = pass.depthStencilDesc;
                    depthConfig.enabled = true;
                    depthConfig.isWindow = depthStencil.isWindow;
                    if (depthStencil.hasFixedSize)
                    {
                        depthConfig.useCustomSize = true;
                        depthConfig.customWidth = depthStencil.width;
                        depthConfig.customHeight = depthStencil.height;
                    }
                    if (depthStencil.format != FCT::Format::UNDEFINED)
                    {
                        depthConfig.useCustomFormat = true;
                        depthConfig.format = FCT::FormatToString(depthStencil.format);
                    }
                }

                for (const auto& texture : passDesc.textures)
                {
                    if (imageNameToId.count(texture.name) > 0)
                    {
                        int imageId = imageNameToId[texture.name];
                        addTextureLink(imageId, passId);
                    }
                }

                for (size_t i = 0; i < passDesc.targets.size() && i < 9; ++i)
                {
                    const auto& target = passDesc.targets[i];
                    if (imageNameToId.count(target.name) > 0)
                    {
                        int imageId = imageNameToId[target.name];
                        addTargetLink(passId, static_cast<int>(i), imageId);
                    }
                }

                for (const auto& depthStencil : passDesc.depthStencils)
                {
                    if (imageNameToId.count(depthStencil.name) > 0)
                    {
                        int imageId = imageNameToId[depthStencil.name];
                        addDepthStencilLink(passId, imageId);
                    }
                }
            }

            autoLayoutGraph();
        }

        std::vector<FCT::PassDesc> RenderGraphEditorComposite::convertCurrentGraphToPassDescs()
        {
            std::vector<FCT::PassDesc> passDescs;
            auto* uiHost = Host::UIHost::InstancePtr();
            FCT::Window* wnd = uiHost ? uiHost->getWindow() : nullptr;

            for (const auto& [id, pass] : m_passes)
            {
                FCT::PassDesc passDesc(pass.name);

                if (pass.enableClear)
                {
                    FCT::ClearTypes clearTypes(0);
                    if (pass.enableClearTarget) clearTypes |= FCT::ClearType::color;
                    if (pass.enableClearDepth) clearTypes |= FCT::ClearType::depth;
                    if (pass.enableClearStencil) clearTypes |= FCT::ClearType::stencil;

                    passDesc.clear = FCT::EnablePassClear(
                        clearTypes,
                        FCT::Vec4(pass.clearColor[0], pass.clearColor[1], pass.clearColor[2], pass.clearColor[3]),
                        pass.clearDepth,
                        static_cast<uint8_t>(pass.clearStencil)
                    );
                }

                for (int texturePinId : pass.texturePins)
                {
                    if (m_passInputLinks.count(texturePinId))
                    {
                        auto& linkInfo = m_passInputLinks[texturePinId];
                        if (m_pinInfoMap.count(linkInfo.startPinId))
                        {
                            auto& pinInfo = m_pinInfoMap[linkInfo.startPinId];
                            if (m_images.count(pinInfo.nodeId))
                            {
                                auto& image = m_images[pinInfo.nodeId];
                                FCT::Texture texture(image.name);
                                passDesc.textures.push_back(texture);
                            }
                        }
                    }
                }

                for (int i = 0; i < 9; ++i)
                {
                    uint32_t targetPinId = pass.targetPinId(i);
                    if (m_passOutputlinks.count(targetPinId))
                    {
                        auto& linkInfo = m_passOutputlinks[targetPinId];
                        if (m_pinInfoMap.count(linkInfo.endPinId))
                        {
                            auto& pinInfo = m_pinInfoMap[linkInfo.endPinId];
                            if (m_images.count(pinInfo.nodeId))
                            {
                                auto& image = m_images[pinInfo.nodeId];
                                FCT::Target target(image.name);

                                if (pass.targetDesc[i].useCustomFormat)
                                {
                                    target.format = FCT::StringToFormat(pass.targetDesc[i].format.c_str());
                                }
                                if (pass.targetDesc[i].useCustomSize)
                                {
                                    target.processArgs(static_cast<uint32_t>(pass.targetDesc[i].customWidth),
                                                       static_cast<uint32_t>(pass.targetDesc[i].customHeight));
                                }
                                if (pass.targetDesc[i].isWindow && wnd)
                                {
                                    target.processArgs(wnd);
                                }
                                passDesc.targets.push_back(target);
                            }
                        }
                    }
                }

                uint32_t depthPinId = pass.depthStencilPinId();
                if (m_passOutputlinks.count(depthPinId))
                {
                    auto& linkInfo = m_passOutputlinks[depthPinId];
                    if (m_pinInfoMap.count(linkInfo.endPinId))
                    {
                        auto& pinInfo = m_pinInfoMap[linkInfo.endPinId];
                        if (m_images.count(pinInfo.nodeId))
                        {
                            auto& image = m_images[pinInfo.nodeId];
                            FCT::DepthStencil depthStencil(image.name);

                            if (pass.depthStencilDesc.enabled)
                            {
                                if (pass.depthStencilDesc.useCustomFormat)
                                {
                                    depthStencil.format = FCT::StringToFormat(pass.depthStencilDesc.format.c_str());
                                }
                                if (pass.depthStencilDesc.useCustomSize)
                                {
                                    depthStencil.processArgs(static_cast<uint32_t>(pass.depthStencilDesc.customWidth),
                                                            static_cast<uint32_t>(pass.depthStencilDesc.customHeight));
                                }
                                if (pass.depthStencilDesc.isWindow && wnd)
                                {
                                    depthStencil.processArgs(wnd);
                                }
                            }
                            passDesc.depthStencils.push_back(depthStencil);
                        }
                    }
                }

                passDescs.push_back(passDesc);
            }

            return passDescs;
        }

        void RenderGraphEditorComposite::autoLayoutGraph()
        {
            if (m_passes.empty() && m_images.empty())
                return;

            const float NODE_HEIGHT = 220.0f;
            const float HORIZONTAL_SPACING = 360.0f;
            const float VERTICAL_SPACING = 180.0f;

            std::map<int, std::set<int>> dependencies;
            std::map<int, int> nodeLevels;

            for (const auto& [passId, pass] : m_passes)
            {
                dependencies[passId] = std::set<int>();
            }
            for (const auto& [imageId, image] : m_images)
            {
                dependencies[imageId] = std::set<int>();
            }

            for (const auto& [pinId, linkInfo] : m_passOutputlinks)
            {
                if (m_pinInfoMap.count(linkInfo.startPinId) && m_pinInfoMap.count(linkInfo.endPinId))
                {
                    auto startPinInfo = m_pinInfoMap[linkInfo.startPinId];
                    auto endPinInfo = m_pinInfoMap[linkInfo.endPinId];
                    if (m_passes.count(startPinInfo.nodeId) && m_images.count(endPinInfo.nodeId))
                    {
                        dependencies[endPinInfo.nodeId].insert(startPinInfo.nodeId);
                    }
                }
            }

            for (const auto& [pinId, linkInfo] : m_passInputLinks)
            {
                if (m_pinInfoMap.count(pinId) && m_pinInfoMap.count(linkInfo.startPinId))
                {
                    auto endPinInfo = m_pinInfoMap[pinId];
                    auto startPinInfo = m_pinInfoMap[linkInfo.startPinId];
                    if (m_images.count(startPinInfo.nodeId) && m_passes.count(endPinInfo.nodeId))
                    {
                        dependencies[endPinInfo.nodeId].insert(startPinInfo.nodeId);
                    }
                }
            }

            std::set<int> visiting;
            std::function<int(int)> calculateLevel = [&](int nodeId) -> int {
                if (nodeLevels.count(nodeId))
                    return nodeLevels[nodeId];

                if (visiting.count(nodeId))
                    return 0; // 防止环路无限递归
                visiting.insert(nodeId);

                int maxDepLevel = 0;
                for (int depNodeId : dependencies[nodeId])
                {
                    maxDepLevel = std::max(maxDepLevel, calculateLevel(depNodeId) + 1);
                }
                visiting.erase(nodeId);

                nodeLevels[nodeId] = maxDepLevel;
                return maxDepLevel;
            };

            for (const auto& [passId, pass] : m_passes)
            {
                calculateLevel(passId);
            }
            for (const auto& [imageId, image] : m_images)
            {
                calculateLevel(imageId);
            }

            std::map<int, std::vector<std::pair<int, std::string>>> levelGroups;
            for (const auto& [passId, level] : nodeLevels)
            {
                if (m_passes.count(passId))
                {
                    levelGroups[level].push_back({passId, "pass"});
                }
            }
            for (const auto& [imageId, level] : nodeLevels)
            {
                if (m_images.count(imageId))
                {
                    levelGroups[level].push_back({imageId, "image"});
                }
            }

            float currentX = 60.0f;
            for (const auto& [level, nodes] : levelGroups)
            {
                float startY = 80.0f;

                std::vector<std::pair<int, std::string>> sortedNodes = nodes;
                std::sort(sortedNodes.begin(), sortedNodes.end(),
                          [this](const auto& a, const auto& b) {
                              if (a.second != b.second)
                              {
                                  return a.second == "pass";
                              }
                              if (a.second == "pass")
                              {
                                  return m_passes[a.first].name < m_passes[b.first].name;
                              }
                              else
                              {
                                  return m_images[a.first].name < m_images[b.first].name;
                              }
                          });

                for (size_t i = 0; i < sortedNodes.size(); ++i)
                {
                    int nodeId = sortedNodes[i].first;
                    std::string nodeType = sortedNodes[i].second;
                    float posX = currentX;
                    float posY = startY + static_cast<float>(i) * (NODE_HEIGHT + VERTICAL_SPACING);

                    if (m_imnodesCtx && m_context)
                    {
                        ImNodes::SetCurrentContext(m_imnodesCtx);
                        ImNodes::EditorContextSet(m_context);
                        ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(posX, posY));
                    }
                    if (nodeType == "pass")
                    {
                        m_passes[nodeId].pos[0] = posX;
                        m_passes[nodeId].pos[1] = posY;
                    }
                    else
                    {
                        m_images[nodeId].pos[0] = posX;
                        m_images[nodeId].pos[1] = posY;
                    }
                }

                currentX += HORIZONTAL_SPACING;
            }
        }

    } // namespace MQEditor
} // namespace MQEngine
