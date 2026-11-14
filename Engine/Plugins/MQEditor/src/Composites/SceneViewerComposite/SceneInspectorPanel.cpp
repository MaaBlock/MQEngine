/**
 * @file SceneInspectorPanel.cpp
 * @brief 基于 SchemaTable 与 C++20 Boost.PFR 零宏反射的全自动实体属性检视器面板 (Inspector) 与相机控制面板实现
 */
#include "SceneViewerComposite.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/table/SchemaTable.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            bool isHiddenSystemSchema(const std::string& typeName)
            {
                return (typeName == "WorldTransform" ||
                        typeName == "HierarchyRelation" ||
                        typeName == "EditorNameComponent" ||
                        typeName == "UuidComponent" ||
                        typeName == "DisabledTag" ||
                        typeName == "TransientTag");
            }

            bool isNonRemovableSchema(const std::string& typeName)
            {
                return (typeName == "LocalTransform" ||
                        typeName == "WorldTransform" ||
                        typeName == "HierarchyRelation" ||
                        typeName == "EditorNameComponent" ||
                        typeName == "UuidComponent" ||
                        typeName == "DisabledTag" ||
                        typeName == "TransientTag");
            }

            bool isHiddenInternalField(const Resource::FieldDescriptor& field)
            {
                if (field.primitiveKind == Resource::EFieldPrimitiveKind::Unknown)
                {
                    return true;
                }
                return (field.name == "isDirty" ||
                        field.name == "dirtyVersion" ||
                        field.name == "uuid");
            }

            int getSchemaSortPriority(const std::string& typeName)
            {
                if (typeName == "LocalTransform") return 0;
                if (typeName == "StaticMeshComponent") return 1;
                if (typeName == "PbrMaterialComponent") return 2;
                if (typeName == "CameraComponent") return 3;
                if (typeName == "DirectionalLightComponent") return 4;
                if (typeName == "PointLightComponent") return 5;
                if (typeName == "RigidBodyComponent") return 6;
                if (typeName == "NativeScriptComponent") return 7;
                return 100;
            }

            std::string toLowerCopy(std::string_view input)
            {
                std::string out(input);
                std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return out;
            }

            bool containsAsciiIgnoreCase(std::string_view haystack, std::string_view needle)
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

            bool isDragBindablePathField(const std::string& fieldName)
            {
                return containsAsciiIgnoreCase(fieldName, "AssetPath") ||
                       containsAsciiIgnoreCase(fieldName, "Script") ||
                       containsAsciiIgnoreCase(fieldName, "Shader") ||
                       containsAsciiIgnoreCase(fieldName, "Source");
            }

            bool editSchemaFloatField(
                const std::string& compName,
                const std::string& fieldName,
                const char* label,
                float* valPtr)
            {
                const bool isUnitFactor =
                    (fieldName == "metallic" ||
                     fieldName == "roughness" ||
                     fieldName == "ao" ||
                     fieldName == "restitution" ||
                     fieldName == "friction");
                bool edited = isUnitFactor
                    ? ImGui::SliderFloat(label, valPtr, 0.0f, 1.0f)
                    : ImGui::DragFloat(label, valPtr, (compName == "CameraComponent" && fieldName == "orthoSize") ? 0.25f : 0.05f);
                if (edited && compName == "CameraComponent")
                {
                    if (fieldName == "fov") *valPtr = std::clamp(*valPtr, 5.0f, 160.0f);
                    else if (fieldName == "orthoSize") *valPtr = std::max(*valPtr, 0.1f);
                    else if (fieldName == "nearPlane") *valPtr = std::max(*valPtr, 0.001f);
                    else if (fieldName == "farPlane") *valPtr = std::max(*valPtr, 0.5f);
                }
                return edited;
            }

            bool canOpenStringFieldInIde(const std::string& fieldName, const std::string& fieldValue)
            {
                if (fieldValue.empty())
                {
                    return false;
                }
                if (EditorBase::EditorUiComposite::isCodeEditorSourceFile(fieldValue))
                {
                    return true;
                }
                return containsAsciiIgnoreCase(fieldName, "Script") ||
                       containsAsciiIgnoreCase(fieldName, "Shader") ||
                       containsAsciiIgnoreCase(fieldName, "Source");
            }

            std::string searchComponentHeaderRecursive(
                ResourceLoader* loader,
                const std::string& dirPath,
                const std::string& targetStemLower,
                int depth)
            {
                if (!loader || depth > 4 || !loader->accessDir(dirPath).ok())
                {
                    return "";
                }
                auto entriesOr = loader->readDir(dirPath);
                if (!entriesOr.ok())
                {
                    return "";
                }

                for (const auto& entry : entriesOr.value())
                {
                    const std::string itemPath = SAI::Path::lexicallyNormal(entry->path());
                    if (loader->accessDir(itemPath).ok())
                    {
                        const std::string subName = SAI::Path::fileName(itemPath);
                        if (!subName.empty() && subName[0] != '.' && subName != "Dist")
                        {
                            std::string found = searchComponentHeaderRecursive(loader, itemPath, targetStemLower, depth + 1);
                            if (!found.empty())
                            {
                                return found;
                            }
                        }
                        continue;
                    }

                    const std::string extLower = SAI::Path::extensionLower(itemPath);
                    if (extLower == ".h" || extLower == ".hpp")
                    {
                        const std::string stemLower = toLowerCopy(SAI::Path::stem(itemPath));
                        if (stemLower == targetStemLower)
                        {
                            return itemPath;
                        }
                    }
                }
                return "";
            }

            void openComponentSourceOrProjectInIde(EditorBase::EditorUiComposite* editorUi, const std::string& compName)
            {
                if (!editorUi)
                {
                    return;
                }
                auto* resMod = Resource::Resource::InstancePtr();
                auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
                const std::string projDir = editorUi->getActiveProjectDirectory();

                if (loader && !projDir.empty())
                {
                    const std::string targetLower = toLowerCopy(compName);
                    const std::string nativeFound = searchComponentHeaderRecursive(
                        loader, SAI::Path::join(projDir, "Native"), targetLower, 0);
                    if (!nativeFound.empty())
                    {
                        (void)editorUi->openFileInCodeEditor(nativeFound, 0, 0, true);
                        return;
                    }
                    const std::string pluginFound = searchComponentHeaderRecursive(
                        loader, SAI::Path::join(projDir, "Plugins"), targetLower, 0);
                    if (!pluginFound.empty())
                    {
                        (void)editorUi->openFileInCodeEditor(pluginFound, 0, 0, true);
                        return;
                    }
                }

                (void)editorUi->openProjectInCodeEditor("", true);
            }

            void openNativeScriptSourceInIde(
                EditorBase::EditorUiComposite* editorUi,
                const std::string& className,
                const std::string& sourceHint)
            {
                if (!editorUi)
                {
                    return;
                }
                auto* resMod = Resource::Resource::InstancePtr();
                auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
                const std::string projDir = editorUi->getActiveProjectDirectory();

                if (loader && !sourceHint.empty())
                {
                    if (loader->exists(sourceHint))
                    {
                        (void)editorUi->openFileInCodeEditor(sourceHint, 0, 0, true);
                        return;
                    }
                    if (!projDir.empty())
                    {
                        const std::string joined = SAI::Path::lexicallyNormal(SAI::Path::join(projDir, sourceHint));
                        if (loader->exists(joined))
                        {
                            (void)editorUi->openFileInCodeEditor(joined, 0, 0, true);
                            return;
                        }
                    }
                }
                openComponentSourceOrProjectInIde(editorUi, className);
            }

            void populateDefaultScriptSlotProperties(
                const Domain::GamePlay::NativeScriptClassDescriptor* scriptDesc,
                Domain::Scene::NativeScriptSlot& slot)
            {
                if (!scriptDesc)
                {
                    return;
                }
                for (const auto& propMeta : scriptDesc->properties)
                {
                    if (!Domain::Scene::FindNativeScriptProperty(slot, propMeta.name))
                    {
                        Domain::Scene::UpsertNativeScriptProperty(slot, propMeta.name, propMeta.defaultValue);
                    }
                }
            }

            void renderNativeScriptSlotProperties(
                FCT::ECS::registry& registry,
                FCT::ECS::entity ecsEntity,
                const Resource::TypeDescriptor& compDesc,
                Domain::Scene::NativeScriptComponent& scriptComp,
                size_t slotIndex,
                const Domain::GamePlay::NativeScriptClassDescriptor* scriptDesc,
                EditorBase::EditorUiComposite* editorUi,
                const std::function<void()>& onSyncAfterModify)
            {
                auto* domain = Domain::Domain::InstancePtr();
                auto& slot = scriptComp.scripts[slotIndex];
                if (domain)
                {
                    domain->gameplay.syncLiveScriptPropertiesToSlot(registry, ecsEntity, slot);
                }

                static EditorBase::DynamicComponentBlob s_propDragStartBlob;
                auto commitSlotChange = [&](EditorBase::DynamicComponentBlob beforeBlob, const std::string& propName) {
                    Domain::Scene::NormalizeNativeScriptSlots(scriptComp);
                    if (domain)
                    {
                        domain->gameplay.applySlotPropertiesToLiveScript(
                            registry, ecsEntity, scriptComp.scripts[slotIndex]);
                    }
                    if (editorUi && (!domain || !Runtime::Engine::getInstance() || !Runtime::Engine::getInstance()->hasApplicationTag("Game")))
                    {
                        EditorBase::DynamicComponentBlob afterBlob(&compDesc, &scriptComp);
                        editorUi->getCommandStack().executeCommand(
                            makeUnique<EditorBase::DynamicComponentModifyCommand>(
                                &registry,
                                ecsEntity,
                                &compDesc,
                                std::move(beforeBlob),
                                std::move(afterBlob),
                                propName,
                                onSyncAfterModify));
                    }
                    else if (onSyncAfterModify)
                    {
                        onSyncAfterModify();
                    }
                };

                if (scriptDesc && !scriptDesc->properties.empty())
                {
                    for (const auto& propMeta : scriptDesc->properties)
                    {
                        if (propMeta.kind == Resource::EFieldPrimitiveKind::Unknown)
                        {
                            continue;
                        }
                        const Domain::Scene::ScriptPropertyValue* curProp =
                            Domain::Scene::FindNativeScriptProperty(slot, propMeta.name);
                        Domain::Scene::ScriptPropertyValue val = curProp ? *curProp : propMeta.defaultValue;

                        const char* fname = propMeta.name.c_str();
                        switch (propMeta.kind)
                        {
                        case Resource::EFieldPrimitiveKind::Bool:
                        {
                            EditorBase::DynamicComponentBlob beforeBlob(&compDesc, &scriptComp);
                            bool tmp = val.boolVal;
                            if (ImGui::Checkbox(fname, &tmp))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot, propMeta.name, Domain::Scene::ScriptPropertyValue::fromBool(tmp));
                                commitSlotChange(std::move(beforeBlob), propMeta.name);
                            }
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::Int32:
                        {
                            int tmp = val.intVal;
                            if (ImGui::DragInt(fname, &tmp, 1.0f))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot, propMeta.name, Domain::Scene::ScriptPropertyValue::fromInt32(tmp));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::UInt32:
                        {
                            int tmp = static_cast<int>(val.uintVal);
                            if (ImGui::DragInt(fname, &tmp, 1.0f, 0, INT32_MAX))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot,
                                    propMeta.name,
                                    Domain::Scene::ScriptPropertyValue::fromUInt32(static_cast<uint32_t>(std::max(0, tmp))));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::Float:
                        {
                            float tmp = static_cast<float>(val.floatVal);
                            if (ImGui::DragFloat(fname, &tmp, 0.05f))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot, propMeta.name, Domain::Scene::ScriptPropertyValue::fromFloat(tmp));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::Double:
                        {
                            float tmp = static_cast<float>(val.floatVal);
                            if (ImGui::DragFloat(fname, &tmp, 0.05f))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot, propMeta.name, Domain::Scene::ScriptPropertyValue::fromDouble(static_cast<double>(tmp)));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::String:
                        {
                            char strBuf[256] = {0};
                            std::strncpy(strBuf, val.stringVal.c_str(), sizeof(strBuf) - 1);
                            if (ImGui::InputText(fname, strBuf, sizeof(strBuf)))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot, propMeta.name, Domain::Scene::ScriptPropertyValue::fromString(strBuf));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::Vec2:
                        {
                            float v2[2] = {val.vecVal.x, val.vecVal.y};
                            if (ImGui::DragFloat2(fname, v2, 0.05f))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot, propMeta.name, Domain::Scene::ScriptPropertyValue::fromVec2(FCT::Vec2(v2[0], v2[1])));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::Vec3:
                        {
                            float v3[3] = {val.vecVal.x, val.vecVal.y, val.vecVal.z};
                            const bool isColor =
                                (propMeta.name.find("color") != std::string::npos ||
                                 propMeta.name.find("Color") != std::string::npos);
                            const bool edited = isColor
                                ? ImGui::ColorEdit3(fname, v3)
                                : ImGui::DragFloat3(fname, v3, 0.05f);
                            if (edited)
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot,
                                    propMeta.name,
                                    Domain::Scene::ScriptPropertyValue::fromVec3(FCT::Vec3<>(v3[0], v3[1], v3[2])));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        case Resource::EFieldPrimitiveKind::Vec4:
                        {
                            float v4[4] = {val.vecVal.x, val.vecVal.y, val.vecVal.z, val.vecVal.w};
                            if (ImGui::DragFloat4(fname, v4, 0.05f))
                            {
                                Domain::Scene::UpsertNativeScriptProperty(
                                    slot,
                                    propMeta.name,
                                    Domain::Scene::ScriptPropertyValue::fromVec4(FCT::Vec4(v4[0], v4[1], v4[2], v4[3])));
                                if (domain) domain->gameplay.applySlotPropertiesToLiveScript(registry, ecsEntity, slot);
                            }
                            if (ImGui::IsItemActivated()) s_propDragStartBlob = EditorBase::DynamicComponentBlob(&compDesc, &scriptComp);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitSlotChange(std::move(s_propDragStartBlob), propMeta.name);
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
                else if (!slot.properties.empty())
                {
                    for (auto& kv : slot.properties)
                    {
                        ImGui::TextDisabled("%s (unloaded schema)", kv.first.c_str());
                    }
                }
            }

            void renderNativeScriptComponentInspector(
                FCT::ECS::registry& registry,
                FCT::ECS::entity ecsEntity,
                const Resource::TypeDescriptor& desc,
                EditorBase::EditorUiComposite* editorUi,
                const std::function<void()>& onSyncAfterModify)
            {
                auto* scriptComp = registry.try_get<Domain::Scene::NativeScriptComponent>(ecsEntity);
                if (!scriptComp)
                {
                    return;
                }
                Domain::Scene::NormalizeNativeScriptSlots(*scriptComp);

                auto L = [editorUi](const char* zh, const char* en) -> const char* {
                    return editorUi ? editorUi->tr(zh, en) : zh;
                };

                const std::string headerLabel = std::string(L("C++ 原生脚本 (NativeScriptComponent)", "Native Scripts (NativeScriptComponent)")) +
                                                "###DynamicSchema_NativeScriptComponent";
                if (!ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    return;
                }

                if (ImGui::BeginPopupContextItem("NativeScriptComponent_DynCtxMenu"))
                {
                    if (ImGui::MenuItem(L("移除该组件", "Remove Component")))
                    {
                        EditorBase::DynamicComponentBlob savedBlob(&desc, scriptComp);
                        if (editorUi)
                        {
                            editorUi->getCommandStack().executeCommand(
                                makeUnique<EditorBase::DynamicComponentAddRemoveCommand>(
                                    &registry, ecsEntity, &desc, false, std::move(savedBlob), onSyncAfterModify));
                        }
                        else if (desc.ecsRemove)
                        {
                            desc.ecsRemove(registry, ecsEntity);
                            if (onSyncAfterModify) onSyncAfterModify();
                        }
                        ImGui::EndPopup();
                        return;
                    }
                    ImGui::EndPopup();
                }

                auto* domain = Domain::Domain::InstancePtr();
                const std::vector<std::string> regScriptNames =
                    domain ? domain->gameplay.getRegisteredNativeScriptNames() : std::vector<std::string>{};

                ImGui::PushID("NativeScriptComponentSlots");
                int pendingRemoveSlot = -1;
                for (size_t i = 0; i < scriptComp->scripts.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    auto& slot = scriptComp->scripts[i];
                    const auto* scriptDesc =
                        (domain && !slot.className.empty()) ? domain->gameplay.getNativeScriptDescriptor(slot.className) : nullptr;
                    if (scriptDesc)
                    {
                        populateDefaultScriptSlotProperties(scriptDesc, slot);
                    }

                    EditorBase::DynamicComponentBlob beforeEnableBlob(&desc, scriptComp);
                    if (ImGui::Checkbox("##SlotEnabled", &slot.enabled))
                    {
                        Domain::Scene::NormalizeNativeScriptSlots(*scriptComp);
                        if (editorUi)
                        {
                            EditorBase::DynamicComponentBlob afterBlob(&desc, scriptComp);
                            editorUi->getCommandStack().executeCommand(
                                makeUnique<EditorBase::DynamicComponentModifyCommand>(
                                    &registry, ecsEntity, &desc, std::move(beforeEnableBlob), std::move(afterBlob), "enabled", onSyncAfterModify));
                        }
                    }
                    ImGui::SameLine();

                    const std::string comboPreview = slot.className.empty()
                        ? std::string(L("<选择原生脚本类>", "<Select NativeScript>"))
                        : slot.className;
                    ImGui::SetNextItemWidth(editorUi ? -96.0f : -36.0f);
                    if (ImGui::BeginCombo("##ScriptClassCombo", comboPreview.c_str()))
                    {
                        for (const std::string& candidate : regScriptNames)
                        {
                            const bool isSel = (slot.className == candidate);
                            if (ImGui::Selectable(candidate.c_str(), isSel) && !isSel)
                            {
                                EditorBase::DynamicComponentBlob beforeClassBlob(&desc, scriptComp);
                                slot.className = candidate;
                                slot.properties.clear();
                                if (domain)
                                {
                                    populateDefaultScriptSlotProperties(
                                        domain->gameplay.getNativeScriptDescriptor(candidate), slot);
                                }
                                Domain::Scene::NormalizeNativeScriptSlots(*scriptComp);
                                if (editorUi)
                                {
                                    EditorBase::DynamicComponentBlob afterBlob(&desc, scriptComp);
                                    editorUi->getCommandStack().executeCommand(
                                        makeUnique<EditorBase::DynamicComponentModifyCommand>(
                                            &registry, ecsEntity, &desc, std::move(beforeClassBlob), std::move(afterBlob), "className", onSyncAfterModify));
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (editorUi)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(L("源码", "Src")))
                        {
                            openNativeScriptSourceInIde(
                                editorUi,
                                slot.className,
                                scriptDesc ? scriptDesc->sourceFileHint : std::string{});
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X"))
                    {
                        pendingRemoveSlot = static_cast<int>(i);
                    }

                    ImGui::Indent(12.0f);
                    if (!slot.className.empty() && !scriptDesc)
                    {
                        ImGui::TextColored(
                            ImVec4(0.95f, 0.72f, 0.30f, 1.0f),
                            "%s: %s",
                            L("未找到已加载脚本类 (请先构建 Native DLL)", "Unregistered Script Class"),
                            slot.className.c_str());
                    }
                    renderNativeScriptSlotProperties(
                        registry, ecsEntity, desc, *scriptComp, i, scriptDesc, editorUi, onSyncAfterModify);
                    ImGui::Unindent(12.0f);
                    ImGui::Separator();
                    ImGui::PopID();
                }

                if (pendingRemoveSlot >= 0 && static_cast<size_t>(pendingRemoveSlot) < scriptComp->scripts.size())
                {
                    EditorBase::DynamicComponentBlob beforeRemBlob(&desc, scriptComp);
                    scriptComp->scripts.erase(scriptComp->scripts.begin() + pendingRemoveSlot);
                    Domain::Scene::NormalizeNativeScriptSlots(*scriptComp);
                    if (editorUi)
                    {
                        EditorBase::DynamicComponentBlob afterBlob(&desc, scriptComp);
                        editorUi->getCommandStack().executeCommand(
                            makeUnique<EditorBase::DynamicComponentModifyCommand>(
                                &registry, ecsEntity, &desc, std::move(beforeRemBlob), std::move(afterBlob), "removeScript", onSyncAfterModify));
                    }
                }

                if (ImGui::Button(L("+ 挂载原生脚本 (+ Add NativeScript)", "+ Add NativeScript"), ImVec2(-1.0f, 0.0f)))
                {
                    EditorBase::DynamicComponentBlob beforeAddBlob(&desc, scriptComp);
                    Domain::Scene::NativeScriptSlot newSlot{};
                    newSlot.enabled = true;
                    if (!regScriptNames.empty())
                    {
                        newSlot.className = regScriptNames.front();
                        if (domain)
                        {
                            populateDefaultScriptSlotProperties(
                                domain->gameplay.getNativeScriptDescriptor(newSlot.className), newSlot);
                        }
                    }
                    scriptComp->scripts.push_back(std::move(newSlot));
                    Domain::Scene::NormalizeNativeScriptSlots(*scriptComp);
                    if (editorUi)
                    {
                        EditorBase::DynamicComponentBlob afterBlob(&desc, scriptComp);
                        editorUi->getCommandStack().executeCommand(
                            makeUnique<EditorBase::DynamicComponentModifyCommand>(
                                &registry, ecsEntity, &desc, std::move(beforeAddBlob), std::move(afterBlob), "addScript", onSyncAfterModify));
                    }
                }
                ImGui::PopID();
            }

            std::vector<const Resource::TypeDescriptor*> getSortedInspectableSchemas()
            {
                std::vector<const Resource::TypeDescriptor*> result;
                auto* schemaTable = Resource::SchemaTable::InstancePtr();
                if (!schemaTable)
                {
                    return result;
                }
                for (const Resource::TypeDescriptor* desc : schemaTable->getAllLatestSchemas())
                {
                    if (!desc || isHiddenSystemSchema(desc->typeName))
                    {
                        continue;
                    }
                    result.push_back(desc);
                }
                std::sort(result.begin(), result.end(), [](const Resource::TypeDescriptor* a, const Resource::TypeDescriptor* b) {
                    const int pa = getSchemaSortPriority(a->typeName);
                    const int pb = getSchemaSortPriority(b->typeName);
                    if (pa != pb)
                    {
                        return pa < pb;
                    }
                    return a->typeName < b->typeName;
                });
                return result;
            }

            /**
             * @brief 基于 SchemaTable / TypeDescriptor 的全反射通用 ECS 组件检视器：
             *        无需手写具体组件 UI，自动遍历反射字段生成 ImGui 控件、枚举下拉框、颜色拾取器与资产拖拽槽，
             *        并统一通过 DynamicComponentModifyCommand / DynamicComponentAddRemoveCommand 接入撤销重做栈。
             */
            void renderDynamicSchemaComponent(
                FCT::ECS::registry& registry,
                FCT::ECS::entity ecsEntity,
                const Resource::TypeDescriptor& desc,
                EditorBase::EditorUiComposite* editorUi,
                std::function<void()> onSyncAfterModify)
            {
                if (ecsEntity == FCT::ECS::null || !registry.valid(ecsEntity) || !desc.ecsGetMutable)
                {
                    return;
                }
                if (desc.typeName == "NativeScriptComponent")
                {
                    renderNativeScriptComponentInspector(registry, ecsEntity, desc, editorUi, onSyncAfterModify);
                    return;
                }

                void* compPtr = desc.ecsGetMutable(registry, ecsEntity);
                if (!compPtr)
                {
                    return;
                }

                const std::string& compName = desc.typeName;
                const std::string headerLabel = compName + "###DynamicSchema_" + compName;
                if (!ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    return;
                }

                if (ImGui::BeginPopupContextItem((compName + "_DynCtxMenu").c_str()))
                {
                    if (editorUi)
                    {
                        const char* openSrcLabel = editorUi->tr(
                            "在代码编辑器中打开组件源码 / 工程",
                            "Open Component Source / Project in IDE");
                        if (ImGui::MenuItem(openSrcLabel))
                        {
                            openComponentSourceOrProjectInIde(editorUi, compName);
                        }
                    }

                    if (!isNonRemovableSchema(compName))
                    {
                        if (editorUi)
                        {
                            ImGui::Separator();
                        }
                        const char* removeLabel = editorUi ? editorUi->tr("移除该组件", "Remove Component") : "移除该组件";
                        if (ImGui::MenuItem(removeLabel))
                        {
                            EditorBase::DynamicComponentBlob savedBlob(&desc, compPtr);
                            if (editorUi)
                            {
                                editorUi->getCommandStack().executeCommand(
                                    makeUnique<EditorBase::DynamicComponentAddRemoveCommand>(
                                        &registry,
                                        ecsEntity,
                                        &desc,
                                        false,
                                        std::move(savedBlob),
                                        onSyncAfterModify));
                            }
                            else if (desc.ecsRemove)
                            {
                                desc.ecsRemove(registry, ecsEntity);
                                if (onSyncAfterModify) onSyncAfterModify();
                            }
                            ImGui::EndPopup();
                            return;
                        }
                    }
                    ImGui::EndPopup();
                }

                static EditorBase::DynamicComponentBlob s_dragStartBlob;
                uint8_t* baseBytes = static_cast<uint8_t*>(compPtr);

                ImGui::PushID(compName.c_str());
                for (const auto& field : desc.fields)
                {
                    if (isHiddenInternalField(field))
                    {
                        continue;
                    }

                    uint8_t* fieldAddr = baseBytes + field.offset;
                    const char* fname = field.name.c_str();

                    auto commitDynamicModify = [&](EditorBase::DynamicComponentBlob beforeBlob) {
                        EditorBase::DynamicComponentBlob afterBlob(&desc, compPtr);
                        if (editorUi)
                        {
                            editorUi->getCommandStack().executeCommand(
                                makeUnique<EditorBase::DynamicComponentModifyCommand>(
                                    &registry,
                                    ecsEntity,
                                    &desc,
                                    std::move(beforeBlob),
                                    std::move(afterBlob),
                                    field.name,
                                    onSyncAfterModify));
                        }
                        else if (onSyncAfterModify)
                        {
                            onSyncAfterModify();
                        }
                    };

                    switch (field.primitiveKind)
                    {
                    case Resource::EFieldPrimitiveKind::Bool:
                    {
                        EditorBase::DynamicComponentBlob beforeBlob(&desc, compPtr);
                        bool* valPtr = reinterpret_cast<bool*>(fieldAddr);
                        if (ImGui::Checkbox(fname, valPtr))
                        {
                            commitDynamicModify(std::move(beforeBlob));
                        }
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::Int32:
                    {
                        int32_t* valPtr = reinterpret_cast<int32_t*>(fieldAddr);
                        if (field.name == "primitiveType")
                        {
                            EditorBase::DynamicComponentBlob beforeBlob(&desc, compPtr);
                            if (ImGui::Combo(fname, valPtr, "Cube\0Plane\0Sphere\0CustomMesh\0\0"))
                            {
                                commitDynamicModify(std::move(beforeBlob));
                            }
                        }
                        else if (field.name == "bodyType")
                        {
                            EditorBase::DynamicComponentBlob beforeBlob(&desc, compPtr);
                            if (ImGui::Combo(fname, valPtr, "Static\0Dynamic\0Kinematic\0\0"))
                            {
                                commitDynamicModify(std::move(beforeBlob));
                            }
                        }
                        else if (field.name == "colliderType")
                        {
                            EditorBase::DynamicComponentBlob beforeBlob(&desc, compPtr);
                            if (ImGui::Combo(fname, valPtr, "Box\0Plane\0Sphere\0Capsule\0\0"))
                            {
                                commitDynamicModify(std::move(beforeBlob));
                            }
                        }
                        else
                        {
                            if (ImGui::DragInt(fname, valPtr, 1.0f) && onSyncAfterModify)
                            {
                                onSyncAfterModify();
                            }
                            if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                            if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));
                        }
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::UInt32:
                    {
                        uint32_t* valPtr = reinterpret_cast<uint32_t*>(fieldAddr);
                        int tmpVal = static_cast<int>(*valPtr);
                        if (ImGui::DragInt(fname, &tmpVal, 1.0f, 0, INT32_MAX))
                        {
                            *valPtr = static_cast<uint32_t>(std::max(0, tmpVal));
                            if (onSyncAfterModify) onSyncAfterModify();
                        }
                        if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                        if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::Float:
                    {
                        float* valPtr = reinterpret_cast<float*>(fieldAddr);
                        const bool edited = editSchemaFloatField(compName, field.name, fname, valPtr);
                        if (edited && onSyncAfterModify)
                        {
                            onSyncAfterModify();
                        }
                        if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                        if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::Double:
                    {
                        double* valPtr = reinterpret_cast<double*>(fieldAddr);
                        float tmpVal = static_cast<float>(*valPtr);
                        if (ImGui::DragFloat(fname, &tmpVal, 0.05f))
                        {
                            *valPtr = static_cast<double>(tmpVal);
                            if (onSyncAfterModify) onSyncAfterModify();
                        }
                        if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                        if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::String:
                    {
                        std::string* strPtr = reinterpret_cast<std::string*>(fieldAddr);
                        const bool showIdeOpenBtn = (editorUi != nullptr) && canOpenStringFieldInIde(field.name, *strPtr);
                        if (showIdeOpenBtn)
                        {
                            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::CalcItemWidth() - 68.0f));
                        }
                        char buf[256] = {};
                        std::strncpy(buf, strPtr->c_str(), sizeof(buf) - 1);
                        if (ImGui::InputText(fname, buf, sizeof(buf)))
                        {
                            *strPtr = buf;
                            if (onSyncAfterModify) onSyncAfterModify();
                        }
                        if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                        if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));

                        // 对资产、脚本与着色器路径字段自动启用拖拽绑定
                        if (isDragBindablePathField(field.name) && ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ASSET_PAYLOAD"))
                            {
                                EditorBase::DynamicComponentBlob beforeBlob(&desc, compPtr);
                                const char* assetPath = static_cast<const char*>(payload->Data);
                                *strPtr = assetPath;
                                if (compName == "StaticMeshComponent")
                                {
                                    if (auto* meshComp = registry.try_get<Domain::Scene::StaticMeshComponent>(ecsEntity))
                                    {
                                        std::string ext = Resource::TextureDecoderRegistry::extractExtension(assetPath);
                                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                                            return static_cast<char>(std::tolower(c));
                                        });
                                        if (ext == "obj" || ext == "gltf" || ext == "glb" || ext == "fbx")
                                        {
                                            if (auto* renderer = Domain::Renderer::RendererSubsystem::InstancePtr())
                                            {
                                                Domain::Renderer::LoadedModelMeshInfo modelInfo{};
                                                meshComp->customMesh = renderer->loadOrGetModelMesh(assetPath, &modelInfo);
                                                meshComp->primitiveType = 3;
                                            }
                                        }
                                    }
                                }
                                commitDynamicModify(std::move(beforeBlob));
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (showIdeOpenBtn)
                        {
                            ImGui::SameLine();
                            const std::string btnId = std::string(editorUi->tr("源码", "IDE")) + "##OpenIde_" + field.name;
                            if (ImGui::SmallButton(btnId.c_str()))
                            {
                                (void)editorUi->openFileInCodeEditor(*strPtr, 0, 0, true);
                            }
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s: %s",
                                                  editorUi->tr("在代码编辑器中打开源码", "Open source in Code Editor"),
                                                  strPtr->c_str());
                            }
                        }
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::Vec2:
                    {
                        FCT::Vec2* vecPtr = reinterpret_cast<FCT::Vec2*>(fieldAddr);
                        if (ImGui::DragFloat2(fname, &vecPtr->x, 0.1f) && onSyncAfterModify)
                        {
                            onSyncAfterModify();
                        }
                        if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                        if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::Vec3:
                    {
                        FCT::Vec3<>* vecPtr = reinterpret_cast<FCT::Vec3<>*>(fieldAddr);
                        const bool isColor =
                            (field.name.find("color") != std::string::npos ||
                             field.name.find("Color") != std::string::npos);
                        const float dragSpeed = (field.name == "rotation") ? 0.5f : 0.05f;
                        const bool edited = isColor
                            ? ImGui::ColorEdit3(fname, &vecPtr->x)
                            : ImGui::DragFloat3(fname, &vecPtr->x, dragSpeed);
                        if (edited)
                        {
                            constexpr float kDegToRad = 3.1415926535f / 180.0f;
                            constexpr float kRadToDeg = 180.0f / 3.1415926535f;
                            if (compName == "LocalTransform" && field.name == "rotation")
                            {
                                if (auto* dlComp = registry.try_get<Domain::Scene::DirectionalLightComponent>(ecsEntity))
                                {
                                    const float pitchRad = vecPtr->x * kDegToRad;
                                    const float yawRad = vecPtr->y * kDegToRad;
                                    dlComp->direction = FCT::Vec3<>(
                                        std::cos(pitchRad) * std::sin(yawRad),
                                        -std::sin(pitchRad),
                                        std::cos(pitchRad) * std::cos(yawRad)).normalize();
                                }
                            }
                            else if (compName == "DirectionalLightComponent" && field.name == "direction")
                            {
                                if (vecPtr->lengthSquared() > 1e-6f)
                                {
                                    const FCT::Vec3<> normDir = vecPtr->normalize();
                                    if (auto* localTf = registry.try_get<Domain::Scene::LocalTransform>(ecsEntity))
                                    {
                                        localTf->rotation.x = std::asin(std::clamp(-normDir.y, -1.0f, 1.0f)) * kRadToDeg;
                                        localTf->rotation.y = std::atan2(normDir.x, normDir.z) * kRadToDeg;
                                        localTf->isDirty = true;
                                    }
                                }
                            }
                            if (onSyncAfterModify)
                            {
                                onSyncAfterModify();
                            }
                        }
                        if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                        if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));
                        break;
                    }
                    case Resource::EFieldPrimitiveKind::Vec4:
                    {
                        FCT::Vec4* vecPtr = reinterpret_cast<FCT::Vec4*>(fieldAddr);
                        if (ImGui::ColorEdit4(fname, &vecPtr->x) && onSyncAfterModify)
                        {
                            onSyncAfterModify();
                        }
                        if (ImGui::IsItemActivated()) s_dragStartBlob = EditorBase::DynamicComponentBlob(&desc, compPtr);
                        if (ImGui::IsItemDeactivatedAfterEdit()) commitDynamicModify(std::move(s_dragStartBlob));
                        break;
                    }
                    default:
                        break;
                    }
                }
                ImGui::PopID();
            }

            void renderAllRegisteredSchemaComponents(
                FCT::ECS::registry& registry,
                FCT::ECS::entity ecsEntity,
                EditorBase::EditorUiComposite* editorUi,
                std::function<void()> onSyncAfterModify)
            {
                for (const Resource::TypeDescriptor* desc : getSortedInspectableSchemas())
                {
                    renderDynamicSchemaComponent(registry, ecsEntity, *desc, editorUi, onSyncAfterModify);
                }
            }

            void renderAllRegisteredAddComponentMenuItems(
                FCT::ECS::registry& registry,
                FCT::ECS::entity ecsEntity,
                EditorBase::EditorUiComposite* editorUi,
                std::function<void()> onSyncAfterModify)
            {
                if (ecsEntity == FCT::ECS::null || !registry.valid(ecsEntity))
                {
                    return;
                }
                for (const Resource::TypeDescriptor* desc : getSortedInspectableSchemas())
                {
                    if (!desc->ecsEmplaceOrReplace || isNonRemovableSchema(desc->typeName))
                    {
                        continue;
                    }
                    const bool alreadyHas = desc->ecsHas ? desc->ecsHas(registry, ecsEntity) : false;
                    if (ImGui::MenuItem(desc->typeName.c_str(), nullptr, false, !alreadyHas))
                    {
                        if (editorUi)
                        {
                            editorUi->getCommandStack().executeCommand(
                                makeUnique<EditorBase::DynamicComponentAddRemoveCommand>(
                                    &registry,
                                    ecsEntity,
                                    desc,
                                    true,
                                    EditorBase::DynamicComponentBlob{},
                                    onSyncAfterModify));
                        }
                        else
                        {
                            desc->ecsEmplaceOrReplace(registry, ecsEntity, nullptr);
                            if (onSyncAfterModify) onSyncAfterModify();
                        }
                    }
                }
            }
        } // namespace

        void SceneViewerComposite::renderInspector()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };
            const std::string inspTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle("实体检视器 (Inspector)")
                : "实体检视器###实体检视器 (Inspector)";
            ImGui::Begin(inspTitle.c_str());

            if (m_editorUiComposite)
            {
                const std::string ideName = m_editorUiComposite->getActiveCodeEditorDisplayName();
                const std::string openIdeProjLabel = !ideName.empty()
                    ? (m_editorUiComposite->getLanguage() == EditorBase::EditorLanguage::EnUS
                        ? ("Open C++ Project in " + ideName)
                        : ("在 " + ideName + " 中打开 C++ 工程"))
                    : std::string(L("在代码编辑器中打开 C++ 工程", "Open C++ Project in Code Editor"));
                if (ImGui::Button(openIdeProjLabel.c_str(), ImVec2(-1, 0)))
                {
                    (void)m_editorUiComposite->openProjectInCodeEditor("", true);
                }
                ImGui::Separator();
            }

            auto* entity = getSelectedEntity();
            if (!entity)
            {
                ImGui::TextDisabled("%s", L("未选中任何实体", "No entity selected"));
                ImGui::End();
                return;
            }

            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;

            // 顶栏启用状态直接映射零字节 DisabledTag
            FCT::ECS::registry* activeReg = resolveEntityRegistry(*entity);
            if (activeReg && entity->ecsEntity != FCT::ECS::null && activeReg->valid(entity->ecsEntity))
            {
                entity->active = !activeReg->all_of<DisabledTag>(entity->ecsEntity);
            }

            if (ImGui::Checkbox("##EntityActive", &entity->active))
            {
                if (activeReg && entity->ecsEntity != FCT::ECS::null && activeReg->valid(entity->ecsEntity))
                {
                    if (entity->active)
                    {
                        activeReg->remove<DisabledTag>(entity->ecsEntity);
                    }
                    else
                    {
                        activeReg->emplace_or_replace<DisabledTag>(entity->ecsEntity);
                    }
                }
                syncEntityToDomain(*entity);
            }
            ImGui::SameLine();
            char nameBuf[128] = {0};
            std::strncpy(nameBuf, entity->name.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText(L("名称", "Name"), nameBuf, sizeof(nameBuf)))
            {
                entity->name = nameBuf;
                syncEntityToDomain(*entity);
            }
            if (!entity->hasTransform)
            {
                ImGui::TextColored(
                    ImVec4(0.45f, 0.80f, 1.0f, 1.0f),
                    "%s",
                    L("纯逻辑实体 (无空间 Transform 组件)", "Logic Entity (No Spatial Transform)"));
            }

            // 所属 ECS Registry 分区检视与跨分区迁移
            const char* regKindLabel = L("主场景", "Runtime");
            if (scene)
            {
                auto kindToStr = [&](Domain::Scene::ERegistryKind kind) -> const char* {
                    switch (kind)
                    {
                    case Domain::Scene::ERegistryKind::Main:
                        return L("主场景运行时", "Main Runtime");
                    case Domain::Scene::ERegistryKind::Editor:
                        return L("编辑器专属", "Editor-Only");
                    case Domain::Scene::ERegistryKind::Streaming:
                        return L("流式子场景", "Streaming Trunk");
                    case Domain::Scene::ERegistryKind::Custom:
                    default:
                        return L("自定义分区", "Custom Partition");
                    }
                };
                regKindLabel = kindToStr(scene->getRegistryKind(entity->registryName));

                const std::vector<Domain::Scene::RegistryPartitionInfo> partitions = scene->getRegistryPartitions();
                const std::string previewText = entity->registryName + " [" + regKindLabel + "]";
                ImGui::SetNextItemWidth(-95.0f);
                if (ImGui::BeginCombo(L("所属 Registry", "Registry"), previewText.c_str()))
                {
                    for (const auto& part : partitions)
                    {
                        const std::string itemLabel = part.name + " [" + kindToStr(part.kind) + "]";
                        if (ImGui::Selectable(itemLabel.c_str(), entity->registryName == part.name) &&
                            entity->registryName != part.name)
                        {
                            setEntityParent(entity->id, 0);
                            scene->destroyEntityInRegistry(entity->registryName, entity->ecsEntity);
                            entity->ecsEntity = FCT::ECS::null;
                            entity->registryName = part.name;
                            syncEntityToDomain(*entity);
                            regKindLabel = kindToStr(part.kind);
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::TextDisabled(
                L("实体 ID: %u | ECS 句柄: %u | Registry: %s (%s)",
                  "Entity ID: %u | ECS Handle: %u | Registry: %s (%s)"),
                entity->id,
                static_cast<uint32_t>(FCT::ECS::to_integral(entity->ecsEntity)),
                entity->registryName.c_str(),
                regKindLabel);
            ImGui::Separator();

            // 完全通过 SchemaTable 与 Boost.PFR 反射驱动全部 ECS 组件检视与编辑
            FCT::ECS::registry* targetReg = resolveEntityRegistry(*entity);
            if (targetReg && entity->ecsEntity != FCT::ECS::null && targetReg->valid(entity->ecsEntity))
            {
                auto onSyncCallback = [this]() {
                    syncEntitiesFromDomain();
                };
                renderAllRegisteredSchemaComponents(
                    *targetReg,
                    entity->ecsEntity,
                    m_editorUiComposite,
                    onSyncCallback);

                ImGui::Separator();
                if (ImGui::Button(L("添加组件...", "Add Component..."), ImVec2(-1, 30)))
                {
                    ImGui::OpenPopup("AddComponentPopup");
                }

                if (ImGui::BeginPopup("AddComponentPopup"))
                {
                    ImGui::TextDisabled("%s", L("可挂载 ECS 组件 (反射注册):", "Available ECS Components:"));
                    renderAllRegisteredAddComponentMenuItems(
                        *targetReg,
                        entity->ecsEntity,
                        m_editorUiComposite,
                        onSyncCallback);
                    ImGui::EndPopup();
                }
            }

            ImGui::End();
        }

        void SceneViewerComposite::renderCameraControl()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };
            const std::string camTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle("相机控制 (Camera Control)")
                : "相机控制###相机控制 (Camera Control)";
            ImGui::Begin(camTitle.c_str());

            int srcMode = static_cast<int>(m_cameraSource);
            const char* srcItems = L(
                "编辑器自由相机 (EditorViewport)\0游戏主视角 (MainWindow)\0预览指定场景相机实体 (CameraComponent)\0\0",
                "Editor Free Camera\0Game Main Camera (MainWindow)\0Specific Scene Camera Entity\0\0");
            if (ImGui::Combo(L("活跃视口相机源", "Active Camera Source"), &srcMode, srcItems))
            {
                if (srcMode == 0)
                {
                    setViewportCameraSource(ViewportCameraSource::EditorFree);
                }
                else if (srcMode == 1)
                {
                    setViewportCameraSource(ViewportCameraSource::ActiveGameMain);
                }
                else
                {
                    for (const auto& ent : m_entities)
                    {
                        if (ent.hasCamera && ent.ecsEntity != FCT::ECS::null)
                        {
                            setSpecificCameraEntity(ent.registryName, ent.ecsEntity);
                            break;
                        }
                    }
                }
            }

            if (m_cameraSource == ViewportCameraSource::SpecificEntity)
            {
                std::string curCamLabel = L("<选择相机实体>", "<Select Camera Entity>");
                for (const auto& ent : m_entities)
                {
                    if (ent.ecsEntity == m_specificCameraEcsEntity && ent.registryName == m_specificCameraRegistry)
                    {
                        curCamLabel = ent.name + " [" + ent.registryName + "]";
                        break;
                    }
                }
                if (ImGui::BeginCombo(L("目标相机实体", "Target Camera"), curCamLabel.c_str()))
                {
                    for (const auto& ent : m_entities)
                    {
                        if (!ent.hasCamera || ent.ecsEntity == FCT::ECS::null)
                        {
                            continue;
                        }
                        const std::string itemLabel = ent.name + " [" + ent.registryName + "]";
                        const bool isSel =
                            (ent.ecsEntity == m_specificCameraEcsEntity && ent.registryName == m_specificCameraRegistry);
                        if (ImGui::Selectable(itemLabel.c_str(), isSel))
                        {
                            setSpecificCameraEntity(ent.registryName, ent.ecsEntity);
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            if (ImGui::Button(m_cameraSource == ViewportCameraSource::EditorFree
                    ? L("切换到游戏主相机 (F8)", "Switch to Game Main Camera (F8)")
                    : L("切换到编辑器自由相机 (F8)", "Switch to Editor Free Camera (F8)")))
            {
                toggleEditorAndGameCamera();
            }
            if (m_cameraSource != ViewportCameraSource::EditorFree)
            {
                ImGui::SameLine();
                if (ImGui::Button(L("对齐自由相机到当前视角", "Align Free Cam to View")))
                {
                    alignEditorCameraToCurrentView();
                }
                ImGui::TextDisabled("%s", L("提示: 下列参数控制编辑器自由相机，编辑态与 PIE 运行态均可随时切换",
                                            "Note: Controls below drive Editor Free Camera in both Edit and PIE modes"));
            }
            ImGui::Separator();

            bool posOrTargetChanged = false;
            posOrTargetChanged |= ImGui::DragFloat3(L("相机位置", "Position"), m_cameraPos, 0.1f, -500.0f, 500.0f);
            posOrTargetChanged |= ImGui::DragFloat3(L("观察焦点", "Target"), m_cameraTarget, 0.1f, -500.0f, 500.0f);
            if (posOrTargetChanged)
            {
                FCT::Vec3<> fwd(
                    m_cameraTarget[0] - m_cameraPos[0],
                    m_cameraTarget[1] - m_cameraPos[1],
                    m_cameraTarget[2] - m_cameraPos[2]);
                if (fwd.lengthSquared() > 1e-6f)
                {
                    fwd = fwd.normalize();
                    constexpr float kRadToDeg = 180.0f / 3.14159265f;
                    m_cameraRot[0] = std::asin(std::clamp(-fwd.y, -1.0f, 1.0f)) * kRadToDeg;
                    m_cameraRot[1] = std::atan2(fwd.x, fwd.z) * kRadToDeg;
                }
            }

            bool rotChanged = ImGui::DragFloat3(L("相机姿态角", "Pitch/Yaw/Roll"), m_cameraRot, 0.5f, -180.0f, 180.0f);
            if (rotChanged)
            {
                constexpr float kDegToRad = 3.14159265f / 180.0f;
                const float pitchRad = std::clamp(m_cameraRot[0], -89.0f, 89.0f) * kDegToRad;
                const float yawRad = m_cameraRot[1] * kDegToRad;
                const FCT::Vec3<> eye(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]);
                const FCT::Vec3<> target(m_cameraTarget[0], m_cameraTarget[1], m_cameraTarget[2]);
                const float dist = std::max((target - eye).length(), 1.0f);
                const FCT::Vec3<> newFwd(
                    std::cos(pitchRad) * std::sin(yawRad),
                    -std::sin(pitchRad),
                    std::cos(pitchRad) * std::cos(yawRad));
                m_cameraTarget[0] = eye.x + newFwd.x * dist;
                m_cameraTarget[1] = eye.y + newFwd.y * dist;
                m_cameraTarget[2] = eye.z + newFwd.z * dist;
            }

            bool changed = posOrTargetChanged || rotChanged;
            int projMode = m_editorCameraOrthographic ? 1 : 0;
            const char* projItems = L("透视投影 (Perspective)\0正交投影 (Orthographic)\0\0", "Perspective\0Orthographic\0\0");
            if (ImGui::Combo(L("自由相机投影", "Editor Projection"), &projMode, projItems))
            {
                m_editorCameraOrthographic = (projMode == 1);
                changed = true;
            }
            if (!m_editorCameraOrthographic)
            {
                changed |= ImGui::SliderFloat(L("视场角", "FOV"), &m_cameraFov, 20.0f, 120.0f);
            }
            else
            {
                changed |= ImGui::DragFloat(L("正交半高 (Ortho Size)", "Ortho Half Size"), &m_editorCameraOrthoSize, 0.25f, 0.5f, 500.0f);
            }
            changed |= ImGui::DragFloat(L("近裁剪面", "Near Clip"), &m_cameraNear, 0.01f, 0.01f, 10.0f);
            changed |= ImGui::DragFloat(L("远裁剪面", "Far Clip"), &m_cameraFar, 1.0f, 10.0f, 10000.0f);
            ImGui::SliderFloat(L("漫游移动速度", "Fly Speed"), &m_cameraSpeed, 0.5f, 50.0f);

            if (ImGui::Button(L("重置相机视角", "Reset View")))
            {
                m_cameraPos[0] = 0.0f; m_cameraPos[1] = 4.5f; m_cameraPos[2] = -9.5f;
                m_cameraTarget[0] = 0.0f; m_cameraTarget[1] = 1.0f; m_cameraTarget[2] = 0.0f;
                m_cameraRot[0] = 20.0f; m_cameraRot[1] = 0.0f; m_cameraRot[2] = 0.0f;
                m_cameraFov = 60.0f;
                m_editorCameraOrthographic = false;
                m_editorCameraOrthoSize = 12.0f;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("顶视图", "Top View")))
            {
                m_cameraPos[0] = 0.0f; m_cameraPos[1] = 16.0f; m_cameraPos[2] = 0.0f;
                m_cameraTarget[0] = 0.0f; m_cameraTarget[1] = 0.0f; m_cameraTarget[2] = 0.0f;
                m_cameraRot[0] = 89.0f; m_cameraRot[1] = 0.0f; m_cameraRot[2] = 0.0f;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("前视图", "Front View")))
            {
                m_cameraPos[0] = 0.0f; m_cameraPos[1] = 1.5f; m_cameraPos[2] = -12.0f;
                m_cameraTarget[0] = 0.0f; m_cameraTarget[1] = 1.5f; m_cameraTarget[2] = 0.0f;
                m_cameraRot[0] = 0.0f; m_cameraRot[1] = 0.0f; m_cameraRot[2] = 0.0f;
                changed = true;
            }

            if (changed)
            {
                syncCameraToRenderer();
            }

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine
