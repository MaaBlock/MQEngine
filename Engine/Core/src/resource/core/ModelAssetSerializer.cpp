#include "ModelAssetSerializer.h"
#include "../data/ModelCache_generated.h"
#include "DiskResourceLoader.h"
#include "../../runtime/core/EngineGlobal.h"

namespace MQEngine
{
    namespace
    {
        constexpr const char* kModelTimestampIdentifier = "MQMT";

        ResourceLoader* resolveResourceLoader(ResourceLoader* loader)
        {
            if (loader)
            {
                return loader;
            }
            if (g_engineGlobal.resourceLoader)
            {
                return g_engineGlobal.resourceLoader;
            }
            static DiskResourceLoader fallbackLoader;
            return &fallbackLoader;
        }

        MQEngine::FlatBuffers::Vec3f toFlatVec3(const FCT::Vec3<>& v)
        {
            return MQEngine::FlatBuffers::Vec3f(v.x, v.y, v.z);
        }

        FCT::Vec3<> fromFlatVec3(const MQEngine::FlatBuffers::Vec3f* v)
        {
            if (!v)
            {
                return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
            }
            return FCT::Vec3<>(v->x(), v->y(), v->z());
        }

        flatbuffers::Offset<MQEngine::FlatBuffers::MeshInfo> buildFlatMeshInfo(
            flatbuffers::FlatBufferBuilder& fbb,
            const FCT::ModelInfo::MeshInfo& mesh)
        {
            std::vector<uint8_t> texCoords(mesh.hasTexCoords.begin(), mesh.hasTexCoords.end());
            std::vector<uint8_t> vertexColors(mesh.hasVertexColors.begin(), mesh.hasVertexColors.end());

            auto bboxMin = toFlatVec3(mesh.boundingBoxMin);
            auto bboxMax = toFlatVec3(mesh.boundingBoxMax);
            auto bboxCenter = toFlatVec3(mesh.boundingBoxCenter);

            return MQEngine::FlatBuffers::CreateMeshInfoDirect(
                fbb,
                mesh.name.c_str(),
                mesh.vertexCount,
                mesh.indexCount,
                mesh.triangleCount,
                mesh.isIndexed,
                mesh.hasPositions,
                mesh.hasNormals,
                mesh.hasTangents,
                mesh.hasBitangents,
                &texCoords,
                &vertexColors,
                &bboxMin,
                &bboxMax,
                &bboxCenter,
                mesh.hasBones,
                mesh.boneCount);
        }

        FCT::ModelInfo::MeshInfo unpackFlatMeshInfo(
            const MQEngine::FlatBuffers::MeshInfo* fbMesh)
        {
            FCT::ModelInfo::MeshInfo mesh;
            if (!fbMesh)
            {
                return mesh;
            }
            if (fbMesh->name())
            {
                mesh.name = fbMesh->name()->str();
            }
            mesh.vertexCount = fbMesh->vertex_count();
            mesh.indexCount = fbMesh->index_count();
            mesh.triangleCount = fbMesh->triangle_count();
            mesh.isIndexed = fbMesh->is_indexed();
            mesh.hasPositions = fbMesh->has_positions();
            mesh.hasNormals = fbMesh->has_normals();
            mesh.hasTangents = fbMesh->has_tangents();
            mesh.hasBitangents = fbMesh->has_bitangents();
            mesh.hasBones = fbMesh->has_bones();
            mesh.boneCount = fbMesh->bone_count();

            if (const auto* tc = fbMesh->has_tex_coords())
            {
                const size_t count = std::min<size_t>(tc->size(), mesh.hasTexCoords.size());
                for (size_t i = 0; i < count; ++i)
                {
                    mesh.hasTexCoords[i] = (tc->Get(static_cast<flatbuffers::uoffset_t>(i)) != 0);
                }
            }

            if (const auto* vc = fbMesh->has_vertex_colors())
            {
                const size_t count = std::min<size_t>(vc->size(), mesh.hasVertexColors.size());
                for (size_t i = 0; i < count; ++i)
                {
                    mesh.hasVertexColors[i] = (vc->Get(static_cast<flatbuffers::uoffset_t>(i)) != 0);
                }
            }

            mesh.boundingBoxMin = fromFlatVec3(fbMesh->bounding_box_min());
            mesh.boundingBoxMax = fromFlatVec3(fbMesh->bounding_box_max());
            mesh.boundingBoxCenter = fromFlatVec3(fbMesh->bounding_box_center());
            return mesh;
        }

        flatbuffers::Offset<MQEngine::FlatBuffers::TextureInfo> buildFlatTextureInfo(
            flatbuffers::FlatBufferBuilder& fbb,
            const FCT::ModelInfo::TextureInfo& tex)
        {
            std::vector<int32_t> types;
            types.reserve(tex.types.size());
            for (const auto t : tex.types)
            {
                types.push_back(static_cast<int32_t>(t));
            }
            return MQEngine::FlatBuffers::CreateTextureInfoDirect(
                fbb,
                tex.path.c_str(),
                tex.isInner,
                &types);
        }

        FCT::ModelInfo::TextureInfo unpackFlatTextureInfo(
            const MQEngine::FlatBuffers::TextureInfo* fbTex)
        {
            FCT::ModelInfo::TextureInfo tex;
            if (!fbTex)
            {
                return tex;
            }
            if (fbTex->path())
            {
                tex.path = fbTex->path()->str();
            }
            tex.isInner = fbTex->is_inner();
            if (const auto* types = fbTex->types())
            {
                for (flatbuffers::uoffset_t i = 0; i < types->size(); ++i)
                {
                    tex.types.insert(static_cast<FCT::ModelInfo::TextureType>(types->Get(i)));
                }
            }
            return tex;
        }

        std::string trimAsciiWhitespace(const std::string& raw)
        {
            const auto first = raw.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return "";
            }
            const auto last = raw.find_last_not_of(" \t\r\n");
            return raw.substr(first, last - first + 1);
        }
    }

    std::string ModelAssetSerializer::serializeSceneInfoToFlatBuffer(
        const FCT::ModelInfo::SceneInfo& sceneInfo)
    {
        flatbuffers::FlatBufferBuilder fbb(1024);

        std::vector<flatbuffers::Offset<MQEngine::FlatBuffers::MeshInfo>> meshOffsets;
        meshOffsets.reserve(sceneInfo.meshInfos.size());
        for (const auto& mesh : sceneInfo.meshInfos)
        {
            meshOffsets.push_back(buildFlatMeshInfo(fbb, mesh));
        }

        std::vector<flatbuffers::Offset<MQEngine::FlatBuffers::MaterialInfo>> matOffsets;
        matOffsets.reserve(sceneInfo.materialInfos.size());
        for (size_t i = 0; i < sceneInfo.materialInfos.size(); ++i)
        {
            matOffsets.push_back(MQEngine::FlatBuffers::CreateMaterialInfo(fbb));
        }

        std::vector<flatbuffers::Offset<MQEngine::FlatBuffers::TextureInfo>> texOffsets;
        texOffsets.reserve(sceneInfo.textureInfos.size());
        for (const auto& tex : sceneInfo.textureInfos)
        {
            texOffsets.push_back(buildFlatTextureInfo(fbb, tex));
        }

        std::vector<flatbuffers::Offset<flatbuffers::String>> animOffsets;
        animOffsets.reserve(sceneInfo.animationNames.size());
        for (const auto& animName : sceneInfo.animationNames)
        {
            animOffsets.push_back(fbb.CreateString(animName));
        }

        auto root = MQEngine::FlatBuffers::CreateSceneInfoDirect(
            fbb,
            sceneInfo.name.c_str(),
            &meshOffsets,
            &matOffsets,
            &texOffsets,
            &animOffsets,
            sceneInfo.boneCount);
        MQEngine::FlatBuffers::FinishSceneInfoBuffer(fbb, root);

        return std::string(
            reinterpret_cast<const char*>(fbb.GetBufferPointer()),
            fbb.GetSize());
    }

    StatusOr<FCT::ModelInfo::SceneInfo> ModelAssetSerializer::deserializeSceneInfoFromFlatBuffer(
        const std::string& content)
    {
        const auto* dataPtr = reinterpret_cast<const uint8_t*>(content.data());
        flatbuffers::Verifier verifier(dataPtr, content.size());
        if (!MQEngine::FlatBuffers::VerifySceneInfoBuffer(verifier))
        {
            return InvalidArgumentError("Invalid FlatBuffers SceneInfo buffer");
        }

        const auto* fbScene = MQEngine::FlatBuffers::GetSceneInfo(dataPtr);
        if (!fbScene)
        {
            return InvalidArgumentError("Null FlatBuffers SceneInfo root");
        }

        FCT::ModelInfo::SceneInfo result;
        if (fbScene->name())
        {
            result.name = fbScene->name()->str();
        }

        if (const auto* meshes = fbScene->mesh_infos())
        {
            result.meshInfos.reserve(meshes->size());
            for (flatbuffers::uoffset_t i = 0; i < meshes->size(); ++i)
            {
                result.meshInfos.push_back(unpackFlatMeshInfo(meshes->Get(i)));
            }
        }

        if (const auto* mats = fbScene->material_infos())
        {
            result.materialInfos.resize(mats->size());
        }

        if (const auto* texs = fbScene->texture_infos())
        {
            result.textureInfos.reserve(texs->size());
            for (flatbuffers::uoffset_t i = 0; i < texs->size(); ++i)
            {
                result.textureInfos.push_back(unpackFlatTextureInfo(texs->Get(i)));
            }
        }

        if (const auto* anims = fbScene->animation_names())
        {
            result.animationNames.reserve(anims->size());
            for (flatbuffers::uoffset_t i = 0; i < anims->size(); ++i)
            {
                if (const auto* s = anims->Get(i))
                {
                    result.animationNames.push_back(s->str());
                }
            }
        }
        result.boneCount = fbScene->bone_count();

        return result;
    }

    StatusOr<FCT::ModelInfo::SceneInfo> ModelAssetSerializer::deserializeLegacySceneInfo(
        const std::string& content,
        const std::string& filePath)
    {
        try
        {
            std::istringstream iss(content, std::ios::binary);
            boost::archive::binary_iarchive ia(iss);
            FCT::ModelInfo::SceneInfo info;
            ia >> info;
            return info;
        }
        catch (const std::exception& e)
        {
            return InternalError(StrCat("Failed to deserialize model index '", filePath, "': ", e.what()));
        }
    }

    Status ModelAssetSerializer::saveModelIndex(
        ResourceLoader* loader,
        const std::string& filePath,
        const FCT::ModelInfo::SceneInfo& sceneInfo)
    {
        loader = resolveResourceLoader(loader);
        std::string buffer = serializeSceneInfoToFlatBuffer(sceneInfo);
        return loader->saveFile(filePath, buffer);
    }

    StatusOr<FCT::ModelInfo::SceneInfo> ModelAssetSerializer::loadModelIndex(
        ResourceLoader* loader,
        const std::string& filePath)
    {
        loader = resolveResourceLoader(loader);
        auto contentOr = loader->loadFile(filePath);
        CHECK_STATUS(contentOr);

        const std::string& content = contentOr.value();
        if (content.size() >= 8 && MQEngine::FlatBuffers::SceneInfoBufferHasIdentifier(content.data()))
        {
            auto fbResult = deserializeSceneInfoFromFlatBuffer(content);
            if (fbResult.ok())
            {
                return fbResult;
            }
        }
        return deserializeLegacySceneInfo(content, filePath);
    }

    std::string ModelAssetSerializer::serializeTimestampToFlatBuffer(
        std::time_t timestamp,
        const std::string& sourcePath)
    {
        flatbuffers::FlatBufferBuilder fbb(256);
        auto root = MQEngine::FlatBuffers::CreateModelTimestampDirect(
            fbb,
            static_cast<int64_t>(timestamp),
            sourcePath.c_str());
        fbb.Finish(root, kModelTimestampIdentifier);

        return std::string(
            reinterpret_cast<const char*>(fbb.GetBufferPointer()),
            fbb.GetSize());
    }

    StatusOr<ModelTimestampData> ModelAssetSerializer::deserializeTimestampFromFlatBuffer(
        const std::string& content)
    {
        const auto* dataPtr = reinterpret_cast<const uint8_t*>(content.data());
        flatbuffers::Verifier verifier(dataPtr, content.size());
        if (!verifier.VerifyBuffer<MQEngine::FlatBuffers::ModelTimestamp>(kModelTimestampIdentifier))
        {
            return InvalidArgumentError("Invalid FlatBuffers ModelTimestamp buffer");
        }

        const auto* fbTs = flatbuffers::GetRoot<MQEngine::FlatBuffers::ModelTimestamp>(dataPtr);
        if (!fbTs)
        {
            return InvalidArgumentError("Null FlatBuffers ModelTimestamp root");
        }

        ModelTimestampData data;
        data.timestamp = static_cast<std::time_t>(fbTs->timestamp());
        if (fbTs->source_path())
        {
            data.sourcePath = fbTs->source_path()->str();
        }
        return data;
    }

    StatusOr<ModelTimestampData> ModelAssetSerializer::deserializeLegacyTimestamp(
        const std::string& content,
        const std::string& filePath)
    {
        try
        {
            std::istringstream iss(content, std::ios::binary);
            boost::archive::binary_iarchive ia(iss);
            ModelTimestampData data;
            ia >> data.timestamp;
            try
            {
                ia >> data.sourcePath;
            }
            catch (...)
            {
            }
            return data;
        }
        catch (const std::exception& e)
        {
            return InternalError(StrCat("Failed to deserialize model timestamp '", filePath, "': ", e.what()));
        }
    }

    Status ModelAssetSerializer::saveModelTimestamp(
        ResourceLoader* loader,
        const std::string& filePath,
        std::time_t timestamp,
        const std::string& sourcePath)
    {
        loader = resolveResourceLoader(loader);
        std::string buffer = serializeTimestampToFlatBuffer(timestamp, sourcePath);
        return loader->saveFile(filePath, buffer);
    }

    StatusOr<ModelTimestampData> ModelAssetSerializer::loadModelTimestamp(
        ResourceLoader* loader,
        const std::string& filePath)
    {
        loader = resolveResourceLoader(loader);
        auto contentOr = loader->loadFile(filePath);
        CHECK_STATUS(contentOr);

        const std::string& content = contentOr.value();
        if (content.size() >= 8 && flatbuffers::BufferHasIdentifier(content.data(), kModelTimestampIdentifier))
        {
            auto fbResult = deserializeTimestampFromFlatBuffer(content);
            if (fbResult.ok())
            {
                return fbResult;
            }
        }
        return deserializeLegacyTimestamp(content, filePath);
    }

    Status ModelAssetSerializer::saveModelUuid(
        ResourceLoader* loader,
        const std::string& filePath,
        const ModelUuidFile& data)
    {
        loader = resolveResourceLoader(loader);
        return ConfigDocument::saveStructToFile(loader, filePath, data);
    }

    StatusOr<ModelUuidFile> ModelAssetSerializer::loadModelUuid(
        ResourceLoader* loader,
        const std::string& filePath)
    {
        loader = resolveResourceLoader(loader);
        auto tomlResult = ConfigDocument::loadStructFromFile<ModelUuidFile>(loader, filePath);
        if (tomlResult.ok() && !tomlResult->uuid.empty())
        {
            return tomlResult;
        }

        auto contentOr = loader->loadFile(filePath);
        CHECK_STATUS(contentOr);
        try
        {
            std::istringstream iss(contentOr.value(), std::ios::binary);
            boost::archive::binary_iarchive ia(iss);
            ModelUuidFile data;
            ia >> data;
            return data;
        }
        catch (const std::exception& e)
        {
            return InternalError(StrCat("Failed to load model.uuid '", filePath, "': ", e.what()));
        }
    }

    Status ModelAssetSerializer::saveSceneUuid(
        ResourceLoader* loader,
        const std::string& filePath,
        const SceneUuidFile& data)
    {
        loader = resolveResourceLoader(loader);
        return ConfigDocument::saveStructToFile(loader, filePath, data);
    }

    StatusOr<SceneUuidFile> ModelAssetSerializer::loadSceneUuid(
        ResourceLoader* loader,
        const std::string& filePath)
    {
        loader = resolveResourceLoader(loader);
        auto tomlResult = ConfigDocument::loadStructFromFile<SceneUuidFile>(loader, filePath);
        if (tomlResult.ok() && !tomlResult->uuid.empty())
        {
            return tomlResult;
        }

        auto contentOr = loader->loadFile(filePath);
        CHECK_STATUS(contentOr);
        try
        {
            std::istringstream iss(contentOr.value(), std::ios::binary);
            boost::archive::binary_iarchive ia(iss);
            SceneUuidFile data;
            ia >> data;
            if (!data.uuid.empty())
            {
                return data;
            }
        }
        catch (...)
        {
        }

        std::string plainUuid = trimAsciiWhitespace(contentOr.value());
        if (!plainUuid.empty())
        {
            SceneUuidFile data;
            data.uuid = plainUuid;
            return data;
        }
        return InternalError(StrCat("Failed to parse scene.uuid '", filePath, "'"));
    }

    Status ModelAssetSerializer::saveProjectSetting(
        ResourceLoader* loader,
        const std::string& filePath,
        const ProjectSetting& setting)
    {
        loader = resolveResourceLoader(loader);
        return ConfigDocument::saveStructToFile(loader, filePath, setting);
    }

    StatusOr<ProjectSetting> ModelAssetSerializer::loadProjectSetting(
        ResourceLoader* loader,
        const std::string& filePath)
    {
        loader = resolveResourceLoader(loader);
        auto tomlResult = ConfigDocument::loadStructFromFile<ProjectSetting>(loader, filePath);
        if (tomlResult.ok())
        {
            return tomlResult;
        }

        auto contentOr = loader->loadFile(filePath);
        CHECK_STATUS(contentOr);
        try
        {
            std::istringstream iss(contentOr.value(), std::ios::binary);
            boost::archive::binary_iarchive ia(iss);
            ProjectSetting setting;
            ia >> setting;
            return setting;
        }
        catch (const std::exception& e)
        {
            return InternalError(StrCat("Failed to load project.setting '", filePath, "': ", e.what()));
        }
    }
}
