#include "RenderGraphViewer.h"
#include "../thirdparty/thirdparty.h"
#include <fstream>
#include <functional>
#include <iostream>
#define NOMINMAX
#ifdef _WIN32
#include <Windows.h>
#endif

#define TEXT(str) (const char*)u8##str
using namespace FCT;
namespace MQEngine
{
    /**
     * 临时代码c
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
    void RenderGraphViewer::removePassPin(int pinHash)
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
    void RenderGraphViewer::deletePass(int contextMenuNodeId)
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
    void RenderGraphViewer::removeImagePin(int pinHash)
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
    void RenderGraphViewer::deleteImage(int contextMenuNodeId)
    {
        uint32_t targetInputPinId = generatePinId(contextMenuNodeId, "target", 0);
        removeImagePin(targetInputPinId);

        uint32_t depthInputPinId = generatePinId(contextMenuNodeId, "depth", 0);
        removeImagePin(depthInputPinId);

        uint32_t texturePinId = generatePinId(contextMenuNodeId, "texture", 0);
        removeImagePin(texturePinId);

        m_images.erase(contextMenuNodeId);
    }
    void RenderGraphViewer::deleteNode(int contextMenuNodeId)
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


    void RenderGraphViewer::render()
    {
        ImNodes::EditorContextSet(m_context);
        ImGui::Begin("Pass代码生成器");

        if (ImGui::Button("保存图表..."))
        {
            std::string filename = openPassGraphFileDialog(true);
            if (!filename.empty()) {
                saveToFile(filename);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("读取图表..."))
        {
            std::string filename = openPassGraphFileDialog(false);
            if (!filename.empty()) {
                loadFromFile(filename);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("清空图表"))
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
        ImGui::SameLine();
        if (ImGui::Button("整理图表"))
        {
            autoLayoutGraph();
        }
        ImGui::SameLine();
        if (ImGui::Button("读取图表从当前"))
        {
            auto rg = m_ctx->getModule<FCT::RenderGraph>();
            if (rg)
            {
                try
                {
                    // 从当前渲染图表获取PassDesc
                    std::vector<FCT::PassDesc> passDescs = rg->getOriginalPasses();
                    if (!passDescs.empty())
                    {
                        createGraphFromPassDescs(passDescs);
                        std::cout << "成功从当前渲染图表读取了 " << passDescs.size() << " 个Pass" << std::endl;
                    }
                    else
                    {
                        std::cout << "当前渲染图表中没有Pass" << std::endl;
                    }
                }
                catch (const std::exception& e)
                {
                    std::cout << "读取当前图表失败: " << e.what() << std::endl;
                }
            }
            else
            {
                std::cout << "无法获取RenderGraph模块" << std::endl;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("编译图表到当前"))
        {
            auto rg = m_ctx->getModule<FCT::RenderGraph>();
            if (rg)
            {
                // 将当前图表转换为PassDesc
                std::vector<FCT::PassDesc> passDescs = convertCurrentGraphToPassDescs();
                
                if (!passDescs.empty())
                {
                    // 清除原有的Pass
                    rg->clearOriginalPasses();
                    
                    // 添加新的Pass
                    for (const auto& passDesc : passDescs)
                    {
                        rg->addPassDesc(passDesc);
                    }
                    
                    // 重新编译
                    rg->recompile();
                    
                    std::cout << "成功编译图表到当前，共 " << passDescs.size() << " 个Pass" << std::endl;
                }
                else
                {
                    std::cout << "当前图表为空，无法编译" << std::endl;
                }
            }
            else
            {
                std::cout << "无法获取RenderGraph模块" << std::endl;
            }
        }

        ImGui::Text("节点数: Pass(%zu) Image(%zu) 连接数: %zu",
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

        ImGui::Separator();

        // 代码生成按钮
        if (ImGui::Button("生成代码"))
        {
            m_generatedCode = generatorCode();
        }
        ImGui::SameLine();
        if (ImGui::Button("清空代码"))
        {
            m_generatedCode.clear();
        }

        // 代码展示区域
        if (!m_generatedCode.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("复制到剪贴板"))
            {
                ImGui::SetClipboardText(m_generatedCode.c_str());
            }

            ImGui::Text("生成的代码:");

            ImGui::BeginChild("CodeDisplay", ImVec2(0, 400), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(m_generatedCode.c_str());
            ImGui::EndChild();
        }

        int startAttr, endAttr;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
        {
            addLink(startAttr,endAttr);
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
            if (ImGui::MenuItem("添加Pass节点"))
            {
                newPassNode();
            }
            if (ImGui::MenuItem("添加Image节点"))
            {
                newImageNode();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("NodeContextMenu"))
        {
            if (m_passes.count(m_contextMenuNodeId))
            {
                ImGui::Text("Pass节点: %s", m_passes[m_contextMenuNodeId].name.c_str());
            }
            else if (m_images.count(m_contextMenuNodeId))
            {
                ImGui::Text("Image节点: %s", m_images[m_contextMenuNodeId].name.c_str());
            }

            ImGui::Separator();

            if (ImGui::MenuItem("删除节点"))
            {
                deleteNode(m_contextMenuNodeId);
            }

            ImGui::EndPopup();
        }

        ImGui::End();
    }
    void RenderGraphViewer::addLink(int startHash, int endHash)
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

    int RenderGraphViewer::newPassNode(const std::string& name)
    {
        PassNode newPass;
        newPass.id = getNextNodeId();
        newPass.name = findPassNode(name) == -1 ? name : name + " " + std::to_string(newPass.id);
        newPass.enableClear = false;
        newPass.enableClearDepth = false;
        newPass.enableClearStencil = true;
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
    void RenderGraphViewer::newTexturePin(PassNode& pass)
    {
        int id = generatePinId(pass.id,"texture",++pass.texturePinIndex);
        pass.texturePins.push_back(id);
    }

    int RenderGraphViewer::newImageNode(const std::string& name)
    {
        ImageNode newImage;
        newImage.id = getNextNodeId();

        newImage.name = findImageNode(name) == -1 ? name : name + " " + std::to_string(newImage.id);

        m_images[newImage.id] = newImage;
        generatePinId(newImage.id, "texture", 0);
        generatePinId(newImage.id, "target", 0);
        generatePinId(newImage.id, "depth", 0);
        return newImage.id;
    }

    int RenderGraphViewer::findImageNode(const std::string& name)
    {
        for (const auto& [id, image] : m_images)
        {
            if (image.name == name)
                return id;
        }
        return -1;
    }

    int RenderGraphViewer::findPassNode(const std::string& name)
    {
        for (const auto& [id, pass] : m_passes)
            if (pass.name == name)
                return id;
        return -1;
    }

    RenderGraphViewer::RenderGraphViewer(FCT::Context* ctx, FCT::Window* wnd)
    {
        m_ctx = ctx;
        m_wnd = wnd;
        m_context = ImNodes::EditorContextCreate();
    }

    RenderGraphViewer::~RenderGraphViewer()
    {
        ImNodes::EditorContextFree(m_context);
    }


    void RenderGraphViewer::addTextureLink(int imageId, int passId)
    {
        if (m_images.count(imageId) && m_passes.count(passId))
        {
            auto passes = m_passes[passId];
            auto image = m_images[imageId];
            addLink(image.texturePinId(), passes.lastTexturePinId());
        }
    }

    void RenderGraphViewer::addTargetLink(int passId, int index, int imageId)
    {
        if (m_passes.count(passId) && m_images.count(imageId))
        {
            auto pass = m_passes[passId];
            auto image = m_images[imageId];
            addLink(pass.targetPinId(index), image.targetPinId());
        }
    }

    void RenderGraphViewer::addDepthStencilLink(int passId, int imageId)
    {
        if (m_passes.count(passId) && m_images.count(imageId))
        {
            addLink(m_passes[passId].depthStencilPinId(), m_images[imageId].depthStencilPinId());
        }
    }

    void RenderGraphViewer::createGraphFromPassDescs(const std::vector<FCT::PassDesc>& passDescs)
    {
        m_passes.clear();
        m_images.clear();
        m_pinInfoMap.clear();
        m_passOutputlinks.clear();
        m_passInputLinks.clear();
        m_nextNodeId = 0;
        m_linkId = 0;

        std::cout << "开始从PassDesc创建图表，共 " << passDescs.size() << " 个Pass" << std::endl;

        std::map<std::string, int> imageNameToId;
        
        for (const auto& passDesc : passDescs)
        {
            for (const auto& texture : passDesc.textures)
            {
                if (imageNameToId.find(texture.name) == imageNameToId.end())
                {
                    int imageId = newImageNode(texture.name);
                    imageNameToId[texture.name] = imageId;
                    std::cout << "创建Image节点: " << texture.name << " (ID: " << imageId << ")" << std::endl;
                }
            }

            for (const auto& target : passDesc.targets)
            {
                if (imageNameToId.find(target.name) == imageNameToId.end())
                {
                    int imageId = newImageNode(target.name);
                    imageNameToId[target.name] = imageId;
                    std::cout << "创建Image节点: " << target.name << " (ID: " << imageId << ")" << std::endl;
                }
            }

            for (const auto& depthStencil : passDesc.depthStencils)
            {
                if (imageNameToId.find(depthStencil.name) == imageNameToId.end())
                {
                    int imageId = newImageNode(depthStencil.name);
                    imageNameToId[depthStencil.name] = imageId;
                    std::cout << "创建Image节点: " << depthStencil.name << " (ID: " << imageId << ")" << std::endl;
                }
            }
        }

        for (const auto& passDesc : passDescs)
        {
            int passId = newPassNode(passDesc.name);
            std::cout << "创建Pass节点: " << passDesc.name << " (ID: " << passId << ")" << std::endl;

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
                    targetConfig.format = FormatToString(target.format);
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
                    depthConfig.format = FormatToString(depthStencil.format);
                }
            }

            for (const auto& texture : passDesc.textures)
            {
                if (imageNameToId.count(texture.name) > 0)
                {
                    int imageId = imageNameToId[texture.name];
                    addTextureLink(imageId, passId);
                    std::cout << "建立texture连接: " << texture.name << " -> " << passDesc.name << std::endl;
                }
            }

            for (size_t i = 0; i < passDesc.targets.size(); ++i)
            {
                const auto& target = passDesc.targets[i];
                if (imageNameToId.count(target.name) > 0)
                {
                    int imageId = imageNameToId[target.name];
                    addTargetLink(passId, i, imageId);
                    std::cout << "建立target连接: " << passDesc.name << "[" << i << "] -> " << target.name << std::endl;
                }
            }

            for (const auto& depthStencil : passDesc.depthStencils)
            {
                if (imageNameToId.count(depthStencil.name) > 0)
                {
                    int imageId = imageNameToId[depthStencil.name];
                    addDepthStencilLink(passId, imageId);
                    std::cout << "建立depthStencil连接: " << passDesc.name << " -> " << depthStencil.name << std::endl;
                }
            }
        }

        std::cout << "图表创建完成！" << std::endl;
        std::cout << "  Passes: " << m_passes.size() << std::endl;
        std::cout << "  Images: " << m_images.size() << std::endl;
        std::cout << "  OutputLinks: " << m_passOutputlinks.size() << std::endl;
        std::cout << "  InputLinks: " << m_passInputLinks.size() << std::endl;
        autoLayoutGraph();
    }


    void RenderGraphViewer::saveToFile(const std::string& filename)
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
    std::string RenderGraphViewer::generatePassCode(const PassNode& pass)
    {
        std::string code;

        code += "graph->addPass(\n";
        code += "    \"" + pass.name + "\",\n";
        for (int texturePin : pass.texturePins)
        {
            if (m_passInputLinks.count(texturePin))
            {
                auto linkInfo = m_passInputLinks.at(texturePin);
                auto startPinInfo = m_pinInfoMap.at(linkInfo.startPinId);

                if (m_images.count(startPinInfo.nodeId))
                {
                    auto& sourceImage = m_images.at(startPinInfo.nodeId);
                    code += "    Texture(\"" + sourceImage.name + "\"),\n";
                }
            }
        }
        if (pass.enableClear && (pass.enableClearDepth || pass.enableClearStencil || pass.enableClearTarget))
        {
            std::string clearType;
            std::vector<std::string> clearTypes;

            if (pass.enableClearTarget)
            {
                clearTypes.push_back("ClearType::color");
            }
            if (pass.enableClearDepth)
            {
                clearTypes.push_back("ClearType::depth");
            }
            if (pass.enableClearStencil)
            {
                clearTypes.push_back("ClearType::stencil");
            }

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
            code += "    EnablePassClear(" + clearType;

            bool needColorParam = pass.enableClearTarget &&
                !(pass.clearColor[0] == 0.0f && pass.clearColor[1] == 0.0f &&
                  pass.clearColor[2] == 0.0f && pass.clearColor[3] == 1.0f);

            bool needDepthParam = pass.enableClearDepth && (pass.clearDepth != 1.0f);

            bool needStencilParam = pass.enableClearStencil && (pass.clearStencil != 0);

            if (needColorParam)
            {
                code += ",\n        Vec4(" + std::to_string(pass.clearColor[0]) + ","
                        + std::to_string(pass.clearColor[1]) + ","
                        + std::to_string(pass.clearColor[2]) + ","
                        + std::to_string(pass.clearColor[3]) + ")";
            }

            if (needDepthParam)
            {
                code += needColorParam ? "," : ",";
                code += std::to_string(pass.clearDepth) + "f";
            }

            if (needStencilParam)
            {
                code += (needColorParam || needDepthParam) ? "," : ",";
                code += std::to_string(pass.clearStencil);
            }

            code += "),\n";
        }
        for (int i = 0; i < 9; ++i)
        {
            uint32_t targetPinId = generatePinId(pass.id, "target", i);
            if (m_passOutputlinks.count(targetPinId))
            {
                auto linkInfo = m_passOutputlinks.at(targetPinId);
                auto endPinInfo = m_pinInfoMap.at(linkInfo.endPinId);

                if (m_images.count(endPinInfo.nodeId))
                {
                    auto& targetImage = m_images.at(endPinInfo.nodeId);
                    const auto& targetConfig = pass.targetDesc[i];

                    code += "    Target(\"" + targetImage.name + "\"";

                    if (targetConfig.enabled)
                    {
                        if (targetConfig.useCustomSize)
                        {
                            code += "," + std::to_string(targetConfig.customWidth)
                                    + ", " + std::to_string(targetConfig.customHeight);
                        }

                        if (targetConfig.useCustomFormat)
                        {
                            code += ", Format::" + targetConfig.format;
                        }
                    }

                    code += "),\n";
                }
            }
        }

        uint32_t depthPinId = generatePinId(pass.id, "depth");
        if (m_passOutputlinks.count(depthPinId))
        {
            auto linkInfo = m_passOutputlinks.at(depthPinId);
            auto endPinInfo = m_pinInfoMap.at(linkInfo.endPinId);

            if (m_images.count(endPinInfo.nodeId))
            {
                auto& depthImage = m_images.at(endPinInfo.nodeId);
                const auto& depthConfig = pass.depthStencilDesc;

                code += "    DepthStencil(\"" + depthImage.name + "\"";

                if (depthConfig.enabled)
                {
                    if (depthConfig.useCustomSize)
                    {
                        code += "," + std::to_string(depthConfig.customWidth)
                                + "," + std::to_string(depthConfig.customHeight);
                    }

                    if (depthConfig.useCustomFormat)
                    {
                        code += ",\n        Format::" + depthConfig.format;
                    }
                }
                code += " )\n";
            }
        }

        if (code.back() == '\n' && code[code.length()-2] == ',')
        {
            code = code.substr(0, code.length()-2) + "\n";
        }

        code += "    );\n\n";

        return code;
    }
    std::string RenderGraphViewer::generatorCode()
    {
        std::string code;

        for (const auto& [passId, pass] : m_passes)
        {
            code += generatePassCode(pass);
        }

        return code;
    }

    void RenderGraphViewer::loadFromFile(const std::string& filename)
    {
        try
        {
            std::ifstream ifs(filename);
            if (!ifs.is_open())
            {
                std::cout << "无法打开文件: " << filename << std::endl;
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

    void RenderGraphViewer::renderPassNode(PassNode& pass)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12);

        ImNodes::BeginNode(pass.id);

        ImNodes::BeginNodeTitleBar();
        char nameBuffer[256];
        snprintf(nameBuffer, sizeof(nameBuffer), "%s", pass.name.c_str());


        float currentTextWidth = ImGui::CalcTextSize(nameBuffer).x;
        float inputWidth = std::max(currentTextWidth + 30.0f, 120.0f); // 最小120px

        ImGui::SetNextItemWidth(inputWidth);
        if (ImGui::InputText("##PassName", nameBuffer, sizeof(nameBuffer)))
        {
            pass.name = nameBuffer;
        }
        ImNodes::EndNodeTitleBar();

        float contentWidth = std::max(inputWidth, 150.0f);

        ImGui::PushItemWidth(contentWidth);
        for (auto texturePin : pass.texturePins)
        {
            ImNodes::BeginInputAttribute(texturePin);
            ImGui::Text("Texture");
            ImNodes::EndInputAttribute();
        }
        ImGui::Separator();
        // Clear设置
        ImGui::Checkbox("开启Clear", &pass.enableClear);
        if (pass.enableClear)
        {
            ImGui::Checkbox("Clear Target", &pass.enableClearTarget);
            if (pass.enableClearTarget)
            {
                ImGui::ColorEdit4("颜色", pass.clearColor);
            }

            ImGui::Checkbox("Clear Depth", &pass.enableClearDepth);
            if (pass.enableClearDepth)
            {
                ImGui::SliderFloat("深度值", &pass.clearDepth, 0.0f, 1.0f);
            }
            ImGui::Checkbox("Clear Stencil", &pass.enableClearStencil);
            if (pass.enableClearStencil)
            {
                ImGui::SliderInt("模板值", &pass.clearStencil, 0, 255);
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
            ImGui::Checkbox("为target指定属性", &targetConfig.enabled);

            if (targetConfig.enabled)
            {
                ImGui::Checkbox("窗口", &targetConfig.isWindow);

                if (targetConfig.isWindow)
                {
                    targetConfig.useCustomFormat = false;
                    targetConfig.useCustomSize = false;

                    ImGui::BeginDisabled();
                    bool tempFormat = false;
                    ImGui::Checkbox("自定义格式", &tempFormat);
                    ImGui::EndDisabled();

                    ImGui::BeginDisabled();
                    bool tempSize = false;
                    ImGui::Checkbox("自定义尺寸", &tempSize);
                    ImGui::EndDisabled();
                }
                else
                {
                    ImGui::Checkbox("自定义格式", &targetConfig.useCustomFormat);

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

                    ImGui::Checkbox("自定义尺寸", &targetConfig.useCustomSize);

                    if (targetConfig.useCustomSize)
                    {
                        ImGui::PushItemWidth(100);
                        ImGui::InputInt("W##width", &targetConfig.customWidth);
                        ImGui::InputInt("H##height", &targetConfig.customHeight);
                        ImGui::PopItemWidth();
                    }
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
        ImGui::Checkbox("为DepthStencil指定属性", &depthConfig.enabled);

        if (depthConfig.enabled)
        {
            ImGui::Checkbox("窗口##depth", &depthConfig.isWindow);
            if (depthConfig.isWindow)
            {
                depthConfig.useCustomFormat = false;
                depthConfig.useCustomSize = false;

                ImGui::BeginDisabled();
                bool tempFormat = false;
                ImGui::Checkbox("自定义格式##depth", &tempFormat);
                ImGui::EndDisabled();

                ImGui::BeginDisabled();
                bool tempSize = false;
                ImGui::Checkbox("自定义尺寸##depth", &tempSize);
                ImGui::EndDisabled();
            } else
            {
                ImGui::Checkbox("自定义格式##depth", &depthConfig.useCustomFormat);

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

                ImGui::Checkbox("自定义尺寸##depth", &depthConfig.useCustomSize);

                if (depthConfig.useCustomSize)
                {
                    ImGui::PushItemWidth(100);
                    ImGui::InputInt("W##depthwidth", &depthConfig.customWidth);
                    ImGui::InputInt("H##depthheight", &depthConfig.customHeight);
                    ImGui::PopItemWidth();
                }
            }
        }


        ImGui::PopItemWidth();
        ImNodes::EndNode();

        ImGui::PopStyleVar(3);
    }

    void RenderGraphViewer::renderImageNode(ImageNode& image)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12);

        ImNodes::BeginNode(image.id);

        ImNodes::BeginNodeTitleBar();
        char nameBuffer[256];
        snprintf(nameBuffer, sizeof(nameBuffer), "%s", image.name.c_str());

        // 计算标题宽度
        float titleWidth = ImGui::CalcTextSize(nameBuffer).x + 30.0f;
        float minWidth = std::max(titleWidth, 100.0f);

        ImGui::SetNextItemWidth(minWidth);
        if (ImGui::InputText("##ImageName", nameBuffer, sizeof(nameBuffer)))
        {
            image.name = nameBuffer;
        }
        ImNodes::EndNodeTitleBar();

        // 调整内容宽度
        ImGui::PushItemWidth(std::max(minWidth - 20.0f, 80.0f));

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

    int RenderGraphViewer::generatePinId(int nodeId, const std::string& pinType, int index)
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


    void RenderGraphViewer::autoLayoutGraph()
    {
        if (m_passes.empty() && m_images.empty())
        {
            std::cout << "图表为空，无需整理" << std::endl;
            return;
        }

        std::cout << "开始自动整理图表布局..." << std::endl;

        const float NODE_WIDTH = 250.0f;
        const float NODE_HEIGHT = 200.0f;
        const float HORIZONTAL_SPACING = 350.0f;
        const float VERTICAL_SPACING = 250.0f;

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
            auto startPinInfo = m_pinInfoMap[linkInfo.startPinId];
            auto endPinInfo = m_pinInfoMap[linkInfo.endPinId];

            if (m_passes.count(startPinInfo.nodeId) && m_images.count(endPinInfo.nodeId))
            {
                dependencies[endPinInfo.nodeId].insert(startPinInfo.nodeId);
                std::cout << "依赖关系: Image[" << m_images[endPinInfo.nodeId].name
                          << "] 依赖于 Pass[" << m_passes[startPinInfo.nodeId].name << "]" << std::endl;
            }
        }

        for (const auto& [pinId, linkInfo] : m_passInputLinks)
        {
            auto endPinInfo = m_pinInfoMap[pinId];
            auto startPinInfo = m_pinInfoMap[linkInfo.startPinId];

            if (m_images.count(startPinInfo.nodeId) && m_passes.count(endPinInfo.nodeId))
            {
                dependencies[endPinInfo.nodeId].insert(startPinInfo.nodeId);
                std::cout << "依赖关系: Pass[" << m_passes[endPinInfo.nodeId].name
                          << "] 依赖于 Image[" << m_images[startPinInfo.nodeId].name << "]" << std::endl;
            }
        }

        std::function<int(int)> calculateLevel = [&](int nodeId) -> int {
            if (nodeLevels.count(nodeId))
                return nodeLevels[nodeId];

            int maxDepLevel = 0;
            for (int depNodeId : dependencies[nodeId])
            {
                maxDepLevel = std::max(maxDepLevel, calculateLevel(depNodeId) + 1);
            }

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

        std::map<int, std::vector<std::pair<int, std::string>>> levelGroups; // level -> [(nodeId, type)]

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

        float currentX = 0.0f;
        for (const auto& [level, nodes] : levelGroups)
        {
            std::cout << "层级 " << level << " 包含 " << nodes.size() << " 个节点" << std::endl;

            float totalHeight = nodes.size() * NODE_HEIGHT + (nodes.size() - 1) * VERTICAL_SPACING;
            float startY = -totalHeight / 2.0f;

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
                float posY = startY + i * (NODE_HEIGHT + VERTICAL_SPACING);

                ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(posX, posY));

                if (nodeType == "pass")
                {
                    std::cout << "设置Pass节点 " << m_passes[nodeId].name
                              << " 位置: (" << posX << ", " << posY << ") 层级: " << level << std::endl;
                }
                else
                {
                    std::cout << "设置Image节点 " << m_images[nodeId].name
                              << " 位置: (" << posX << ", " << posY << ") 层级: " << level << std::endl;
                }
            }

            currentX += HORIZONTAL_SPACING;
        }

        std::cout << "图表布局整理完成！" << std::endl;
        std::cout << "  处理了 " << m_passes.size() << " 个Pass节点" << std::endl;
        std::cout << "  处理了 " << m_images.size() << " 个Image节点" << std::endl;
        std::cout << "  分为 " << levelGroups.size() << " 个渲染层级" << std::endl;
        std::cout << "  X轴跨度: " << (levelGroups.size() - 1) * HORIZONTAL_SPACING << "px" << std::endl;
    }

    std::vector<FCT::PassDesc> RenderGraphViewer::convertCurrentGraphToPassDescs()
    {
        std::vector<FCT::PassDesc> passDescs;

        std::map<std::string, ImageNode> imageNameMap;
        for (const auto& [id, image] : m_images)
        {
            imageNameMap[image.name] = image;
        }

        for (const auto& [id, pass] : m_passes)
        {
            FCT::PassDesc passDesc(pass.name);

             if (pass.enableClear)
             {
                 FCT::ClearTypes clearTypes(0);
                 if (pass.enableClearTarget)
                 {
                     clearTypes |= FCT::ClearType::color;
                 }
                 if (pass.enableClearDepth)
                 {
                     clearTypes |= FCT::ClearType::depth;
                 }
                 if (pass.enableClearStencil)
                 {
                     clearTypes |= FCT::ClearType::stencil;
                 }
                 
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

             for (size_t i = 0; i < 9; ++i)
             {
                 uint32_t targetPinId = generatePinId(pass.id, "target", i);
                 if (m_passOutputlinks.count(targetPinId))
                 {
                     auto& linkInfo = m_passOutputlinks[targetPinId];
                     // 通过pinInfo找到对应的Image node
                     if (m_pinInfoMap.count(linkInfo.endPinId))
                     {
                         auto& pinInfo = m_pinInfoMap[linkInfo.endPinId];
                         if (m_images.count(pinInfo.nodeId))
                         {
                             auto& image = m_images[pinInfo.nodeId];
                             FCT::Target target(image.name);

                             if (pass.targetDesc[i].useCustomFormat)
                             {
                                 target.format = StringToFormat(pass.targetDesc[i].format.c_str());
                             }

                             if (pass.targetDesc[i].useCustomSize)
                             {
                                 target.processArgs(pass.targetDesc[i].customWidth, pass.targetDesc[i].customHeight);
                             }

                             if (pass.targetDesc[i].isWindow)
                             {
                                 target.processArgs(m_wnd);
                             }

                             passDesc.targets.push_back(target);
                         }
                     }
                 }
             }
            
                                                                                                       // 添加depthStencil输出
               uint32_t depthPinId = generatePinId(pass.id, "depth");
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
                                   depthStencil.format = StringToFormat(pass.depthStencilDesc.format.c_str());
                               }
                               
                               if (pass.depthStencilDesc.useCustomSize)
                               {
                                   depthStencil.processArgs(pass.depthStencilDesc.customWidth, pass.depthStencilDesc.customHeight);
                               }
                               
                               if (pass.depthStencilDesc.isWindow)
                               {
                                   depthStencil.processArgs(m_wnd);
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
}