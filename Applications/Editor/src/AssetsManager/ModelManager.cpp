//
// Created by Administrator on 2025/8/23.
//

#include "ModelManager.h"
#include "g_editorIconMesh.h"
#include "g_editorIconMaterial.h"
#include "g_editorIconTexture.h"
#include "../thirdparty/thirdparty.h"
#define TEXT(str) (const char*)u8##str
using namespace FCT;
namespace MQEngine
{
    ModelManager::ModelManager()
    {
    }

    void ModelManager::init()
    {
        m_dataManager = g_editorGlobal.dataManager;
        m_modelLoader = g_editorGlobal.rt->createModelLoader();
        m_supportedExtensions = m_modelLoader->getSupportedExtensions();
        g_editorGlobal.wnd->getCallBack()->addFileDropCallback([this](Window*,const std::vector<std::string>& files)
        {
            for (const auto& file : files) {
                std::string extension = SAI::Path::extension(file);
                fout << "加载了文件: " << file << std::endl;
                if (SAI::Path::hasAnyExtension(file, {".tscn", ".gd"})) {
                    if (auto* engine = Runtime::Engine::getInstance(); engine && engine->script->scriptSystem) {
                        (void)Runtime::Runtime::instance().loadPlugin("GodotImporter");
                        if (auto* pyEnv = engine->script->scriptSystem->getPyEnvironment()) {
                            try {
                                if (SAI::Path::hasExtension(file, ".gd")) {
                                    (void)pyEnv->callFunction<std::string>("importGdScriptFile", SAI::Path::normalize(file), std::string(""));
                                } else {
                                    (void)pyEnv->callFunction<int>("importGodotScene", SAI::Path::normalize(file), std::string(""));
                                }
                            } catch (const std::exception& e) {
                                spdlog::error("拖拽导入 Godot 文件失败: {}", e.what());
                            }
                        }
                    }
                } else if (m_supportedExtensions.count(extension)) {
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

    ModelManager::~ModelManager()
    {
        
    }

    void ModelManager::importModel(const std::string& modelPath)
    {
        std::string modelBaseName = SAI::Path::stem(modelPath);
        std::string targetDirName = SAI::FileSystem::getUniqueDirectoryName("./Assets/Models", modelBaseName);
        std::string targetDir = SAI::Path::join("./Assets/Models", targetDirName);
        (void)SAI::FileSystem::createDirectories(targetDir);

        std::set<std::string> allModelPaths = m_modelLoader->resolveModePaths(modelPath);
        std::string sourceBaseDir = SAI::Path::parentPath(modelPath);
        std::string modelSubDir = SAI::Path::join(targetDir, "source");
        (void)SAI::FileSystem::createDirectories(modelSubDir);

        for (const auto& filePath : allModelPaths) {
            if (SAI::FileSystem::exists(filePath)) {
                (void)SAI::FileSystem::copyFilePreservingRelative(filePath, sourceBaseDir, modelSubDir);
            }
        }
        saveModelIndex(targetDir, modelBaseName, modelPath);
        saveModelTimestamp(targetDir, modelPath);
    }

    void ModelManager::saveModelIndex(const std::string& targetDir, const std::string& modelBaseName, const std::string& originalPath)
    {
        (void)modelBaseName;
        std::string indexFile = SAI::Path::join(targetDir, "index.edit.dat");

        try {
            ModelInfo::SceneInfo sceneInfo = m_modelLoader->loadModelInfo(originalPath);
            auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
            Status st = ModelAssetSerializer::saveModelIndex(loader, indexFile, sceneInfo);
            if (st.ok()) {
                fout << "模型索引保存成功: " << indexFile << std::endl;
                fout << "场景名称: " << sceneInfo.name << std::endl;
                fout << "网格数量: " << sceneInfo.meshInfos.size() << std::endl;
                fout << "材质数量: " << sceneInfo.materialInfos.size() << std::endl;
                fout << "纹理数量: " << sceneInfo.textureInfos.size() << std::endl;
            } else {
                fout << "保存模型索引失败: " << st.message() << std::endl;
            }
        } catch (const std::exception& e) {
            fout << "保存模型索引失败: " << e.what() << std::endl;
        }
        generateUuidFile(targetDir, originalPath);
    }

    void ModelManager::generateUuidFile(const std::string& targetDir, const std::string& originalPath)
    {
        try {
            static boost::uuids::random_generator uuidGen;
            boost::uuids::uuid modelUuid = uuidGen();
            std::string uuidString = boost::uuids::to_string(modelUuid);

            std::string originalFileName = SAI::Path::fileName(originalPath);
            std::string modelRelativePath = SAI::Path::join("source", originalFileName);

            MQEngine::ModelUuidFile modelUuidFile;
            modelUuidFile.uuid = uuidString;
            modelUuidFile.modelRelativePath = modelRelativePath;

            std::string uuidFile = SAI::Path::join(targetDir, "model.uuid");
            auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
            Status st = ModelAssetSerializer::saveModelUuid(loader, uuidFile, modelUuidFile);
            if (st.ok()) {
                fout << "模型 UUID 文件保存成功: " << uuidFile << std::endl;
                fout << "模型 UUID: " << uuidString << std::endl;
                fout << "模型相对路径: " << modelRelativePath << std::endl;
            } else {
                fout << "无法创建 UUID 文件: " << st.message() << std::endl;
            }
        } catch (const std::exception& e) {
            fout << "生成 UUID 文件失败: " << e.what() << std::endl;
        }
    }

    void ModelManager::saveModelTimestamp(const std::string& targetDir, const std::string& modelPath)
    {
        std::string timestampFile = SAI::Path::join(targetDir, "timestamp.dat");
        int64_t timestamp = SAI::FileSystem::getLastWriteTimeSeconds(modelPath);
        auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
        Status st = ModelAssetSerializer::saveModelTimestamp(loader, timestampFile, timestamp, modelPath);
        if (!st.ok()) {
            fout << "保存时间戳失败: " << st.message() << std::endl;
        }
    }

    void ModelManager::render()
    {
        auto modelPaths = m_dataManager->getModelList();

        ImGui::Columns(2, "ModelManagerColumns");
        ImGui::SetColumnWidth(0, 350);

        ImGui::BeginChild("模型列表", ImVec2(0, 0), true);
        {
            std::string modelsDir = "./Assets/Models";
            for (const auto& modelDirPath : SAI::FileSystem::listSubDirectories(modelsDir)) {
                std::string modelName = SAI::Path::fileName(modelDirPath);
                if (m_selectedModel.empty()) {
                    m_selectedModel = modelName;
                    loadSelectedModelInfo(modelDirPath);
                }

                if (ImGui::Selectable(modelName.c_str(), m_selectedModel == modelName)) {
                    m_selectedModel = modelName;
                    loadSelectedModelInfo(modelDirPath);
                }

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    std::string modelUuid = getModelUuid(modelName);
                    std::string dragData = modelUuid + "|" + modelName;
                    ImGui::SetDragDropPayload("ASSET_MODEL", dragData.c_str(), dragData.size() + 1);
                    ImGui::Text(TEXT("模型实例: %s"), modelName.c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("删除模型")) {
                        if (SAI::FileSystem::removeAll(modelDirPath).ok()) {
                            if (m_selectedModel == modelName) {
                                m_selectedModel.clear();
                                m_selectedModelInfo = ModelInfo::SceneInfo();
                            }
                        } else {
                            ShowErrorDialog("删除模型失败", modelDirPath);
                        }
                    }
                    if (ImGui::MenuItem("在文件夹中显示")) {
#ifdef _WIN32
                        std::string command = "explorer \"" + modelDirPath + "\"";
                        system(command.c_str());
#endif
                    }
                    ImGui::EndPopup();
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
                    if (m_selectedModelInfo.boneCount > 0 || !m_selectedModelInfo.animationNames.empty()) {
                        ImGui::Text("骨骼数: %u | 动画片段数: %zu", m_selectedModelInfo.boneCount, m_selectedModelInfo.animationNames.size());
                        for (size_t animIdx = 0; animIdx < m_selectedModelInfo.animationNames.size(); ++animIdx) {
                            ImGui::BulletText("%s", m_selectedModelInfo.animationNames[animIdx].c_str());
                        }
                        ImGui::Separator();
                    }
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
                                if (ImGui::ImageButton(("mesh_" + std::to_string(i)).c_str(), ImTextureRef(meshIconID), ImVec2(iconSize, iconSize))) {
                                    // 点击事件
                                    fout << "选中网格: " << meshInfo.name << std::endl;
                                }

                                // 拖拽源
                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                    std::string modelUuid = getModelUuid(m_selectedModel);
                                    std::string dragData = modelUuid + "|" + meshInfo.name;
                                    ImGui::SetDragDropPayload("ASSET_MESH_FROM_MODEL", dragData.c_str(), dragData.size() + 1);

                                    ImGui::Image(ImTextureRef(meshIconID), ImVec2(32, 32));
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
                                if (meshInfo.hasBones) {
                                    ImGui::Text("骨骼蒙皮: 是 (%u 根骨骼)", meshInfo.boneCount);
                                }
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
                                if (ImGui::ImageButton(("texture_" + std::to_string(i)).c_str(), ImTextureRef(textureIconID), ImVec2(iconSize, iconSize))) {
                                    fout << "选中纹理: " << textureInfo.path << std::endl;
                                }

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                    std::string modelUuid = getModelUuid(m_selectedModel);
                                    std::string dragData = modelUuid + "|" + textureInfo.path;
                                    ImGui::SetDragDropPayload("ASSET_TEXTURE_FROM_MODEL", dragData.c_str(), dragData.size() + 1);

                                    ImGui::Image(ImTextureRef(textureIconID), ImVec2(32, 32));
                                    ImGui::SameLine();
                                    ImGui::Text("%s", SAI::Path::fileName(textureInfo.path).c_str());

                                    ImGui::EndDragDropSource();
                                }

                                std::string fileName = SAI::Path::fileName(textureInfo.path);
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
                                ImGui::Text("存储: %s", textureInfo.isInner ? "内嵌" : "外部");
                                
                                // 显示多个纹理类型
                                if (!textureInfo.types.empty()) {
                                    std::string typesStr = "类型: ";
                                    bool first = true;
                                    for (const auto& type : textureInfo.types) {
                                        if (!first) {
                                            typesStr += ", ";
                                        }
                                        typesStr += textureTypeToChineseString(type);
                                        first = false;
                                    }
                                    ImGui::Text("%s", typesStr.c_str());
                                } else {
                                    ImGui::Text("类型: 未知");
                                }
                                
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
                                if (ImGui::ImageButton(("material_" + std::to_string(i)).c_str(), ImTextureRef(materialIconID), ImVec2(iconSize, iconSize))) {
                                    fout << "选中材质: " << i << std::endl;
                                }

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                    std::string modelUuid = getModelUuid(m_selectedModel);
                                    std::string dragData = modelUuid + "|" + std::to_string(i);
                                    ImGui::SetDragDropPayload("ASSET_MATERIAL_FROM_MODEL", dragData.c_str(), dragData.size() + 1);

                                    ImGui::Image(ImTextureRef(materialIconID), ImVec2(32, 32));
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
    }
    std::string ModelManager::getModelUuid(const std::string& modelName) {
        std::string uuidFile = SAI::Path::join("./Assets/Models", modelName, "model.uuid");
        auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
        auto uuidOr = ModelAssetSerializer::loadModelUuid(loader, uuidFile);
        if (uuidOr.ok()) {
            return uuidOr->uuid;
        }
        fout << "读取模型UUID失败: " << uuidOr.status().message() << std::endl;
        return "";
    }

    void ModelManager::loadSelectedModelInfo(const std::string& modelDir)
    {
        std::string indexFile = SAI::Path::join(modelDir, "index.edit.dat");
        auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
        auto sceneInfoOr = ModelAssetSerializer::loadModelIndex(loader, indexFile);
        if (sceneInfoOr.ok()) {
            m_selectedModelInfo = std::move(sceneInfoOr.value());
            fout << "成功加载模型信息: " << m_selectedModel << std::endl;
        } else {
            fout << "加载模型信息失败: " << sceneInfoOr.status().message() << std::endl;
            m_selectedModelInfo = ModelInfo::SceneInfo();
        }
    }
}
