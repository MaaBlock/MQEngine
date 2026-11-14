/**
 * @file AssetBrowserComposite.h
 * @brief 内容资产浏览器 Composite
 */
#ifndef MQENGINE_MQ_EDITOR_ASSET_BROWSER_COMPOSITE_H
#define MQENGINE_MQ_EDITOR_ASSET_BROWSER_COMPOSITE_H

#include "../../MQEditorApi.h"
#include "AssetBrowserTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief 内容资产浏览器 Composite：
         *        提供基于工程双根路径的资产与源码树形导航、卡片网格浏览、面包屑返回、
         *        .mqmeta 伴生元数据管理、外部 IDE（CLion / Rider）一键直连与拖拽导入管道
         */
        class ASSET_BROWSER_COMPOSITE_API AssetBrowserComposite : public Asset::Composite
        {
        public:
            AssetBrowserComposite();
            ~AssetBrowserComposite() override;

            AssetBrowserComposite(const AssetBrowserComposite&) = delete;
            AssetBrowserComposite& operator=(const AssetBrowserComposite&) = delete;

            // ==================== Composite 接口实现 ====================

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "AssetBrowserComposite"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor"}; }

            void enqueueDroppedFiles(const std::vector<std::string>& files);
            bool importExternalFile(const std::string& srcFilePath, const std::string& targetDirectory);

        private:
            // 目录扫描、工程根目录同步、代码文件识别与 .mqmeta 侧车管理 (AssetBrowserComposite.cpp)
            std::string resolveActiveProjectRoot() const;
            std::string resolveInitialAssetDirectory(const std::string& projectRoot) const;
            void syncProjectRootIfChanged();
            void refreshDirectoryContents(const std::string& directoryPath);
            void navigateTo(const std::string& directoryPath);
            void navigateUp();
            void saveMetadataSidecar(const std::string& metaPath, const AssetMetadataSidecar& sidecar);
            AssetCategory determineFileType(const std::string& fileName, const std::string& ext, bool isDir) const;
            const char* getFileTypeIcon(AssetCategory type) const;
            bool isDirectCodeEditorAsset(const AssetEntry& item) const;
            bool canOpenInCodeEditor(const AssetEntry& item) const;
            std::string buildOpenFileInIdeLabel() const;
            std::string buildOpenProjectInIdeLabel() const;
            void openFileInExternalCodeEditor(const std::string& filePath, int line = 0);
            void openProjectInExternalCodeEditor(const std::string& targetProjectDir = "");
            std::string resolveCurrentMainScenePath() const;
            bool isMainSceneAsset(const AssetEntry& item) const;
            Status setSceneAsProjectMain(const std::string& scenePath);

            // 文件夹树、卡片网格、缩略图、上下文菜单與导入/重命名/删除弹窗 (AssetBrowserPanels.cpp)
            void renderAssetBrowser();
            void renderTopNavigationToolbar();
            void renderLeftFolderTreePanel();
            void renderProjectSourceTreeSection();
            void renderAssetFolderTreeSection();
            void renderSubDirectoryTreeNodes(const std::string& parentDirPath, int depth);
            void renderFolderSelectableNode(const std::string& label, const std::string& folderPath);
            void renderRightAssetContentPanel();
            void renderAssetGridCard(const AssetEntry& item);
            void renderAssetListRow(const AssetEntry& item);
            void renderAssetItemContextMenu(const AssetEntry& item);
            void renderAssetModalsAndContextMenus();
            void handleAssetOpen(const AssetEntry& item);

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;
            FCT::EventHandler* m_fileDropHandler = nullptr;

            LockFreeSpscQueue<std::string, 256> m_pendingDroppedFiles;

            std::string m_projectRoot;
            std::string m_currentDirectory = "./Assets";
            std::string m_selectedItemPath;
            std::vector<std::string> m_history;
            int m_historyIndex = -1;

            std::vector<AssetEntry> m_currentItems;
            char m_searchFilter[128] = {0};
            char m_importPathBuffer[512] = {0};
            char m_renameBuffer[128] = {0};
            float m_thumbnailSize = 72.0f;
            bool m_listViewMode = false;
            bool m_openRenameModal = false;
            bool m_openDeleteModal = false;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_ASSET_BROWSER_COMPOSITE_H
