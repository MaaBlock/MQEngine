//
// Created by MaaBlock on 2025/11/3.
//

#include "SceneSystem.h"
#include "ComponentReflection.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../manager/RegistriesManager.h"

namespace MQEngine {

    namespace {

        constexpr uint32_t kExtBinaryMagic = 0x4D514558u; // "MQEX"
        constexpr uint32_t kExtBinaryVersion = 1u;

        enum class BinaryComponentValueTag : uint8_t {
            Bool = 0,
            Int = 1,
            Float = 2,
            Double = 3,
            String = 4,
            Vec2 = 5,
            Vec3 = 6,
            Vec4 = 7,
            StringArray = 8
        };

        void writeComponentValueBinary(boost::archive::binary_oarchive& ar, const ComponentValue& val)
        {
            std::visit([&ar](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::Bool);
                    ar << tag << v;
                } else if constexpr (std::is_same_v<T, int>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::Int);
                    ar << tag << v;
                } else if constexpr (std::is_same_v<T, float>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::Float);
                    ar << tag << v;
                } else if constexpr (std::is_same_v<T, double>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::Double);
                    ar << tag << v;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::String);
                    ar << tag << v;
                } else if constexpr (std::is_same_v<T, Vec2>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::Vec2);
                    ar << tag << v.x << v.y;
                } else if constexpr (std::is_same_v<T, Vec3<>>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::Vec3);
                    ar << tag << v.x << v.y << v.z;
                } else if constexpr (std::is_same_v<T, Vec4>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::Vec4);
                    ar << tag << v.x << v.y << v.z << v.w;
                } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                    uint8_t tag = static_cast<uint8_t>(BinaryComponentValueTag::StringArray);
                    ar << tag << v;
                }
            }, val);
        }

        bool coerceComponentValueType(const ComponentValue& rawVal, const std::string& fieldType, ComponentValue& outVal)
        {
            auto asDouble = [](const ComponentValue& v, double& out) -> bool {
                if (const auto* pInt = std::get_if<int>(&v)) {
                    out = static_cast<double>(*pInt);
                    return true;
                }
                if (const auto* pFloat = std::get_if<float>(&v)) {
                    out = static_cast<double>(*pFloat);
                    return true;
                }
                if (const auto* pDouble = std::get_if<double>(&v)) {
                    out = *pDouble;
                    return true;
                }
                return false;
            };

            if (fieldType == "bool" && std::holds_alternative<bool>(rawVal)) {
                outVal = rawVal;
                return true;
            }
            if (fieldType == "int") {
                double num = 0.0;
                if (asDouble(rawVal, num)) {
                    outVal = static_cast<int>(num);
                    return true;
                }
            }
            if (fieldType == "float") {
                double num = 0.0;
                if (asDouble(rawVal, num)) {
                    outVal = static_cast<float>(num);
                    return true;
                }
            }
            if (fieldType == "double") {
                double num = 0.0;
                if (asDouble(rawVal, num)) {
                    outVal = num;
                    return true;
                }
            }
            if (fieldType == "string" && std::holds_alternative<std::string>(rawVal)) {
                outVal = rawVal;
                return true;
            }
            if (fieldType == "Vec2" && std::holds_alternative<Vec2>(rawVal)) {
                outVal = rawVal;
                return true;
            }
            if (fieldType == "Vec3" && std::holds_alternative<Vec3<>>(rawVal)) {
                outVal = rawVal;
                return true;
            }
            if (fieldType == "Vec4" && std::holds_alternative<Vec4>(rawVal)) {
                outVal = rawVal;
                return true;
            }
            if (fieldType == "string[]" && std::holds_alternative<std::vector<std::string>>(rawVal)) {
                outVal = rawVal;
                return true;
            }
            return false;
        }

        bool readComponentValueBinary(boost::archive::binary_iarchive& ar, ComponentValue& outVal)
        {
            uint8_t rawTag = 0;
            ar >> rawTag;
            switch (static_cast<BinaryComponentValueTag>(rawTag)) {
                case BinaryComponentValueTag::Bool: {
                    bool v = false;
                    ar >> v;
                    outVal = v;
                    return true;
                }
                case BinaryComponentValueTag::Int: {
                    int v = 0;
                    ar >> v;
                    outVal = v;
                    return true;
                }
                case BinaryComponentValueTag::Float: {
                    float v = 0.0f;
                    ar >> v;
                    outVal = v;
                    return true;
                }
                case BinaryComponentValueTag::Double: {
                    double v = 0.0;
                    ar >> v;
                    outVal = v;
                    return true;
                }
                case BinaryComponentValueTag::String: {
                    std::string v;
                    ar >> v;
                    outVal = std::move(v);
                    return true;
                }
                case BinaryComponentValueTag::Vec2: {
                    float x = 0.0f, y = 0.0f;
                    ar >> x >> y;
                    outVal = Vec2(x, y);
                    return true;
                }
                case BinaryComponentValueTag::Vec3: {
                    float x = 0.0f, y = 0.0f, z = 0.0f;
                    ar >> x >> y >> z;
                    outVal = Vec3<>(x, y, z);
                    return true;
                }
                case BinaryComponentValueTag::Vec4: {
                    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
                    ar >> x >> y >> z >> w;
                    outVal = Vec4(x, y, z, w);
                    return true;
                }
                case BinaryComponentValueTag::StringArray: {
                    std::vector<std::string> v;
                    ar >> v;
                    outVal = std::move(v);
                    return true;
                }
                default:
                    return false;
            }
        }

        const ComponentReflection* getReflection()
        {
            if (g_engineGlobal.componentReflection) {
                return g_engineGlobal.componentReflection;
            }
            return Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->foundation->getComponentReflection() : nullptr;
        }

        size_t findExtBinaryOffset(const std::string& rawBytes) noexcept
        {
            constexpr char kSignature[8] = {
                static_cast<char>(0x58),
                static_cast<char>(0x45),
                static_cast<char>(0x51),
                static_cast<char>(0x4D),
                static_cast<char>(0x01),
                static_cast<char>(0x00),
                static_cast<char>(0x00),
                static_cast<char>(0x00)
            };
            const std::string_view view(rawBytes);
            const std::string_view sig(kSignature, sizeof(kSignature));
            return view.find(sig);
        }

    } // namespace

    void SceneSystem::updateLogic()
    {
    }

    void SceneSystem::updateRender()
    {
    }

    void SceneSystem::purgeRuntimeSubEntities(entt::registry& registry)
    {
        auto view = registry.view<RuntimeSubEntityTag>();
        if (g_engineGlobal.registriesManager) {
            for (auto entity : view) {
                g_engineGlobal.registriesManager->destroyEntityDeferred(&registry, entity, false);
            }
        } else {
            std::vector<entt::entity> runtimeEntities;
            for (auto entity : view) {
                runtimeEntities.push_back(entity);
            }
            for (auto entity : runtimeEntities) {
                if (registry.valid(entity)) {
                    registry.destroy(entity);
                }
            }
        }
    }

    void SceneSystem::resolveHierarchyParentLinks(entt::registry& registry)
    {
        std::unordered_map<std::string, entt::entity> uuidToEntity;
        auto nameView = registry.view<NameTag>();
        for (auto entity : nameView) {
            const auto& tag = nameView.get<NameTag>(entity);
            if (!tag.uuid.empty()) {
                uuidToEntity[tag.uuid] = entity;
            }
        }

        auto hierarchyView = registry.view<HierarchyComponent>();
        for (auto entity : hierarchyView) {
            auto& h = hierarchyView.get<HierarchyComponent>(entity);
            if (!h.parentUuid.empty()) {
                auto it = uuidToEntity.find(h.parentUuid);
                if (it != uuidToEntity.end() && it->second != entity) {
                    h.parentEntity = static_cast<uint32_t>(it->second);
                } else {
                    h.parentEntity = static_cast<uint32_t>(entt::null);
                }
            } else {
                h.parentEntity = static_cast<uint32_t>(entt::null);
            }
        }
    }

    void SceneSystem::saveRegistryExtBinary(entt::registry& registry, boost::archive::binary_oarchive& archive)
    {
        const ComponentReflection* reflection = getReflection();
        if (!reflection) {
            return;
        }

        const auto& compInfos = reflection->getComponentInfos();
        std::vector<entt::entity> validEntities;
        const auto& entityStorage = registry.storage<entt::entity>();
        for (auto entity : entityStorage) {
            if (registry.valid(entity) && !registry.all_of<RuntimeSubEntityTag>(entity)) {
                validEntities.push_back(entity);
            }
        }

        archive << kExtBinaryMagic;
        archive << kExtBinaryVersion;
        uint32_t entityCount = static_cast<uint32_t>(validEntities.size());
        archive << entityCount;

        for (auto entity : validEntities) {
            auto& nameTag = registry.get_or_emplace<NameTag>(entity);
            if (nameTag.uuid.empty()) {
                nameTag = NameTag{nameTag.name.empty() ? "entity_" + std::to_string(static_cast<uint32_t>(entity)) : nameTag.name};
            }

            archive << nameTag.uuid;
            archive << nameTag.name;

            std::vector<const ComponentInfo*> activeComps;
            for (const auto& [compName, info] : compInfos) {
                if (compName == "RuntimeSubEntityTag" || compName == "CacheResource") {
                    continue;
                }
                if (info.hasComponent && info.getComponent && info.hasComponent(registry, entity)) {
                    if (info.getComponent(registry, entity) != nullptr) {
                        activeComps.push_back(&info);
                    }
                }
            }

            uint32_t compCount = static_cast<uint32_t>(activeComps.size());
            archive << compCount;
            for (const ComponentInfo* info : activeComps) {
                void* compPtr = info->getComponent(registry, entity);
                archive << info->name;

                uint32_t fieldCount = 0;
                for (const auto& field : info->fields) {
                    if (field.getter) {
                        ++fieldCount;
                    }
                }
                archive << fieldCount;
                for (const auto& field : info->fields) {
                    if (field.getter) {
                        archive << field.name;
                        writeComponentValueBinary(archive, field.getter(compPtr));
                    }
                }
            }
        }
    }

    bool SceneSystem::loadRegistryExtBinary(entt::registry& registry, boost::archive::binary_iarchive& archive)
    {
        const ComponentReflection* reflection = getReflection();
        bool parsedExt = false;
        try {
            uint32_t magic = 0;
            uint32_t version = 0;
            archive >> magic;
            if (magic != kExtBinaryMagic) {
                resolveHierarchyParentLinks(registry);
                return false;
            }
            archive >> version;
            if (version != kExtBinaryVersion) {
                resolveHierarchyParentLinks(registry);
                return false;
            }
            parsedExt = true;

            uint32_t entityCount = 0;
            archive >> entityCount;

            std::unordered_map<std::string, entt::entity> uuidToEntity;
            for (auto entity : registry.view<NameTag>()) {
                const auto& tag = registry.get<NameTag>(entity);
                if (!tag.uuid.empty()) {
                    uuidToEntity[tag.uuid] = entity;
                }
            }

            for (uint32_t i = 0; i < entityCount; ++i) {
                std::string uuid;
                std::string name;
                archive >> uuid;
                archive >> name;

                entt::entity entity = entt::null;
                if (!uuid.empty()) {
                    auto it = uuidToEntity.find(uuid);
                    if (it != uuidToEntity.end()) {
                        entity = it->second;
                    }
                }
                if (entity == entt::null || !registry.valid(entity)) {
                    entity = registry.create();
                    NameTag tag(name.empty() ? "Entity" : name);
                    if (!uuid.empty()) {
                        tag.uuid = uuid;
                    }
                    registry.emplace_or_replace<NameTag>(entity, tag);
                    if (!tag.uuid.empty()) {
                        uuidToEntity[tag.uuid] = entity;
                    }
                }

                uint32_t compCount = 0;
                archive >> compCount;
                for (uint32_t c = 0; c < compCount; ++c) {
                    std::string compName;
                    uint32_t fieldCount = 0;
                    archive >> compName;
                    archive >> fieldCount;

                    const ComponentInfo* info = reflection ? reflection->findComponent(compName) : nullptr;
                    void* compPtr = nullptr;
                    if (info && info->hasComponent && info->addComponent && info->getComponent) {
                        if (!info->hasComponent(registry, entity)) {
                            info->addComponent(registry, entity);
                        }
                        compPtr = info->getComponent(registry, entity);
                    }

                    for (uint32_t f = 0; f < fieldCount; ++f) {
                        std::string fieldName;
                        archive >> fieldName;
                        ComponentValue rawVal;
                        if (!readComponentValueBinary(archive, rawVal)) {
                            continue;
                        }
                        if (!info || !compPtr) {
                            continue;
                        }
                        for (const auto& field : info->fields) {
                            if (field.name == fieldName && field.setter) {
                                ComponentValue coercedVal;
                                if (coerceComponentValueType(rawVal, field.type, coercedVal)) {
                                    field.setter(compPtr, coercedVal);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception&) {
            resolveHierarchyParentLinks(registry);
            return false;
        }

        resolveHierarchyParentLinks(registry);
        return parsedExt;
    }

    bool SceneSystem::hasExtBinarySection(const std::string& rawBytes) noexcept
    {
        return findExtBinaryOffset(rawBytes) != std::string::npos;
    }

    bool SceneSystem::recoverRegistryFromExtSection(entt::registry& registry, const std::string& rawBytes)
    {
        const size_t extOffset = findExtBinaryOffset(rawBytes);
        if (extOffset == std::string::npos) {
            return false;
        }

        try {
            std::stringstream headerStream(std::ios::in | std::ios::out | std::ios::binary);
            {
                boost::archive::binary_oarchive headerArchive(headerStream);
            }
            std::string patchedPayload = headerStream.str();
            patchedPayload.append(rawBytes.data() + extOffset, rawBytes.size() - extOffset);

            std::stringstream extStream(patchedPayload, std::ios::in | std::ios::binary);
            boost::archive::binary_iarchive extArchive(extStream);
            return loadRegistryExtBinary(registry, extArchive);
        } catch (const std::exception&) {
            return false;
        }
    }

} // MQEngine
