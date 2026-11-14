/**
 * @file AssetBrowserComposite.cpp
 * @brief 内容资产浏览器生命周期、工程双根目录扫描、外部 IDE 直连与 .mqmeta 侧车元数据管理实现
 */
#include "AssetBrowserComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/asset/loader/AsyncTextureUploader.h>
#include <MQEngine/host/core/Host.h>
#include <MQEngine/host/ui/UIHost.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kAssetBrowserWindowTitle = "内容资产浏览器 (Content Browser)";

            std::string toLowerAscii(std::string_view input)
            {
                std::string lowered(input);
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return lowered;
            }

            std::string trimTrailingSlash(std::string path)
            {
                while (path.size() > 3 && (path.back() == '/' || path.back() == '\\'))
                {
                    path.pop_back();
                }
                return path;
            }

            void importModelAssetSidecar(
                ResourceLoader* resLoader,
                const std::string& parsePath,
                const std::string& modelPkgDir,
                const std::string& fileName,
                std::string& outSummaryMsg,
                const std::function<void(const std::string&, const AssetMetadataSidecar&)>& saveSidecarFn)
            {
                auto* host = Host::Host::InstancePtr();
                if (!host || !host->getFctRuntime() || !resLoader || !resLoader->exists(parsePath))
                {
                    return;
                }

                FCT::ModelLoader* modelLoader = host->getFctRuntime()->createModelLoader();
                if (!modelLoader)
                {
                    return;
                }

                const std::string modelSourceDir = SAI::Path::join(modelPkgDir, "source");
                (void)resLoader->ensureDir(modelPkgDir);
                (void)resLoader->ensureDir(modelSourceDir);

                const auto depPaths = modelLoader->resolveModePaths(parsePath);
                for (const auto& dep : depPaths)
                {
                    if (resLoader->exists(dep))
                    {
                        const std::string depName = SAI::Path::fileName(dep);
                        (void)resLoader->copyFile(dep, SAI::Path::join(modelSourceDir, depName), true);
                    }
                }

                const FCT::ModelInfo::SceneInfo sceneInfo = modelLoader->loadModelInfo(parsePath);
                AssetMetadataSidecar sidecar;
                sidecar.sceneName = sceneInfo.name;
                sidecar.meshCount = static_cast<uint32_t>(sceneInfo.meshInfos.size());
                sidecar.materialCount = static_cast<uint32_t>(sceneInfo.materialInfos.size());
                sidecar.textureCount = static_cast<uint32_t>(sceneInfo.textureInfos.size());
                sidecar.boneCount = sceneInfo.boneCount;
                sidecar.animationCount = static_cast<uint32_t>(sceneInfo.animationNames.size());
                sidecar.hasSidecar = true;
                for (const auto& mi : sceneInfo.meshInfos)
                {
                    sidecar.totalVertices += mi.vertexCount;
                    sidecar.totalTriangles += mi.triangleCount;
                }
                saveSidecarFn(SAI::Path::join(modelPkgDir, "model.mqmeta"), sidecar);

                outSummaryMsg = "Assimp 模型解析导入成功: " + fileName +
                                " (网格:" + std::to_string(sidecar.meshCount) +
                                ", 顶点:" + std::to_string(sidecar.totalVertices) +
                                ", 三角面:" + std::to_string(sidecar.totalTriangles) + ")";

                FCT_DELETE(modelLoader);
            }

            void uploadImportedTextureAsync(
                EditorBase::EditorUiComposite* editorUi,
                const std::string& dstPath,
                const std::string& fileName)
            {
                auto* uploader = Asset::AsyncTextureUploader::InstancePtr();
                if (!uploader)
                {
                    return;
                }

                Asset::TextureUploadDesc uploadDesc;
                uploadDesc.filePath = dstPath;
                uploadDesc.debugName = fileName;
                uploadDesc.generateMips = true;
                (void)uploader->uploadTextureAsync(uploadDesc, [editorUi, fileName](StatusOr<Asset::TextureUploadResult> resOr) {
                    if (resOr.ok() && editorUi)
                    {
                        const auto& res = resOr.value();
                        editorUi->addLog(
                            EditorBase::LogEntry::Level::Info,
                            "FCT 纹理异步解码上传完成: " + fileName + " (" +
                                std::to_string(res.width) + "x" + std::to_string(res.height) +
                                ", " + std::to_string(res.channels) + " 通道)",
                            "TextureLoader");
                    }
                });
            }

            class AssetFileDropHandler : public FCT::EventHandler
            {
            public:
                explicit AssetFileDropHandler(AssetBrowserComposite* browser)
                    : m_browser(browser)
                {
                }

                void onResize(FCT::Window*, int, int) override {}
                void onMouseMove(FCT::Window*, int, int) override {}
                void onMouseWheel(FCT::Window*, int) override {}
                void onLButtonDown(FCT::Window*, int, int) override {}
                void onLButtonUp(FCT::Window*, int, int) override {}
                void onRButtonDown(FCT::Window*, int, int) override {}
                void onRButtonUp(FCT::Window*, int, int) override {}
                void onKeyDown(FCT::Window*, int) override {}
                void onKeyUp(FCT::Window*, int) override {}

                void onFileDrop(FCT::Window*, const std::vector<std::string>& files) override
                {
                    if (m_browser && !files.empty())
                    {
                        m_browser->enqueueDroppedFiles(files);
                    }
                }

            private:
                AssetBrowserComposite* m_browser = nullptr;
            };

            class AssetImportUndoCommand : public EditorBase::IUndoableCommand
            {
            public:
                AssetImportUndoCommand(
                    std::string name,
                    std::function<void()> doFn,
                    std::function<void()> undoFn)
                    : m_name(std::move(name))
                    , m_doFn(std::move(doFn))
                    , m_undoFn(std::move(undoFn))
                {
                }

                void execute() override
                {
                    if (m_doFn) m_doFn();
                }

                void undo() override
                {
                    if (m_undoFn) m_undoFn();
                }

                std::string getName() const override
                {
                    return m_name;
                }

            private:
                std::string m_name;
                std::function<void()> m_doFn;
                std::function<void()> m_undoFn;
            };
        } // namespace

        AssetBrowserComposite::AssetBrowserComposite() = default;

        AssetBrowserComposite::~AssetBrowserComposite()
        {
            onUnload();
        }

        Status AssetBrowserComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kAssetBrowserWindowTitle,
                    EditorBase::EditorDockSlot::Bottom,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderAssetBrowser();
                    });
            }

            m_projectRoot = resolveActiveProjectRoot();
            navigateTo(resolveInitialAssetDirectory(m_projectRoot));
            return OkStatus();
        }

        Status AssetBrowserComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));

            syncProjectRootIfChanged();

            if (auto* uiHost = Host::UIHost::InstancePtr())
            {
                if (auto* wnd = uiHost->getWindow())
                {
                    m_fileDropHandler = FCT_NEW(AssetFileDropHandler, this);
                    wnd->registerHandler(m_fileDropHandler);
                }
            }

            return OkStatus();
        }

        void AssetBrowserComposite::onTick(float deltaTime)
        {
            (void)deltaTime;

            syncProjectRootIfChanged();

            std::string filePath;
            while (m_pendingDroppedFiles.pop(filePath))
            {
                if (!filePath.empty())
                {
                    (void)importExternalFile(filePath, m_currentDirectory);
                }
            }
        }

        void AssetBrowserComposite::onUnload()
        {
            if (m_fileDropHandler)
            {
                if (auto* uiHost = Host::UIHost::InstancePtr())
                {
                    if (auto* wnd = uiHost->getWindow())
                    {
                        wnd->unregisterHandler(m_fileDropHandler);
                    }
                }
                m_fileDropHandler->release();
                m_fileDropHandler = nullptr;
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kAssetBrowserWindowTitle);
            }

            m_currentItems.clear();
            m_history.clear();
            m_projectRoot.clear();
            m_editorUiComposite = nullptr;

            Asset::Composite::onUnload();
        }

        std::string AssetBrowserComposite::resolveActiveProjectRoot() const
        {
            std::string candidate;
            if (m_editorUiComposite)
            {
                candidate = m_editorUiComposite->getActiveProjectDirectory();
            }
            if (candidate.empty())
            {
                if (auto* resMod = Resource::Resource::InstancePtr())
                {
                    candidate = resMod->getProjectDir();
                    if (candidate.empty() && resMod->getConfig())
                    {
                        candidate = resMod->getConfig()->getProjectDirectory();
                    }
                }
            }
            if (candidate.empty())
            {
                return ".";
            }
            return trimTrailingSlash(SAI::Path::lexicallyNormal(candidate));
        }

        std::string AssetBrowserComposite::resolveInitialAssetDirectory(const std::string& projectRoot) const
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;

            if (!projectRoot.empty() && projectRoot != ".")
            {
                const std::string projAssets = SAI::Path::join(projectRoot, "Assets");
                if (loader && loader->accessDir(projAssets).ok())
                {
                    return projAssets;
                }
                if (loader && loader->accessDir(projectRoot).ok())
                {
                    return projectRoot;
                }
            }
            return "./Assets";
        }

        std::string AssetBrowserComposite::resolveCurrentMainScenePath() const
        {
            if (m_editorUiComposite)
            {
                const auto& menuState = m_editorUiComposite->getUiContext().state<EditorBase::MainMenuViewState>();
                if (!menuState.mainScenePath.empty())
                {
                    return menuState.mainScenePath;
                }
            }
            if (auto* resMod = Resource::Resource::InstancePtr())
            {
                if (const auto* cfg = resMod->getConfig())
                {
                    const auto& proj = cfg->getCurrentProject();
                    if (!proj.startupScene.empty())
                    {
                        return proj.startupScene;
                    }
                    if (!proj.initialSceneUuid.empty())
                    {
                        return proj.initialSceneUuid;
                    }
                }
            }
            return "";
        }

        bool AssetBrowserComposite::isMainSceneAsset(const AssetEntry& item) const
        {
            if (item.isDirectory || (item.extension != ".mqscene" && item.extension != ".scene"))
            {
                return false;
            }
            const std::string mainScene = resolveCurrentMainScenePath();
            if (mainScene.empty())
            {
                return false;
            }

            const std::string projRoot = resolveActiveProjectRoot();
            std::string normMain = trimTrailingSlash(SAI::Path::lexicallyNormal(mainScene));
            if (normMain.size() >= 2 && normMain[0] == '.' && normMain[1] == '/')
            {
                normMain.erase(0, 2);
            }
            const std::string absMain = (projRoot != "." && normMain.find(':') == std::string::npos)
                ? trimTrailingSlash(SAI::Path::lexicallyNormal(SAI::Path::join(projRoot, normMain)))
                : normMain;

            std::string normItem = trimTrailingSlash(SAI::Path::lexicallyNormal(item.path));
            if (normItem.size() >= 2 && normItem[0] == '.' && normItem[1] == '/')
            {
                normItem.erase(0, 2);
            }
            return toLowerAscii(normItem) == toLowerAscii(absMain) ||
                   toLowerAscii(normItem) == toLowerAscii(normMain);
        }

        Status AssetBrowserComposite::setSceneAsProjectMain(const std::string& scenePath)
        {
            ZoneScoped();
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            auto* cfg = resMod ? resMod->getConfig() : nullptr;
            if (!cfg || !loader)
            {
                return FailedPreconditionError("Resource::Config 或 ResourceLoader 未就绪");
            }

            const std::string projRoot = resolveActiveProjectRoot();
            if (projRoot.empty() || projRoot == ".")
            {
                return FailedPreconditionError("当前未打开工程目录，无法写入 project.mq 主场景");
            }

            const std::string normScene = trimTrailingSlash(SAI::Path::lexicallyNormal(scenePath));
            std::string relScene = normScene;
            if (normScene.size() > projRoot.size() + 1)
            {
                const std::string prefix = toLowerAscii(projRoot) + "/";
                if (toLowerAscii(normScene.substr(0, prefix.size())) == prefix)
                {
                    relScene = normScene.substr(prefix.size());
                }
            }
            if (relScene.size() >= 2 && relScene[0] == '.' && relScene[1] == '/')
            {
                relScene.erase(0, 2);
            }

            const std::string projectFile = SAI::Path::join(projRoot, Resource::Config::kDefaultProjectFileName);
            StatusOr<ProjectConfig> loadedOr = cfg->loadProjectConfig(projectFile, loader);
            CHECK_STATUS(loadedOr.status());

            ProjectConfig updated = std::move(loadedOr.value());
            updated.startupScene = relScene;
            updated.initialSceneUuid = relScene;
            CHECK_STATUS(cfg->saveProjectConfig(projectFile, updated, loader));
            (void)cfg->activateProjectConfig(projectFile, loader);

            if (m_editorUiComposite)
            {
                auto& menuState = m_editorUiComposite->getUiContext().mutableState<EditorBase::MainMenuViewState>();
                menuState.mainScenePath = relScene;
                m_editorUiComposite->addLog(
                    EditorBase::LogEntry::Level::Info,
                    std::string(m_editorUiComposite->tr("已设置工程主场景: ", "Project main scene set to: ")) + relScene,
                    "Scene");
                m_editorUiComposite->showToast(
                    m_editorUiComposite->tr("主场景已更新", "Main Scene Updated"),
                    relScene,
                    EditorBase::LogEntry::Level::Info,
                    2.8f);
            }

            return OkStatus();
        }

        void AssetBrowserComposite::syncProjectRootIfChanged()
        {
            const std::string latestRoot = resolveActiveProjectRoot();
            if (!latestRoot.empty() && latestRoot != m_projectRoot)
            {
                m_projectRoot = latestRoot;
                m_history.clear();
                m_historyIndex = -1;
                navigateTo(resolveInitialAssetDirectory(m_projectRoot));
            }
        }

        void AssetBrowserComposite::enqueueDroppedFiles(const std::vector<std::string>& files)
        {
            for (const auto& f : files)
            {
                if (!f.empty())
                {
                    m_pendingDroppedFiles.push(f);
                }
            }
        }

        void AssetBrowserComposite::saveMetadataSidecar(const std::string& metaPath, const AssetMetadataSidecar& sidecar)
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* resLoader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!resLoader)
            {
                return;
            }

            std::ostringstream metaOss;
            metaOss << "# MQEngine Assimp Model Metadata\n"
                    << "sceneName=" << sidecar.sceneName << "\n"
                    << "meshCount=" << sidecar.meshCount << "\n"
                    << "materialCount=" << sidecar.materialCount << "\n"
                    << "textureCount=" << sidecar.textureCount << "\n"
                    << "boneCount=" << sidecar.boneCount << "\n"
                    << "animationCount=" << sidecar.animationCount << "\n"
                    << "totalVertices=" << sidecar.totalVertices << "\n"
                    << "totalTriangles=" << sidecar.totalTriangles << "\n";
            (void)resLoader->saveFile(metaPath, metaOss.str());
        }

        bool AssetBrowserComposite::importExternalFile(const std::string& srcFilePath, const std::string& targetDirectory)
        {
            if (srcFilePath.empty())
            {
                return false;
            }

            const std::string fileName = SAI::Path::fileName(srcFilePath);
            const std::string ext = SAI::Path::extensionLower(srcFilePath);
            if (fileName.empty())
            {
                return false;
            }

            const std::string stemName = SAI::Path::stem(srcFilePath);
            const std::string dstDir = targetDirectory.empty() ? m_currentDirectory : targetDirectory;
            const std::string dstPath = SAI::Path::join(dstDir, fileName);

            auto* resModule = Resource::Resource::InstancePtr();
            auto* loader = resModule ? resModule->getResourceLoader() : nullptr;
            if (!loader)
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Error, "导入失败: ResourceLoader 未就绪", "AssetImport");
                }
                return false;
            }

            const AssetCategory fileType = determineFileType(fileName, ext, false);
            std::string importSummaryMsg = "已成功导入资产: " + fileName + " -> " + dstPath;

            auto doImport = [this, srcFilePath, dstDir, dstPath, fileName, stemName, ext, fileType, &importSummaryMsg]() {
                auto* resMod = Resource::Resource::InstancePtr();
                auto* resLoader = resMod ? resMod->getResourceLoader() : nullptr;
                if (resLoader)
                {
                    (void)resLoader->ensureDir(dstDir);
                    if (srcFilePath != dstPath && resLoader->exists(srcFilePath))
                    {
                        (void)resLoader->copyFile(srcFilePath, dstPath, true);
                    }
                    else if (!resLoader->exists(dstPath))
                    {
                        (void)resLoader->saveFile(dstPath, "# MQEngine Imported Asset: " + fileName + "\n");
                    }
                }

                if (fileType == AssetCategory::Mesh)
                {
                    const std::string parsePath = (resLoader && resLoader->exists(srcFilePath)) ? srcFilePath : dstPath;
                    const std::string assetsBase = (!m_projectRoot.empty() && m_projectRoot != ".")
                        ? SAI::Path::join(m_projectRoot, "Assets/Models")
                        : std::string("./Assets/Models");
                    const std::string modelPkgDir = SAI::Path::join(assetsBase, stemName);
                    importModelAssetSidecar(
                        resLoader,
                        parsePath,
                        modelPkgDir,
                        fileName,
                        importSummaryMsg,
                        [this](const std::string& metaPath, const AssetMetadataSidecar& sidecar) {
                            saveMetadataSidecar(metaPath, sidecar);
                        });
                }
                else if (fileType == AssetCategory::Texture)
                {
                    uploadImportedTextureAsync(m_editorUiComposite, dstPath, fileName);
                }

                refreshDirectoryContents(m_currentDirectory);
                const bool foundInList = std::any_of(
                    m_currentItems.begin(),
                    m_currentItems.end(),
                    [&dstPath, &fileName](const AssetEntry& item) {
                        return item.path == dstPath || item.name == fileName;
                    });
                if (!foundInList && dstDir == m_currentDirectory)
                {
                    m_currentItems.push_back(AssetEntry{
                        .name = fileName,
                        .path = dstPath,
                        .extension = ext,
                        .type = fileType,
                        .sizeBytes = 4096,
                        .isDirectory = false
                    });
                }
                m_selectedItemPath = dstPath;
            };

            auto undoImport = [this, dstPath]() {
                auto* resMod = Resource::Resource::InstancePtr();
                auto* resLoader = resMod ? resMod->getResourceLoader() : nullptr;
                if (resLoader && resLoader->exists(dstPath))
                {
                    (void)resLoader->unlink(dstPath);
                }
                m_currentItems.erase(
                    std::remove_if(m_currentItems.begin(), m_currentItems.end(),
                                   [&dstPath](const AssetEntry& item) { return item.path == dstPath; }),
                    m_currentItems.end());
                refreshDirectoryContents(m_currentDirectory);
            };

            if (m_editorUiComposite)
            {
                m_editorUiComposite->getCommandStack().executeCommand(
                    makeUnique<AssetImportUndoCommand>(
                        std::string(m_editorUiComposite->tr("导入资产: ", "Import Asset: ")) + fileName,
                        doImport,
                        undoImport));
                m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Info, importSummaryMsg, "AssetImport");
            }
            else
            {
                doImport();
            }

            return true;
        }

        AssetCategory AssetBrowserComposite::determineFileType(const std::string& fileName, const std::string& ext, bool isDir) const
        {
            if (isDir)
            {
                return AssetCategory::Directory;
            }

            const std::string lowerExt = toLowerAscii(ext);
            const std::string lowerName = toLowerAscii(fileName);

            if (lowerName == "cmakelists.txt" ||
                lowerExt == ".cpp" || lowerExt == ".cc" || lowerExt == ".cxx" || lowerExt == ".c" ||
                lowerExt == ".h" || lowerExt == ".hpp" || lowerExt == ".inl" || lowerExt == ".cmake")
            {
                return AssetCategory::SourceCode;
            }

            if (lowerExt == ".toml" || lowerExt == ".mq" || lowerExt == ".json" || lowerExt == ".ini")
            {
                return AssetCategory::Config;
            }

            if (lowerExt == ".obj" || lowerExt == ".fbx" || lowerExt == ".gltf" || lowerExt == ".glb" || lowerExt == ".dae" ||
                lowerExt == ".3ds" || lowerExt == ".ply" || lowerExt == ".stl" || lowerExt == ".pmd" || lowerExt == ".pmx" || lowerExt == ".mesh")
            {
                return AssetCategory::Mesh;
            }

            if (lowerExt == ".png" || lowerExt == ".jpg" || lowerExt == ".jpeg" || lowerExt == ".tga" || lowerExt == ".bmp" ||
                lowerExt == ".hdr" || lowerExt == ".exr" || lowerExt == ".dds" || lowerExt == ".ktx" || lowerExt == ".ktx2" || lowerExt == ".webp" ||
                (Resource::TextureDecoderRegistry::InstancePtr() && Resource::TextureDecoderRegistry::InstancePtr()->isExtensionSupported(lowerExt)))
            {
                return AssetCategory::Texture;
            }

            if (lowerExt == ".mat" || lowerExt == ".material")
            {
                return AssetCategory::Material;
            }

            if (lowerExt == ".hlsl" || lowerExt == ".hlsli" || lowerExt == ".glsl" || lowerExt == ".shader" ||
                lowerExt == ".comp" || lowerExt == ".mqshader" || lowerExt == ".sgedt")
            {
                return AssetCategory::Shader;
            }

            if (lowerExt == ".js" || lowerExt == ".ts" || lowerExt == ".py" || lowerExt == ".lua" ||
                lowerExt == ".rml" || lowerExt == ".rcss")
            {
                return AssetCategory::Script;
            }

            if (lowerExt == ".scene" || lowerExt == ".mqscene")
            {
                return AssetCategory::Scene;
            }

            return AssetCategory::Unknown;
        }

        const char* AssetBrowserComposite::getFileTypeIcon(AssetCategory type) const
        {
            switch (type)
            {
            case AssetCategory::Directory:  return "[DIR]";
            case AssetCategory::Mesh:       return "[MESH]";
            case AssetCategory::Texture:    return "[TEX]";
            case AssetCategory::Material:   return "[MAT]";
            case AssetCategory::Shader:     return "[SHD]";
            case AssetCategory::Script:     return "[TS/UI]";
            case AssetCategory::SourceCode: return "[C++]";
            case AssetCategory::Config:     return "[CFG]";
            case AssetCategory::Scene:      return "[SCN]";
            default:                        return "[FILE]";
            }
        }

        bool AssetBrowserComposite::isDirectCodeEditorAsset(const AssetEntry& item) const
        {
            if (item.isDirectory)
            {
                return false;
            }
            if (toLowerAscii(item.name) == "cmakelists.txt")
            {
                return true;
            }
            return SAI::Path::hasAnyExtension(item.name, {
                ".cpp", ".cc", ".cxx", ".c",
                ".h", ".hpp", ".inl",
                ".ts", ".js",
                ".cmake", ".toml", ".mq"
            });
        }

        bool AssetBrowserComposite::canOpenInCodeEditor(const AssetEntry& item) const
        {
            if (item.isDirectory)
            {
                return false;
            }
            return isDirectCodeEditorAsset(item) ||
                   EditorBase::EditorUiComposite::isCodeEditorSourceFile(item.path);
        }

        std::string AssetBrowserComposite::buildOpenFileInIdeLabel() const
        {
            const std::string ideName = m_editorUiComposite ? m_editorUiComposite->getActiveCodeEditorDisplayName() : "";
            if (!ideName.empty())
            {
                return m_editorUiComposite->getLanguage() == EditorBase::EditorLanguage::EnUS
                    ? ("Open in " + ideName)
                    : ("使用代码编辑器打开 (Open in " + ideName + ")");
            }
            return m_editorUiComposite
                ? m_editorUiComposite->tr("使用代码编辑器打开 (Open in CLion / Rider)", "Open in Code Editor (CLion / Rider)")
                : "使用代码编辑器打开 (Open in CLion / Rider)";
        }

        std::string AssetBrowserComposite::buildOpenProjectInIdeLabel() const
        {
            const std::string ideName = m_editorUiComposite ? m_editorUiComposite->getActiveCodeEditorDisplayName() : "";
            if (!ideName.empty())
            {
                return m_editorUiComposite->getLanguage() == EditorBase::EditorLanguage::EnUS
                    ? ("Open C++ Project in " + ideName)
                    : ("在代码编辑器中打开 C++ 工程 (" + ideName + ")");
            }
            return m_editorUiComposite
                ? m_editorUiComposite->tr("在代码编辑器中打开 C++ 工程", "Open C++ Project in Code Editor")
                : "在代码编辑器中打开 C++ 工程";
        }

        void AssetBrowserComposite::openFileInExternalCodeEditor(const std::string& filePath, int line)
        {
            if (!m_editorUiComposite || filePath.empty())
            {
                return;
            }
            (void)m_editorUiComposite->openFileInCodeEditor(filePath, line, 0, true);
        }

        void AssetBrowserComposite::openProjectInExternalCodeEditor(const std::string& targetProjectDir)
        {
            if (!m_editorUiComposite)
            {
                return;
            }
            const std::string resolvedDir = targetProjectDir.empty() ? resolveActiveProjectRoot() : targetProjectDir;
            (void)m_editorUiComposite->openProjectInCodeEditor(resolvedDir == "." ? "" : resolvedDir, true);
        }

        void AssetBrowserComposite::navigateTo(const std::string& directoryPath)
        {
            const std::string normalizedDir = trimTrailingSlash(SAI::Path::lexicallyNormal(directoryPath));
            m_currentDirectory = normalizedDir.empty() ? directoryPath : normalizedDir;
            refreshDirectoryContents(m_currentDirectory);

            if (m_historyIndex < 0 ||
                m_historyIndex >= static_cast<int>(m_history.size()) ||
                m_history[m_historyIndex] != m_currentDirectory)
            {
                if (m_historyIndex + 1 < static_cast<int>(m_history.size()))
                {
                    m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
                }
                m_history.push_back(m_currentDirectory);
                m_historyIndex = static_cast<int>(m_history.size()) - 1;
            }
        }

        void AssetBrowserComposite::navigateUp()
        {
            const std::string parentDir = trimTrailingSlash(SAI::Path::parentPath(m_currentDirectory));
            if (parentDir.empty() || parentDir == m_currentDirectory)
            {
                return;
            }

            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader || loader->accessDir(parentDir).ok())
            {
                navigateTo(parentDir);
            }
        }

        void AssetBrowserComposite::refreshDirectoryContents(const std::string& directoryPath)
        {
            ZoneScoped();
            m_currentItems.clear();

            auto* resModule = Resource::Resource::InstancePtr();
            auto* loader = resModule ? resModule->getResourceLoader() : nullptr;
            const bool dirExistsOnDisk = (loader && loader->accessDir(directoryPath).ok());

            if (dirExistsOnDisk)
            {
                auto dirRes = loader->readDir(directoryPath);
                if (dirRes.ok())
                {
                    for (const auto& file : dirRes.value())
                    {
                        const std::string normPath = trimTrailingSlash(SAI::Path::lexicallyNormal(file->path()));
                        const std::string entryName = SAI::Path::fileName(normPath);
                        if (entryName.empty() || entryName == ".git")
                        {
                            continue;
                        }

                        AssetEntry item;
                        item.name = entryName;
                        item.path = normPath;
                        item.isDirectory = loader->accessDir(normPath).ok();
                        item.extension = item.isDirectory ? std::string{} : SAI::Path::extensionLower(entryName);
                        item.type = determineFileType(item.name, item.extension, item.isDirectory);
                        m_currentItems.push_back(std::move(item));
                    }

                    std::sort(m_currentItems.begin(), m_currentItems.end(), [](const AssetEntry& a, const AssetEntry& b) {
                        if (a.isDirectory != b.isDirectory)
                        {
                            return a.isDirectory > b.isDirectory;
                        }
                        return toLowerAscii(a.name) < toLowerAscii(b.name);
                    });
                }
            }
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::AssetBrowserComposite)
