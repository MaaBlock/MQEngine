/**
 * @file SceneHierarchyPanel.cpp
 * @brief 场景层级大纲树面板 (Scene Hierarchy)：按 ECS Registry 分区（主场景 Main / 编辑器 Editor / 流式分块 Streaming）组织实体树与流式挂载
 */
#include "SceneViewerComposite.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <map>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            const char* getRegistryKindBadge(Domain::Scene::ERegistryKind kind, EditorBase::EditorUiComposite* ui)
            {
                auto tr = [ui](const char* zh, const char* en) -> const char* {
                    return ui ? ui->tr(zh, en) : zh;
                };
                switch (kind)
                {
                case Domain::Scene::ERegistryKind::Main:
                    return tr("主场景", "Runtime");
                case Domain::Scene::ERegistryKind::Editor:
                    return tr("编辑器", "Editor");
                case Domain::Scene::ERegistryKind::Streaming:
                    return tr("流式分块", "Streaming");
                case Domain::Scene::ERegistryKind::Custom:
                default:
                    return tr("自定义", "Custom");
                }
            }
        } // namespace

        void SceneViewerComposite::renderHierarchyNode(SceneEntityDesc& entity)
        {
            // 缓存节点 ID 与父级标识，防止右键新建/粘贴/克隆导致 m_entities 扩容后引用失效
            const uint32_t currentEntityId = entity.id;
            const uint32_t currentParentId = entity.parentId;
            auto hasMatchingDescendant = [this](auto& self, uint32_t parentId) -> bool {
                for (const auto& child : m_entities)
                {
                    if (child.parentId == parentId)
                    {
                        if (isEntityPassingFilter(child) || self(self, child.id))
                        {
                            return true;
                        }
                    }
                }
                return false;
            };

            if (!isEntityPassingFilter(entity) && !hasMatchingDescendant(hasMatchingDescendant, entity.id))
            {
                return;
            }

            size_t directChildCount = 0;
            for (const auto& child : m_entities)
            {
                if (child.parentId == entity.id)
                {
                    ++directChildCount;
                }
            }
            const bool hasChildren = (directChildCount > 0);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (hasChildren && directChildCount <= 16)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (!hasChildren)
            {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (entity.id == m_selectedEntityId)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            std::string displayLabel = entity.active
                ? entity.name
                : (entity.name + " (Inactive)");
            if (directChildCount > 16)
            {
                displayLabel += " [" + std::to_string(directChildCount) + "]";
            }

            if (!entity.active)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.52f, 0.56f, 1.0f));
            }
            const bool opened = ImGui::TreeNodeEx(
                reinterpret_cast<void*>(static_cast<uintptr_t>(entity.id)),
                flags,
                "%s",
                displayLabel.c_str());
            if (!entity.active)
            {
                ImGui::PopStyleColor();
            }

            if (ImGui::IsItemClicked())
            {
                m_selectedEntityId = entity.id;
                if (m_editorUiComposite)
                {
                    EditorBase::SelectEntitySignal sig;
                    sig.entityId = entity.id;
                    sig.entityName = entity.name;
                    m_editorUiComposite->getUiContext().emit(std::move(sig));
                }
            }

            // 拖拽改变父子层级 (HierarchyRelation Reparenting)
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("MQ_ENTITY_REPARENT", &entity.id, sizeof(uint32_t));
                ImGui::Text("移动节点: %s", entity.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ENTITY_REPARENT"))
                {
                    const uint32_t draggedChildId = *static_cast<const uint32_t*>(payload->Data);
                    setEntityParent(draggedChildId, entity.id);
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem())
            {
                auto L = [this](const char* zh, const char* en) -> const char* {
                    return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
                };
                m_selectedEntityId = currentEntityId;
                if (ImGui::BeginMenu(L("创建子物体", "Create Child")))
                {
                    renderCategorizedEntityCreateMenus(currentEntityId);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem(L("解除父级", "Unparent"), nullptr, false, currentParentId != 0))
                {
                    setEntityParent(currentEntityId, 0);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(L("复制 (Copy)", "Copy"), "Ctrl+C"))
                {
                    copyEntityToClipboard(currentEntityId);
                }
                if (ImGui::MenuItem(L("粘贴为子物体 (Paste as Child)", "Paste as Child"), nullptr, false, hasClipboardEntities()))
                {
                    pasteEntityFromClipboard(currentEntityId, false);
                }
                if (ImGui::MenuItem(L("粘贴为同级 (Paste)", "Paste"), "Ctrl+V", false, hasClipboardEntities()))
                {
                    pasteEntityFromClipboard(0, true);
                }
                if (ImGui::MenuItem(L("克隆副本 (Duplicate)", "Duplicate"), "Ctrl+D"))
                {
                    duplicateEntitySubtree(currentEntityId);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(L("删除", "Delete")))
                {
                    ImGui::EndPopup();
                    if (opened) ImGui::TreePop();
                    deleteEntity(currentEntityId);
                    return;
                }
                ImGui::EndPopup();
            }

            if (opened)
            {
                for (size_t i = 0; i < m_entities.size(); ++i)
                {
                    if (m_entities[i].parentId == currentEntityId)
                    {
                        renderHierarchyNode(m_entities[i]);
                    }
                }
                ImGui::TreePop();
            }
        }

        void SceneViewerComposite::renderCategorizedEntityCreateMenus(uint32_t parentId)
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            struct PresetMenuEntry
            {
                EntityPresetKind kind;
                const char* zhLabel;
                const char* enLabel;
            };
            struct PresetCategoryDef
            {
                const char* zhCategory;
                const char* enCategory;
                const PresetMenuEntry* entries;
                size_t count;
            };

            static constexpr PresetMenuEntry kBasicEntries[] = {
                {EntityPresetKind::EmptyNode, "空间空节点 (Empty Node)", "Empty Node"},
                {EntityPresetKind::LogicEntity, "纯逻辑实体 (无 Transform)", "Logic Entity (No Transform)"},
                {EntityPresetKind::NativeScriptEntity, "原生脚本实体 (Native Script Entity)", "Native Script Entity"},
            };
            static constexpr PresetMenuEntry kPrimitiveEntries[] = {
                {EntityPresetKind::Cube, "立方体 (Cube)", "Cube"},
                {EntityPresetKind::Plane, "平面 (Plane)", "Plane"},
                {EntityPresetKind::Sphere, "球体 (Sphere)", "Sphere"},
                {EntityPresetKind::StaticMesh, "静态网格实体 (Static Mesh)", "Static Mesh"},
            };
            static constexpr PresetMenuEntry kLightEntries[] = {
                {EntityPresetKind::DirectionalLight, "方向光 (Directional Light)", "Directional Light"},
                {EntityPresetKind::PointLight, "点光源 (Point Light)", "Point Light"},
            };
            static constexpr PresetMenuEntry kCameraEntries[] = {
                {EntityPresetKind::PerspectiveCamera, "透视相机 (Perspective Camera)", "Perspective Camera"},
                {EntityPresetKind::OrthographicCamera, "正交相机 (Orthographic Camera)", "Orthographic Camera"},
            };
            static constexpr PresetMenuEntry kPhysicsEntries[] = {
                {EntityPresetKind::StaticBoxCollider, "静态碰撞盒 (Static Box Collider)", "Static Box Collider"},
                {EntityPresetKind::DynamicBox, "动态刚体立方体 (Dynamic RigidBody Box)", "Dynamic RigidBody Box"},
                {EntityPresetKind::DynamicSphere, "动态刚体球体 (Dynamic RigidBody Sphere)", "Dynamic RigidBody Sphere"},
                {EntityPresetKind::TriggerVolume, "触发器区域 (Trigger Volume)", "Trigger Volume"},
            };

            static constexpr PresetCategoryDef kCategories[] = {
                {"基础 (Basic)", "Basic", kBasicEntries, sizeof(kBasicEntries) / sizeof(kBasicEntries[0])},
                {"3D 几何体 (3D Primitives)", "3D Primitives", kPrimitiveEntries, sizeof(kPrimitiveEntries) / sizeof(kPrimitiveEntries[0])},
                {"灯光 (Lights)", "Lights", kLightEntries, sizeof(kLightEntries) / sizeof(kLightEntries[0])},
                {"相机 (Cameras)", "Cameras", kCameraEntries, sizeof(kCameraEntries) / sizeof(kCameraEntries[0])},
                {"物理 (Physics)", "Physics", kPhysicsEntries, sizeof(kPhysicsEntries) / sizeof(kPhysicsEntries[0])},
            };

            for (const auto& cat : kCategories)
            {
                if (ImGui::BeginMenu(L(cat.zhCategory, cat.enCategory)))
                {
                    for (size_t i = 0; i < cat.count; ++i)
                    {
                        const auto& item = cat.entries[i];
                        if (ImGui::MenuItem(L(item.zhLabel, item.enLabel)))
                        {
                            (void)createEntityFromPreset(item.kind, parentId);
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        }

        void SceneViewerComposite::renderTrunkStreamingSection()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };
            if (!ImGui::CollapsingHeader(L("子场景分块流式管理###SceneTrunkStreaming", "SceneTrunk Streaming###SceneTrunkStreaming"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            if (!scene)
            {
                ImGui::TextDisabled("%s", L("SceneSubsystem 尚未初始化", "SceneSubsystem not initialized"));
                return;
            }

            std::map<std::string, std::string> discoveredTrunks;
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (loader)
            {
                std::vector<std::string> candidateDirs;
                if (m_editorUiComposite && !m_editorUiComposite->getActiveProjectDirectory().empty())
                {
                    const std::string& projDir = m_editorUiComposite->getActiveProjectDirectory();
                    candidateDirs.push_back(SAI::Path::join(projDir, "Scenes/Trunks"));
                    candidateDirs.push_back(SAI::Path::join(projDir, "Saved/Trunks"));
                }
                else
                {
                    candidateDirs.push_back("Scenes/Trunks");
                    candidateDirs.push_back("Saved/Trunks");
                }
                for (const auto& dirPath : candidateDirs)
                {
                    if (!loader->accessDir(dirPath).ok())
                    {
                        continue;
                    }
                    auto filesOr = loader->readDir(dirPath);
                    if (!filesOr.ok())
                    {
                        continue;
                    }
                    for (const auto& file : *filesOr)
                    {
                        if (file && file->extension() == ".mqtrunk")
                        {
                            discoveredTrunks.emplace(file->stem(), dirPath);
                        }
                    }
                }
            }

            for (const auto& part : scene->getRegistryPartitions())
            {
                if (part.kind == Domain::Scene::ERegistryKind::Streaming)
                {
                    discoveredTrunks.emplace(part.name, "Saved/Trunks");
                }
            }

            if (discoveredTrunks.empty())
            {
                ImGui::TextDisabled("%s", L("当前工程暂无流式子场景分块 (.mqtrunk)", "No streaming scene trunks (.mqtrunk) configured"));
                return;
            }

            for (const auto& [trunkName, trunkDir] : discoveredTrunks)
            {
                const bool loaded = scene->isTrunkLoaded(trunkName);
                ImGui::BulletText("%s [%s]", trunkName.c_str(), loaded ? L("已挂载", "Loaded") : L("未加载", "Unloaded"));
                ImGui::SameLine();
                std::string btnLabel = loaded
                    ? (std::string(L("卸载##", "Unload##")) + trunkName)
                    : (std::string(L("流式加载##", "Stream Load##")) + trunkName);
                if (ImGui::SmallButton(btnLabel.c_str()))
                {
                    if (loaded)
                    {
                        scene->unloadTrunk(trunkName);
                        if (m_selectedRegistryFilter == trunkName)
                        {
                            m_selectedRegistryFilter = "All";
                        }
                        if (m_activeTargetRegistry == trunkName)
                        {
                            m_activeTargetRegistry = "Main";
                        }
                    }
                    else
                    {
                        (void)scene->loadTrunk(trunkName, trunkDir);
                    }
                    syncEntitiesFromDomain();
                    syncFilterVisibilityToScene();
                }
            }
        }

        void SceneViewerComposite::renderHierarchy()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };
            const std::string hierTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle("层级大纲 (Scene Hierarchy)")
                : "层级大纲###层级大纲 (Scene Hierarchy)";
            ImGui::Begin(hierTitle.c_str());
            handleHierarchyAndViewportShortcuts(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));

            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;

            ImGui::InputTextWithHint("##Filter", L("按名称或 Registry 搜索...", "Search entity or registry..."), m_hierarchyFilter, sizeof(m_hierarchyFilter));
            ImGui::SameLine();
            if (ImGui::Button(L("+ 新建", "+ Create")))
            {
                ImGui::OpenPopup("CreateEntityPopup");
            }

            // ==================== ECS Registry 分区过滤栏 ====================
            if (scene)
            {
                const std::vector<Domain::Scene::RegistryPartitionInfo> partitions = scene->getRegistryPartitions();

                ImGui::SetNextItemWidth(155.0f);
                std::string regComboPreview = std::string(L("全部 Registry", "All Registries"));
                if (m_selectedRegistryFilter != "All")
                {
                    const auto kind = scene->getRegistryKind(m_selectedRegistryFilter);
                    regComboPreview = m_selectedRegistryFilter + " [" + getRegistryKindBadge(kind, m_editorUiComposite) + "]";
                }
                if (ImGui::BeginCombo("##RegistryFilterCombo", regComboPreview.c_str()))
                {
                    if (ImGui::Selectable(L("全部 Registry (All)", "All Registries"), m_selectedRegistryFilter == "All"))
                    {
                        m_selectedRegistryFilter = "All";
                        syncFilterVisibilityToScene();
                    }
                    for (const auto& part : partitions)
                    {
                        const std::string itemLabel =
                            part.name + " [" + getRegistryKindBadge(part.kind, m_editorUiComposite) + "]";
                        if (ImGui::Selectable(itemLabel.c_str(), m_selectedRegistryFilter == part.name))
                        {
                            m_selectedRegistryFilter = part.name;
                            m_activeTargetRegistry = part.name;
                            syncFilterVisibilityToScene();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(L("+ 分区", "+ Reg")))
                {
                    ImGui::OpenPopup("CreateRegistryPopup");
                }
                if (ImGui::BeginPopup("CreateRegistryPopup"))
                {
                    ImGui::Text("%s", L("新建独立 ECS Registry 分区:", "Create ECS Registry Partition:"));
                    ImGui::InputTextWithHint("##NewRegName", "EditorPreview / SubLevel / VFX...", m_newRegistryNameBuf, sizeof(m_newRegistryNameBuf));
                    if (ImGui::Button(L("创建并切换", "Create & Switch")) && m_newRegistryNameBuf[0] != '\0')
                    {
                        scene->getOrCreateRegistry(m_newRegistryNameBuf);
                        m_activeTargetRegistry = m_newRegistryNameBuf;
                        m_selectedRegistryFilter = m_newRegistryNameBuf;
                        m_newRegistryNameBuf[0] = '\0';
                        syncFilterVisibilityToScene();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(L("视口联动", "Sync View"), &m_syncFilterToViewport))
                {
                    syncFilterVisibilityToScene();
                }
            }

            if (ImGui::BeginPopup("CreateEntityPopup"))
            {
                if (scene)
                {
                    const std::vector<Domain::Scene::RegistryPartitionInfo> partitions = scene->getRegistryPartitions();
                    if (ImGui::BeginCombo(L("目标 Registry", "Target Registry"), m_activeTargetRegistry.c_str()))
                    {
                        for (const auto& part : partitions)
                        {
                            const std::string itemLabel =
                                part.name + " [" + getRegistryKindBadge(part.kind, m_editorUiComposite) + "]";
                            if (ImGui::Selectable(itemLabel.c_str(), m_activeTargetRegistry == part.name))
                            {
                                m_activeTargetRegistry = part.name;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::Separator();
                }
                renderCategorizedEntityCreateMenus(0);
                ImGui::EndPopup();
            }

            ImGui::Separator();

            // 按 ECS Registry 分区分组渲染层级树，清晰隔离主场景、编辑器实体与流式加载分块
            if (scene)
            {
                const std::vector<Domain::Scene::RegistryPartitionInfo> partitions = scene->getRegistryPartitions();
                for (const auto& part : partitions)
                {
                    if (m_selectedRegistryFilter != "All" && m_selectedRegistryFilter != part.name)
                    {
                        continue;
                    }

                    uint32_t uiCount = 0;
                    for (const auto& e : m_entities)
                    {
                        const std::string rName = e.registryName.empty() ? "Main" : e.registryName;
                        if (rName == part.name)
                        {
                            ++uiCount;
                        }
                    }

                    ImGui::PushID(("RegSection_" + part.name).c_str());
                    bool regVis = part.visible;
                    if (ImGui::Checkbox("##RegVisToggle", &regVis))
                    {
                        scene->setRegistryVisible(part.name, regVis);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", L("控制该 Registry 分区在 3D 视口中的显隐", "Toggle 3D viewport visibility for this Registry"));
                    }
                    ImGui::SameLine();

                    const std::string headerText =
                        part.name + " [" + getRegistryKindBadge(part.kind, m_editorUiComposite) + "] (" +
                        std::to_string(uiCount) + ")###RegHeader_" + part.name;

                    ImVec4 headerCol(0.86f, 0.90f, 0.98f, 1.0f);
                    if (part.kind == Domain::Scene::ERegistryKind::Editor)
                    {
                        headerCol = ImVec4(0.62f, 0.85f, 1.0f, 1.0f);
                    }
                    else if (part.kind == Domain::Scene::ERegistryKind::Streaming)
                    {
                        headerCol = ImVec4(0.55f, 0.94f, 0.72f, 1.0f);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Text, headerCol);
                    const bool regOpened = ImGui::TreeNodeEx(
                        headerText.c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                    ImGui::PopStyleColor();

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ENTITY_REPARENT"))
                        {
                            const uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
                            if (auto* draggedEnt = findEntityById(draggedId);
                                draggedEnt && draggedEnt->registryName != part.name)
                            {
                                setEntityParent(draggedEnt->id, 0);
                                scene->destroyEntityInRegistry(draggedEnt->registryName, draggedEnt->ecsEntity);
                                draggedEnt->ecsEntity = FCT::ECS::null;
                                draggedEnt->registryName = part.name;
                                syncEntityToDomain(*draggedEnt);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (regOpened)
                    {
                        for (size_t i = 0; i < m_entities.size(); ++i)
                        {
                            const std::string rName = m_entities[i].registryName.empty() ? "Main" : m_entities[i].registryName;
                            if (m_entities[i].parentId == 0 && rName == part.name)
                            {
                                renderHierarchyNode(m_entities[i]);
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            else
            {
                for (size_t i = 0; i < m_entities.size(); ++i)
                {
                    if (m_entities[i].parentId == 0)
                    {
                        renderHierarchyNode(m_entities[i]);
                    }
                }
            }

            ImGui::Separator();
            renderTrunkStreamingSection();

            if (ImGui::BeginPopupContextWindow(
                    "HierarchyEmptyContextMenu",
                    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem(L("粘贴 (Paste)", "Paste"), "Ctrl+V", false, hasClipboardEntities()))
                {
                    pasteEntityFromClipboard(0, false);
                }
                ImGui::Separator();
                renderCategorizedEntityCreateMenus(0);
                ImGui::EndPopup();
            }

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine
