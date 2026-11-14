/**
 * @file SceneSubsystem.cpp
 * @brief 场景子系统实现：三层正交场景图协同、Resource::DataBase 对接与严格无锁生命周期
 */
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/resource/config/ConfigDocument.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/table/SchemaTable.h>
#include <MQEngine/runtime/core/Engine.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            namespace
            {
                template <typename... ComponentTypes>
                void warmComponentStorages(FCT::ECS::registry& registry)
                {
                    (static_cast<void>(registry.storage<ComponentTypes>()), ...);
                }

                void warmCoreSceneComponentStorages(FCT::ECS::registry& registry)
                {
                    warmComponentStorages<
                        LocalTransform,
                        WorldTransform,
                        HierarchyRelation,
                        DisabledTag,
                        TransientTag,
                        ModelAnchorTag,
                        UuidComponent,
                        EditorNameComponent,
                        CameraComponent,
                        DirectionalLightComponent,
                        PointLightComponent,
                        StaticMeshComponent,
                        PbrMaterialComponent,
                        RigidBodyComponent,
                        NativeScriptComponent>(registry);
                }

                ResourceLoader* resolveSceneResourceLoader(ResourceLoader* explicitLoader) noexcept
                {
                    if (explicitLoader) {
                        return explicitLoader;
                    }
                    if (auto* res = Resource::Resource::InstancePtr()) {
                        if (auto* loader = res->getResourceLoader()) {
                            return loader;
                        }
                    }
                    static DiskResourceLoader s_fallbackLoader;
                    return &s_fallbackLoader;
                }

                ConfigNode makeVec3Node(const FCT::Vec3<>& v)
                {
                    ConfigArray arr;
                    arr.reserve(3);
                    arr.emplace_back(static_cast<double>(v.x));
                    arr.emplace_back(static_cast<double>(v.y));
                    arr.emplace_back(static_cast<double>(v.z));
                    return ConfigNode(std::move(arr));
                }

                ConfigNode makeVec4Node(const FCT::Vec4& v)
                {
                    ConfigArray arr;
                    arr.reserve(4);
                    arr.emplace_back(static_cast<double>(v.x));
                    arr.emplace_back(static_cast<double>(v.y));
                    arr.emplace_back(static_cast<double>(v.z));
                    arr.emplace_back(static_cast<double>(v.w));
                    return ConfigNode(std::move(arr));
                }

                FCT::Vec3<> readVec3Node(const ConfigNode& tbl, const char* key, const FCT::Vec3<>& fallback)
                {
                    const ConfigNode* node = tbl.findNode(key);
                    if (!node || !node->isArray()) {
                        return fallback;
                    }
                    const auto& arr = node->asArray();
                    if (arr.size() < 3) {
                        return fallback;
                    }
                    return FCT::Vec3<>(
                        static_cast<float>(arr[0].asFloat(fallback.x)),
                        static_cast<float>(arr[1].asFloat(fallback.y)),
                        static_cast<float>(arr[2].asFloat(fallback.z)));
                }

                FCT::Vec4 readVec4Node(const ConfigNode& tbl, const char* key, const FCT::Vec4& fallback)
                {
                    const ConfigNode* node = tbl.findNode(key);
                    if (!node || !node->isArray()) {
                        return fallback;
                    }
                    const auto& arr = node->asArray();
                    if (arr.size() < 4) {
                        return fallback;
                    }
                    return FCT::Vec4(
                        static_cast<float>(arr[0].asFloat(fallback.x)),
                        static_cast<float>(arr[1].asFloat(fallback.y)),
                        static_cast<float>(arr[2].asFloat(fallback.z)),
                        static_cast<float>(arr[3].asFloat(fallback.w)));
                }

                bool readProjectionOrthographicFlag(const ConfigNode& camNode, bool fallback)
                {
                    if (camNode.hasKey("orthographic")) {
                        return camNode.getBool("orthographic", fallback);
                    }
                    if (camNode.hasKey("is_orthographic")) {
                        return camNode.getBool("is_orthographic", fallback);
                    }
                    const std::string projStr = camNode.getStringOr({"projection", "projection_type"}, "");
                    if (projStr == "orthographic" || projStr == "ortho") {
                        return true;
                    }
                    if (projStr == "perspective") {
                        return false;
                    }
                    return fallback;
                }

                void applyCameraFromNode(const ConfigNode& camNode, CameraComponent& cam)
                {
                    cam.fov = static_cast<float>(camNode.getFloat("fov", camNode.getFloat("fov_deg", cam.fov)));
                    cam.nearPlane = static_cast<float>(camNode.getFloat("near_plane", camNode.getFloat("nearPlane", cam.nearPlane)));
                    cam.farPlane = static_cast<float>(camNode.getFloat("far_plane", camNode.getFloat("farPlane", cam.farPlane)));
                    if (camNode.hasKey("active")) {
                        cam.active = camNode.getBool("active", cam.active);
                    } else if (camNode.hasKey("is_primary")) {
                        cam.active = camNode.getBool("is_primary", cam.active);
                    } else if (camNode.hasKey("isPrimary")) {
                        cam.active = camNode.getBool("isPrimary", cam.active);
                    }
                    cam.reverseZ = camNode.getBool("reverse_z", camNode.getBool("reverseZ", cam.reverseZ));
                    cam.clearColor = readVec4Node(camNode, "clear_color", cam.clearColor);
                    cam.orthographic = readProjectionOrthographicFlag(camNode, cam.orthographic);
                    cam.orthoSize = static_cast<float>(camNode.getFloat("ortho_size", camNode.getFloat("orthoSize", cam.orthoSize)));
                    cam.priority = static_cast<int>(camNode.getInt("priority", cam.priority));
                    cam.targetName = camNode.getStringOr({"target_name", "targetName"}, cam.targetName);
                }

                void applyEntityComponentsFromNode(
                    const ConfigNode& entNode,
                    FCT::ECS::registry& reg,
                    FCT::ECS::entity entity)
                {
                    if (const auto* camNode = entNode.findSection("camera")) {
                        CameraComponent cam{};
                        applyCameraFromNode(*camNode, cam);
                        reg.emplace_or_replace<CameraComponent>(entity, cam);
                    }
                    if (const auto* dlNode = entNode.findSection("directional_light")) {
                        DirectionalLightComponent dl{};
                        dl.direction = readVec3Node(*dlNode, "direction", dl.direction);
                        dl.color = readVec3Node(*dlNode, "color", dl.color);
                        dl.intensity = static_cast<float>(dlNode->getFloat("intensity", dl.intensity));
                        dl.castShadows = dlNode->getBool("cast_shadows", dl.castShadows);
                        reg.emplace_or_replace<DirectionalLightComponent>(entity, dl);
                    }
                    if (const auto* plNode = entNode.findSection("point_light")) {
                        PointLightComponent pl{};
                        pl.color = readVec3Node(*plNode, "color", pl.color);
                        pl.intensity = static_cast<float>(plNode->getFloat("intensity", pl.intensity));
                        pl.range = static_cast<float>(plNode->getFloat("range", pl.range));
                        reg.emplace_or_replace<PointLightComponent>(entity, pl);
                    }
                    if (const auto* meshNode = entNode.findSection("static_mesh")) {
                        StaticMeshComponent mesh{};
                        mesh.primitiveType = static_cast<int>(meshNode->getInt("primitive_type", meshNode->getInt("primitiveType", 0)));
                        mesh.meshAssetPath = meshNode->getStringOr({"mesh_asset_path", "meshAssetPath"}, "");
                        mesh.castShadows = meshNode->getBool("cast_shadows", mesh.castShadows);
                        mesh.visible = meshNode->getBool("visible", mesh.visible);
                        reg.emplace_or_replace<StaticMeshComponent>(entity, mesh);
                    }
                    if (const auto* matNode = entNode.findSection("pbr_material")) {
                        PbrMaterialComponent mat{};
                        mat.baseColor = readVec3Node(*matNode, "base_color", mat.baseColor);
                        mat.metallic = static_cast<float>(matNode->getFloat("metallic", mat.metallic));
                        mat.roughness = static_cast<float>(matNode->getFloat("roughness", mat.roughness));
                        mat.ao = static_cast<float>(matNode->getFloat("ao", mat.ao));
                        mat.emissiveColor = readVec3Node(*matNode, "emissive_color", mat.emissiveColor);
                        mat.emissiveIntensity = static_cast<float>(matNode->getFloat("emissive_intensity", mat.emissiveIntensity));
                        mat.shaderAssetPath = matNode->getStringOr({"shader_asset_path", "shaderAssetPath"}, mat.shaderAssetPath);
                        reg.emplace_or_replace<PbrMaterialComponent>(entity, mat);
                    }
                    if (const auto* rbNode = entNode.findSection("rigid_body")) {
                        RigidBodyComponent rb{};
                        rb.bodyType = static_cast<int>(rbNode->getInt("body_type", rb.bodyType));
                        rb.mass = static_cast<float>(rbNode->getFloat("mass", rb.mass));
                        rb.linearDamping = static_cast<float>(rbNode->getFloat("linear_damping", rb.linearDamping));
                        rb.angularDamping = static_cast<float>(rbNode->getFloat("angular_damping", rb.angularDamping));
                        rb.useGravity = rbNode->getBool("use_gravity", rb.useGravity);
                        rb.colliderType = static_cast<int>(rbNode->getInt("collider_type", rb.colliderType));
                        rb.colliderHalfExtents = readVec3Node(*rbNode, "collider_half_extents", rb.colliderHalfExtents);
                        rb.restitution = static_cast<float>(rbNode->getFloat("restitution", rb.restitution));
                        rb.friction = static_cast<float>(rbNode->getFloat("friction", rb.friction));
                        rb.isTrigger = rbNode->getBool("is_trigger", rb.isTrigger);
                        reg.emplace_or_replace<RigidBodyComponent>(entity, rb);
                    }

                    auto parsePropertyNode = [](const ConfigNode& valNode, ScriptPropertyValue& outVal) -> bool {
                        if (valNode.isBool()) {
                            outVal = ScriptPropertyValue::fromBool(valNode.asBool());
                            return true;
                        }
                        if (valNode.isInt()) {
                            const int64_t iv = valNode.asInt();
                            if (iv >= 0 && iv > INT32_MAX) {
                                outVal = ScriptPropertyValue::fromUInt32(static_cast<uint32_t>(iv));
                            } else {
                                outVal = ScriptPropertyValue::fromInt32(static_cast<int32_t>(iv));
                            }
                            return true;
                        }
                        if (valNode.isFloat()) {
                            outVal = ScriptPropertyValue::fromDouble(valNode.asFloat());
                            return true;
                        }
                        if (valNode.isString()) {
                            outVal = ScriptPropertyValue::fromString(valNode.asString());
                            return true;
                        }
                        if (valNode.isArray()) {
                            const auto& arr = valNode.asArray();
                            if (arr.size() == 2) {
                                outVal = ScriptPropertyValue::fromVec2(FCT::Vec2(
                                    static_cast<float>(arr[0].asFloat()),
                                    static_cast<float>(arr[1].asFloat())));
                                return true;
                            }
                            if (arr.size() == 3) {
                                outVal = ScriptPropertyValue::fromVec3(FCT::Vec3<>(
                                    static_cast<float>(arr[0].asFloat()),
                                    static_cast<float>(arr[1].asFloat()),
                                    static_cast<float>(arr[2].asFloat())));
                                return true;
                            }
                            if (arr.size() >= 4) {
                                outVal = ScriptPropertyValue::fromVec4(FCT::Vec4(
                                    static_cast<float>(arr[0].asFloat()),
                                    static_cast<float>(arr[1].asFloat()),
                                    static_cast<float>(arr[2].asFloat()),
                                    static_cast<float>(arr[3].asFloat())));
                                return true;
                            }
                        }
                        return false;
                    };

                    auto parseScriptSlot = [&](const ConfigNode& slotNode, NativeScriptSlot& outSlot) -> bool {
                        if (!slotNode.isTable()) {
                            return false;
                        }
                        outSlot.className = slotNode.getStringOr({"class_name", "className", "script_class", "name"}, "");
                        if (outSlot.className.empty()) {
                            return false;
                        }
                        outSlot.enabled = slotNode.getBool("enabled", slotNode.getBool("active", true));
                        if (const auto* propsNode = slotNode.findSection("props"); propsNode && propsNode->isTable()) {
                            for (const auto& [propKey, propValNode] : propsNode->asTable()) {
                                ScriptPropertyValue parsed{};
                                if (parsePropertyNode(propValNode, parsed)) {
                                    UpsertNativeScriptProperty(outSlot, propKey, std::move(parsed));
                                }
                            }
                        }
                        return true;
                    };

                    NativeScriptComponent parsedScripts{};
                    bool hasNativeScriptSection = false;
                    if (const auto* singleNode = entNode.findSection("native_script")) {
                        hasNativeScriptSection = true;
                        NativeScriptSlot slot{};
                        if (parseScriptSlot(*singleNode, slot)) {
                            parsedScripts.scripts.push_back(std::move(slot));
                        }
                    }
                    if (const auto* arrNode = entNode.findNode("native_scripts"); arrNode && arrNode->isArray()) {
                        hasNativeScriptSection = true;
                        for (const auto& itemNode : arrNode->asArray()) {
                            NativeScriptSlot slot{};
                            if (parseScriptSlot(itemNode, slot)) {
                                parsedScripts.scripts.push_back(std::move(slot));
                            }
                        }
                    }
                    if (hasNativeScriptSection) {
                        NormalizeNativeScriptSlots(parsedScripts);
                        reg.emplace_or_replace<NativeScriptComponent>(entity, std::move(parsedScripts));
                    }
                }

                ConfigNode makeScriptPropertyConfigNode(const ScriptPropertyValue& val)
                {
                    switch (val.kind) {
                    case Resource::EFieldPrimitiveKind::Bool:
                        return ConfigNode(val.boolVal);
                    case Resource::EFieldPrimitiveKind::Int32:
                        return ConfigNode(static_cast<int64_t>(val.intVal));
                    case Resource::EFieldPrimitiveKind::UInt32:
                        return ConfigNode(static_cast<int64_t>(val.uintVal));
                    case Resource::EFieldPrimitiveKind::Float:
                    case Resource::EFieldPrimitiveKind::Double:
                        return ConfigNode(val.floatVal);
                    case Resource::EFieldPrimitiveKind::String:
                        return ConfigNode(val.stringVal);
                    case Resource::EFieldPrimitiveKind::Vec2: {
                        ConfigArray arr;
                        arr.emplace_back(static_cast<double>(val.vecVal.x));
                        arr.emplace_back(static_cast<double>(val.vecVal.y));
                        return ConfigNode(std::move(arr));
                    }
                    case Resource::EFieldPrimitiveKind::Vec3:
                        return makeVec3Node(FCT::Vec3<>(val.vecVal.x, val.vecVal.y, val.vecVal.z));
                    case Resource::EFieldPrimitiveKind::Vec4:
                        return makeVec4Node(val.vecVal);
                    default:
                        return ConfigNode();
                    }
                }

                ConfigNode serializeEntityToNode(
                    const FCT::ECS::registry& reg,
                    FCT::ECS::entity entity,
                    const std::unordered_map<uint32_t, std::string>& uuidByEntityId)
                {
                    ConfigNode entNode;
                    const uint32_t entId = static_cast<uint32_t>(FCT::ECS::to_integral(entity));
                    if (const auto* nameComp = reg.try_get<EditorNameComponent>(entity)) {
                        entNode.setString("name", nameComp->name);
                    } else {
                        entNode.setString("name", "Entity_" + std::to_string(entId));
                    }
                    if (const auto* uuidComp = reg.try_get<UuidComponent>(entity);
                        uuidComp && !uuidComp->uuid.empty()) {
                        entNode.setString("uuid", uuidComp->uuid);
                    } else {
                        entNode.setString("uuid", "entity-" + std::to_string(entId));
                    }
                    entNode.setBool("active", !reg.all_of<DisabledTag>(entity));

                    std::string parentUuid;
                    if (const auto* rel = reg.try_get<HierarchyRelation>(entity);
                        rel && rel->parent != FCT::ECS::null) {
                        const uint32_t pId = static_cast<uint32_t>(FCT::ECS::to_integral(rel->parent));
                        if (auto it = uuidByEntityId.find(pId); it != uuidByEntityId.end()) {
                            parentUuid = it->second;
                        }
                    }
                    if (!parentUuid.empty()) {
                        entNode.setString("parent_uuid", parentUuid);
                    }

                    if (const auto* local = reg.try_get<LocalTransform>(entity)) {
                        entNode.setNode("position", makeVec3Node(local->position));
                        entNode.setNode("rotation", makeVec3Node(local->rotation));
                        entNode.setNode("scale", makeVec3Node(local->scale));
                    } else {
                        entNode.setBool("has_transform", false);
                    }
                    if (const auto* world = reg.try_get<WorldTransform>(entity)) {
                        entNode.setBool("visible", world->isVisible);
                    }

                    if (const auto* cam = reg.try_get<CameraComponent>(entity)) {
                        ConfigNode camNode;
                        camNode.setString("projection", cam->orthographic ? "orthographic" : "perspective");
                        camNode.setBool("orthographic", cam->orthographic);
                        camNode.setFloat("ortho_size", cam->orthoSize);
                        camNode.setFloat("fov", cam->fov);
                        camNode.setFloat("near_plane", cam->nearPlane);
                        camNode.setFloat("far_plane", cam->farPlane);
                        camNode.setBool("active", cam->active);
                        camNode.setBool("reverse_z", cam->reverseZ);
                        camNode.setNode("clear_color", makeVec4Node(cam->clearColor));
                        camNode.setInt("priority", cam->priority);
                        camNode.setString("target_name", cam->targetName);
                        entNode.setNode("camera", std::move(camNode));
                    }
                    if (const auto* dl = reg.try_get<DirectionalLightComponent>(entity)) {
                        ConfigNode dlNode;
                        dlNode.setNode("direction", makeVec3Node(dl->direction));
                        dlNode.setNode("color", makeVec3Node(dl->color));
                        dlNode.setFloat("intensity", dl->intensity);
                        dlNode.setBool("cast_shadows", dl->castShadows);
                        entNode.setNode("directional_light", std::move(dlNode));
                    }
                    if (const auto* pl = reg.try_get<PointLightComponent>(entity)) {
                        ConfigNode plNode;
                        plNode.setNode("color", makeVec3Node(pl->color));
                        plNode.setFloat("intensity", pl->intensity);
                        plNode.setFloat("range", pl->range);
                        entNode.setNode("point_light", std::move(plNode));
                    }
                    if (const auto* mesh = reg.try_get<StaticMeshComponent>(entity)) {
                        ConfigNode meshNode;
                        meshNode.setInt("primitive_type", mesh->primitiveType);
                        meshNode.setString("mesh_asset_path", mesh->meshAssetPath);
                        meshNode.setBool("cast_shadows", mesh->castShadows);
                        meshNode.setBool("visible", mesh->visible);
                        entNode.setNode("static_mesh", std::move(meshNode));
                    }
                    if (const auto* mat = reg.try_get<PbrMaterialComponent>(entity)) {
                        ConfigNode matNode;
                        matNode.setNode("base_color", makeVec3Node(mat->baseColor));
                        matNode.setFloat("metallic", mat->metallic);
                        matNode.setFloat("roughness", mat->roughness);
                        matNode.setFloat("ao", mat->ao);
                        matNode.setNode("emissive_color", makeVec3Node(mat->emissiveColor));
                        matNode.setFloat("emissive_intensity", mat->emissiveIntensity);
                        matNode.setString("shader_asset_path", mat->shaderAssetPath);
                        entNode.setNode("pbr_material", std::move(matNode));
                    }
                    if (const auto* rb = reg.try_get<RigidBodyComponent>(entity)) {
                        ConfigNode rbNode;
                        rbNode.setInt("body_type", rb->bodyType);
                        rbNode.setFloat("mass", rb->mass);
                        rbNode.setFloat("linear_damping", rb->linearDamping);
                        rbNode.setFloat("angular_damping", rb->angularDamping);
                        rbNode.setBool("use_gravity", rb->useGravity);
                        rbNode.setInt("collider_type", rb->colliderType);
                        rbNode.setNode("collider_half_extents", makeVec3Node(rb->colliderHalfExtents));
                        rbNode.setFloat("restitution", rb->restitution);
                        rbNode.setFloat("friction", rb->friction);
                        rbNode.setBool("is_trigger", rb->isTrigger);
                        entNode.setNode("rigid_body", std::move(rbNode));
                    }
                    if (const auto* ns = reg.try_get<NativeScriptComponent>(entity)) {
                        NativeScriptComponent normalized = *ns;
                        NormalizeNativeScriptSlots(normalized);
                        if (!normalized.scripts.empty()) {
                            ConfigArray slotsArr;
                            slotsArr.reserve(normalized.scripts.size());
                            for (const auto& slot : normalized.scripts) {
                                if (slot.className.empty()) {
                                    continue;
                                }
                                ConfigNode slotNode;
                                slotNode.setString("class_name", slot.className);
                                slotNode.setBool("enabled", slot.enabled);
                                if (!slot.properties.empty()) {
                                    ConfigNode propsNode;
                                    for (const auto& [propKey, propVal] : slot.properties) {
                                        ConfigNode valNode = makeScriptPropertyConfigNode(propVal);
                                        if (!valNode.isNull()) {
                                            propsNode.setNode(propKey, std::move(valNode));
                                        }
                                    }
                                    slotNode.setNode("props", std::move(propsNode));
                                }
                                slotsArr.push_back(std::move(slotNode));
                            }
                            if (!slotsArr.empty()) {
                                entNode.setNode("native_scripts", ConfigNode(std::move(slotsArr)));
                            }
                        }
                    }

                    return entNode;
                }

                void applySceneMetadataSections(const ConfigDocument& doc, FCT::ECS::registry& reg)
                {
                    if (const auto* envNode = doc.root().findSection("environment")) {
                        auto dlView = reg.view<DirectionalLightComponent>();
                        for (auto ent : dlView) {
                            auto& dl = dlView.get<DirectionalLightComponent>(ent);
                            dl.direction = readVec3Node(*envNode, "sun_direction", dl.direction);
                            dl.color = readVec3Node(*envNode, "sun_color", dl.color);
                            dl.intensity = static_cast<float>(envNode->getFloat("sun_intensity", dl.intensity));
                            dl.castShadows = envNode->getBool("cast_shadows", dl.castShadows);
                            break;
                        }
                    }

                    if (const auto* camNode = doc.root().findSection("camera")) {
                        auto camView = reg.view<CameraComponent>();
                        FCT::ECS::entity fallbackCam = FCT::ECS::null;
                        bool updated = false;
                        for (auto ent : camView) {
                            auto& cam = camView.get<CameraComponent>(ent);
                            if (fallbackCam == FCT::ECS::null) {
                                fallbackCam = ent;
                            }
                            if (cam.active && (cam.targetName.empty() || cam.targetName == "MainWindow")) {
                                applyCameraFromNode(*camNode, cam);
                                updated = true;
                                break;
                            }
                        }
                        if (!updated && fallbackCam != FCT::ECS::null) {
                            applyCameraFromNode(*camNode, camView.get<CameraComponent>(fallbackCam));
                        }
                    }
                }

                struct PendingSceneEntityRecord
                {
                    FCT::ECS::entity entity = FCT::ECS::null;
                    FCT::ECS::registry* registry = nullptr;
                    std::string parentUuid;
                };

                void populateEntityFromNode(
                    const ConfigNode& entNode,
                    FCT::ECS::registry& reg,
                    FCT::ECS::entity entity,
                    uint32_t fallbackIndex,
                    bool attachEditorMetadata)
                {
                    if (attachEditorMetadata) {
                        EditorNameComponent editorName{};
                        editorName.name = entNode.getString("name", "Entity_" + std::to_string(fallbackIndex));
                        reg.emplace_or_replace<EditorNameComponent>(entity, std::move(editorName));
                    }

                    const std::string uuid = entNode.getString("uuid", "");
                    if (!uuid.empty()) {
                        reg.emplace_or_replace<UuidComponent>(entity, UuidComponent{uuid});
                    } else if (attachEditorMetadata) {
                        reg.emplace_or_replace<UuidComponent>(
                            entity,
                            UuidComponent{"scene-entity-" + std::to_string(fallbackIndex)});
                    }

                    if (entNode.getBool("active", true)) {
                        reg.remove<DisabledTag>(entity);
                    } else {
                        reg.emplace_or_replace<DisabledTag>(entity);
                    }

                    const bool hasTransform = entNode.getBool("has_transform", true);
                    if (hasTransform) {
                        auto& local = reg.get_or_emplace<LocalTransform>(entity);
                        local.position = readVec3Node(entNode, "position", FCT::Vec3<>(0.0f, 0.0f, 0.0f));
                        local.rotation = readVec3Node(entNode, "rotation", FCT::Vec3<>(0.0f, 0.0f, 0.0f));
                        local.scale = readVec3Node(entNode, "scale", FCT::Vec3<>(1.0f, 1.0f, 1.0f));
                        local.isDirty = true;

                        auto& world = reg.get_or_emplace<WorldTransform>(entity);
                        world.isVisible = entNode.getBool("visible", true);
                    } else {
                        reg.remove<LocalTransform>(entity);
                        reg.remove<WorldTransform>(entity);
                    }

                    applyEntityComponentsFromNode(entNode, reg, entity);
                }

                void collectEntitiesFromRegistry(
                    const std::string& registryName,
                    const FCT::ECS::registry& reg,
                    ConfigArray& outEntities)
                {
                    std::unordered_map<uint32_t, std::string> uuidById;
                    std::unordered_set<uint32_t> seenIds;
                    std::vector<FCT::ECS::entity> entities;

                    auto tryRecordEntity = [&](FCT::ECS::entity ent) {
                        if (ent == FCT::ECS::null || !reg.valid(ent) || reg.all_of<TransientTag>(ent)) {
                            return;
                        }
                        const uint32_t entId = static_cast<uint32_t>(FCT::ECS::to_integral(ent));
                        if (!seenIds.insert(entId).second) {
                            return;
                        }
                        entities.push_back(ent);
                        if (const auto* uuidComp = reg.try_get<UuidComponent>(ent);
                            uuidComp && !uuidComp->uuid.empty()) {
                            uuidById[entId] = uuidComp->uuid;
                        } else {
                            uuidById[entId] = "entity-" + std::to_string(entId);
                        }
                    };

                    for (auto ent : reg.view<LocalTransform>()) {
                        tryRecordEntity(ent);
                    }
                    for (auto ent : reg.view<UuidComponent>()) {
                        tryRecordEntity(ent);
                    }
                    for (auto ent : reg.view<NativeScriptComponent>()) {
                        tryRecordEntity(ent);
                    }
                    for (auto ent : reg.view<EditorNameComponent>()) {
                        tryRecordEntity(ent);
                    }

                    std::sort(entities.begin(), entities.end(), [](FCT::ECS::entity a, FCT::ECS::entity b) {
                        return static_cast<uint32_t>(FCT::ECS::to_integral(a)) <
                               static_cast<uint32_t>(FCT::ECS::to_integral(b));
                    });

                    for (auto ent : entities) {
                        ConfigNode entNode = serializeEntityToNode(reg, ent, uuidById);
                        if (!registryName.empty() && registryName != "Main") {
                            entNode.setString("registry", registryName);
                        }
                        outEntities.push_back(std::move(entNode));
                    }
                }

                uint32_t countRegistryEntities(const FCT::ECS::registry& reg)
                {
                    std::unordered_set<uint32_t> seenIds;
                    auto record = [&](FCT::ECS::entity ent) {
                        if (ent != FCT::ECS::null && reg.valid(ent)) {
                            seenIds.insert(static_cast<uint32_t>(FCT::ECS::to_integral(ent)));
                        }
                    };
                    for (auto ent : reg.view<LocalTransform>()) {
                        record(ent);
                    }
                    for (auto ent : reg.view<EditorNameComponent>()) {
                        record(ent);
                    }
                    for (auto ent : reg.view<UuidComponent>()) {
                        record(ent);
                    }
                    for (auto ent : reg.view<NativeScriptComponent>()) {
                        record(ent);
                    }
                    return static_cast<uint32_t>(seenIds.size());
                }

                bool tryLoadLegacyLineScene(SceneSubsystem& scene, const std::string& content)
                {
                    bool parsedAny = false;
                    std::istringstream iss(content);
                    std::string line;
                    auto& reg = scene.getRegistry();
                    const bool attachEditorMetadata = scene.shouldAttachEditorMetadata();
                    uint32_t index = 0;

                    while (std::getline(iss, line)) {
                        if (line.rfind("ENTITY ", 0) != 0) {
                            continue;
                        }
                        if (!parsedAny) {
                            scene.clearScene(false);
                            parsedAny = true;
                        }
                        FCT::ECS::entity ent = scene.createEntity();
                        std::string entName = "Entity_" + std::to_string(index);
                        bool entActive = true;

                        auto& local = reg.get<LocalTransform>(ent);
                        std::istringstream ls(line.substr(7));
                        std::string token;
                        while (ls >> token) {
                            const size_t eq = token.find('=');
                            if (eq == std::string::npos) {
                                continue;
                            }
                            const std::string key = token.substr(0, eq);
                            const std::string val = token.substr(eq + 1);
                            if (key == "name") {
                                entName = val;
                            } else if (key == "active") {
                                entActive = (val != "0" && val != "false" && val != "FALSE");
                            } else if (key == "pos") {
                                std::sscanf(val.c_str(), "%f,%f,%f",
                                    &local.position.x, &local.position.y, &local.position.z);
                            } else if (key == "rot") {
                                std::sscanf(val.c_str(), "%f,%f,%f",
                                    &local.rotation.x, &local.rotation.y, &local.rotation.z);
                            } else if (key == "scale") {
                                std::sscanf(val.c_str(), "%f,%f,%f",
                                    &local.scale.x, &local.scale.y, &local.scale.z);
                            }
                        }
                        local.isDirty = true;
                        if (attachEditorMetadata) {
                            reg.emplace_or_replace<EditorNameComponent>(
                                ent, EditorNameComponent{std::move(entName)});
                            reg.emplace_or_replace<UuidComponent>(
                                ent, UuidComponent{"legacy-entity-" + std::to_string(index)});
                        }
                        if (!entActive) {
                            reg.emplace_or_replace<DisabledTag>(ent);
                        }
                        ++index;
                    }
                    return parsedAny;
                }
            } // namespace

            SceneSubsystem::SceneSubsystem() = default;

            SceneSubsystem::~SceneSubsystem()
            {
                term();
            }

            void SceneSubsystem::registerInfo()
            {
            }

            Status SceneSubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                // 对接 Resource 模块全局 DataBase (Docs 01~03 & 07)
                auto* res = Resource::Resource::InstancePtr();
                if (res) {
                    m_registry = &res->getDataBase().getRegistry();
                } else {
                    if (!m_fallbackRegistry) {
                        m_fallbackRegistry = makeUnique<FCT::ECS::registry>();
                    }
                    m_registry = m_fallbackRegistry.get();
                }

                if (auto* schemaTable = Resource::SchemaTable::InstancePtr()) {
                    (void)schemaTable->registerSchema<LocalTransform>("LocalTransform", 1);
                    (void)schemaTable->registerSchema<WorldTransform>("WorldTransform", 1);
                    (void)schemaTable->registerSchema<HierarchyRelation>("HierarchyRelation", 1);
                    (void)schemaTable->registerSchema<UuidComponent>("UuidComponent", 1);
                    (void)schemaTable->registerSchema<EditorNameComponent>("EditorNameComponent", 1);
                    (void)schemaTable->registerSchema<CameraComponent>("CameraComponent", 1);
                    (void)schemaTable->registerSchema<DirectionalLightComponent>("DirectionalLightComponent", 1);
                    (void)schemaTable->registerSchema<PointLightComponent>("PointLightComponent", 1);
                    (void)schemaTable->registerSchema<StaticMeshComponent>("StaticMeshComponent", 1);
                    (void)schemaTable->registerSchema<PbrMaterialComponent>("PbrMaterialComponent", 1);
                    (void)schemaTable->registerSchema<RigidBodyComponent>("RigidBodyComponent", 1);
                    (void)schemaTable->registerSchema<NativeScriptComponent>("NativeScriptComponent", 1);
                }

                if (const auto* engine = Runtime::Engine::getInstance()) {
                    m_editorMetadataEnabled.store(
                        engine->hasApplicationTag("Editor") || engine->hasApplicationTag("CliEditor"),
                        std::memory_order_release);
                } else {
                    m_editorMetadataEnabled.store(false, std::memory_order_release);
                }

                // 预创建编辑器专属 ECS Registry 分区，用于存放漫游相机与编辑器辅助实体（与运行时 Main 物理隔离）
                if (m_namedRegistries.find("Editor") == m_namedRegistries.end()) {
                    m_namedRegistries["Editor"] = makeUnique<FCT::ECS::registry>();
                }
                // 在 Engine.dll 内提前固化核心组件存储池虚表，避免插件/Native 模块首次 emplace 后产生跨模块析构悬空风险
                if (m_registry) {
                    warmCoreSceneComponentStorages(*m_registry);
                }
                if (auto editorIt = m_namedRegistries.find("Editor"); editorIt != m_namedRegistries.end() && editorIt->second) {
                    warmCoreSceneComponentStorages(*editorIt->second);
                }

                m_initialized.store(true, std::memory_order_release);
                refreshActiveEntityCount();
                return OkStatus();
            }

            bool SceneSubsystem::shouldAttachEditorMetadata() const noexcept
            {
                if (const auto* engine = Runtime::Engine::getInstance()) {
                    return engine->hasApplicationTag("Editor") || engine->hasApplicationTag("CliEditor");
                }
                return m_editorMetadataEnabled.load(std::memory_order_acquire);
            }

            FCT::ECS::registry& SceneSubsystem::getRegistry() noexcept
            {
                if (m_registry) {
                    return *m_registry;
                }
                if (auto* res = Resource::Resource::InstancePtr()) {
                    m_registry = &res->getDataBase().getRegistry();
                    return *m_registry;
                }
                if (!m_fallbackRegistry) {
                    m_fallbackRegistry = makeUnique<FCT::ECS::registry>();
                }
                m_registry = m_fallbackRegistry.get();
                return *m_registry;
            }

            const FCT::ECS::registry& SceneSubsystem::getRegistry() const noexcept
            {
                if (m_registry) {
                    return *m_registry;
                }
                if (auto* res = Resource::Resource::InstancePtr()) {
                    m_registry = &res->getDataBase().getRegistry();
                    return *m_registry;
                }
                if (!m_fallbackRegistry) {
                    m_fallbackRegistry = makeUnique<FCT::ECS::registry>();
                }
                m_registry = m_fallbackRegistry.get();
                return *m_registry;
            }

            void SceneSubsystem::clearScene(bool includeEditorRegistry)
            {
                ZoneScoped();
                auto* dom = Domain::InstancePtr();
                auto& reg = getRegistry();
                if (dom) {
                    dom->gameplay.shutdownAllNativeScriptInstances(&reg);
                }
                reg.clear();
                m_spatialGraph.clear();
                m_spatialGraph.publishSnapshot();

                std::vector<std::string> trunkNames;
                trunkNames.reserve(m_trunks.size());
                for (const auto& kv : m_trunks) {
                    trunkNames.push_back(kv.first);
                }
                for (const std::string& trunkName : trunkNames) {
                    unloadTrunk(trunkName);
                }

                for (auto& kv : m_namedRegistries) {
                    if (!includeEditorRegistry && kv.first == "Editor") {
                        continue;
                    }
                    if (kv.second) {
                        if (dom) {
                            dom->gameplay.shutdownAllNativeScriptInstances(kv.second.get());
                        }
                        kv.second->clear();
                    }
                }

                refreshActiveEntityCount();
            }

            Status SceneSubsystem::loadSceneFromFile(const std::string& filePath, ResourceLoader* loader)
            {
                ZoneScoped();
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return FailedPreconditionError("SceneSubsystem 尚未初始化");
                }
                if (filePath.empty()) {
                    return InvalidArgumentError("场景文件路径不能为空");
                }

                if (auto* res = Resource::Resource::InstancePtr(); res && !res->getProjectNativeDir().empty()) {
                    if (auto* dom = Domain::InstancePtr()) {
                        (void)dom->gameplay.loadProjectNativeScriptLibraries(res->getProjectNativeDir());
                    }
                }

                ResourceLoader* activeLoader = resolveSceneResourceLoader(loader);
                auto contentOr = activeLoader->loadFile(filePath);
                CHECK_STATUS(contentOr);
                const std::string& content = contentOr.value();

                auto docOr = ConfigDocument::parse(content, ConfigFormat::Auto);
                if (!docOr.ok()) {
                    if (tryLoadLegacyLineScene(*this, content)) {
                        update();
                        return OkStatus();
                    }
                    return docOr.status();
                }

                const ConfigDocument& doc = docOr.value();
                if (const ConfigNode* entitiesNode = doc.root().findNode("entities")) {
                    // Note: 仅当 .mqscene 显式声明 entities 时清空运行时分区，避免清空工程 Native 模块或脚本程序化生成的世界
                    clearScene(false);
                    if (entitiesNode->isArray()) {
                        const auto& entArray = entitiesNode->asArray();
                        std::unordered_map<std::string, PendingSceneEntityRecord> uuidMap;
                        std::vector<PendingSceneEntityRecord> createdRecords;
                        createdRecords.reserve(entArray.size());

                        const bool attachEditorMetadata = shouldAttachEditorMetadata();
                        uint32_t fallbackIndex = 0;
                        for (const auto& entNode : entArray) {
                            if (!entNode.isTable()) {
                                ++fallbackIndex;
                                continue;
                            }
                            const std::string regName = entNode.getString("registry", "Main");
                            if (regName == "Editor") {
                                ++fallbackIndex;
                                continue;
                            }
                            FCT::ECS::registry& targetReg = (regName.empty() || regName == "Main")
                                ? getRegistry()
                                : getOrCreateRegistry(regName);
                            const FCT::ECS::entity ent = (regName.empty() || regName == "Main")
                                ? createEntity()
                                : createEntityInRegistry(regName);
                            populateEntityFromNode(entNode, targetReg, ent, fallbackIndex, attachEditorMetadata);

                            PendingSceneEntityRecord rec{};
                            rec.entity = ent;
                            rec.registry = &targetReg;
                            rec.parentUuid = entNode.getStringOr({"parent_uuid", "parent"}, "");
                            if (const auto* uuidComp = targetReg.try_get<UuidComponent>(ent);
                                uuidComp && !uuidComp->uuid.empty()) {
                                uuidMap[uuidComp->uuid] = rec;
                            }
                            createdRecords.push_back(std::move(rec));
                            ++fallbackIndex;
                        }

                        for (const auto& rec : createdRecords) {
                            if (rec.parentUuid.empty() || !rec.registry) {
                                continue;
                            }
                            if (auto it = uuidMap.find(rec.parentUuid);
                                it != uuidMap.end() && it->second.registry == rec.registry && it->second.entity != rec.entity) {
                                m_transformGraph.attachChild(*rec.registry, it->second.entity, rec.entity);
                            }
                        }
                    }
                }

                applySceneMetadataSections(doc, getRegistry());
                update();
                return OkStatus();
            }

            Status SceneSubsystem::saveSceneToFile(const std::string& filePath, ResourceLoader* loader) const
            {
                ZoneScoped();
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return FailedPreconditionError("SceneSubsystem 尚未初始化");
                }
                if (filePath.empty()) {
                    return InvalidArgumentError("保存场景路径不能为空");
                }

                ResourceLoader* activeLoader = resolveSceneResourceLoader(loader);
                const std::string parentDir = SAI::Path::parentPath(filePath);
                if (!parentDir.empty()) {
                    CHECK_STATUS(activeLoader->ensureDir(parentDir));
                }

                ConfigDocument doc;
                const std::string stem = SAI::Path::stem(filePath);
                doc.setString("scene_name", stem.empty() ? "Scene" : stem);

                ConfigArray entitiesArr;
                collectEntitiesFromRegistry("Main", getRegistry(), entitiesArr);
                for (const auto& kv : m_namedRegistries) {
                    if (kv.first == "Editor" || !kv.second) {
                        continue;
                    }
                    collectEntitiesFromRegistry(kv.first, *kv.second, entitiesArr);
                }
                doc.root().setNode("entities", ConfigNode(std::move(entitiesArr)));

                return doc.saveToFile(activeLoader, filePath, ConfigFormat::Toml);
            }

            void SceneSubsystem::refreshActiveEntityCount() noexcept
            {
                const auto& reg = getRegistry();
                const auto view = reg.view<WorldTransform>();
                m_activeEntityCount.store(static_cast<uint32_t>(view.size()), std::memory_order_relaxed);
            }

            void SceneSubsystem::term() noexcept
            {
                if (!m_initialized.load(std::memory_order_relaxed)) {
                    return;
                }

                if (auto* dom = Domain::InstancePtr()) {
                    dom->gameplay.shutdownAllNativeScriptInstances(nullptr);
                }

                m_replay.clear();
                m_spatialGraph.clear();
                m_trunks.clear();
                m_namedRegistries.clear();
                m_hiddenRegistries.clear();
                if (m_registry) {
                    m_registry->clear();
                }

                m_initialized.store(false, std::memory_order_release);
            }

            void SceneSubsystem::update()
            {
                ZoneScopedN("SceneSubsystem::update");
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return;
                }

                auto& reg = getRegistry();
                const auto t0 = std::chrono::steady_clock::now();
                m_currentFrameIndex++;

                // 1. 若处于录像状态，在清除脏标记前记录首帧/周期性关键帧与帧间增量变更
                if (m_replay.isRecording()) {
                    ZoneScopedN("SceneReplay::recordFrame");
                    if (m_replay.getRecordedFrameCount() == 0 || m_currentFrameIndex % 60 == 0) {
                        m_replay.captureKeyframe(m_currentFrameIndex, reg);
                    }
                    auto dirtyView = reg.view<LocalTransform>();
                    for (auto entity : dirtyView) {
                        const auto& local = dirtyView.get<LocalTransform>(entity);
                        if (local.isDirty) {
                            m_replay.recordDelta(
                                m_currentFrameIndex,
                                static_cast<uint32_t>(FCT::ECS::to_integral(entity)),
                                local);
                        }
                    }
                }

                // 2. 变换传播图分层并行求值（覆盖主 Registry、流式 SceneTrunk 与自定义命名 Registry）
                const auto tTf0 = std::chrono::steady_clock::now();
                m_transformGraph.evaluateParallel(reg);
                for (auto& kv : m_trunks) {
                    if (kv.second) {
                        m_transformGraph.evaluateParallel(kv.second->getRegistry());
                    }
                }
                for (auto& kv : m_namedRegistries) {
                    if (kv.second) {
                        m_transformGraph.evaluateParallel(*kv.second);
                    }
                }
                const auto tTf1 = std::chrono::steady_clock::now();

                // 3. 将变动的世界包围盒同步至空间松散 BVH
                uint32_t entityCount = 0;
                {
                    ZoneScopedN("SpatialGraph::syncEntities");
                    auto view = reg.view<WorldTransform>();
                    for (auto entity : view) {
                        const auto& world = view.get<WorldTransform>(entity);
                        m_spatialGraph.updateEntity(entity, world.aabbMin, world.aabbMax);
                        ++entityCount;
                    }
                    m_spatialGraph.publishSnapshot();
                }
                const auto tEnd = std::chrono::steady_clock::now();

                m_lastTransformEvalMs.store(
                    std::chrono::duration<float, std::milli>(tTf1 - tTf0).count(),
                    std::memory_order_relaxed);
                m_lastSpatialRebuildMs.store(
                    std::chrono::duration<float, std::milli>(tEnd - tTf1).count(),
                    std::memory_order_relaxed);
                m_lastUpdateMs.store(
                    std::chrono::duration<float, std::milli>(tEnd - t0).count(),
                    std::memory_order_relaxed);
                m_activeEntityCount.store(entityCount, std::memory_order_relaxed);
            }

            FCT::ECS::entity SceneSubsystem::createEntity(std::string_view debugName)
            {
                auto& reg = getRegistry();
                auto entity = reg.create();
                reg.emplace<HierarchyRelation>(entity);
                reg.emplace<LocalTransform>(entity);
                reg.emplace<WorldTransform>(entity);
                if (shouldAttachEditorMetadata()) {
                    reg.emplace<EditorNameComponent>(
                        entity,
                        EditorNameComponent{debugName.empty() ? std::string("Entity") : std::string(debugName)});
                }
                return entity;
            }

            void SceneSubsystem::destroyEntity(FCT::ECS::entity entity)
            {
                auto& reg = getRegistry();
                if (reg.valid(entity)) {
                    if (auto* dom = Domain::InstancePtr()) {
                        dom->gameplay.notifyEntityDestroyed(entity, reg);
                    }
                    m_transformGraph.detachChild(reg, entity);
                    m_spatialGraph.removeEntity(entity);
                    reg.destroy(entity);
                }
            }

            bool SceneSubsystem::valid(FCT::ECS::entity entity) const noexcept
            {
                return getRegistry().valid(entity);
            }

            void SceneSubsystem::attachChild(FCT::ECS::entity parent, FCT::ECS::entity child)
            {
                auto& reg = getRegistry();
                m_transformGraph.attachChild(reg, parent, child);
            }

            void SceneSubsystem::detachChild(FCT::ECS::entity child)
            {
                auto& reg = getRegistry();
                m_transformGraph.detachChild(reg, child);
            }

            Status SceneSubsystem::loadTrunk(const std::string& trunkName, const std::string& directoryPath)
            {
                auto trunk = makeUnique<SceneTrunk>(trunkName);
                Status st = trunk->load(directoryPath);
                if (!st.ok()) {
                    return st;
                }
                m_trunks[trunkName] = std::move(trunk);
                return OkStatus();
            }

            void SceneSubsystem::unloadTrunk(const std::string& trunkName)
            {
                if (auto it = m_trunks.find(trunkName); it != m_trunks.end()) {
                    if (it->second) {
                        if (auto* dom = Domain::InstancePtr()) {
                            dom->gameplay.shutdownAllNativeScriptInstances(&it->second->getRegistry());
                        }
                    }
                    m_trunks.erase(it);
                }
            }

            bool SceneSubsystem::isTrunkLoaded(const std::string& trunkName) const noexcept
            {
                return m_trunks.find(trunkName) != m_trunks.end();
            }

            SceneTrunk* SceneSubsystem::getTrunk(const std::string& trunkName) noexcept
            {
                auto it = m_trunks.find(trunkName);
                return (it != m_trunks.end()) ? it->second.get() : nullptr;
            }

            const SceneTrunk* SceneSubsystem::getTrunk(const std::string& trunkName) const noexcept
            {
                auto it = m_trunks.find(trunkName);
                return (it != m_trunks.end()) ? it->second.get() : nullptr;
            }

            std::vector<std::string> SceneSubsystem::getRegistryNames() const
            {
                std::vector<std::string> names;
                names.push_back("Main");
                if (m_namedRegistries.find("Editor") != m_namedRegistries.end()) {
                    names.push_back("Editor");
                }
                for (const auto& kv : m_trunks) {
                    if (kv.second && std::find(names.begin(), names.end(), kv.first) == names.end()) {
                        names.push_back(kv.first);
                    }
                }
                for (const auto& kv : m_namedRegistries) {
                    if (kv.second && std::find(names.begin(), names.end(), kv.first) == names.end()) {
                        names.push_back(kv.first);
                    }
                }
                return names;
            }

            ERegistryKind SceneSubsystem::getRegistryKind(const std::string& registryName) const noexcept
            {
                if (registryName.empty() || registryName == "Main") {
                    return ERegistryKind::Main;
                }
                if (registryName == "Editor") {
                    return ERegistryKind::Editor;
                }
                if (m_trunks.find(registryName) != m_trunks.end()) {
                    return ERegistryKind::Streaming;
                }
                return ERegistryKind::Custom;
            }

            std::vector<RegistryPartitionInfo> SceneSubsystem::getRegistryPartitions() const
            {
                const std::vector<std::string> names = getRegistryNames();
                std::vector<RegistryPartitionInfo> partitions;
                partitions.reserve(names.size());
                for (const std::string& name : names) {
                    RegistryPartitionInfo info;
                    info.name = name;
                    info.kind = getRegistryKind(name);
                    info.visible = isRegistryVisible(name);
                    if (const auto* reg = getRegistryByName(name)) {
                        info.entityCount = countRegistryEntities(*reg);
                    }
                    partitions.push_back(std::move(info));
                }
                return partitions;
            }

            FCT::ECS::registry* SceneSubsystem::getRegistryByName(const std::string& registryName) noexcept
            {
                if (registryName.empty() || registryName == "Main") {
                    return &getRegistry();
                }
                if (auto it = m_trunks.find(registryName); it != m_trunks.end() && it->second) {
                    return &it->second->getRegistry();
                }
                if (auto it = m_namedRegistries.find(registryName); it != m_namedRegistries.end() && it->second) {
                    return it->second.get();
                }
                return nullptr;
            }

            const FCT::ECS::registry* SceneSubsystem::getRegistryByName(const std::string& registryName) const noexcept
            {
                if (registryName.empty() || registryName == "Main") {
                    return &getRegistry();
                }
                if (auto it = m_trunks.find(registryName); it != m_trunks.end() && it->second) {
                    return &it->second->getRegistry();
                }
                if (auto it = m_namedRegistries.find(registryName); it != m_namedRegistries.end() && it->second) {
                    return it->second.get();
                }
                return nullptr;
            }

            FCT::ECS::registry& SceneSubsystem::getOrCreateRegistry(const std::string& registryName)
            {
                if (auto* existing = getRegistryByName(registryName)) {
                    return *existing;
                }
                auto created = makeUnique<FCT::ECS::registry>();
                warmCoreSceneComponentStorages(*created);
                auto* ptr = created.get();
                m_namedRegistries[registryName] = std::move(created);
                return *ptr;
            }

            FCT::ECS::entity SceneSubsystem::createEntityInRegistry(
                const std::string& registryName,
                std::string_view debugName)
            {
                if (registryName.empty() || registryName == "Main") {
                    return createEntity(debugName);
                }
                auto& reg = getOrCreateRegistry(registryName);
                auto entity = reg.create();
                reg.emplace<HierarchyRelation>(entity);
                reg.emplace<LocalTransform>(entity);
                reg.emplace<WorldTransform>(entity);
                if (shouldAttachEditorMetadata()) {
                    reg.emplace<EditorNameComponent>(
                        entity,
                        EditorNameComponent{debugName.empty() ? std::string("Entity") : std::string(debugName)});
                }
                return entity;
            }

            void SceneSubsystem::destroyEntityInRegistry(const std::string& registryName, FCT::ECS::entity entity)
            {
                if (registryName.empty() || registryName == "Main") {
                    destroyEntity(entity);
                    return;
                }
                if (auto* reg = getRegistryByName(registryName)) {
                    if (reg->valid(entity)) {
                        if (auto* dom = Domain::InstancePtr()) {
                            dom->gameplay.notifyEntityDestroyed(entity, *reg);
                        }
                        m_transformGraph.detachChild(*reg, entity);
                        reg->destroy(entity);
                    }
                }
            }

            void SceneSubsystem::setRegistryVisible(const std::string& registryName, bool visible)
            {
                const std::string key = registryName.empty() ? "Main" : registryName;
                if (visible) {
                    m_hiddenRegistries.erase(key);
                } else {
                    m_hiddenRegistries.insert(key);
                }
                if (auto* trunk = getTrunk(key)) {
                    trunk->setVisible(visible);
                }
            }

            bool SceneSubsystem::isRegistryVisible(const std::string& registryName) const noexcept
            {
                const std::string key = registryName.empty() ? "Main" : registryName;
                if (m_hiddenRegistries.find(key) != m_hiddenRegistries.end()) {
                    return false;
                }
                if (const auto* trunk = getTrunk(key)) {
                    return trunk->isVisible();
                }
                return true;
            }

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine
