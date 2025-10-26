#include "DataManager.h"

#include "../core/EngineGlobal.h"

namespace MQEngine
{
    namespace fs = std::filesystem;
    void DataManager::loadRes()
    {
        m_dataLoader->ensureDirectory("./res");
        loadProjectSetting();
        loadScenePathList();
        updateModelPathList();
    }

    void DataManager::loadScenePathList()
    {
        m_dataLoader->ensureDirectory("./res/scenes");
        m_scenePathList = m_dataLoader->getSubDirectories("./res/scenes");
        loadSceneUuidMapping();
    }

    void DataManager::newScene(const std::string& sceneName)
    {
        m_dataLoader->createDirectory("./res/scenes/" + sceneName);
    }

    void DataManager::updateModelPathList()
    {
        m_dataLoader->ensureDirectory("./res/models");
        m_modelPathList = m_dataLoader->getSubDirectories("./res/models");
        loadModelUuidMapping();
    }

    std::vector<entt::registry*> DataManager::currentRegistries() const
    {
        return m_currentRegistries;
    }

    void DataManager::appendRegistry(entt::registry* registry)
    {
        m_currentRegistries.push_back(registry);
    }

    void DataManager::removeRegistry(entt::registry* registry)
    {
        m_currentRegistries.erase(std::remove(m_currentRegistries.begin(), m_currentRegistries.end(), registry), m_currentRegistries.end());
    }

    void DataManager::openScene(const std::string& uuid)
    {
        try {
            if (m_currentScene == uuid) {
                return;
            }
            auto sceneIt = m_uuidToScenePath.find(uuid);
            if (sceneIt == m_uuidToScenePath.end()) {
                throw DataError("场景UUID不存在: " + uuid);
            }

            if (m_loadScenes.find(uuid) == m_loadScenes.end()) {
                loadScene(uuid);
            }

            if (!m_currentScene.empty()) {
                closeScene(m_currentScene);
            }

            m_currentScene = uuid;

            auto scenePtr = m_loadScenes[uuid];
            if (scenePtr) {
                appendRegistry(&scenePtr->getRegistry());

                for (const auto& trunkName : scenePtr->getTrunkList()) {
                    if (scenePtr->isLoad(trunkName)) {
                        auto trunk = scenePtr->getLoadedTrunk(trunkName);
                        if (trunk) {
                            appendRegistry(&trunk->getRegistry());
                        }
                    }
                }
            }
            g_engineGlobal.sceneEventPipe.trigger<SceneEvent::EntryScene>(
            {
                .uuid = uuid,
                .scene = scenePtr.get()
            });
        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("打开场景失败: " + std::string(e.what()));
        }
    }

    void DataManager::closeScene(const std::string& uuid)
    {
        if (m_loadScenes.find(uuid) != m_loadScenes.end()) {
            auto scenePtr = m_loadScenes[uuid];
            if (scenePtr) {
                removeRegistry(&scenePtr->getRegistry());
                for (const auto& trunkName : scenePtr->getTrunkList()) {
                    if (scenePtr->isLoad(trunkName)) {
                        auto trunk = scenePtr->getLoadedTrunk(trunkName);
                        if (trunk) {
                            removeRegistry(&trunk->getRegistry());
                        }
                    }
                }
            }
        }
        
        g_engineGlobal.sceneEventPipe.trigger<SceneEvent::ExitScene>(
            {
                .uuid = uuid,
                .scene = m_loadScenes[uuid].get()
            });
    }

    bool DataManager::saveScene(const std::string& uuid)
    {
        if (m_loadScenes.find(uuid) == m_loadScenes.end()) {
            return true;
        }
        m_loadScenes[uuid]->save();
        return true;
    }

    std::string DataManager::getCurrentSceneUuid() const
    {
        return m_currentScene;
    }

    std::string DataManager::getSceneNameByUuid(const std::string& uuid) const
    {

        if (m_uuidToSceneName.count(uuid))
        {
            return m_uuidToSceneName.at(uuid);
        }
        return "";
    }

    DataLoader* DataManager::getDataLoader() const
    {
        return m_dataLoader.get();
    }

    Scene* DataManager::getCurrentScene() const
    {
        if (m_currentScene.empty())
        {
            return nullptr;
        }
        auto it = m_loadScenes.find(m_currentScene);
        if (it == m_loadScenes.end())
        {
            return nullptr;
        }
        return it->second.get();
    }

    StatusOr<std::string> DataManager::locateModelFile(const std::string& modelUuid) const
    {
        auto it = m_uuidToModel.find(modelUuid);
        if (it == m_uuidToModel.end())
            return NotFoundError("模型UUID不存在: " + modelUuid);
        const fs::path modelPath = it->second;
        const fs::path modelUuidFilePath = modelPath / "model.uuid";
        std::ifstream file(modelUuidFilePath, std::ios::binary);
        if (!file.is_open())
                return NotFoundError("无法打开模型元数据文件: " + modelUuidFilePath.string());
        try {
            boost::archive::binary_iarchive ia(file);
            ModelUuidFile modelUuidFile;
            ia >> modelUuidFile;
            if (modelUuidFile.modelRelativePath.empty())
                return InternalError("模型元数据文件中的相对路径为空: " + modelUuidFilePath.string());
            fs::path finalModelPath = modelPath / modelUuidFile.modelRelativePath;
            return finalModelPath.string();
        } catch (const std::exception& e) {
            return InternalError("读取或解析模型元数据文件失败: " + std::string(e.what()));
        }
    }

    std::string DataManager::getModelPathByUuid(const std::string& uuid) const
    {
        auto it = m_uuidToModel.find(uuid);
        if (it != m_uuidToModel.end()) {
            return it->second;
        }
        return "";
    }

    std::string DataManager::getModelRelativePathByUuid(const std::string& uuid) const
    {
        std::string modelPath = getModelPathByUuid(uuid);
        if (modelPath.empty()) {
            return "";
        }
        
        std::string modelUuidFilePath = modelPath + "/model.uuid";
        std::ifstream file(modelUuidFilePath);
        if (!file.is_open()) {
            return "";
        }
        
        try {
            boost::archive::binary_iarchive ia(file);
            ModelUuidFile modelUuidFile;
            ia >> modelUuidFile;
            return modelUuidFile.modelRelativePath;
        } catch (const std::exception& e) {
            FCT::fout << "读取模型UUID文件失败: " << e.what() << std::endl;
            return "";
        }
    }

    void DataManager::loadProjectSetting()
    {
        std::string settingFilePath = "./res/project.setting";
        
        if (!m_dataLoader->fileExists(settingFilePath)) {
            m_projectSetting.initialSceneUuid = "";
            return;
        }
        
        try {
            auto inputStream = m_dataLoader->openBinaryInputStream(settingFilePath);
            if (inputStream && inputStream->is_open()) {
                boost::archive::binary_iarchive archive(*inputStream);
                archive >> m_projectSetting;
            }
        } catch (const std::exception& e) {
            FCT::fout << "读取项目设置文件失败: " << e.what() << std::endl;
            m_projectSetting.initialSceneUuid = "";
        }
    }

    void DataManager::saveProjectSetting(const ProjectSetting& setting)
    {
        std::string settingFilePath = "./res/project.setting";
        m_projectSetting = setting;
        
        try {
            auto outputStream = m_dataLoader->openBinaryOutputStream(settingFilePath);
            if (outputStream && outputStream->is_open()) {
                boost::archive::binary_oarchive archive(*outputStream);
                archive << m_projectSetting;
            }
        } catch (const std::exception& e) {
            FCT::fout << "保存项目设置文件失败: " << e.what() << std::endl;
        }
    }

    ProjectSetting DataManager::getProjectSetting() const
    {
        return m_projectSetting;
    }

    void DataManager::setInitialSceneUuid(const std::string& uuid)
    {
        m_projectSetting.initialSceneUuid = uuid;
        saveProjectSetting(m_projectSetting);
    }

    std::string DataManager::getInitialSceneUuid() const { return m_projectSetting.initialSceneUuid; }
    StatusOr<int> ParseEmbeddedTextureIndex(std::string_view texturePath)
    {
        if (texturePath.empty() || texturePath.front() != '*')
            return InvalidArgumentError(
                StrCat(
                    "无效的内嵌纹理路径 '",
                    texturePath,
                    "'：必须以 '*' 开头。"));
        std::string_view indexSv = texturePath.substr(1);
        if (indexSv.empty())
            return InvalidArgumentError(
                StrCat(
                    "不完整的内嵌纹理路径 '",
                    texturePath,
                     "'：'*' 后面缺少索引数字。"));
        std::string indexStr(indexSv);
        int textureIndex;
        std::size_t pos;
        try {
            textureIndex = std::stoi(indexStr, &pos);
        } catch (const std::invalid_argument&) {
            return InvalidArgumentError(
                StrCat("无效的纹理索引 '", indexStr, "'：无法解析为数字。"));
        } catch (const std::out_of_range&) {
            return OutOfRangeError(
               StrCat("纹理索引 '", indexStr, "' 超出有效范围。"));
        }
        if (pos != indexStr.length())
            return InvalidArgumentError(
                StrCat("纹理索引 '", indexStr, "' 后包含无效字符。"));
        if (textureIndex < 0)
            return InvalidArgumentError(
                StrCat("纹理索引不能为负数：", textureIndex));
        return textureIndex;
    }
    StatusOr<std::vector<unsigned char>> DataManager::extractImage(const std::string& modelUuid,
                                                                   const std::string& texturePath)
    {
        if (texturePath[0] == '*')
        {
            std::vector<unsigned char> ret;
            auto path = locateModelFile(modelUuid);
            CHECK_STATUS(path);
            auto index = ParseEmbeddedTextureIndex(texturePath);
            CHECK_STATUS(index);
            if (m_modelLoader->getEmbeddedTextureData(path.value(), index.value(), ret))
            {
                return ret;
            }
            return UnknownError("未能成功从模型" + path.value() + "找到内嵌纹理:" + texturePath);
        }
        assert(false && "目前仅支持内嵌纹理。");
        return UnimplementedError("目前仅支持内嵌纹理。");
    }
    StatusOr<std::string> DataManager::getModelTexturePath(const std::string& modelUuid, const std::string& texturePath)
    {
        if (texturePath[0] == '*')
            return InvalidArgumentError("错误的传递了内嵌纹理路径，此函数仅用于处理外部文件纹理。");
        auto modelPathResult = locateModelFile(modelUuid);
        CHECK_STATUS(modelPathResult);
        const std::string& modelFilePath = modelPathResult.value();
        namespace fs = std::filesystem;
        fs::path fullModelPath(modelFilePath);
        fs::path modelDirectory = fullModelPath.parent_path();
        fs::path combinedTexturePath = modelDirectory / texturePath;
        return combinedTexturePath.generic_string();
    }
} // namespace MQEngine
