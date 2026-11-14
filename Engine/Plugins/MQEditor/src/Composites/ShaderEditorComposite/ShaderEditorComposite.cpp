/**
 * @file ShaderEditorComposite.cpp
 * @brief 着色器与材质编辑器生命周期、停靠窗口注册、预设模板与节点属性检视器实现
 */
#include "ShaderEditorComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/resource/core/Resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kShaderEditorWindowTitle = "着色器与材质编辑器 (Shader & Material Editor)";
        }

        ShaderEditorComposite::ShaderEditorComposite() = default;

        ShaderEditorComposite::~ShaderEditorComposite()
        {
            onUnload();
        }

        Status ShaderEditorComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            initDefaultShaderNodes();
            compileShaderCode();

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kShaderEditorWindowTitle,
                    EditorBase::EditorDockSlot::Center,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderShaderEditor();
                    },
                    false);

                m_editorUiComposite->getUiContext().registerSignalHandler<EditorBase::SelectAssetSignal>(
                    [this](const EditorBase::SelectAssetSignal& sig) {
                        if (sig.assetExtension == ".hlsl" || sig.assetExtension == ".glsl" ||
                            sig.assetExtension == ".shader" || sig.assetExtension == ".mat" ||
                            sig.assetExtension == ".mqshader" || sig.assetExtension == ".sgedt")
                        {
                            if (m_editorUiComposite)
                            {
                                m_editorUiComposite->setDockWindowOpen(kShaderEditorWindowTitle, true);
                            }
                            if (sig.assetExtension == ".hlsl" || sig.assetExtension == ".glsl" || sig.assetExtension == ".shader")
                            {
                                m_activeShaderSourcePath = sig.assetPath;
                                if (auto* res = Resource::Resource::InstancePtr())
                                {
                                    if (auto* loader = res->getResourceLoader(); loader && loader->exists(sig.assetPath))
                                    {
                                        auto contentOr = loader->loadFile(sig.assetPath);
                                        if (contentOr.ok() && !contentOr->empty())
                                        {
                                            m_hlslSource = *contentOr;
                                            compileCustomHlslSource();
                                        }
                                    }
                                }
                            }
                        }
                    });
            }

            return OkStatus();
        }

        Status ShaderEditorComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            compileShaderCode();
            return OkStatus();
        }

        void ShaderEditorComposite::onTick(float deltaTime)
        {
            if (m_previewParams.autoRotateSphere)
            {
                m_previewParams.sphereYawRad += deltaTime * 0.85f;
            }
        }

        void ShaderEditorComposite::onUnload()
        {
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kShaderEditorWindowTitle);
            }

            if (m_shaderEditorCtx)
            {
                ImNodes::EditorContextFree(m_shaderEditorCtx);
                m_shaderEditorCtx = nullptr;
            }
            if (m_imnodesCtx)
            {
                ImNodes::DestroyContext(m_imnodesCtx);
                m_imnodesCtx = nullptr;
            }

            m_nodes.clear();
            m_links.clear();
            m_editorUiComposite = nullptr;

            Asset::Composite::onUnload();
        }

        void ShaderEditorComposite::ensureImNodesInitialized()
        {
            ImGuiContext* imguiCtx = ImGui::GetCurrentContext();
            if (!imguiCtx)
                return;

            ImNodes::SetImGuiContext(imguiCtx);
            if (!m_imnodesCtx)
            {
                m_imnodesCtx = ImNodes::CreateContext();
                ImNodes::SetCurrentContext(m_imnodesCtx);
                ImNodes::StyleColorsDark();
                m_shaderEditorCtx = ImNodes::EditorContextCreate();
            }
            else
            {
                ImNodes::SetCurrentContext(m_imnodesCtx);
            }
        }

        void ShaderEditorComposite::initDefaultShaderNodes()
        {
            m_nodes.clear();
            m_links.clear();
            m_shaderNodesPositioned = false;

            // 1. Texture2D 采样节点
            ShaderGraphNode texNode;
            texNode.id = 1;
            texNode.title = "Texture2D Sample";
            texNode.category = ShaderNodeCategory::Texture;
            texNode.pos[0] = 40.0f;
            texNode.pos[1] = 60.0f;
            texNode.pins.push_back(ShaderPin{101, "UV", true, ShaderPinType::Float2});
            texNode.pins.push_back(ShaderPin{102, "RGBA", false, ShaderPinType::Float4});
            m_nodes.push_back(texNode);

            // 2. Base Color 颜色常数节点
            ShaderGraphNode colorNode;
            colorNode.id = 2;
            colorNode.title = "Base Color (Vec4)";
            colorNode.category = ShaderNodeCategory::Constant;
            colorNode.pos[0] = 40.0f;
            colorNode.pos[1] = 220.0f;
            colorNode.color[0] = 1.0f;
            colorNode.color[1] = 1.0f;
            colorNode.color[2] = 1.0f;
            colorNode.color[3] = 1.0f;
            colorNode.pins.push_back(ShaderPin{103, "Color Out", false, ShaderPinType::Float4});
            m_nodes.push_back(colorNode);

            // 3. 乘法合成节点
            ShaderGraphNode mulNode;
            mulNode.id = 3;
            mulNode.title = "Multiply (A * B)";
            mulNode.category = ShaderNodeCategory::Math;
            mulNode.pos[0] = 300.0f;
            mulNode.pos[1] = 120.0f;
            mulNode.pins.push_back(ShaderPin{104, "A", true, ShaderPinType::Float4});
            mulNode.pins.push_back(ShaderPin{105, "B", true, ShaderPinType::Float4});
            mulNode.pins.push_back(ShaderPin{106, "Out", false, ShaderPinType::Float4});
            m_nodes.push_back(mulNode);

            // 4. PBR Master 输出节点
            ShaderGraphNode masterNode;
            masterNode.id = 4;
            masterNode.title = "PBR Master Output";
            masterNode.category = ShaderNodeCategory::Output;
            masterNode.pos[0] = 560.0f;
            masterNode.pos[1] = 90.0f;
            masterNode.value = 0.35f;
            masterNode.pins.push_back(ShaderPin{107, "Albedo", true, ShaderPinType::Float4});
            masterNode.pins.push_back(ShaderPin{108, "Metallic", true, ShaderPinType::Float});
            masterNode.pins.push_back(ShaderPin{109, "Roughness", true, ShaderPinType::Float});
            masterNode.pins.push_back(ShaderPin{110, "Normal", true, ShaderPinType::Float3});
            m_nodes.push_back(masterNode);

            m_links.push_back(ShaderGraphLink{1, 102, 104});
            m_links.push_back(ShaderGraphLink{2, 103, 105});
            m_links.push_back(ShaderGraphLink{3, 106, 107});
            m_nextNodeId = 10;
            m_nextPinId = 200;
            m_nextLinkId = 1000;
            m_selectedNodeId = 4;
        }

        void ShaderEditorComposite::applyTemplatePreset(int presetIndex)
        {
            initDefaultShaderNodes();
            if (presetIndex == 0)
            {
                // 标准金属网格 PBR 预设
                m_previewParams.baseColor[0] = 0.82f;
                m_previewParams.baseColor[1] = 0.85f;
                m_previewParams.baseColor[2] = 0.92f;
                m_previewParams.metallic = 0.25f;
                m_previewParams.roughness = 0.35f;
            }
            else if (presetIndex == 1)
            {
                // 拉丝黄金材质预设
                for (auto& node : m_nodes)
                {
                    if (node.title.find("Color") != std::string::npos)
                    {
                        node.color[0] = 1.0f;
                        node.color[1] = 0.78f;
                        node.color[2] = 0.32f;
                    }
                    else if (node.title.find("Master") != std::string::npos)
                    {
                        node.value = 0.18f;
                    }
                }
                m_previewParams.baseColor[0] = 1.0f;
                m_previewParams.baseColor[1] = 0.78f;
                m_previewParams.baseColor[2] = 0.32f;
                m_previewParams.metallic = 0.92f;
                m_previewParams.roughness = 0.18f;
            }
            else if (presetIndex == 2)
            {
                // 哑光陶瓷预设
                for (auto& node : m_nodes)
                {
                    if (node.title.find("Color") != std::string::npos)
                    {
                        node.color[0] = 0.35f;
                        node.color[1] = 0.68f;
                        node.color[2] = 0.88f;
                    }
                    else if (node.title.find("Master") != std::string::npos)
                    {
                        node.value = 0.65f;
                    }
                }
                m_previewParams.baseColor[0] = 0.35f;
                m_previewParams.baseColor[1] = 0.68f;
                m_previewParams.baseColor[2] = 0.88f;
                m_previewParams.metallic = 0.04f;
                m_previewParams.roughness = 0.65f;
            }
            compileShaderCode();
        }

        void ShaderEditorComposite::clearShaderGraph()
        {
            m_nodes.clear();
            m_links.clear();
            m_nextNodeId = 10;
            m_nextPinId = 100;
            m_nextLinkId = 1000;
            m_selectedNodeId = -1;
            m_shaderNodesPositioned = false;
            m_compileOutput = "[ShaderGraph] 节点图已清空\n";
        }

        void ShaderEditorComposite::renderToolbarAndPresets()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const char* presetItems[] = {
                L("预设: 标准 PBR", "Preset: Standard PBR"),
                L("预设: 镜面黄金", "Preset: Brushed Gold"),
                L("预设: 哑光陶瓷", "Preset: Matte Ceramic")
            };
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("##ShaderPresetCombo", &m_presetIndex, presetItems, 3))
            {
                applyTemplatePreset(m_presetIndex);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputText("##ShaderGraphName", m_shaderGraphName, sizeof(m_shaderGraphName));
            ImGui::SameLine();
            if (ImGui::Button(L("保存 (.mqshader)", "Save (.mqshader)")))
            {
                saveShaderGraph(m_shaderGraphName);
            }
            ImGui::SameLine();
            if (ImGui::Button(L("读取", "Load")))
            {
                loadShaderGraph(m_shaderGraphName);
            }
            ImGui::SameLine();
            if (ImGui::Button(L("生成并编译 HLSL", "Compile HLSL")))
            {
                compileShaderCode();
            }
            if (m_editorUiComposite)
            {
                ImGui::SameLine();
                const std::string ideName = m_editorUiComposite->getActiveCodeEditorDisplayName();
                const std::string openIdeLabel = !ideName.empty()
                    ? (m_editorUiComposite->getLanguage() == EditorBase::EditorLanguage::EnUS
                        ? ("Open Source in " + ideName)
                        : ("在 " + ideName + " 中打开源码"))
                    : std::string(L("在代码编辑器中打开源码", "Open Source in IDE"));
                if (ImGui::Button(openIdeLabel.c_str()))
                {
                    openActiveShaderInCodeEditor();
                }
            }
        }

        std::string ShaderEditorComposite::resolveActiveShaderDiskPath() const
        {
            const std::string relOrAbs = m_activeShaderSourcePath.empty()
                ? std::string("Assets/Shaders/PBR_Standard.hlsl")
                : m_activeShaderSourcePath;
            if (SAI::Path::isAbsolute(relOrAbs))
            {
                return SAI::Path::lexicallyNormal(relOrAbs);
            }
            if (m_editorUiComposite)
            {
                const std::string projDir = m_editorUiComposite->getActiveProjectDirectory();
                if (!projDir.empty() && projDir != ".")
                {
                    return SAI::Path::join(projDir, relOrAbs);
                }
            }
            return relOrAbs;
        }

        void ShaderEditorComposite::openActiveShaderInCodeEditor()
        {
            if (!m_editorUiComposite)
            {
                return;
            }
            const std::string targetPath = resolveActiveShaderDiskPath();
            if (auto* res = Resource::Resource::InstancePtr())
            {
                if (auto* loader = res->getResourceLoader())
                {
                    if (!loader->exists(targetPath) && !m_hlslSource.empty())
                    {
                        (void)loader->ensureDir(SAI::Path::parentPath(targetPath));
                        (void)loader->saveFile(targetPath, m_hlslSource);
                    }
                }
            }
            (void)m_editorUiComposite->openFileInCodeEditor(targetPath, 0, 0, true);
        }

        void ShaderEditorComposite::reloadActiveShaderFromDisk()
        {
            const std::string targetPath = resolveActiveShaderDiskPath();
            auto* res = Resource::Resource::InstancePtr();
            auto* loader = res ? res->getResourceLoader() : nullptr;
            if (!loader || !loader->exists(targetPath))
            {
                return;
            }

            auto contentOr = loader->loadFile(targetPath);
            if (!contentOr.ok())
            {
                return;
            }

            m_hlslSource = contentOr.value();
            compileCustomHlslSource();
            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "ShaderEditor",
                    "已从磁盘同步并重编着色器: " + SAI::Path::fileName(targetPath),
                    EditorBase::LogEntry::Level::Info);
            }
        }

        void ShaderEditorComposite::renderNodePropertyInspector()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("节点属性检视器 (Node Property Inspector)", "Node Property Inspector"));
            ImGui::Separator();

            ShaderGraphNode* targetNode = nullptr;
            for (auto& node : m_nodes)
            {
                if (node.id == m_selectedNodeId)
                {
                    targetNode = &node;
                    break;
                }
            }
            if (!targetNode && !m_nodes.empty())
            {
                targetNode = &m_nodes.back();
            }

            if (!targetNode)
            {
                ImGui::TextDisabled("%s", L("当前未选中任何着色器节点", "No shader node selected"));
                return;
            }

            ImGui::Text(L("节点 ID: %d | 标题: %s", "Node ID: %d | Title: %s"),
                        targetNode->id,
                        targetNode->title.c_str());

            if (targetNode->title.find("Color") != std::string::npos)
            {
                if (ImGui::ColorEdit4(L("颜色常量", "Constant Color"), targetNode->color))
                {
                    m_previewParams.baseColor[0] = targetNode->color[0];
                    m_previewParams.baseColor[1] = targetNode->color[1];
                    m_previewParams.baseColor[2] = targetNode->color[2];
                    m_previewParams.baseColor[3] = targetNode->color[3];
                    compileShaderCode();
                }
            }
            else if (targetNode->title.find("Master") != std::string::npos)
            {
                if (ImGui::SliderFloat(L("基础粗糙度", "Base Roughness"), &targetNode->value, 0.05f, 1.0f, "%.2f"))
                {
                    m_previewParams.roughness = targetNode->value;
                    compileShaderCode();
                }
            }
            else
            {
                ImGui::SliderFloat(L("节点权重参数", "Node Scalar Value"), &targetNode->value, 0.0f, 4.0f, "%.2f");
            }
        }

        void ShaderEditorComposite::renderHlslSourceEditor()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };
            if (ImGui::Button(L("编译着色器", "Compile Shader")))
            {
                m_compileOutput = L("[HLSL 源码编辑器] 正在编译自定义 HLSL 像素着色器...\n",
                                    "[HLSL Editor] Compiling custom HLSL pixel shader...\n");
                compileCustomHlslSource();
            }
            if (m_editorUiComposite)
            {
                ImGui::SameLine();
                if (ImGui::Button(L("使用外部 IDE 编辑", "Edit in External IDE")))
                {
                    openActiveShaderInCodeEditor();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("从磁盘同步重编", "Reload & Compile from Disk")))
                {
                    reloadActiveShaderFromDisk();
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", L("着色器类型: 像素着色器 (HLSL 6.0 -> SPIR-V)", "Shader Stage: Pixel Shader (HLSL 6.0 -> SPIR-V)"));

            ImGui::Separator();

            std::vector<char> sourceBuf(m_hlslSource.begin(), m_hlslSource.end());
            sourceBuf.resize(65536, 0);

            if (ImGui::InputTextMultiline("##HlslEditor", sourceBuf.data(), sourceBuf.size(),
                                         ImVec2(-1, ImGui::GetContentRegionAvail().y - 120),
                                         ImGuiInputTextFlags_AllowTabInput))
            {
                m_hlslSource = sourceBuf.data();
            }

            ImGui::Separator();

            ImGui::Text("%s", L("编译诊断结果:", "Compiler Output:"));
            ImVec4 statusColor = m_compileSuccess ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
            ImGui::TextWrapped("%s", m_compileOutput.c_str());
            ImGui::PopStyleColor();
        }

        void ShaderEditorComposite::renderShaderEditor()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };
            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kShaderEditorWindowTitle)
                : "着色器与材质编辑器###着色器与材质编辑器 (Shader & Material Editor)";
            bool* openPtr = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowOpenPtr(kShaderEditorWindowTitle)
                : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            renderToolbarAndPresets();
            ImGui::Separator();

            if (ImGui::BeginTabBar("ShaderEditorTabBar"))
            {
                if (ImGui::BeginTabItem(L("可视化节点图###TabShaderGraph", "ShaderGraph###TabShaderGraph")))
                {
                    m_selectedTab = 0;
                    renderShaderGraphCanvas();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(L("HLSL 源码编辑###TabHlslSrc", "HLSL Source###TabHlslSrc")))
                {
                    m_selectedTab = 1;
                    renderHlslSourceEditor();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(L("PBR 材质球预览###TabPbrPreview", "PBR Material Preview###TabPbrPreview")))
                {
                    m_selectedTab = 2;
                    renderPbrMaterialPreview();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::ShaderEditorComposite)
