#ifndef MODEL_ASSET_SERIALIZER_H
#define MODEL_ASSET_SERIALIZER_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "ResourceLoader.h"
#include "../../base/ConfigDocument.h"

namespace MQEngine
{
    struct ModelUuidFile
    {
        std::string uuid;
        std::string modelRelativePath;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & uuid;
            ar & modelRelativePath;
        }
    };

    struct SceneUuidFile
    {
        std::string uuid;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & uuid;
        }
    };

    struct ProjectSetting
    {
        std::string initialSceneUuid;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & initialSceneUuid;
        }
    };

    struct ModelTimestampData
    {
        std::time_t timestamp = 0;
        std::string sourcePath;
    };

    /**
     * @brief 资产分层序列化工具：高频二进制烘焙缓存（模型索引、时间戳）采用 FlatBuffers，轻量级人类可读元数据（UUID、工程设置）采用 ConfigDocument (TOML)
     */
    class ENGINE_API ModelAssetSerializer
    {
    public:
        static Status saveModelIndex(
            ResourceLoader* loader,
            const std::string& filePath,
            const FCT::ModelInfo::SceneInfo& sceneInfo);

        static StatusOr<FCT::ModelInfo::SceneInfo> loadModelIndex(
            ResourceLoader* loader,
            const std::string& filePath);

        static Status saveModelTimestamp(
            ResourceLoader* loader,
            const std::string& filePath,
            std::time_t timestamp,
            const std::string& sourcePath);

        static StatusOr<ModelTimestampData> loadModelTimestamp(
            ResourceLoader* loader,
            const std::string& filePath);

        static Status saveModelUuid(
            ResourceLoader* loader,
            const std::string& filePath,
            const ModelUuidFile& data);

        static StatusOr<ModelUuidFile> loadModelUuid(
            ResourceLoader* loader,
            const std::string& filePath);

        static Status saveSceneUuid(
            ResourceLoader* loader,
            const std::string& filePath,
            const SceneUuidFile& data);

        static StatusOr<SceneUuidFile> loadSceneUuid(
            ResourceLoader* loader,
            const std::string& filePath);

        static Status saveProjectSetting(
            ResourceLoader* loader,
            const std::string& filePath,
            const ProjectSetting& setting);

        static StatusOr<ProjectSetting> loadProjectSetting(
            ResourceLoader* loader,
            const std::string& filePath);

    private:
        static std::string serializeSceneInfoToFlatBuffer(
            const FCT::ModelInfo::SceneInfo& sceneInfo);

        static StatusOr<FCT::ModelInfo::SceneInfo> deserializeSceneInfoFromFlatBuffer(
            const std::string& content);

        static StatusOr<FCT::ModelInfo::SceneInfo> deserializeLegacySceneInfo(
            const std::string& content,
            const std::string& filePath);

        static std::string serializeTimestampToFlatBuffer(
            std::time_t timestamp,
            const std::string& sourcePath);

        static StatusOr<ModelTimestampData> deserializeTimestampFromFlatBuffer(
            const std::string& content);

        static StatusOr<ModelTimestampData> deserializeLegacyTimestamp(
            const std::string& content,
            const std::string& filePath);
    };
}

#endif // MODEL_ASSET_SERIALIZER_H
