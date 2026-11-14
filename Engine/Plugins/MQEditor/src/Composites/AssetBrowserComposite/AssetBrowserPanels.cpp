/**
 * @file AssetBrowserPanels.cpp
 * @brief 内容资产浏览器工程源码树、资产卡片网格、外部 IDE 上下文菜单及导入/重命名/删除模态弹窗实现
 */
#include "AssetBrowserComposite.h"

#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            ImVec4 resolveAssetCardColor(const AssetEntry& item)
            {
                if (item.isDirectory)
                {
                    return ImVec4(0.20f, 0.26f, 0.36f, 1.0f);
                }
                switch (item.type)
                {
                case AssetCategory::SourceCode:
                    return ImVec4(0.16f, 0.25f, 0.34f, 1.0f);
                case AssetCategory::Script:
                    return ImVec4(0.16f, 0.28f, 0.26f, 1.0f);
                case AssetCategory::Shader:
                    return ImVec4(0.24f, 0.19f, 0.32f, 1.0f);
                case AssetCategory::Config:
                    return ImVec4(0.27f, 0.23f, 0.17f, 1.0f);
                default:
                    return ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
                }
            }

            bool containsCaseInsensitive(std::string_view haystack, std::string_view needle)
            {
                if (needle.empty())
                {
                    return true;
                }
                auto it = std::search(
                    haystack.begin(), haystack.end(),
                    needle.begin(), needle.end(),
                    [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) ==
                               std::tolower(static_cast<unsigned char>(b));
                    });
                return it != haystack.end();
            }

            bool isSkippedSubTreeFolder(std::string_view folderName)
            {
                return folderName.empty() ||
                       folderName[0] == '.' ||
                       folderName == "Dist" ||
                       folderName == "Output" ||
                       folderName.rfind("build", 0) == 0 ||
                       folderName.rfind("cmake-build", 0) == 0;
            }
        } // namespace

        void AssetBrowserComposite::handleAssetOpen(const AssetEntry& item)
        {
            if (item.isDirectory)
            {
                navigateTo(item.path);
                return;
            }

            if (m_editorUiComposite)
            {
                EditorBase::SelectAssetSignal sig;
                sig.assetPath = item.path;
                sig.assetExtension = item.extension;
                m_editorUiComposite->getUiContext().emit(std::move(sig));
            }

            if (isDirectCodeEditorAsset(item))
            {
                openFileInExternalCodeEditor(item.path);
                return;
            }

            if (m_editorUiComposite && (item.extension == ".mqscene" || item.extension == ".scene"))
            {
                m_editorUiComposite->requestOpenScene(item.path);
                return;
            }
        }

        void AssetBrowserComposite::renderTopNavigationToolbar()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const bool canGoBack = (m_historyIndex > 0);
            const bool canGoForward = (m_historyIndex >= 0 && m_historyIndex + 1 < static_cast<int>(m_history.size()));

            if (ImGui::Button("<", ImVec2(24, 24)) && canGoBack)
            {
                m_historyIndex--;
                m_currentDirectory = m_history[m_historyIndex];
                refreshDirectoryContents(m_currentDirectory);
            }
            ImGui::SameLine();
            if (ImGui::Button(">", ImVec2(24, 24)) && canGoForward)
            {
                m_historyIndex++;
                m_currentDirectory = m_history[m_historyIndex];
                refreshDirectoryContents(m_currentDirectory);
            }
            ImGui::SameLine();
            if (ImGui::Button("..", ImVec2(28, 24)))
            {
                navigateUp();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", L("返回上一级目录", "Navigate to parent directory"));
            }
            ImGui::SameLine();

            ImGui::TextDisabled(L("路径: %s", "Path: %s"), m_currentDirectory.c_str());
            ImGui::SameLine();

            ImGui::SetNextItemWidth(135.0f);
            ImGui::InputTextWithHint("##AssetSearch", L("搜索资产或源码...", "Search assets or code..."), m_searchFilter, sizeof(m_searchFilter));
            ImGui::SameLine();

            ImGui::SetNextItemWidth(85.0f);
            ImGui::SliderFloat(L("卡片", "Size"), &m_thumbnailSize, 48.0f, 128.0f);
            ImGui::SameLine();
            ImGui::Checkbox(L("列表", "List"), &m_listViewMode);
            ImGui::SameLine();

            if (ImGui::Button(L("导入资产...", "Import...")))
            {
                ImGui::OpenPopup("ImportAssetPopup");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", L(
                    "支持直接从系统资源管理器拖拽模型 (.obj/.fbx/.gltf) 或纹理 (.png/.jpg/.hdr/.dds/.ktx2) 到窗口自动导入",
                    "Drag & drop 3D models or textures from OS Explorer into window to import"));
            }

            ImGui::SameLine();
            const std::string openProjBtnLabel = buildOpenProjectInIdeLabel();
            if (ImGui::Button(openProjBtnLabel.c_str()))
            {
                openProjectInExternalCodeEditor();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", L(
                    "在 CLion / JetBrains Rider 等外部代码编辑器中打开当前 C++20 / CMake 工程",
                    "Open the active C++20 / CMake project in CLion / JetBrains Rider"));
            }

            if (!m_selectedItemPath.empty() && EditorBase::EditorUiComposite::isCodeEditorSourceFile(m_selectedItemPath))
            {
                ImGui::SameLine();
                if (ImGui::Button(L("IDE 打开选中文件", "Open File in IDE")))
                {
                    openFileInExternalCodeEditor(m_selectedItemPath);
                }
            }

            if (ImGui::BeginPopup("ImportAssetPopup"))
            {
                ImGui::Text("%s %s:", L("导入外部文件至", "Import external file into"), m_currentDirectory.c_str());
                ImGui::SetNextItemWidth(340.0f);
                ImGui::InputTextWithHint(
                    "##ImportPath",
                    L("输入外部文件路径或直接拖拽文件到窗口...", "Enter external file path or drag file into window..."),
                    m_importPathBuffer,
                    sizeof(m_importPathBuffer));
                if (ImGui::Button(L("确认导入", "Import Now")) && m_importPathBuffer[0] != '\0')
                {
                    (void)importExternalFile(m_importPathBuffer, m_currentDirectory);
                    m_importPathBuffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void AssetBrowserComposite::renderFolderSelectableNode(const std::string& label, const std::string& folderPath)
        {
            const std::string normFolder = SAI::Path::lexicallyNormal(folderPath);
            if (ImGui::Selectable(label.c_str(), m_currentDirectory == normFolder || m_currentDirectory == folderPath))
            {
                navigateTo(folderPath);
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ASSET_PAYLOAD"))
                {
                    const char* droppedPath = static_cast<const char*>(payload->Data);
                    (void)importExternalFile(droppedPath, folderPath);
                }
                ImGui::EndDragDropTarget();
            }
        }

        void AssetBrowserComposite::renderSubDirectoryTreeNodes(const std::string& parentDirPath, int depth)
        {
            if (depth > 4)
            {
                return;
            }
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader || !loader->accessDir(parentDirPath).ok())
            {
                return;
            }

            auto entriesOr = loader->readDir(parentDirPath);
            if (!entriesOr.ok())
            {
                return;
            }

            std::vector<DirectoryTreeNode> subDirs;
            for (const auto& entry : entriesOr.value())
            {
                const std::string subPath = SAI::Path::lexicallyNormal(entry->path());
                if (!loader->accessDir(subPath).ok())
                {
                    continue;
                }
                const std::string dirName = SAI::Path::fileName(subPath);
                if (isSkippedSubTreeFolder(dirName))
                {
                    continue;
                }
                subDirs.push_back(DirectoryTreeNode{dirName, subPath});
            }

            std::sort(subDirs.begin(), subDirs.end(), [](const DirectoryTreeNode& a, const DirectoryTreeNode& b) {
                return a.label < b.label;
            });

            for (const auto& sub : subDirs)
            {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (m_currentDirectory == sub.folderPath)
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                const std::string nodeId = sub.label + "###SubDir_" + sub.folderPath;
                const bool opened = ImGui::TreeNodeEx(nodeId.c_str(), flags);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                {
                    navigateTo(sub.folderPath);
                }
                if (opened)
                {
                    renderSubDirectoryTreeNodes(sub.folderPath, depth + 1);
                    ImGui::TreePop();
                }
            }
        }

        void AssetBrowserComposite::renderProjectSourceTreeSection()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            const std::string projRoot = resolveActiveProjectRoot();
            if (!loader || projRoot.empty() || projRoot == "." || !loader->accessDir(projRoot).ok())
            {
                return;
            }

            if (ImGui::TreeNodeEx(L("工程与源码 (Project & Source)###ProjSourceTree", "Project & Source###ProjSourceTree"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                const std::string projFolderName = SAI::Path::fileName(projRoot);
                const std::string rootLabel = (projFolderName.empty() ? std::string("Project") : projFolderName) + " (Root)";
                renderFolderSelectableNode(rootLabel, projRoot);

                struct CandidateSourceDir
                {
                    const char* zhLabel;
                    const char* enLabel;
                    const char* relPath;
                    bool expandable;
                };

                const CandidateSourceDir candidates[] = {
                    {"Native (C++ 源码)", "Native (C++ Source)", "Native", true},
                    {"Plugins (项目插件)", "Plugins (Project Plugins)", "Plugins", true},
                    {"UserCode (用户代码)", "UserCode", "UserCode", true},
                    {"Scripts (TS/JS 脚本)", "Scripts (TS/JS)", "Assets/Scripts", true},
                    {"Config (工程配置)", "Config", "Config", false},
                    {"Scenes (工程场景)", "Scenes", "Scenes", false}
                };

                for (const auto& cand : candidates)
                {
                    const std::string fullDir = SAI::Path::join(projRoot, cand.relPath);
                    if (!loader->accessDir(fullDir).ok())
                    {
                        continue;
                    }

                    const char* dispLabel = L(cand.zhLabel, cand.enLabel);
                    if (cand.expandable)
                    {
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (m_currentDirectory == fullDir)
                        {
                            flags |= ImGuiTreeNodeFlags_Selected;
                        }
                        const std::string treeId = std::string(dispLabel) + "###ProjSrc_" + cand.relPath;
                        const bool opened = ImGui::TreeNodeEx(treeId.c_str(), flags);
                        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                        {
                            navigateTo(fullDir);
                        }
                        if (opened)
                        {
                            renderSubDirectoryTreeNodes(fullDir, 1);
                            ImGui::TreePop();
                        }
                    }
                    else
                    {
                        renderFolderSelectableNode(dispLabel, fullDir);
                    }
                }

                ImGui::TreePop();
            }
            ImGui::Separator();
        }

        void AssetBrowserComposite::renderAssetFolderTreeSection()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (!ImGui::TreeNodeEx(L("资产目录###AssetTree", "Assets###AssetTree"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            const std::string projRoot = resolveActiveProjectRoot();
            const bool hasValidProjRoot = (loader && !projRoot.empty() && projRoot != "." && loader->accessDir(projRoot).ok());

            auto makeProjPath = [&](const char* relFromRoot, const char* fallbackRel) -> std::string {
                return hasValidProjRoot ? SAI::Path::join(projRoot, relFromRoot) : std::string(fallbackRel);
            };

            const DirectoryTreeNode folderNodes[] = {
                {"Assets", makeProjPath("Assets", "./Assets")},
                {"Scripts", makeProjPath("Assets/Scripts", "./Assets/Scripts")},
                {"Shaders", makeProjPath("Assets/Shaders", "./Assets/Shaders")},
                {"UI", makeProjPath("Assets/UI", "./Assets/UI")},
                {"Models", makeProjPath("Assets/Models", "./Assets/Models")},
                {"Textures", makeProjPath("Assets/Textures", "./Assets/Textures")},
                {"Materials", makeProjPath("Assets/Materials", "./Assets/Materials")},
                {"Scenes", makeProjPath("Scenes", "./Assets/Scenes")},
                {"Saved", makeProjPath("Saved", "Saved")}
            };

            for (const auto& node : folderNodes)
            {
                if (hasValidProjRoot && node.label != "Assets" && !loader->accessDir(node.folderPath).ok())
                {
                    continue;
                }
                renderFolderSelectableNode(node.label, node.folderPath);
            }

            ImGui::TreePop();
        }

        void AssetBrowserComposite::renderLeftFolderTreePanel()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const float leftWidth = 225.0f;
            ImGui::BeginChild("AssetLeftFolderTree", ImVec2(leftWidth, 0), true);

            renderProjectSourceTreeSection();
            renderAssetFolderTreeSection();

            ImGui::Separator();
            if (ImGui::TreeNodeEx(L("GPU 资源表###GpuResTable", "GPU Resource Table###GpuResTable"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (auto* gpuTable = Resource::GpuResourceTable::InstancePtr())
                {
                    ImGui::Text(L("活跃句柄数: %zu", "Active Handles: %zu"), gpuTable->activeCount());
                    ImGui::Text(L("总显存占用: %.2f KB", "Total VRAM: %.2f KB"), static_cast<double>(gpuTable->totalVramBytes()) / 1024.0);
                    ImGui::TextDisabled("%s", L("主键策略: 原生指针", "Key Policy: Native Pointer"));
                }
                ImGui::TreePop();
            }

            ImGui::Separator();
            if (ImGui::TreeNodeEx(L("活跃组件拓扑###CompTopo", "Active Composites###CompTopo"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (auto* assetMod = Asset::Asset::InstancePtr())
                {
                    const auto& activeComps = assetMod->compositeManager.getActiveExecutionOrder();
                    for (auto* comp : activeComps)
                    {
                        if (!comp) continue;
                        ImGui::BulletText("%s", comp->getName().c_str());
                    }
                }
                ImGui::TreePop();
            }
            ImGui::EndChild();
        }

        void AssetBrowserComposite::renderAssetItemContextMenu(const AssetEntry& item)
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (!ImGui::BeginPopupContextItem("AssetItemCtxMenu"))
            {
                return;
            }

            m_selectedItemPath = item.path;

            if (canOpenInCodeEditor(item))
            {
                const std::string openInIdeText = buildOpenFileInIdeLabel();
                if (ImGui::MenuItem(openInIdeText.c_str()))
                {
                    openFileInExternalCodeEditor(item.path);
                }
                ImGui::Separator();
            }
            else if (item.isDirectory)
            {
                if (ImGui::MenuItem(L("在代码编辑器中打开此目录", "Open Directory in Code Editor")))
                {
                    openProjectInExternalCodeEditor(item.path);
                }
                ImGui::Separator();
            }

            if (!item.isDirectory && (item.extension == ".mqscene" || item.extension == ".scene"))
            {
                if (m_editorUiComposite && ImGui::MenuItem(L("打开场景", "Open Scene")))
                {
                    m_editorUiComposite->requestOpenScene(item.path);
                }
                const bool isMain = isMainSceneAsset(item);
                const char* setMainLabel = isMain
                    ? L("设为主场景 (当前已是主场景)", "Set as Main Scene (Active)")
                    : L("设为主场景 (Set as Main Scene)", "Set as Main Scene");
                if (ImGui::MenuItem(setMainLabel, nullptr, isMain))
                {
                    Status st = setSceneAsProjectMain(item.path);
                    if (!st.ok() && m_editorUiComposite)
                    {
                        m_editorUiComposite->showToast(
                            L("设为主场景失败", "Set Main Scene Failed"),
                            std::string(st.message()),
                            EditorBase::LogEntry::Level::Error,
                            3.0f);
                    }
                }
                ImGui::Separator();
            }

            const std::string openProjText = buildOpenProjectInIdeLabel();
            if (ImGui::MenuItem(openProjText.c_str()))
            {
                openProjectInExternalCodeEditor();
            }
            ImGui::Separator();

            if (ImGui::MenuItem(L("重命名...", "Rename...")))
            {
                std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", item.name.c_str());
                m_openRenameModal = true;
            }
            if (ImGui::MenuItem(L("删除资产", "Delete Asset")))
            {
                m_openDeleteModal = true;
            }
            ImGui::EndPopup();
        }

        void AssetBrowserComposite::renderAssetGridCard(const AssetEntry& item)
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::PushID(item.path.c_str());

            const bool isMainScene = isMainSceneAsset(item);
            const std::string mainBadge = isMainScene ? (std::string("\n") + L("[主场景]", "[Main]")) : std::string("");
            const std::string iconLabel = std::string(getFileTypeIcon(item.type)) + mainBadge + "\n" + item.name;
            ImGui::PushStyleColor(ImGuiCol_Button, isMainScene ? ImVec4(0.20f, 0.33f, 0.24f, 1.0f) : resolveAssetCardColor(item));

            if (ImGui::Button(iconLabel.c_str(), ImVec2(m_thumbnailSize, m_thumbnailSize)))
            {
                m_selectedItemPath = item.path;
                if (m_editorUiComposite && !item.isDirectory)
                {
                    EditorBase::SelectAssetSignal sig;
                    sig.assetPath = item.path;
                    sig.assetExtension = item.extension;
                    m_editorUiComposite->getUiContext().emit(std::move(sig));
                }
            }

            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
            {
                if (isDirectCodeEditorAsset(item))
                {
                    ImGui::SetTooltip("%s\n%s",
                                      item.path.c_str(),
                                      L("双击在外部代码编辑器 (CLion / Rider) 中打开", "Double-click to open in CLion / Rider"));
                }
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    handleAssetOpen(item);
                }
            }

            renderAssetItemContextMenu(item);

            if (!item.isDirectory && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("MQ_ASSET_PAYLOAD", item.path.c_str(), item.path.size() + 1);
                ImGui::Text(L("拖拽资产: %s", "Drag Asset: %s"), item.name.c_str());
                ImGui::EndDragDropSource();
            }
            else if (item.isDirectory && ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ASSET_PAYLOAD"))
                {
                    const char* droppedPath = static_cast<const char*>(payload->Data);
                    (void)importExternalFile(droppedPath, item.path);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }

        void AssetBrowserComposite::renderAssetListRow(const AssetEntry& item)
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::PushID(item.path.c_str());
            const std::string mainTag = isMainSceneAsset(item) ? (std::string(" ") + L("[主场景]", "[Main]")) : std::string("");
            const std::string label = std::string(getFileTypeIcon(item.type)) + " " + item.name + mainTag;
            if (ImGui::Selectable(label.c_str(), m_selectedItemPath == item.path))
            {
                m_selectedItemPath = item.path;
                if (m_editorUiComposite && !item.isDirectory)
                {
                    EditorBase::SelectAssetSignal sig;
                    sig.assetPath = item.path;
                    sig.assetExtension = item.extension;
                    m_editorUiComposite->getUiContext().emit(std::move(sig));
                }
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                handleAssetOpen(item);
            }

            renderAssetItemContextMenu(item);

            if (!item.isDirectory && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("MQ_ASSET_PAYLOAD", item.path.c_str(), item.path.size() + 1);
                ImGui::Text(L("拖拽资产: %s", "Drag Asset: %s"), item.name.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }

        void AssetBrowserComposite::renderAssetModalsAndContextMenus()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (ImGui::BeginPopupContextWindow("AssetBrowserEmptyAreaPopup", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem(L("刷新", "Refresh")))
                {
                    refreshDirectoryContents(m_currentDirectory);
                }
                const std::string openProjText = buildOpenProjectInIdeLabel();
                if (ImGui::MenuItem(openProjText.c_str()))
                {
                    openProjectInExternalCodeEditor();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(L("新建文件夹", "New Folder")))
                {
                    const std::string newFolder = SAI::Path::join(m_currentDirectory, "New_Folder");
                    if (auto* res = Resource::Resource::InstancePtr(); res && res->getResourceLoader())
                    {
                        (void)res->getResourceLoader()->ensureDir(newFolder);
                    }
                    refreshDirectoryContents(m_currentDirectory);
                }
                if (ImGui::MenuItem(L("新建材质", "New Material")))
                {
                    m_currentItems.push_back(AssetEntry{"New_Material.mat", SAI::Path::join(m_currentDirectory, "New_Material.mat"), ".mat", AssetCategory::Material, 1024, false});
                }
                ImGui::EndPopup();
            }

            if (m_openRenameModal)
            {
                ImGui::OpenPopup("RenameAssetModal");
                m_openRenameModal = false;
            }
            if (ImGui::BeginPopupModal("RenameAssetModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("%s", L("重命名资产条目:", "Rename Asset Entry:"));
                ImGui::InputText("##RenameAssetInput", m_renameBuffer, sizeof(m_renameBuffer));
                if (ImGui::Button(L("确认", "Confirm")) && m_renameBuffer[0] != '\0')
                {
                    for (auto& item : m_currentItems)
                    {
                        if (item.path == m_selectedItemPath)
                        {
                            item.name = m_renameBuffer;
                            item.path = SAI::Path::join(m_currentDirectory, item.name);
                            m_selectedItemPath = item.path;
                            break;
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("取消", "Cancel")))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (m_openDeleteModal)
            {
                ImGui::OpenPopup("DeleteAssetModal");
                m_openDeleteModal = false;
            }
            if (ImGui::BeginPopupModal("DeleteAssetModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text(L("确认删除资产: %s ?", "Delete asset: %s ?"), m_selectedItemPath.c_str());
                if (ImGui::Button(L("确认删除", "Delete")))
                {
                    if (auto* res = Resource::Resource::InstancePtr())
                    {
                        if (auto* loader = res->getResourceLoader(); loader && loader->exists(m_selectedItemPath))
                        {
                            (void)loader->unlink(m_selectedItemPath);
                        }
                    }
                    m_currentItems.erase(
                        std::remove_if(m_currentItems.begin(), m_currentItems.end(),
                                       [this](const AssetEntry& item) { return item.path == m_selectedItemPath; }),
                        m_currentItems.end());
                    m_selectedItemPath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("取消", "Cancel")))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void AssetBrowserComposite::renderRightAssetContentPanel()
        {
            ImGui::BeginChild("AssetRightContentGrid", ImVec2(0, 0), true);

            const float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = static_cast<int>(panelWidth / (m_thumbnailSize + 20.0f));
            if (columnCount < 1)
            {
                columnCount = 1;
            }

            const std::string_view searchStr = m_searchFilter;

            if (m_listViewMode)
            {
                for (const auto& item : m_currentItems)
                {
                    if (!containsCaseInsensitive(item.name, searchStr))
                    {
                        continue;
                    }
                    renderAssetListRow(item);
                }
            }
            else
            {
                ImGui::Columns(columnCount, nullptr, false);
                for (const auto& item : m_currentItems)
                {
                    if (!containsCaseInsensitive(item.name, searchStr))
                    {
                        continue;
                    }
                    renderAssetGridCard(item);
                }
                ImGui::Columns(1);
            }

            renderAssetModalsAndContextMenus();

            ImGui::EndChild();
        }

        void AssetBrowserComposite::renderAssetBrowser()
        {
            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle("内容资产浏览器 (Content Browser)")
                : "内容资产浏览器###内容资产浏览器 (Content Browser)";
            bool* openPtr = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowOpenPtr("内容资产浏览器 (Content Browser)")
                : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            renderTopNavigationToolbar();
            ImGui::Separator();

            renderLeftFolderTreePanel();
            ImGui::SameLine();
            renderRightAssetContentPanel();

            // 接收从层级树拖拽过来的实体，自动导出为当前目录下的预制体资产 (.mqscene)
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ENTITY_PAYLOAD"))
                {
                    const uint32_t entityId = *static_cast<const uint32_t*>(payload->Data);
                    const std::string prefabName = "EntityPrefab_" + std::to_string(entityId) + ".mqscene";
                    (void)importExternalFile(SAI::Path::join(m_currentDirectory, prefabName), m_currentDirectory);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine
