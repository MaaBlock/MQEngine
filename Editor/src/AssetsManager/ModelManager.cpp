//
// Created by Administrator on 2025/8/23.
//

#include "ModelManager.h"
#define TEXT(str) (const char*)u8##str
using namespace FCT;
namespace MQEngine
{
    ModelManager::ModelManager(DataManager* dataManager)
    {
        m_dataManager = dataManager;
        m_modelLoader = g_global.rt->createModelLoader();
        m_supportedExtensions = m_modelLoader->getSupportedExtensions();
        g_global.wnd->getCallBack()->addFileDropCallback([this](Window*,const std::vector<std::string>& files)
        {
            for (const auto& file : files) {
                std::string extension = std::filesystem::path(file).extension().string();
                fout << "加载了模型文件: " << file << std::endl;
                if (m_supportedExtensions.count(extension))
                {
                    importModel(file);
                }
            }
        });
    }

    void ModelManager::importModel(const std::string& modelPath)
    {
        std::filesystem::path modelFilePath(modelPath);
        std::string modelBaseName = modelFilePath.stem().string();
        std::string targetDirName = GetUniqueDirectoryName("./res/models",modelBaseName);
        std::filesystem::path targetDir = std::filesystem::path("./res/models") / targetDirName;
        std::filesystem::create_directories(targetDir);

        std::set<std::string> allModelPaths = m_modelLoader->resolveModePaths(modelPath);

        std::filesystem::path sourceBaseDir = std::filesystem::path(modelPath).parent_path();

        std::filesystem::path modelSubDir = targetDir / "source";
        std::filesystem::create_directories(modelSubDir);

        for (const auto& filePath : allModelPaths) {
            if (std::filesystem::exists(filePath)) {
                std::filesystem::path sourceFile(filePath);
                std::filesystem::path targetFile;

                try {
                    std::filesystem::path relativePath = std::filesystem::relative(sourceFile, sourceBaseDir);
                    targetFile = modelSubDir / relativePath;
                } catch (const std::filesystem::filesystem_error&) {
                    targetFile = modelSubDir / sourceFile.filename();
                }

                std::filesystem::create_directories(targetFile.parent_path());

                std::filesystem::copy_file(sourceFile, targetFile,
                    std::filesystem::copy_options::overwrite_existing);
            }
        }
        saveModelIndex(targetDir, modelBaseName, modelPath);
        saveModelTimestamp(targetDir, modelPath);
    }

    void ModelManager::saveModelIndex(const std::filesystem::path& targetDir, const std::string& modelBaseName, const std::string& originalPath)
    {
        std::filesystem::path indexFile = targetDir / "index.edit.dat";
        std::ofstream indexOut(indexFile, std::ios::binary);

        if (indexOut.is_open()) {
            try {
                ModelInfo::SceneInfo sceneInfo = m_modelLoader->loadModelInfo(originalPath);

                boost::archive::binary_oarchive archive(indexOut);
                archive << sceneInfo;

                fout << "模型索引保存成功: " << indexFile << std::endl;
                fout << "场景名称: " << sceneInfo.name << std::endl;
                fout << "网格数量: " << sceneInfo.meshInfos.size() << std::endl;
                fout << "材质数量: " << sceneInfo.materialInfos.size() << std::endl;
                fout << "纹理数量: " << sceneInfo.textureInfos.size() << std::endl;

            } catch (const std::exception& e) {
                fout << "保存模型索引失败: " << e.what() << std::endl;
            }
        } else {
            fout << "无法创建索引文件: " << indexFile << std::endl;
        }
    }

    void ModelManager::saveModelTimestamp(const std::filesystem::path& targetDir, const std::string& modelPath)
    {
        std::filesystem::path timestampFile = targetDir / "timestamp.dat";
        std::ofstream timestampOut(timestampFile, std::ios::binary);

        if (timestampOut.is_open()) {
            try {
                auto fileTime = std::filesystem::last_write_time(modelPath);

                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();

                boost::archive::binary_oarchive archive(timestampOut);
                archive << timestamp;
                archive << modelPath;

            } catch (const std::exception& e) {
                fout << "保存时间戳失败: " << e.what() << std::endl;
            }
        }
    }

    void ModelManager::render()
    {
        ImGui::Begin(TEXT("模型资产管理"));
        auto model = m_dataManager->getModelList();

        ImGui::Columns(2, "ModelManagerColumns");
        ImGui::SetColumnWidth(0, 350);

        ImGui::BeginChild(TEXT("模型列表"), ImVec2(0, 0), true);
        {
            std::filesystem::path modelsDir = "./res/models";
            if (std::filesystem::exists(modelsDir) && std::filesystem::is_directory(modelsDir)) {
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(modelsDir)) {
                        if (entry.is_directory()) {
                            std::string modelName = entry.path().filename().string();

                            if (ImGui::Selectable(modelName.c_str(), m_selectedModel == modelName)) {
                                m_selectedModel = modelName;
                                loadSelectedModelInfo(entry.path());
                            }

                            if (ImGui::BeginPopupContextItem()) {
                                if (ImGui::MenuItem(TEXT("删除模型"))) {
                                    try {
                                        std::filesystem::remove_all(entry.path());
                                        if (m_selectedModel == modelName) {
                                            m_selectedModel.clear();
                                            m_selectedModelInfo = ModelInfo::SceneInfo();
                                        }
                                    } catch (const std::filesystem::filesystem_error& e) {
                                        ShowErrorDialog("删除模型失败", e.what());
                                    }
                                }
                                if (ImGui::MenuItem(TEXT("在文件夹中显示"))) {
#ifdef _WIN32
                                    std::string command = "explorer \"" + entry.path().string() + "\"";
                                    system(command.c_str());
#endif
                                }
                                ImGui::EndPopup();
                            }
                        }
                    }
                } catch (const std::filesystem::filesystem_error& e) {
                    ImGui::Text("Error reading models directory: %s", e.what());
                }
            }
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        ImGui::BeginChild(TEXT("模型详情"), ImVec2(0, 0), true);
        {
            if (!m_selectedModel.empty()) {
                ImGui::Text(TEXT("模型: %s"), m_selectedModel.c_str());
                ImGui::Separator();

                if (!m_selectedModelInfo.name.empty() || !m_selectedModelInfo.meshInfos.empty()) {
                    ImGui::Text(TEXT("场景名称: %s"), m_selectedModelInfo.name.c_str());

                    if (ImGui::CollapsingHeader(TEXT("网格 (%zu)"), m_selectedModelInfo.meshInfos.size())) {
                        for (size_t i = 0; i < m_selectedModelInfo.meshInfos.size(); i++) {
                            const auto& meshInfo = m_selectedModelInfo.meshInfos[i];
                            if (ImGui::TreeNode((TEXT("网格 ") + std::to_string(i) + ": " + meshInfo.name).c_str())) {
                                ImGui::Text(TEXT("顶点数: %u"), meshInfo.vertexCount);
                                ImGui::Text(TEXT("索引数: %u"), meshInfo.indexCount);
                                ImGui::Text(TEXT("三角形数: %u"), meshInfo.triangleCount);
                                ImGui::Text(TEXT("包围盒: (%.2f,%.2f,%.2f) - (%.2f,%.2f,%.2f)"),
                                    meshInfo.boundingBoxMin.x, meshInfo.boundingBoxMin.y, meshInfo.boundingBoxMin.z,
                                    meshInfo.boundingBoxMax.x, meshInfo.boundingBoxMax.y, meshInfo.boundingBoxMax.z);

                                if (ImGui::Button((TEXT("加载网格 ") + meshInfo.name).c_str())) {
                                    // TODO: 加载这个网格到场景
                                    fout << "加载网格: " << meshInfo.name << std::endl;
                                }
                                ImGui::TreePop();
                            }
                        }
                    }

                    if (ImGui::CollapsingHeader(TEXT("纹理 (%zu)"), m_selectedModelInfo.textureInfos.size())) {
                        for (size_t i = 0; i < m_selectedModelInfo.textureInfos.size(); i++) {
                            const auto& textureInfo = m_selectedModelInfo.textureInfos[i];
                            if (ImGui::TreeNode((TEXT("纹理 ") + std::to_string(i)).c_str())) {
                                ImGui::Text(TEXT("路径: %s"), textureInfo.path.c_str());
                                ImGui::Text(TEXT("类型: %s"), textureInfo.isInner ? TEXT("内嵌") : TEXT("外部"));

                                if (ImGui::Button((TEXT("加载纹理 ") + std::to_string(i)).c_str())) {
                                    // TODO: 加载这个纹理
                                    fout << "加载纹理: " << textureInfo.path << std::endl;
                                }
                                ImGui::TreePop();
                            }
                        }
                    }

                    if (ImGui::CollapsingHeader(TEXT("材质 (%zu)"), m_selectedModelInfo.materialInfos.size())) {
                        for (size_t i = 0; i < m_selectedModelInfo.materialInfos.size(); i++) {
                            if (ImGui::TreeNode((TEXT("材质 ") + std::to_string(i)).c_str())) {
                                // TODO: 显示材质详细信息
                                if (ImGui::Button((TEXT("加载材质 ") + std::to_string(i)).c_str())) {
                                    // TODO: 加载这个材质
                                    fout << "加载材质: " << i << std::endl;
                                }
                                ImGui::TreePop();
                            }
                        }
                    }

                    ImGui::Separator();
                    if (ImGui::Button(TEXT("加载整个模型"))) {
                        // TODO: 加载整个模型到场景
                        fout << "加载整个模型: " << m_selectedModel << std::endl;
                    }
                } else {
                    ImGui::Text(TEXT("无法读取模型信息"));
                }
            } else {
                ImGui::Text(TEXT("请选择一个模型"));
            }
        }
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::End();
    }

    void ModelManager::loadSelectedModelInfo(const std::filesystem::path& modelDir)
    {
        std::filesystem::path indexFile = modelDir / "index.edit.dat";

        if (std::filesystem::exists(indexFile)) {
            try {
                std::ifstream indexIn(indexFile, std::ios::binary);
                if (indexIn.is_open()) {
                    boost::archive::binary_iarchive archive(indexIn);
                    archive >> m_selectedModelInfo;
                    fout << "成功加载模型信息: " << m_selectedModel << std::endl;
                }
            } catch (const std::exception& e) {
                fout << "加载模型信息失败: " << e.what() << std::endl;
                m_selectedModelInfo = ModelInfo::SceneInfo();
            }
        } else {
            fout << "模型索引文件不存在: " << indexFile << std::endl;
            m_selectedModelInfo = ModelInfo::SceneInfo();
        }
    }
}
