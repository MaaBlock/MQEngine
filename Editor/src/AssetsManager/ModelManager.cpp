//
// Created by Administrator on 2025/8/23.
//

#include "ModelManager.h"
#include "g_editorIconMesh.h"
#include "g_editorIconMaterial.h"
#include "g_editorIconTexture.h"
#define TEXT(str) (const char*)u8##str
using namespace FCT;
namespace MQEngine
{
    ModelManager::ModelManager(DataManager* dataManager)
    {
        m_dataManager = dataManager;
        m_modelLoader = g_editorGlobal.rt->createModelLoader();
        m_supportedExtensions = m_modelLoader->getSupportedExtensions();
        g_editorGlobal.wnd->getCallBack()->addFileDropCallback([this](Window*,const std::vector<std::string>& files)
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
        m_ctx = g_editorGlobal.ctx;
        m_meshIcon = m_ctx->loadTexture(g_editorIconMesh,g_editorIconMeshSize);
        m_textureIcon = m_ctx->loadTexture(g_editorIconTexture,g_editorIconTextureSize);
        m_materialIcon = m_ctx->loadTexture(g_editorIconMaterial,g_editorIconMaterialSize);
        g_editorGlobal.imguiContext->addTexture("ModelManager_Icon_Mesh", m_meshIcon);
        g_editorGlobal.imguiContext->addTexture("ModelManager_Icon_Texture", m_textureIcon);
        g_editorGlobal.imguiContext->addTexture("ModelManager_Icon_Material", m_materialIcon);

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
        generateUuidFile(targetDir, originalPath);
    }

    void ModelManager::generateUuidFile(const std::filesystem::path& targetDir, const std::string& originalPath)
    {
        try {
            static boost::uuids::random_generator uuidGen;
            boost::uuids::uuid modelUuid = uuidGen();
            std::string uuidString = boost::uuids::to_string(modelUuid);

            std::filesystem::path originalFilePath(originalPath);
            std::string originalFileName = originalFilePath.filename().string();

            std::string modelRelativePath = "source/" + originalFileName;

            MQEngine::ModelUuidFile modelUuidFile;
            modelUuidFile.uuid = uuidString;
            modelUuidFile.modelRelativePath = modelRelativePath;

            std::filesystem::path uuidFile = targetDir / "model.uuid";
            std::ofstream uuidOut(uuidFile, std::ios::binary);

            if (uuidOut.is_open()) {
                boost::archive::binary_oarchive archive(uuidOut);
                archive << modelUuidFile;

                fout << "模型 UUID 文件保存成功: " << uuidFile << std::endl;
                fout << "模型 UUID: " << uuidString << std::endl;
                fout << "模型相对路径: " << modelRelativePath << std::endl;

                std::string modelDirName = targetDir.filename().string();
                std::string dataManagerPath = "models/" + modelDirName;

            } else {
                fout << "无法创建 UUID 文件: " << uuidFile << std::endl;
            }

        } catch (const std::exception& e) {
            fout << "生成 UUID 文件失败: " << e.what() << std::endl;
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
        ImGui::Begin("模型资产管理");
        auto modelPaths = m_dataManager->getModelList();

        ImGui::Columns(2, "ModelManagerColumns");
        ImGui::SetColumnWidth(0, 350);

        ImGui::BeginChild("模型列表", ImVec2(0, 0), true);
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
                                if (ImGui::MenuItem("删除模型")) {
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
                                if (ImGui::MenuItem("在文件夹中显示")) {
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


        // 在render()函数中的模型详情部分，替换整个详情显示逻辑：

        ImGui::BeginChild("模型详情", ImVec2(0, 0), true);
        {
            if (!m_selectedModel.empty()) {
                ImGui::Text("模型: %s", m_selectedModel.c_str());
                ImGui::Separator();

                if (!m_selectedModelInfo.name.empty() || !m_selectedModelInfo.meshInfos.empty()) {
                    ImGui::Text("场景名称: %s", m_selectedModelInfo.name.c_str());
                    ImGui::Separator();

                    // 获取图标
                    ImTextureID meshIconID = g_editorGlobal.imguiContext->getTexture("ModelManager_Icon_Mesh");
                    ImTextureID textureIconID = g_editorGlobal.imguiContext->getTexture("ModelManager_Icon_Texture");
                    ImTextureID materialIconID = g_editorGlobal.imguiContext->getTexture("ModelManager_Icon_Material");

                    const float iconSize = 64.0f;
                    const float iconSpacing = 10.0f;
                    const float itemWidth = iconSize + iconSpacing;

                    // 计算每行可以放多少个图标
                    float windowWidth = ImGui::GetContentRegionAvail().x;
                    int itemsPerRow = std::max(1, (int)(windowWidth / itemWidth));

                    // 网格部分
                    if (!m_selectedModelInfo.meshInfos.empty()) {
                        ImGui::Text("网格 (%zu)", m_selectedModelInfo.meshInfos.size());
                        ImGui::Separator();

                        for (size_t i = 0; i < m_selectedModelInfo.meshInfos.size(); i++) {
                            const auto& meshInfo = m_selectedModelInfo.meshInfos[i];

                            // 计算当前项在行中的位置
                            if (i > 0 && (i % itemsPerRow) != 0) {
                                ImGui::SameLine();
                            }

                            ImGui::BeginGroup();
                            {
                                // 图标按钮，支持拖拽
                                ImGui::PushID((int)i);
                                if (ImGui::ImageButton(("mesh_" + std::to_string(i)).c_str(), meshIconID, ImVec2(iconSize, iconSize))) {
                                    // 点击事件
                                    fout << "选中网格: " << meshInfo.name << std::endl;
                                }

                                // 拖拽源
                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                    std::string modelUuid = getModelUuid(m_selectedModel);
                                    std::string dragData = modelUuid + "|" + meshInfo.name;
                                    ImGui::SetDragDropPayload("ASSET_MESH_FROM_MODEL", dragData.c_str(), dragData.size() + 1);

                                    ImGui::Image(meshIconID, ImVec2(32, 32));
                                    ImGui::SameLine();
                                    ImGui::Text("%s", meshInfo.name.c_str());

                                    ImGui::EndDragDropSource();
                                }

                                // 显示名称（截断过长的名称）
                                std::string displayName = meshInfo.name;
                                if (displayName.length() > 10) {
                                    displayName = displayName.substr(0, 10) + "...";
                                }

                                // 居中显示文字
                                float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
                                float centerOffset = (iconSize - textWidth) * 0.5f;
                                if (centerOffset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
                                ImGui::Text("%s", displayName.c_str());

                                ImGui::PopID();
                            }
                            ImGui::EndGroup();

                            // 悬停提示
                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::Text("网格: %s", meshInfo.name.c_str());
                                ImGui::Text("顶点数: %u", meshInfo.vertexCount);
                                ImGui::Text("三角形数: %u", meshInfo.triangleCount);
                                ImGui::EndTooltip();
                            }
                        }
                        ImGui::Spacing();
                        ImGui::Separator();
                    }

                    if (!m_selectedModelInfo.textureInfos.empty()) {
                        ImGui::Text("纹理 (%zu)", m_selectedModelInfo.textureInfos.size());
                        ImGui::Separator();

                        for (size_t i = 0; i < m_selectedModelInfo.textureInfos.size(); i++) {
                            const auto& textureInfo = m_selectedModelInfo.textureInfos[i];

                            if (i > 0 && (i % itemsPerRow) != 0) {
                                ImGui::SameLine();
                            }

                            ImGui::BeginGroup();
                            {
                                ImGui::PushID((int)(1000 + i));
                                if (ImGui::ImageButton(("texture_" + std::to_string(i)).c_str(), textureIconID, ImVec2(iconSize, iconSize))) {
                                    fout << "选中纹理: " << textureInfo.path << std::endl;
                                }

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                    std::string modelUuid = getModelUuid(m_selectedModel);
                                    std::string dragData = modelUuid + "|" + textureInfo.path;
                                    ImGui::SetDragDropPayload("ASSET_TEXTURE_FROM_MODEL", dragData.c_str(), dragData.size() + 1);

                                    ImGui::Image(textureIconID, ImVec2(32, 32));
                                    ImGui::SameLine();
                                    ImGui::Text("%s", std::filesystem::path(textureInfo.path).filename().string().c_str());

                                    ImGui::EndDragDropSource();
                                }

                                std::string fileName = std::filesystem::path(textureInfo.path).filename().string();
                                if (fileName.length() > 10) {
                                    fileName = fileName.substr(0, 10) + "...";
                                }

                                float textWidth = ImGui::CalcTextSize(fileName.c_str()).x;
                                float centerOffset = (iconSize - textWidth) * 0.5f;
                                if (centerOffset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
                                ImGui::Text("%s", fileName.c_str());

                                ImGui::PopID();
                            }
                            ImGui::EndGroup();

                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::Text("纹理: %s", textureInfo.path.c_str());
                                ImGui::Text("类型: %s", textureInfo.isInner ? "内嵌" : "外部");
                                ImGui::EndTooltip();
                            }
                        }
                        ImGui::Spacing();
                        ImGui::Separator();
                    }

                    // 材质部分
                    if (!m_selectedModelInfo.materialInfos.empty()) {
                        ImGui::Text("材质 (%zu)", m_selectedModelInfo.materialInfos.size());
                        ImGui::Separator();

                        for (size_t i = 0; i < m_selectedModelInfo.materialInfos.size(); i++) {
                            if (i > 0 && (i % itemsPerRow) != 0) {
                                ImGui::SameLine();
                            }

                            ImGui::BeginGroup();
                            {
                                ImGui::PushID((int)(2000 + i));
                                if (ImGui::ImageButton(("material_" + std::to_string(i)).c_str(), materialIconID, ImVec2(iconSize, iconSize))) {
                                    fout << "选中材质: " << i << std::endl;
                                }

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                    std::string modelUuid = getModelUuid(m_selectedModel);
                                    std::string dragData = modelUuid + "|" + std::to_string(i);
                                    ImGui::SetDragDropPayload("ASSET_MATERIAL_FROM_MODEL", dragData.c_str(), dragData.size() + 1);

                                    ImGui::Image(materialIconID, ImVec2(32, 32));
                                    ImGui::SameLine();
                                    ImGui::Text("材质 %zu", i);

                                    ImGui::EndDragDropSource();
                                }

                                std::string materialName = "材质 " + std::to_string(i);
                                float textWidth = ImGui::CalcTextSize(materialName.c_str()).x;
                                float centerOffset = (iconSize - textWidth) * 0.5f;
                                if (centerOffset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
                                ImGui::Text("%s", materialName.c_str());

                                ImGui::PopID();
                            }
                            ImGui::EndGroup();

                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::Text("材质 %zu", i);
                                // TODO: 显示更多材质信息
                                ImGui::EndTooltip();
                            }
                        }
                    }
                } else {
                    ImGui::Text("无法读取模型信息");
                }
            } else {
                ImGui::Text("请选择一个模型");
            }
        }
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::End();
    }
    std::string ModelManager::getModelUuid(const std::string& modelName) {
        std::filesystem::path modelDir = std::filesystem::path("./res/models") / modelName;
        std::filesystem::path uuidFile = modelDir / "model.uuid";

        if (std::filesystem::exists(uuidFile)) {
            try {
                std::ifstream uuidIn(uuidFile, std::ios::binary);
                if (uuidIn.is_open()) {
                    MQEngine::ModelUuidFile modelUuidFile;
                    boost::archive::binary_iarchive archive(uuidIn);
                    archive >> modelUuidFile;
                    return modelUuidFile.uuid;
                }
            } catch (const std::exception& e) {
                fout << "读取模型UUID失败: " << e.what() << std::endl;
            }
        }
        return "";
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
