/**
 * @file HtmlUiEditorComposite.cpp
 * @brief GameHtmlUI 专属所见即所得可视化编辑器生命周期、工程 UI 文档自动发现、顶栏工具条与热重载实现
 */
#include "HtmlUiEditorComposite.h"
#include "../../IRmlUiHotReloadBridge.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>
#include <Composites/EditorUiComposite/CommandStack.h>
#include <Composites/BlueprintEditorComposite/BlueprintEditorComposite.h>

#include <algorithm>

namespace MQEngine
{
    namespace GameHtmlUI
    {
        namespace
        {
            constexpr const char* kWindowTitle = "HTML UI 设计器 (RmlUi Visual Editor)";
            constexpr std::string_view kDesignerOverridesBegin = "/* === MQ_DESIGNER_OVERRIDES_BEGIN === */";

            std::string normalizeForwardSlashes(std::string path)
            {
                std::replace(path.begin(), path.end(), '\\', '/');
                while (path.size() > 1 && path.back() == '/')
                {
                    path.pop_back();
                }
                return path;
            }

            std::string replaceFileExtension(const std::string& path, const std::string& newExt)
            {
                const size_t slashPos = path.find_last_of("/\\");
                const size_t dotPos = path.rfind('.');
                if (dotPos != std::string::npos && (slashPos == std::string::npos || dotPos > slashPos))
                {
                    return path.substr(0, dotPos) + newExt;
                }
                return path + newExt;
            }

            std::string stripDesignerOverridesSection(const std::string& rcssText)
            {
                const size_t markerPos = rcssText.find(kDesignerOverridesBegin);
                if (markerPos == std::string::npos)
                {
                    return rcssText;
                }
                std::string base = rcssText.substr(0, markerPos);
                while (!base.empty() && (base.back() == '\n' || base.back() == '\r' || base.back() == ' ' || base.back() == '\t'))
                {
                    base.pop_back();
                }
                if (!base.empty())
                {
                    base += "\n";
                }
                return base;
            }
        } // namespace

        /**
         * @brief HTML UI 元素变更通用撤销/重做命令
         */
        class HtmlUiElementCommand : public EditorBase::IUndoableCommand
        {
        public:
            HtmlUiElementCommand(
                HtmlUiEditorComposite* editor,
                std::vector<HtmlUiElement> beforeState,
                std::vector<HtmlUiElement> afterState,
                std::string name)
                : m_editor(editor)
                , m_beforeState(std::move(beforeState))
                , m_afterState(std::move(afterState))
                , m_name(std::move(name))
            {
            }

            void execute() override
            {
                if (m_editor)
                {
                    m_editor->applyElementsState(m_afterState);
                }
            }

            void undo() override
            {
                if (m_editor)
                {
                    m_editor->applyElementsState(m_beforeState);
                }
            }

            std::string getName() const override
            {
                return m_name;
            }

        private:
            HtmlUiEditorComposite* m_editor = nullptr;
            std::vector<HtmlUiElement> m_beforeState;
            std::vector<HtmlUiElement> m_afterState;
            std::string m_name;
        };

        HtmlUiEditorComposite::HtmlUiEditorComposite() = default;

        HtmlUiEditorComposite::~HtmlUiEditorComposite()
        {
            onUnload();
        }

        std::string HtmlUiEditorComposite::resolveUiDiskPath(const std::string& virtualOrDiskPath) const
        {
            if (virtualOrDiskPath.empty())
            {
                return virtualOrDiskPath;
            }
            const std::string norm = normalizeForwardSlashes(virtualOrDiskPath);
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (loader && loader->exists(norm))
            {
                return norm;
            }
            if (resMod && !resMod->getProjectDir().empty())
            {
                const std::string projCandidate = normalizeForwardSlashes(
                    FCT::SAI::Path::join(resMod->getProjectDir(), norm));
                if (!loader || loader->exists(projCandidate) || norm.starts_with("Assets/"))
                {
                    return projCandidate;
                }
            }
            return norm;
        }

        std::string HtmlUiEditorComposite::toProjectRelativeUiPath(const std::string& diskOrVirtualPath) const
        {
            std::string norm = normalizeForwardSlashes(diskOrVirtualPath);
            auto* resMod = Resource::Resource::InstancePtr();
            if (resMod && !resMod->getProjectDir().empty())
            {
                const std::string projNorm = normalizeForwardSlashes(resMod->getProjectDir());
                if (!projNorm.empty() && norm.size() > projNorm.size() &&
                    norm.starts_with(projNorm) && norm[projNorm.size()] == '/')
                {
                    norm = norm.substr(projNorm.size() + 1);
                }
            }
            return norm;
        }

        void HtmlUiEditorComposite::refreshAvailableUiFiles()
        {
            m_availableUiFiles.clear();
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return;
            }

            std::vector<std::string> candidateDirs;
            if (resMod && !resMod->getProjectDir().empty())
            {
                candidateDirs.push_back(normalizeForwardSlashes(FCT::SAI::Path::join(resMod->getProjectDir(), "Assets/UI")));
            }
            candidateDirs.push_back("Assets/UI");

            for (const auto& uiDir : candidateDirs)
            {
                auto dirRes = loader->readDir(uiDir);
                if (!dirRes.ok())
                {
                    continue;
                }
                for (const auto& filePtr : *dirRes)
                {
                    if (!filePtr || filePtr->extension() != ".rml")
                    {
                        continue;
                    }
                    const std::string relPath = toProjectRelativeUiPath(filePtr->path());
                    if (std::find(m_availableUiFiles.begin(), m_availableUiFiles.end(), relPath) == m_availableUiFiles.end())
                    {
                        m_availableUiFiles.push_back(relPath);
                    }
                }
            }

            std::sort(m_availableUiFiles.begin(), m_availableUiFiles.end(), [](const std::string& a, const std::string& b) {
                const bool aCity = (a.find("CityHUD.rml") != std::string::npos);
                const bool bCity = (b.find("CityHUD.rml") != std::string::npos);
                if (aCity != bCity)
                {
                    return aCity;
                }
                return a < b;
            });
        }

        bool HtmlUiEditorComposite::loadUiDocumentPair(const std::string& rmlOrRcssPath)
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader || rmlOrRcssPath.empty())
            {
                return false;
            }

            const std::string relInput = toProjectRelativeUiPath(rmlOrRcssPath);
            const std::string rmlRelPath = replaceFileExtension(relInput, ".rml");
            const std::string rcssRelPath = replaceFileExtension(relInput, ".rcss");

            const std::string rmlDiskPath = resolveUiDiskPath(rmlRelPath);
            const std::string rcssDiskPath = resolveUiDiskPath(rcssRelPath);
            if (!loader->exists(rmlDiskPath))
            {
                return false;
            }

            auto rmlRes = loader->loadFile(rmlDiskPath);
            if (!rmlRes.ok())
            {
                return false;
            }

            m_activeFilePath = rmlRelPath;
            m_rmlSource = *rmlRes;
            m_rcssSource.clear();
            if (loader->exists(rcssDiskPath))
            {
                auto rcssRes = loader->loadFile(rcssDiskPath);
                if (rcssRes.ok())
                {
                    m_rcssSource = *rcssRes;
                }
            }

            m_baseRcssSource = stripDesignerOverridesSection(m_rcssSource);
            m_selectedElementId = -1;
            parseRmlAndRcssToElements();
            m_isDirty = false;

            if (std::find(m_availableUiFiles.begin(), m_availableUiFiles.end(), m_activeFilePath) == m_availableUiFiles.end())
            {
                m_availableUiFiles.push_back(m_activeFilePath);
            }
            return true;
        }

        void HtmlUiEditorComposite::ensureProjectUiDocumentLoaded()
        {
            auto* resMod = Resource::Resource::InstancePtr();
            const std::string currentProjDir = resMod ? normalizeForwardSlashes(resMod->getProjectDir()) : "";
            const bool projChanged = (currentProjDir != m_boundProjectDir);
            if (!projChanged && !m_elements.empty())
            {
                return;
            }

            m_boundProjectDir = currentProjDir;
            refreshAvailableUiFiles();

            if (!m_availableUiFiles.empty())
            {
                (void)loadUiDocumentPair(m_availableUiFiles.front());
                return;
            }

            if (!loadUiDocumentPair("Assets/UI/CityHUD.rml"))
            {
                (void)loadUiDocumentPair("Assets/UI/ViewerHUD.rml");
            }
        }

        void HtmlUiEditorComposite::notifyRuntimeHotReload()
        {
            auto* assetModule = Asset::Asset::InstancePtr();
            if (!assetModule)
            {
                return;
            }
            if (auto* comp = assetModule->compositeManager.getComposite("RmlUiComposite"))
            {
                if (auto* reloader = dynamic_cast<IRmlUiHotReloadBridge*>(comp))
                {
                    (void)reloader->reloadDocumentFromDisk();
                }
            }
        }

        HtmlUiElement* HtmlUiEditorComposite::findElementById(uint32_t id)
        {
            for (auto& elem : m_elements)
            {
                if (elem.id == id)
                {
                    return &elem;
                }
            }
            return nullptr;
        }

        const HtmlUiElement* HtmlUiEditorComposite::findElementById(uint32_t id) const
        {
            for (const auto& elem : m_elements)
            {
                if (elem.id == id)
                {
                    return &elem;
                }
            }
            return nullptr;
        }

        void HtmlUiEditorComposite::translateElementSubtree(uint32_t rootId, float dx, float dy)
        {
            if (dx == 0.0f && dy == 0.0f)
            {
                return;
            }
            HtmlUiElement* elem = findElementById(rootId);
            if (!elem)
            {
                return;
            }
            elem->x += dx;
            elem->y += dy;
            for (uint32_t cid : elem->childIds)
            {
                translateElementSubtree(cid, dx, dy);
            }
        }

        void HtmlUiEditorComposite::deleteElementSubtree(uint32_t targetId)
        {
            HtmlUiElement* target = findElementById(targetId);
            if (!target)
            {
                return;
            }

            if (target->parentId != 0)
            {
                if (HtmlUiElement* parent = findElementById(target->parentId))
                {
                    parent->childIds.erase(
                        std::remove(parent->childIds.begin(), parent->childIds.end(), targetId),
                        parent->childIds.end());
                }
            }

            std::vector<uint32_t> toRemove;
            auto collectIds = [this, &toRemove](auto& self, uint32_t id) -> void {
                toRemove.push_back(id);
                if (const HtmlUiElement* e = findElementById(id))
                {
                    for (uint32_t cid : e->childIds)
                    {
                        self(self, cid);
                    }
                }
            };
            collectIds(collectIds, targetId);

            m_elements.erase(
                std::remove_if(m_elements.begin(), m_elements.end(), [&](const HtmlUiElement& e) {
                    return std::find(toRemove.begin(), toRemove.end(), e.id) != toRemove.end();
                }),
                m_elements.end());
        }

        Status HtmlUiEditorComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
                m_blueprintEditorComposite = dynamic_cast<Blueprint::BlueprintEditorComposite*>(
                    assetModule->compositeManager.getComposite("BlueprintEditorComposite"));
            }

            ensureProjectUiDocumentLoaded();

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kWindowTitle,
                    EditorBase::EditorDockSlot::Center,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderHtmlUiEditorWindow();
                    },
                    false);

                m_editorUiComposite->getUiContext().registerSignalHandler<EditorBase::SelectAssetSignal>(
                    [this](const EditorBase::SelectAssetSignal& sig) {
                        if (sig.assetExtension == ".rml" || sig.assetExtension == ".rcss")
                        {
                            if (m_editorUiComposite)
                            {
                                m_editorUiComposite->setDockWindowOpen(kWindowTitle, true);
                            }
                            (void)loadUiDocumentPair(sig.assetPath);
                        }
                    });
            }

            return OkStatus();
        }

        Status HtmlUiEditorComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            ensureProjectUiDocumentLoaded();
            return OkStatus();
        }

        void HtmlUiEditorComposite::onTick(float deltaTime)
        {
            (void)deltaTime;
            ensureProjectUiDocumentLoaded();
        }

        void HtmlUiEditorComposite::onUnload()
        {
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kWindowTitle);
            }
            m_elements.clear();
            m_availableUiFiles.clear();
            m_editorUiComposite = nullptr;
            m_blueprintEditorComposite = nullptr;
            Asset::Composite::onUnload();
        }

        void HtmlUiEditorComposite::applyElementsState(const std::vector<HtmlUiElement>& state)
        {
            m_elements = state;
            m_isDirty = true;
            regenerateRmlAndRcssFromElements();
        }

        void HtmlUiEditorComposite::pushCommand(const std::string& name, std::vector<HtmlUiElement> beforeState)
        {
            m_isDirty = true;
            if (m_editorUiComposite)
            {
                auto cmd = makeUnique<HtmlUiElementCommand>(this, std::move(beforeState), m_elements, name);
                m_editorUiComposite->getCommandStack().executeCommand(std::move(cmd));
            }
        }

        void HtmlUiEditorComposite::bindElementEventToBlueprint(const HtmlUiElement& elem, const std::string& eventType)
        {
            if (!m_blueprintEditorComposite)
            {
                auto* assetModule = Asset::Asset::InstancePtr();
                if (assetModule)
                {
                    m_blueprintEditorComposite = dynamic_cast<Blueprint::BlueprintEditorComposite*>(
                        assetModule->compositeManager.getComposite("BlueprintEditorComposite"));
                }
            }

            if (m_blueprintEditorComposite)
            {
                m_blueprintEditorComposite->bindHtmlUiEventNode(elem.domId, elem.tag, eventType);
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->setDockWindowOpen("蓝图可视化编辑器 (Blueprint Editor)", true);
                    m_editorUiComposite->setDockWindowOpen("蓝图可视化编辑器 (Blueprint Visual Editor)", true);
                    m_editorUiComposite->showToast(
                        "蓝图绑定成功",
                        "已为 #" + elem.domId + " 生成 OnUIClick 入口事件节点与 AST 源码",
                        EditorBase::LogEntry::Level::Info,
                        3.0f);
                }
            }
            else if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "蓝图绑定",
                    "未找到 BlueprintEditorComposite 模块",
                    EditorBase::LogEntry::Level::Warn,
                    2.5f);
            }
        }

        void HtmlUiEditorComposite::renderEditorTopToolbar()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", L("文档:", "Doc:"));
            ImGui::SameLine();

            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##UiDocSelector", m_activeFilePath.c_str()))
            {
                for (const auto& fileRel : m_availableUiFiles)
                {
                    const bool isCurrent = (fileRel == m_activeFilePath);
                    if (ImGui::Selectable(fileRel.c_str(), isCurrent))
                    {
                        (void)loadUiDocumentPair(fileRel);
                    }
                    if (isCurrent)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(L("刷新列表", "Scan")))
            {
                refreshAvailableUiFiles();
                if (m_elements.empty() && !m_availableUiFiles.empty())
                {
                    (void)loadUiDocumentPair(m_availableUiFiles.front());
                }
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(L("重载磁盘", "Reload")))
            {
                (void)loadUiDocumentPair(m_activeFilePath);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(L("保存并热重载 (Ctrl+S)", "Save & Hot-Reload (Ctrl+S)")))
            {
                saveUiFilesToDisk();
            }

            ImGui::SameLine();
            if (m_editorUiComposite && ImGui::SmallButton(L("撤销", "Undo")))
            {
                m_editorUiComposite->getCommandStack().undo();
            }

            ImGui::SameLine();
            if (m_editorUiComposite && ImGui::SmallButton(L("重做", "Redo")))
            {
                m_editorUiComposite->getCommandStack().redo();
            }

            ImGui::SameLine();
            ImGui::TextDisabled("(%zu DOM)", m_elements.size());
        }

        void HtmlUiEditorComposite::renderHtmlUiEditorWindow()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kWindowTitle)
                : "HTML UI 设计器###HTML UI 设计器 (RmlUi Visual Editor)";
            if (m_isDirty)
            {
                winTitle += " *";
            }

            bool* openPtr = m_editorUiComposite ? m_editorUiComposite->getDockWindowOpenPtr(kWindowTitle) : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                {
                    saveUiFilesToDisk();
                }
                else if (m_editorUiComposite && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
                {
                    m_editorUiComposite->getCommandStack().undo();
                }
                else if (m_editorUiComposite && io.KeyCtrl &&
                         (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
                {
                    m_editorUiComposite->getCommandStack().redo();
                }
            }

            renderEditorTopToolbar();
            ImGui::Separator();

            if (ImGui::BeginTabBar("HtmlUiDesignerTabs"))
            {
                if (ImGui::BeginTabItem(L("所见即所得设计画布###TabWysiwyg", "WYSIWYG Designer###TabWysiwyg")))
                {
                    ImGui::BeginChild("HtmlLeftColumn", ImVec2(210.0f, 0.0f), true);
                    renderControlPalettePane();
                    ImGui::Separator();
                    renderDomHierarchyPane();
                    ImGui::EndChild();

                    ImGui::SameLine();

                    const float centerW = std::max(180.0f, ImGui::GetContentRegionAvail().x - 270.0f);
                    ImGui::BeginChild(
                        "HtmlCenterCanvasPane",
                        ImVec2(centerW, 0.0f),
                        false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
                    renderWysiwygCanvas();
                    ImGui::EndChild();

                    ImGui::SameLine();

                    ImGui::BeginChild("HtmlRightInspector", ImVec2(0.0f, 0.0f), true);
                    renderElementInspectorPane();
                    ImGui::EndChild();

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(L("RML 布局源码###TabRmlSource", "RML Markup###TabRmlSource")))
                {
                    std::vector<char> buf(m_rmlSource.begin(), m_rmlSource.end());
                    buf.resize(std::max<size_t>(65536u, m_rmlSource.size() + 4096u), 0);
                    if (ImGui::InputTextMultiline("##RmlCodeEditor", buf.data(), buf.size(), ImVec2(-1.0f, -1.0f)))
                    {
                        m_rmlSource = buf.data();
                        m_isDirty = true;
                        parseRmlAndRcssToElements();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(L("RCSS 样式表###TabRcssSource", "RCSS Stylesheet###TabRcssSource")))
                {
                    std::vector<char> buf(m_rcssSource.begin(), m_rcssSource.end());
                    buf.resize(std::max<size_t>(65536u, m_rcssSource.size() + 4096u), 0);
                    if (ImGui::InputTextMultiline("##RcssCodeEditor", buf.data(), buf.size(), ImVec2(-1.0f, -1.0f)))
                    {
                        m_rcssSource = buf.data();
                        m_baseRcssSource = stripDesignerOverridesSection(m_rcssSource);
                        m_isDirty = true;
                        parseRmlAndRcssToElements();
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        }

    } // namespace GameHtmlUI
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::GameHtmlUI::HtmlUiEditorComposite)
