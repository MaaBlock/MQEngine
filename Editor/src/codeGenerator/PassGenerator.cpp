#include "PassGenerator.h"

#include <fstream>
#include <functional>
#include <iostream>
#include <Windows.h>
#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    /**
     * 临时代码
     * @param save
     * @return
     */
    static std::string s_lastDirectory = "";
    std::string openPassGraphFileDialog(bool save)
    {
#ifdef _WIN32
        OPENFILENAMEA ofn;
        char szFile[260] = {0};

        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "Pass Graph Files\0*.pgraph\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = s_lastDirectory.empty() ? NULL : s_lastDirectory.c_str();
        ofn.lpstrTitle = save ? "保存Pass图表" : "打开Pass图表";

        if (save) {
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
            if (GetSaveFileNameA(&ofn)) {
                std::string filename = szFile;
                if (filename.find(".pgraph") == std::string::npos) {
                    filename += ".pgraph";
                }
                size_t lastSlash = filename.find_last_of("\\/");
                if (lastSlash != std::string::npos) {
                    s_lastDirectory = filename.substr(0, lastSlash);
                }
                return filename;
            }
        } else {
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&ofn)) {
                std::string filename = szFile;
                size_t lastSlash = filename.find_last_of("\\/");
                if (lastSlash != std::string::npos) {
                    s_lastDirectory = filename.substr(0, lastSlash);
                }
                return filename;
            }
        }
        return "";
#else
        std::cout << (save ? "请输入保存文件名: " : "请输入加载文件名: ");
        std::string filename;
        std::getline(std::cin, filename);
        if (!filename.empty() && filename.find(".pgraph") == std::string::npos) {
            filename += ".pgraph";
        }
        return filename;
#endif
    }
    /**
     *  @brief 不会从pass里删除自己
     * @param pinHash
     */
    void PassGenerator::removePassPin(int pinHash)
    {
        if (m_passOutputlinks.count(pinHash))
        {
            m_passOutputlinks.erase(pinHash);
        }
        if (m_passInputLinks.count(pinHash))
        {
            m_passInputLinks.erase(pinHash);
        }
        m_pinInfoMap.erase(pinHash);
    }
    void PassGenerator::deletePass(int contextMenuNodeId)
    {
        for (int i = 0; i < 9; ++i)
        {
            uint32_t pinId = generatePinId(contextMenuNodeId, "target", i);
            removePassPin(pinId);
        }
        uint32_t depthPinId = generatePinId(contextMenuNodeId, "depth");
        removePassPin(depthPinId);
        auto passes = m_passes[contextMenuNodeId];
        for (auto& pin : passes.texturePins)
        {
            removePassPin(pin);
        }
        m_passes.erase(contextMenuNodeId);
    }
    void PassGenerator::removeImagePin(int pinHash)
    {
        std::vector<int> m_needRemovePassOutputLinks;
        for (auto& outputIt : m_passOutputlinks) {
            if (outputIt.second.endPinId == pinHash)
            {
                m_needRemovePassOutputLinks.push_back(outputIt.first);
            }
        }
        for (int id : m_needRemovePassOutputLinks)
        {
            m_passOutputlinks.erase(id);
        }

        std::vector<int> m_needRemovePassInputLinks;
        for (auto& inputIt : m_passInputLinks)
        {
            if (inputIt.second.startPinId == pinHash)
            {
                m_needRemovePassInputLinks.push_back(inputIt.first);

            }
        }
        for (int id : m_needRemovePassInputLinks)
        {
            auto info = m_pinInfoMap[id];
            m_passes[info.nodeId].removeTexturePin(id);
            m_passInputLinks.erase(id);
            m_pinInfoMap.erase(id);
        }

        if (m_pinInfoMap.count(pinHash))
        {
            m_pinInfoMap.erase(pinHash);
        }
    }
    void PassGenerator::deleteImage(int contextMenuNodeId)
    {
        uint32_t targetInputPinId = generatePinId(contextMenuNodeId, "target", 0);
        removeImagePin(targetInputPinId);

        uint32_t depthInputPinId = generatePinId(contextMenuNodeId, "depth", 0);
        removeImagePin(depthInputPinId);

        uint32_t texturePinId = generatePinId(contextMenuNodeId, "texture", 0);
        removeImagePin(texturePinId);

        m_images.erase(contextMenuNodeId);
        std::cout << "Deleted Image node: " << contextMenuNodeId << std::endl;
    }
    void PassGenerator::deleteNode(int contextMenuNodeId)
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

    void PassGenerator::render()
    {
        ImGui::Begin(TEXT("Pass代码生成器"));

        if (ImGui::Button(TEXT("保存图表...")))
        {
            std::string filename = openPassGraphFileDialog(true);
            if (!filename.empty()) {
                saveToFile(filename);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(TEXT("读取图表...")))
        {
            std::string filename = openPassGraphFileDialog(false);
            if (!filename.empty()) {
                loadFromFile(filename);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT("清空图表")))
        {
            m_passes.clear();
            m_images.clear();
            m_pinInfoMap.clear();
            m_passOutputlinks.clear();
            m_passInputLinks.clear();
            m_nextNodeId = 0;
            m_linkId = 0;
            std::cout << "已清空图表" << std::endl;
        }

        ImGui::Text(TEXT("节点数: Pass(%zu) Image(%zu) 连接数: %zu"),
                    m_passes.size(), m_images.size(),
                    m_passOutputlinks.size() + m_passInputLinks.size());

        ImNodes::BeginNodeEditor();

        for (auto& [id, pass] : m_passes)
        {
            renderPassNode(pass);
        }

        for (auto& [id, image] : m_images)
        {
            renderImageNode(image);
        }

        // 处理右键菜单添加节点


        for (auto link : m_passOutputlinks)
        {
            ImNodes::Link(link.second.id,link.second.startPinId, link.second.endPinId);
        }
        for (auto link : m_passInputLinks)
        {
            ImNodes::Link(link.second.id, link.second.startPinId, link.second.endPinId);
        }
        bool isEdtorHover = ImNodes::IsEditorHovered();
        ImNodes::MiniMap();
        ImNodes::EndNodeEditor();
        int startAttr, endAttr;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
        {
            addLink(startAttr,endAttr);
            std::cout << "Created link between attributes: " << startAttr << " and " << endAttr << std::endl;
            printPinkInfo(startAttr);
            printPinkInfo(endAttr);
        }
        int droppedLinkAttr;
        if (ImNodes::IsLinkDropped(&droppedLinkAttr, /*including_detached_links=*/true))
        {
            auto info = m_pinInfoMap[droppedLinkAttr];
            if (m_passes.count(info.nodeId))
            {
                if (m_passOutputlinks.count(droppedLinkAttr))
                    m_passOutputlinks.erase(droppedLinkAttr);
                if (info.pinType == "texture")
                {
                    if (m_passInputLinks.count(droppedLinkAttr))
                    {
                        m_passes[info.nodeId].removeTexturePin(droppedLinkAttr);
                        m_passInputLinks.erase(droppedLinkAttr);
                        m_pinInfoMap.erase(droppedLinkAttr);
                    }
                }
            }
        }
        int link_id;
        if (ImNodes::IsLinkDestroyed(&link_id))
        {
            std::cout << "Destroyed link ID: " << link_id << std::endl;
        }


        int hoveredNodeId = -1;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && isEdtorHover)
        {
            if (!ImNodes::IsNodeHovered(&hoveredNodeId))
            {
                ImGui::OpenPopup("AddNodePopup");
            } else
            {
                ImGui::OpenPopup("NodeContextMenu");
                m_contextMenuNodeId = hoveredNodeId;
            }
        }

        if (ImGui::BeginPopup("AddNodePopup"))
        {
            if (ImGui::MenuItem(TEXT("添加Pass节点")))
            {
                addPassNode();
            }
            if (ImGui::MenuItem(TEXT("添加Image节点")))
            {
                addImageNode();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("NodeContextMenu"))
        {
            if (m_passes.count(m_contextMenuNodeId))
            {
                ImGui::Text(TEXT("Pass节点: %s"), m_passes[m_contextMenuNodeId].name.c_str());
            }
            else if (m_images.count(m_contextMenuNodeId))
            {
                ImGui::Text(TEXT("Image节点: %s"), m_images[m_contextMenuNodeId].name.c_str());
            }

            ImGui::Separator();

            if (ImGui::MenuItem(TEXT("删除节点")))
            {
                deleteNode(m_contextMenuNodeId);
            }

            ImGui::EndPopup();
        }

        ImGui::End();
    }
    void PassGenerator::addLink(int startHash, int endHash)
    {
        auto startPin = m_pinInfoMap[startHash];
        auto endPin = m_pinInfoMap[endHash];
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
        } else if (m_images.count(startPin.nodeId))
        {
            if (startPin.pinType == endPin.pinType)
            {
                if (!m_passInputLinks.count(endHash))
                    newTexturePin(m_passes[endPin.nodeId]);
                LinkInfo info;
                info.id = getNextLinkId();
                info.startPinId = startHash;
                info.endPinId = endHash;
                m_passInputLinks[endHash] = info;
            }
        } else
        {

        }
    }
    void PassGenerator::printPinkInfo(int hash)
    {
        auto info = m_pinInfoMap[hash];
        if (m_passes.count(info.nodeId))
        {
            std::cout << "Pink info for pass " << info.nodeId << ": " << info.pinType << std::endl;
        }
        else if (m_images.count(info.nodeId))
        {
            std::cout << "Pink info for image " << info.nodeId << ": " << info.pinType << std::endl;
        }
        else
        {

        }
    }

    void PassGenerator::addPassNode(const std::string& name)
    {
        PassNode newPass;
        newPass.id = getNextNodeId();
        newPass.name = name + " " + std::to_string(newPass.id);
        newPass.enableClear = true;
        newPass.clearTarget = true;
        newPass.clearDepthStencil = true;

        m_passes[newPass.id] = newPass;
        newTexturePin(m_passes[newPass.id]);
    }
    void PassGenerator::newTexturePin(PassNode& pass)
    {
        int id = generatePinId(pass.id,"texture",++pass.texturePinIndex);
        pass.texturePins.push_back(id);
    }
    void PassGenerator::removeTexturePin()
    {

    }
    void PassGenerator::addImageNode(const std::string& name)
    {
        ImageNode newImage;
        newImage.id = getNextNodeId();
        newImage.name = name.empty() ? "Image " + std::to_string(newImage.id) : name + " " + std::to_string(newImage.id);

        m_images[newImage.id] = newImage;
    }
    void PassGenerator::saveToFile(const std::string& filename)
    {
        try
        {
            std::cout << "保存前数据统计:" << std::endl;
            std::cout << "  Passes: " << m_passes.size() << std::endl;
            std::cout << "  Images: " << m_images.size() << std::endl;
            std::cout << "  OutputLinks: " << m_passOutputlinks.size() << std::endl;
            std::cout << "  InputLinks: " << m_passInputLinks.size() << std::endl;
            std::cout << "  PinInfo: " << m_pinInfoMap.size() << std::endl;

            std::map<int, std::pair<float, float>> nodePositions;
            for (const auto& [id, pass] : m_passes)
            {
                ImVec2 pos = ImNodes::GetNodeGridSpacePos(pass.id);
                nodePositions[id] = {pos.x, pos.y};
            }
            for (const auto& [id, image] : m_images)
            {
                ImVec2 pos = ImNodes::GetNodeGridSpacePos(image.id);
                nodePositions[id] = {pos.x, pos.y};
            }

            std::ofstream posFile(filename + ".pos");
            boost::archive::text_oarchive posOa(posFile);
            posOa << nodePositions;
            posFile.close();

            std::ofstream ofs(filename);
            boost::archive::text_oarchive oa(ofs);
            oa << *this;

            std::cout << "保存成功: " << filename << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cout << "保存失败: " << e.what() << std::endl;
        }
    }

void PassGenerator::loadFromFile(const std::string& filename)
    {
        try
        {
            std::ifstream ifs(filename);
            if (!ifs.is_open())
            {
                std::cout << "❌ 无法打开文件: " << filename << std::endl;
                return;
            }

            boost::archive::text_iarchive ia(ifs);
            ia >> *this;
            ifs.close();

            std::cout << "加载后数据统计:" << std::endl;
            std::cout << "  Passes: " << m_passes.size() << std::endl;
            std::cout << "  Images: " << m_images.size() << std::endl;
            std::cout << "  OutputLinks: " << m_passOutputlinks.size() << std::endl;
            std::cout << "  InputLinks: " << m_passInputLinks.size() << std::endl;
            std::cout << "  PinInfo: " << m_pinInfoMap.size() << std::endl;

            std::ifstream posFile(filename + ".pos");
            if (posFile.is_open())
            {
                std::map<int, std::pair<float, float>> nodePositions;
                boost::archive::text_iarchive posIa(posFile);
                posIa >> nodePositions;

                for (const auto& [nodeId, pos] : nodePositions)
                {
                    ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(pos.first, pos.second));
                }
                posFile.close();
            }

            std::cout << "加载成功: " << filename << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cout << "加载失败: " << e.what() << std::endl;
        }
    }

    void PassGenerator::renderPassNode(PassNode& pass)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12);

        ImNodes::BeginNode(pass.id);

        ImGui::PushItemWidth(150);
        // 节点标题
        ImNodes::BeginNodeTitleBar();
        char nameBuffer[256];
        strcpy_s(nameBuffer, pass.name.c_str());
        if (ImGui::InputText("##PassName", nameBuffer, sizeof(nameBuffer)))
        {
            pass.name = nameBuffer;
        }
        ImNodes::EndNodeTitleBar();
        for (auto texturePin : pass.texturePins)
        {
            ImNodes::BeginInputAttribute(texturePin);
            ImGui::Text("Texture");
            ImNodes::EndInputAttribute();
        }
        ImGui::Separator();
        // Clear设置
        ImGui::Checkbox(TEXT("开启Clear"), &pass.enableClear);
        if (pass.enableClear)
        {
            ImGui::Checkbox("Clear Target", &pass.clearTarget);
            if (pass.clearTarget)
            {
                ImGui::ColorEdit4(TEXT("颜色"), pass.clearColor);
            }

            ImGui::Checkbox("Clear Depth/Stencil", &pass.clearDepthStencil);
            if (pass.clearDepthStencil)
            {
                ImGui::SliderFloat(TEXT("深度值"), &pass.clearDepth, 0.0f, 1.0f);
                ImGui::SliderInt(TEXT("模板值"), &pass.clearStencil, 0, 255);
            }
        }

        ImGui::Separator();

        // 输出pins - Color Targets
          for (int i = 0; i < 9; ++i)
          {
              auto& targetConfig = pass.targetDesc[i];
              uint32_t pinId = generatePinId(pass.id, "target", i);
              ImNodes::BeginOutputAttribute(pinId);
              ImGui::Dummy(ImVec2(80, 0));
              ImGui::SameLine();
              ImGui::Text("Target %d", i);
              ImNodes::EndOutputAttribute();

              ImGui::PushID(i);
              ImGui::Checkbox(TEXT("为target指定属性"), &targetConfig.enabled);

              if (targetConfig.enabled)
              {
                  ImGui::Checkbox(TEXT("自定义格式"), &targetConfig.useCustomFormat);

                  if (targetConfig.useCustomFormat)
                  {
                      ImGui::PushItemWidth(150);

                      const char* colorFormats[] = {
                          "R8G8B8A8_UNORM",
                          "R8G8B8A8_SRGB",
                          "B8G8R8A8_UNORM",
                          "B8G8R8A8_SRGB",
                          "R16G16B16A16_SFLOAT",
                          "R32G32B32A32_SFLOAT",
                          "R16G16_SFLOAT",
                          "R32G32_SFLOAT",
                          "R16_SFLOAT",
                          "R32_SFLOAT",
                          "R8_UNORM",
                          "R16_UNORM",
                          "R8G8_UNORM",
                          "R16G16_UNORM"
                      };

                      int currentFormatIndex = 0;
                      for (int j = 0; j < IM_ARRAYSIZE(colorFormats); j++)
                      {
                          if (targetConfig.format == colorFormats[j])
                          {
                              currentFormatIndex = j;
                              break;
                          }
                      }

                      if (ImGui::Combo("##format", &currentFormatIndex, colorFormats, IM_ARRAYSIZE(colorFormats)))
                      {
                          targetConfig.format = colorFormats[currentFormatIndex];
                      }
                      ImGui::PopItemWidth();
                  }

                  ImGui::Checkbox(TEXT("自定义尺寸"), &targetConfig.useCustomSize);

                  if (targetConfig.useCustomSize)
                  {
                      ImGui::PushItemWidth(100);
                      ImGui::InputInt("W##width", &targetConfig.customWidth);
                      ImGui::InputInt("H##height", &targetConfig.customHeight);
                      ImGui::PopItemWidth();
                  }
              }
              ImGui::PopID();
              if (i < 8) ImGui::Separator();
          }

        uint32_t depthPinId = generatePinId(pass.id, "depth");
        ImNodes::BeginOutputAttribute(depthPinId);
        ImGui::Dummy(ImVec2(80, 0));
        ImGui::SameLine();
        ImGui::Text("DepthStencil");
        ImNodes::EndOutputAttribute();

        auto& depthConfig = pass.depthStencilDesc;
        ImGui::Checkbox(TEXT("为DepthStencil指定属性"), &depthConfig.enabled);

        if (depthConfig.enabled)
        {
            ImGui::Checkbox(TEXT("自定义格式##depth"), &depthConfig.useCustomFormat);

            if (depthConfig.useCustomFormat)
            {
                ImGui::PushItemWidth(150);

                const char* depthFormats[] = {
                    "D32_SFLOAT",
                    "D32_SFLOAT_S8_UINT",
                    "D24_UNORM_S8_UINT",
                    "D16_UNORM",
                    "D16_UNORM_S8_UINT"
                };

                int currentDepthFormatIndex = 0;
                for (int j = 0; j < IM_ARRAYSIZE(depthFormats); j++)
                {
                    if (depthConfig.format == depthFormats[j])
                    {
                        currentDepthFormatIndex = j;
                        break;
                    }
                }

                if (ImGui::Combo("##depthformat", &currentDepthFormatIndex, depthFormats, IM_ARRAYSIZE(depthFormats)))
                {
                    depthConfig.format = depthFormats[currentDepthFormatIndex];
                }
                ImGui::PopItemWidth();
            }

            ImGui::Checkbox(TEXT("自定义尺寸##depth"), &depthConfig.useCustomSize);

            if (depthConfig.useCustomSize)
            {
                ImGui::PushItemWidth(100);
                ImGui::InputInt("W##depthwidth", &depthConfig.customWidth);
                ImGui::InputInt("H##depthheight", &depthConfig.customHeight);
                ImGui::PopItemWidth();
            }
        }


        ImGui::PopItemWidth();
        ImNodes::EndNode();

        ImGui::PopStyleVar(3);
    }

    void PassGenerator::renderImageNode(ImageNode& image)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12);

        ImNodes::BeginNode(image.id);

        ImGui::PushItemWidth(80);
        // 节点标题
        ImNodes::BeginNodeTitleBar();
        char nameBuffer[256];
        strcpy_s(nameBuffer, image.name.c_str());
        if (ImGui::InputText("##ImageName", nameBuffer, sizeof(nameBuffer)))
        {
            image.name = nameBuffer;
        }
        ImNodes::EndNodeTitleBar();

        // 输入pins
        uint32_t targetInputPinId = generatePinId(image.id, "target", 0);
        ImNodes::BeginInputAttribute(targetInputPinId);
        ImGui::Text("Target");
        ImNodes::EndInputAttribute();

        uint32_t depthInputPinId = generatePinId(image.id, "depth", 0);
        ImNodes::BeginInputAttribute(depthInputPinId);
        ImGui::Text("DepthStencil");
        ImNodes::EndInputAttribute();

        ImGui::Separator();

        uint32_t texturePinId = generatePinId(image.id, "texture", 0);
        ImNodes::BeginOutputAttribute(texturePinId);
        ImGui::Dummy(ImVec2(80, 0));
        ImGui::SameLine();
        ImGui::Text("Texture");
        ImNodes::EndOutputAttribute();

        ImGui::PopItemWidth();
        ImNodes::EndNode();

        ImGui::PopStyleVar(3);
    }

    int PassGenerator::generatePinId(int nodeId, const std::string& pinType, int index)
    {
        std::hash<std::string> hasher;
        std::string pinString = std::to_string(nodeId) + "_" + pinType + "_" + std::to_string(index);

        int pinId = static_cast<uint32_t>(hasher(pinString));

        PinInfo info;
        info.nodeId = nodeId;
        info.pinType = pinType;
        info.index = index;
        m_pinInfoMap[pinId] = info;

        return pinId;
    }
}