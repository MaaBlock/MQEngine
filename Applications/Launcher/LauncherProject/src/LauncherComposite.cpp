#include "LauncherComposite.h"
#include <charconv>
#include <set>

using namespace FCT;

namespace MQEngine {

namespace {
    constexpr const char* kDefaultDemoSceneUuid = "default-scene-0001-0000-000000000001";
    constexpr const char* kDefaultDemoSceneName = "DefaultScene";
    constexpr const char* kDemoFloorModelUuid = "demo-model-floor1-0001-000000000001";
    constexpr const char* kDemoRobotModelUuid = "demo-model-robot0-0001-000000000002";
    constexpr const char* kDemoBallModelUuid = "demo-model-ball00-0001-000000000003";
    constexpr const char* kDemoFloorMatModelUuid = "demo-model-flrmat-0001-000000000004";
}

void LauncherComposite::registerInfo() {
    compositeRegisterInfo = Runtime::CompositeRegisterInfo{
        .name = "LauncherComposite",
        .requiredTags = {"Launcher", "HtmlUi"},
        .predecessors = {"HtmlGameUiComposite"},
        .successors = {},
    };
}

void LauncherComposite::locateWorkspaceRoot() {
    std::string currentDir = SAI::System::getCurrentDirectory();
    std::string foundDir = SAI::FileSystem::findDirectoryUpwardsWithAll(
        currentDir, {"CMakeLists.txt", "Engine", "Assets"}, 8);
    m_workspaceRoot = foundDir.empty() ? currentDir : foundDir;
}

std::string LauncherComposite::resolveWorkspacePath(const std::string& relativePath) const {
    return SAI::Path::resolve(m_workspaceRoot, relativePath);
}

Status LauncherComposite::onLoad(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    spdlog::info("LauncherComposite::onLoad 完成");
    return OkStatus();
}

Status LauncherComposite::onInit(Runtime::Engine* engine) {
    ZoneScoped();
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    locateWorkspaceRoot();

    auto* pm = (m_engine && m_engine->host) ? m_engine->host->getPluginManager() : nullptr;
    if (pm) {
        m_htmlUiService = pm->getService<IHtmlGameUiService>();
        if (!m_htmlUiService) {
            auto plugin = pm->getPlugin<Runtime::IPlugin>("HtmlGameUiPlugin");
            if (plugin) {
                m_htmlUiService = dynamic_cast<IHtmlGameUiService*>(plugin.get());
            }
        }
    }

    if (m_htmlUiService) {
        std::string rmlPath = "Assets/UI/launcher.rml";
        auto st = m_htmlUiService->loadDocument(rmlPath);
        if (!st.ok()) {
            spdlog::warn("LauncherComposite: 加载 UI 文档失败: {}", st.message());
        }
        setupHtmlUiBindings();
    }

    setupDefaultCamera();
    ensureDemoProjectInitialized();
    scanProjects();

    auto& runtime = Runtime::Runtime::instance();
    if (runtime.hasProjectPath()) {
        std::string cliProj = SAI::Path::normalize(runtime.getProjectPath());
        std::string launcherProj = resolveWorkspacePath("Applications/Launcher/LauncherProject/project.mq");
        if (cliProj != launcherProj) {
            addOrUpdateProjectEntry(cliProj);
        }
    }

    refreshUiData();
    spdlog::info("LauncherComposite::onInit 完成，已进入 HTML 启动器界面");
    return OkStatus();
}

void LauncherComposite::setupDefaultCamera() {
    if (g_engineGlobal.dataManager) {
        g_engineGlobal.dataManager->appendRegistry(&m_registry);
        m_cameraEntity = m_registry.create();
        m_registry.emplace<PositionComponent>(m_cameraEntity);
        m_registry.emplace<RotationComponent>(m_cameraEntity);
        m_registry.emplace<CameraComponent>(m_cameraEntity, CameraComponent{.active = true});
    }
}

void LauncherComposite::cleanupDefaultCamera() {
    if (g_engineGlobal.dataManager) {
        g_engineGlobal.dataManager->removeRegistry(&m_registry);
    }
    m_registry.clear();
    m_cameraEntity = entt::null;
}

void LauncherComposite::onLogicTick(float /*dt*/) {
    if (m_htmlUiService) {
        m_htmlUiService->update();
    }
}

void LauncherComposite::onRenderTick() {
}

void LauncherComposite::onUnload() {
    if (m_htmlUiService) {
        m_htmlUiService->closeDocument();
        m_htmlUiService = nullptr;
    }
    cleanupDefaultCamera();
    spdlog::info("LauncherComposite::onUnload 完成");
}

void LauncherComposite::setupHtmlUiBindings() {
    if (!m_htmlUiService) {
        return;
    }

    m_htmlUiService->addEventListener("btn-browse-project", "click", [this](const std::string&) {
        browseAndOpenProject();
        refreshUiData();
    });

    m_htmlUiService->addEventListener("btn-new-project", "click", [this](const std::string&) {
        openNewProjectModal();
    });

    m_htmlUiService->addEventListener("btn-refresh-projects", "click", [this](const std::string&) {
        scanProjects();
        m_statusMessage = "已刷新工程列表";
        m_statusIsError = false;
        refreshUiData();
    });

    m_htmlUiService->addEventListener("btn-modal-close", "click", [this](const std::string&) {
        closeNewProjectModal();
    });

    m_htmlUiService->addEventListener("btn-cancel-create-project", "click", [this](const std::string&) {
        closeNewProjectModal();
    });

    m_htmlUiService->addEventListener("btn-confirm-create-project", "click", [this](const std::string&) {
        handleCreateProjectFromModal();
    });

    m_htmlUiService->addEventListener("btn-launch-game", "click", [this](const std::string&) {
        handleLaunchCurrentProject("Game");
    });

    m_htmlUiService->addEventListener("btn-launch-editor", "click", [this](const std::string&) {
        handleLaunchCurrentProject("Editor");
    });

    m_htmlUiService->registerAction("select_project", [this](const std::string& param) {
        int idx = 0;
        auto conv = std::from_chars(param.data(), param.data() + param.size(), idx);
        if (conv.ec == std::errc()) {
            selectProject(idx);
            refreshUiData();
        }
    });

    m_htmlUiService->registerAction("launch_game", [this](const std::string&) {
        handleLaunchCurrentProject("Game");
    });

    m_htmlUiService->registerAction("launch_editor", [this](const std::string&) {
        handleLaunchCurrentProject("Editor");
    });
}

void LauncherComposite::openNewProjectModal() {
    if (m_htmlUiService) {
        m_htmlUiService->setElementProperty("new-project-modal", "display", "block");
    }
}

void LauncherComposite::closeNewProjectModal() {
    if (m_htmlUiService) {
        m_htmlUiService->setElementProperty("new-project-modal", "display", "none");
    }
}

void LauncherComposite::handleCreateProjectFromModal() {
    if (!m_htmlUiService) {
        return;
    }

    std::string projName = m_htmlUiService->getElementValue("input-project-name");
    std::string parentDir = m_htmlUiService->getElementValue("input-project-parent-dir");

    ProjectScriptOptions scriptOpts;
    scriptOpts.enableTypeScript = m_htmlUiService->getElementChecked("chk-lang-ts");
    scriptOpts.enableJavaScript = m_htmlUiService->getElementChecked("chk-lang-js");
    scriptOpts.enablePython = m_htmlUiService->getElementChecked("chk-lang-py");
    scriptOpts.enableCppUserCode = m_htmlUiService->getElementChecked("chk-lang-cpp");

    createNewProject(projName, parentDir, scriptOpts);
    closeNewProjectModal();
    refreshUiData();
}

void LauncherComposite::handleLaunchCurrentProject(const std::string& appName) {
    if (m_selectedProjectIndex < 0 || m_selectedProjectIndex >= static_cast<int>(m_projects.size())) {
        m_statusMessage = "未选择有效工程";
        m_statusIsError = true;
        refreshUiData();
        return;
    }

    const auto& config = m_projects[m_selectedProjectIndex];
    launchProjectInApp(appName, config);
    refreshUiData();
}

void LauncherComposite::selectProject(int index) {
    if (index >= 0 && index < static_cast<int>(m_projects.size())) {
        m_selectedProjectIndex = index;
    }
}

void LauncherComposite::refreshUiData() {
    if (!m_htmlUiService) {
        return;
    }

    // 1. 刷新左侧工程列表 DOM
    std::string listHtml;
    for (size_t i = 0; i < m_projects.size(); ++i) {
        const auto& proj = m_projects[i];
        bool isSelected = (m_selectedProjectIndex == static_cast<int>(i));
        std::string itemClass = isSelected ? "project-item selected" : "project-item";
        listHtml += StrCat(
            "<div class=\"", itemClass, "\" id=\"proj-item-", i, "\" onclick=\"select_project('", i, "')\">\n",
            "  <span class=\"project-item-name\">", proj.projectName, "</span>\n",
            "  <span class=\"project-item-path\" title=\"", proj.projectFilePath, "\">", proj.projectDirectory, "</span>\n",
            "</div>\n"
        );
    }
    m_htmlUiService->setElementInnerRml("project-list-container", listHtml);
    m_htmlUiService->setElementText("project-list-header", StrCat("工程列表 (", m_projects.size(), ")"));

    // 为每个动态条目挂载点击侦听
    for (size_t i = 0; i < m_projects.size(); ++i) {
        std::string itemId = StrCat("proj-item-", i);
        m_htmlUiService->addEventListener(itemId, "click", [this, i](const std::string&) {
            selectProject(static_cast<int>(i));
            refreshUiData();
        });
    }

    // 2. 刷新右侧工程详情
    if (m_selectedProjectIndex < 0 || m_selectedProjectIndex >= static_cast<int>(m_projects.size())) {
        m_htmlUiService->setElementProperty("detail-empty-hint", "display", "block");
        m_htmlUiService->setElementProperty("detail-card", "display", "none");
    } else {
        m_htmlUiService->setElementProperty("detail-empty-hint", "display", "none");
        m_htmlUiService->setElementProperty("detail-card", "display", "block");

        const auto& cfg = m_projects[m_selectedProjectIndex];
        m_htmlUiService->setElementText("detail-title", cfg.projectName);
        m_htmlUiService->setElementText("detail-version-badge", StrCat("v", cfg.version));
        m_htmlUiService->setElementText("detail-config-path", cfg.projectFilePath);
        m_htmlUiService->setElementText("detail-project-dir", cfg.projectDirectory);
        m_htmlUiService->setElementText("detail-asset-root", cfg.resolvedAssetRoot);
        m_htmlUiService->setElementText("detail-pipeline", cfg.pipelinePath.empty() ? "默认管线" : cfg.pipelinePath);
        m_htmlUiService->setElementText("detail-scene-uuid", cfg.initialSceneUuid.empty() ? "未设置" : cfg.initialSceneUuid);
        m_htmlUiService->setElementText("detail-script-languages", detectProjectScriptLanguagesSummary(cfg));

        auto it = cfg.settings.find("description");
        std::string desc = (it != cfg.settings.end()) ? it->second : "-";
        m_htmlUiService->setElementText("detail-description", desc);
    }

    // 3. 刷新状态栏
    if (!m_statusMessage.empty()) {
        m_htmlUiService->setElementText("status-message", m_statusMessage);
        m_htmlUiService->setElementClass("status-message", "error", m_statusIsError);
        m_htmlUiService->setElementProperty("status-bar", "display", "block");
    } else {
        m_htmlUiService->setElementProperty("status-bar", "display", "none");
    }
}

void LauncherComposite::ensureDemoProjectInitialized() {
    ProjectScriptOptions defaultDemoOptions{
        .enableTypeScript = true,
        .enableJavaScript = true,
        .enablePython = false,
        .enableCppUserCode = false
    };

    std::string launcherProjDir = resolveWorkspacePath("Applications/Launcher/LauncherProject");
    (void)initializeProjectDirectory(launcherProjDir, "LauncherProject", kDefaultDemoSceneUuid, defaultDemoOptions);

    std::string demoDir = resolveWorkspacePath("Example/DemoProject");
    auto st = initializeProjectDirectory(demoDir, "DemoProject", kDefaultDemoSceneUuid, defaultDemoOptions);
    if (!st.ok()) {
        m_statusMessage = std::string("初始化 DemoProject 失败: ") + std::string(st.message());
        m_statusIsError = true;
        spdlog::warn("{}", m_statusMessage);
    }
}

Status LauncherComposite::initializeProjectDirectory(const std::string& projectDir,
                                                     const std::string& projectName,
                                                     const std::string& sceneUuid,
                                                     const ProjectScriptOptions& scriptOptions) {
    if (!g_engineGlobal.resourceLoader || !g_engineGlobal.dataManager) {
        return FailedPreconditionError("ResourceLoader 或 DataManager 未就绪");
    }

    std::string assetRoot = projectDir + "/Assets";
    CHECK_STATUS(ensureProjectBaseDirectories(projectDir, assetRoot, scriptOptions));
    CHECK_STATUS(ensureProjectConfigFile(projectDir, projectName, sceneUuid, scriptOptions));
    CHECK_STATUS(initializeSelectedScriptLanguages(projectDir, assetRoot, projectName, scriptOptions));
    CHECK_STATUS(ensureDefaultPipelineConfig(assetRoot));

    return ensureSceneBootstrapFiles(assetRoot, kDefaultDemoSceneName, sceneUuid);
}

Status LauncherComposite::ensureProjectBaseDirectories(const std::string& projectDir,
                                                       const std::string& assetRoot,
                                                       const ProjectScriptOptions& scriptOptions) {
    auto* loader = g_engineGlobal.resourceLoader;
    CHECK_STATUS(loader->ensureDir(projectDir));
    CHECK_STATUS(loader->ensureDir(assetRoot));
    CHECK_STATUS(loader->ensureDir(assetRoot + "/Pipelines"));
    CHECK_STATUS(loader->ensureDir(assetRoot + "/Scenes"));
    CHECK_STATUS(loader->ensureDir(assetRoot + "/Models"));
    CHECK_STATUS(loader->ensureDir(assetRoot + "/ShaderGraph"));
    CHECK_STATUS(loader->ensureDir(assetRoot + "/Snippets"));

    if (scriptOptions.enableTypeScript || scriptOptions.enableJavaScript || scriptOptions.enablePython) {
        CHECK_STATUS(loader->ensureDir(assetRoot + "/Scripts"));
    }

    return OkStatus();
}

Status LauncherComposite::ensureProjectConfigFile(const std::string& projectDir,
                                                 const std::string& projectName,
                                                 const std::string& sceneUuid,
                                                 const ProjectScriptOptions& scriptOptions) {
    auto* loader = g_engineGlobal.resourceLoader;
    std::string projectFilePath = projectDir + "/project.mq";
    if (loader->loadFile(projectFilePath).ok()) {
        return OkStatus();
    }

    ProjectConfig cfg;
    cfg.projectName = projectName;
    cfg.version = "1.0.0";
    cfg.assetRoot = "Assets";
    cfg.pipelinePath = "Assets/Pipelines/default_pipeline.json";
    cfg.userCode.clear();
    cfg.userCodes.clear();
    if (scriptOptions.enableCppUserCode) {
        cfg.userCode = "UserCode";
        cfg.userCodes.push_back("UserCode");
    }
    cfg.initialSceneUuid = sceneUuid;
    cfg.settings["description"] = "MQEngine 默认工程";
    cfg.settings["scriptLanguages"] = formatSelectedScriptLanguages(scriptOptions);

    auto* engine = Runtime::Engine::getInstance();
    if (!engine || !engine->foundation) {
        return FailedPreconditionError("Foundation 未就绪");
    }
    return engine->foundation->saveProjectConfig(projectFilePath, cfg);
}

Status LauncherComposite::ensureDefaultPipelineConfig(const std::string& assetRoot) {
    auto* loader = g_engineGlobal.resourceLoader;
    std::string targetPipelinePath = assetRoot + "/Pipelines/default_pipeline.json";
    if (loader->loadFile(targetPipelinePath).ok()) {
        return OkStatus();
    }

    auto defaultPipeRes = loader->loadFile(resolveWorkspacePath("Assets/Pipelines/default_pipeline.json"));
    if (defaultPipeRes.ok()) {
        return loader->saveFile(targetPipelinePath, defaultPipeRes.value());
    }

    return OkStatus();
}

Status LauncherComposite::initializeSelectedScriptLanguages(const std::string& projectDir,
                                                            const std::string& assetRoot,
                                                            const std::string& projectName,
                                                            const ProjectScriptOptions& scriptOptions) {
    std::string scriptsRoot = assetRoot + "/Scripts";

    if (scriptOptions.enableTypeScript || scriptOptions.enableJavaScript) {
        CHECK_STATUS(ensureNodeEnginePackageModule(scriptsRoot));
        CHECK_STATUS(ensureScriptsPackageJson(scriptsRoot));
    }

    if (scriptOptions.enableTypeScript) {
        CHECK_STATUS(initializeTypeScriptEnvironment(scriptsRoot));
    }

    if (scriptOptions.enableJavaScript) {
        CHECK_STATUS(initializeJavaScriptEnvironment(scriptsRoot));
    }

    if (scriptOptions.enablePython) {
        CHECK_STATUS(initializePythonEnvironment(scriptsRoot));
    }

    if (scriptOptions.enableCppUserCode) {
        CHECK_STATUS(initializeCppUserCodeEnvironment(projectDir, projectName));
    }

    return OkStatus();
}

Status LauncherComposite::ensureNodeEnginePackageModule(const std::string& scriptsRoot) {
    auto* loader = g_engineGlobal.resourceLoader;
    std::string pkgDir = scriptsRoot + "/node_modules/mqengine";
    CHECK_STATUS(loader->ensureDir(pkgDir));

    if (g_engineGlobal.scriptSystem && g_engineGlobal.scriptSystem->getEnginePackage()) {
        auto* pkg = g_engineGlobal.scriptSystem->getEnginePackage();
        CHECK_STATUS(loader->saveFile(pkgDir + "/package.json", pkg->generatePackageJson()));
        CHECK_STATUS(loader->saveFile(pkgDir + "/index.js", pkg->generateIndexJs()));
        CHECK_STATUS(loader->saveFile(pkgDir + "/index.d.ts", pkg->generateTypeScriptDts("MQEngine")));
        return OkStatus();
    }

    static const char* kFallbackPkgJson = R"JSON({
  "name": "mqengine",
  "version": "1.0.0",
  "main": "index.js",
  "types": "index.d.ts"
}
)JSON";
    static const char* kFallbackIndexJs = R"JS("use strict";
module.exports = globalThis.MQEngine || {};
)JS";
    static const char* kFallbackIndexDts = R"TS(export interface Vec2 { x: number; y: number; }
export interface Vec3 { x: number; y: number; z: number; }
export interface Vec4 { x: number; y: number; z: number; w: number; }
export interface EntityHandle { id: number; registry: number; }
export class ScriptBehaviour {
    entity: EntityHandle;
    components: Record<string, any>;
    position: Vec3;
    rotation: Vec3;
    scale: Vec3;
    readonly deltaTime: number;
    getComponent<T = any>(compOrName: any): T | undefined;
    setComponent<T = any>(compOrName: any, value: Partial<T>): boolean;
    hasComponent(compOrName: any): boolean;
    addComponent<T = any>(compOrName: any, initValue?: Partial<T>): T | undefined;
    removeComponent(compOrName: any): boolean;
    onStart?(): void;
    onUpdate?(dt: number): void;
    onTicker?(): void;
}
export interface ScriptInstanceBase extends ScriptBehaviour {}
)TS";
    CHECK_STATUS(saveFileIfMissing(pkgDir + "/package.json", kFallbackPkgJson));
    CHECK_STATUS(saveFileIfMissing(pkgDir + "/index.js", kFallbackIndexJs));
    return saveFileIfMissing(pkgDir + "/index.d.ts", kFallbackIndexDts);
}

Status LauncherComposite::ensureScriptsPackageJson(const std::string& scriptsRoot) {
    static const char* kDefaultScriptsPackageJson = R"JSON({
  "name": "mqengine-game-scripts",
  "version": "1.0.0",
  "private": true,
  "description": "MQEngine TypeScript/JavaScript Game Scripts",
  "scripts": {
    "build": "tsc",
    "watch": "tsc --watch"
  },
  "dependencies": {
    "mqengine": "file:./node_modules/mqengine"
  },
  "devDependencies": {
    "typescript": "^5.6.0"
  }
}
)JSON";
    return saveFileIfMissing(scriptsRoot + "/package.json", kDefaultScriptsPackageJson);
}

Status LauncherComposite::initializeTypeScriptEnvironment(const std::string& scriptsRoot) {
    auto* loader = g_engineGlobal.resourceLoader;
    CHECK_STATUS(loader->ensureDir(scriptsRoot + "/ts"));
    CHECK_STATUS(loader->ensureDir(scriptsRoot + "/src"));
    CHECK_STATUS(loader->ensureDir(scriptsRoot + "/dist"));

    static const char* kDefaultTsConfig = R"JSON({
  "compilerOptions": {
    "target": "ES2022",
    "module": "CommonJS",
    "moduleResolution": "node",
    "rootDir": "./src",
    "outDir": "./dist",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "declaration": false,
    "sourceMap": false,
    "baseUrl": ".",
    "paths": {
      "mqengine": ["./node_modules/mqengine"]
    }
  },
  "include": ["src/**/*.ts", "ts/**/*.ts", "node_modules/mqengine/index.d.ts"],
  "exclude": ["dist", "node_modules"]
}
)JSON";
    CHECK_STATUS(saveFileIfMissing(scriptsRoot + "/tsconfig.json", kDefaultTsConfig));

    static const char* kDefaultUserScriptTemplate = R"TS(import type { ScriptInstanceBase, Vec3 } from "mqengine";

class UserBehaviorScript implements ScriptInstanceBase {
    entity!: { id: number; registry: number };
    components!: any;

    onTicker(): void {
        const pos: Vec3 | undefined = this.components.PositionComponent?.position;
        if (pos) {
            // 工程级用户脚本逻辑
        }
    }
}

(globalThis as any).UserBehaviorScript = UserBehaviorScript;
)TS";
    CHECK_STATUS(saveFileIfMissing(scriptsRoot + "/ts/UserScript.ts", kDefaultUserScriptTemplate));

    static const char* kSampleTs = R"TS(import { ScriptBehaviour, RotationComponent } from "mqengine";

export class SpinBehaviour extends ScriptBehaviour {
    speed: number = 45.0;

    onStart(): void {
        console.log(`[SpinBehaviour] 实体 ${this.entity.id} 脚本已启动`);
    }

    onUpdate(dt: number): void {
        const rot = this.getComponent(RotationComponent);
        if (rot) {
            rot.rotation.y += this.speed * dt;
        }
    }
}
)TS";
    CHECK_STATUS(saveFileIfMissing(scriptsRoot + "/src/SpinBehaviour.ts", kSampleTs));

    static const char* kSampleJs = R"JS("use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.SpinBehaviour = void 0;
const mqengine_1 = require("mqengine");
class SpinBehaviour extends mqengine_1.ScriptBehaviour {
    constructor() {
        super(...arguments);
        this.speed = 45.0;
    }
    onStart() {
        console.log(`[SpinBehaviour] 实体 ${this.entity.id} 脚本已启动`);
    }
    onUpdate(dt) {
        const rot = this.getComponent(mqengine_1.RotationComponent);
        if (rot) {
            rot.rotation.y += this.speed * dt;
        }
    }
}
exports.SpinBehaviour = SpinBehaviour;
)JS";
    return saveFileIfMissing(scriptsRoot + "/dist/SpinBehaviour.js", kSampleJs);
}

Status LauncherComposite::initializeJavaScriptEnvironment(const std::string& scriptsRoot) {
    auto* loader = g_engineGlobal.resourceLoader;
    CHECK_STATUS(loader->ensureDir(scriptsRoot + "/js"));

    static const char* kDefaultJsConfig = R"JSON({
  "compilerOptions": {
    "target": "ES2022",
    "module": "CommonJS",
    "checkJs": false,
    "baseUrl": ".",
    "paths": {
      "mqengine": ["./node_modules/mqengine"]
    }
  },
  "include": ["js/**/*.js", "node_modules/mqengine/index.d.ts"],
  "exclude": ["dist", "node_modules"]
}
)JSON";
    CHECK_STATUS(saveFileIfMissing(scriptsRoot + "/jsconfig.json", kDefaultJsConfig));

    static const char* kDefaultJsUserScript = R"JS(const { ScriptBehaviour, RotationComponent } = require("mqengine");

class SpinBehaviourJs extends ScriptBehaviour {
    constructor() {
        super();
        this.speed = 45.0;
    }

    onStart() {
        console.log(`[SpinBehaviourJs] 实体 ${this.entity.id} JS 脚本已启动`);
    }

    onUpdate(dt) {
        const rot = this.getComponent(RotationComponent);
        if (rot) {
            rot.rotation.y += this.speed * dt;
        }
    }
}

globalThis.SpinBehaviourJs = SpinBehaviourJs;
)JS";
    return saveFileIfMissing(scriptsRoot + "/js/UserScript.js", kDefaultJsUserScript);
}

Status LauncherComposite::initializePythonEnvironment(const std::string& scriptsRoot) {
    auto* loader = g_engineGlobal.resourceLoader;
    std::string pyDir = scriptsRoot + "/py";
    CHECK_STATUS(loader->ensureDir(pyDir));

    if (g_engineGlobal.scriptSystem && g_engineGlobal.scriptSystem->getPyEnginePackage()) {
        std::string pyiContent = g_engineGlobal.scriptSystem->getPyEnginePackage()->generatePythonPyi("MQEngine");
        CHECK_STATUS(loader->saveFile(pyDir + "/mqengine.pyi", pyiContent));
        CHECK_STATUS(loader->saveFile(scriptsRoot + "/mqengine.pyi", pyiContent));
    } else {
        static const char* kFallbackPyi = R"PYI(from typing import Any

class Vec2:
    x: float
    y: float

class Vec3:
    x: float
    y: float
    z: float

class Vec4:
    x: float
    y: float
    z: float
    w: float

class EntityHandle(dict):
    id: int
    registry: int

class ScriptBehaviour:
    entity: EntityHandle
    position: Vec3
    rotation: Vec3
    scale: Vec3
    @property
    def delta_time(self) -> float: ...
    def get_component(self, comp_or_name: Any) -> Any: ...
    def set_component(self, comp_or_name: Any, value: dict[str, Any]) -> bool: ...
    def has_component(self, comp_or_name: Any) -> bool: ...
    def add_component(self, comp_or_name: Any, init_value: dict[str, Any] | None = None) -> Any: ...
    def remove_component(self, comp_or_name: Any) -> bool: ...
    def on_start(self) -> None: ...
    def on_update(self, dt: float) -> None: ...

class RotationComponent:
    rotation: Vec3
)PYI";
        CHECK_STATUS(saveFileIfMissing(pyDir + "/mqengine.pyi", kFallbackPyi));
        CHECK_STATUS(saveFileIfMissing(scriptsRoot + "/mqengine.pyi", kFallbackPyi));
    }

    static const char* kDefaultPyrightConfig = R"JSON({
  "include": ["py"],
  "extraPaths": ["py", "."],
  "typeCheckingMode": "basic"
}
)JSON";
    CHECK_STATUS(saveFileIfMissing(scriptsRoot + "/pyrightconfig.json", kDefaultPyrightConfig));

    static const char* kSamplePy = R"PY(from mqengine import ScriptBehaviour, RotationComponent


class SpinBehaviourPy(ScriptBehaviour):
    def __init__(self):
        super().__init__()
        self.speed = 60.0

    def on_start(self):
        print(f"[SpinBehaviourPy] 实体 {self.entity.id} Python 脚本已启动")

    def on_update(self, dt: float):
        rot = self.get_component(RotationComponent)
        if rot is not None:
            rot.rotation.y += self.speed * dt
)PY";
    return saveFileIfMissing(pyDir + "/SpinBehaviourPy.py", kSamplePy);
}

Status LauncherComposite::initializeCppUserCodeEnvironment(const std::string& projectDir,
                                                           const std::string& projectName) {
    auto* loader = g_engineGlobal.resourceLoader;
    std::string userCodeDir = projectDir + "/UserCode";
    std::string srcDir = userCodeDir + "/src";
    std::string thirdpartyDir = srcDir + "/thirdparty";

    CHECK_STATUS(loader->ensureDir(userCodeDir));
    CHECK_STATUS(loader->ensureDir(srcDir));
    CHECK_STATUS(loader->ensureDir(thirdpartyDir));

    static const char* kDefaultUserCodeCmake = R"CMAKE(cmake_minimum_required(VERSION 3.22)
project(UserCode LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB USER_CODE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB USER_CODE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty/*.h"
)

add_library(UserCode SHARED
    ${USER_CODE_SOURCES}
    ${USER_CODE_HEADERS}
)

target_compile_definitions(UserCode PRIVATE BUILD_USER_CODE)

if(COMMAND target_link_fct_libraries)
    target_link_fct_libraries(UserCode)
    target_link_fct_node_libraries(UserCode)
    target_link_fct_nbr_libraries(UserCode)
    target_link_fct_imgui_libraries(UserCode)
    target_link_engine(UserCode)
    target_link_imgui_plugin(UserCode)
endif()
)CMAKE";
    CHECK_STATUS(saveFileIfMissing(userCodeDir + "/CMakeLists.txt", kDefaultUserCodeCmake));

    static const char* kDefaultThirdpartyHeader = R"CPP(#ifndef MQENGINE_PROJECT_USERCODE_THIRDPARTY_H
#define MQENGINE_PROJECT_USERCODE_THIRDPARTY_H

#include <FCT_Base.h>
#include <FCT_Node.h>
#include <FCT.h>
#include <FCT_NBR.h>
#include <FCT_IMGUI.h>
#include <Engine/headers.h>
#include <ImguiPlugin/ImguiPlugin.h>

#endif // MQENGINE_PROJECT_USERCODE_THIRDPARTY_H
)CPP";
    CHECK_STATUS(saveFileIfMissing(thirdpartyDir + "/thirdparty.h", kDefaultThirdpartyHeader));

    std::string userCodeHeader = StrCat(
        "#ifndef MQENGINE_PROJECT_USER_CODE_H\n"
        "#define MQENGINE_PROJECT_USER_CODE_H\n\n"
        "#include \"thirdparty/thirdparty.h\"\n"
        "#include <string>\n\n"
        "#ifdef _WIN32\n"
        "    #ifdef BUILD_USER_CODE\n"
        "        #define USER_CODE_API __declspec(dllexport)\n"
        "    #else\n"
        "        #define USER_CODE_API __declspec(dllimport)\n"
        "    #endif\n"
        "#else\n"
        "    #define USER_CODE_API\n"
        "#endif\n\n"
        "namespace MQEngine {\n\n"
        "/**\n"
        " * @brief 工程 [", projectName, "] C++ 用户代码模块\n"
        " */\n"
        "class USER_CODE_API ProjectUserCodeModule : public Runtime::NativeCode {\n"
        "public:\n"
        "    ProjectUserCodeModule() = default;\n"
        "    ~ProjectUserCodeModule() override = default;\n\n"
        "    Status onLoad(Runtime::Engine* engine) override;\n"
        "    Status onInit(Runtime::Engine* engine) override;\n"
        "    void onLogicTick(float dt) override;\n"
        "    void onRenderTick() override;\n"
        "    void onUnload() override;\n\n"
        "    std::string getName() const override { return \"UserCode\"; }\n"
        "    std::string getVersion() const override { return \"1.0.0\"; }\n\n"
        "private:\n"
        "    Runtime::Engine* m_engine = nullptr;\n"
        "};\n\n"
        "} // namespace MQEngine\n\n"
        "extern \"C\" USER_CODE_API MQEngine::Runtime::NativeCode* CreateUserCode();\n\n"
        "#endif // MQENGINE_PROJECT_USER_CODE_H\n");
    CHECK_STATUS(saveFileIfMissing(srcDir + "/UserCode.h", userCodeHeader));

    static const char* kDefaultUserCodeCpp = R"CPP(#include "UserCode.h"

namespace MQEngine {

Status ProjectUserCodeModule::onLoad(Runtime::Engine* engine) {
    m_engine = engine;
    spdlog::info("ProjectUserCodeModule::onLoad 完成");
    return OkStatus();
}

Status ProjectUserCodeModule::onInit(Runtime::Engine* engine) {
    m_engine = engine;
    return OkStatus();
}

void ProjectUserCodeModule::onLogicTick(float /*dt*/) {
}

void ProjectUserCodeModule::onRenderTick() {
}

void ProjectUserCodeModule::onUnload() {
}

} // namespace MQEngine

extern "C" USER_CODE_API MQEngine::Runtime::NativeCode* CreateUserCode() {
    return new MQEngine::ProjectUserCodeModule();
}
)CPP";
    return saveFileIfMissing(srcDir + "/UserCode.cpp", kDefaultUserCodeCpp);
}

Status LauncherComposite::saveFileIfMissing(const std::string& filePath, const std::string& content) {
    auto* loader = g_engineGlobal.resourceLoader;
    if (!loader) {
        return FailedPreconditionError("ResourceLoader 未就绪");
    }
    if (loader->loadFile(filePath).ok()) {
        return OkStatus();
    }
    return loader->saveFile(filePath, content);
}

std::string LauncherComposite::formatSelectedScriptLanguages(const ProjectScriptOptions& scriptOptions) const {
    std::vector<std::string> langs;
    if (scriptOptions.enableTypeScript) {
        langs.push_back("TypeScript");
    }
    if (scriptOptions.enableJavaScript) {
        langs.push_back("JavaScript");
    }
    if (scriptOptions.enablePython) {
        langs.push_back("Python");
    }
    if (scriptOptions.enableCppUserCode) {
        langs.push_back("C++ (UserCode)");
    }

    if (langs.empty()) {
        return "无";
    }

    std::string joined;
    for (size_t i = 0; i < langs.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += langs[i];
    }
    return joined;
}

std::string LauncherComposite::detectProjectScriptLanguagesSummary(const ProjectConfig& config) const {
    auto it = config.settings.find("scriptLanguages");
    if (it != config.settings.end() && !it->second.empty()) {
        return it->second;
    }

    auto* loader = g_engineGlobal.resourceLoader;
    if (!loader) {
        return "未知";
    }

    ProjectScriptOptions detected{
        .enableTypeScript = loader->accessDir(config.resolvedAssetRoot + "/Scripts/ts").ok() ||
                            loader->loadFile(config.resolvedAssetRoot + "/Scripts/tsconfig.json").ok(),
        .enableJavaScript = loader->accessDir(config.resolvedAssetRoot + "/Scripts/js").ok() ||
                            loader->loadFile(config.resolvedAssetRoot + "/Scripts/jsconfig.json").ok(),
        .enablePython = loader->accessDir(config.resolvedAssetRoot + "/Scripts/py").ok() ||
                        loader->loadFile(config.resolvedAssetRoot + "/Scripts/pyrightconfig.json").ok(),
        .enableCppUserCode = loader->accessDir(config.projectDirectory + "/UserCode").ok()
    };
    return formatSelectedScriptLanguages(detected);
}

Status LauncherComposite::ensureSceneBootstrapFiles(const std::string& assetRoot,
                                                    const std::string& sceneName,
                                                    const std::string& sceneUuid) {
    auto* dataLoader = g_engineGlobal.dataManager->getDataLoader();
    std::string sceneDir = assetRoot + "/Scenes/" + sceneName;
    dataLoader->ensureDirectory(sceneDir);
    dataLoader->ensureDirectory(sceneDir + "/trunks");

    std::string uuidFilePath = sceneDir + "/scene.uuid";
    if (!dataLoader->fileExists(uuidFilePath)) {
        SceneUuidFile uuidData{.uuid = sceneUuid};
        Status st = ModelAssetSerializer::saveSceneUuid(g_engineGlobal.resourceLoader, uuidFilePath, uuidData);
        if (!st.ok()) {
            return InternalError("无法写入场景 UUID 文件: " + uuidFilePath + " (" + std::string(st.message()) + ")");
        }
    }

    std::string settingPath = assetRoot + "/project.setting";
    if (!dataLoader->fileExists(settingPath)) {
        ProjectSetting setting{.initialSceneUuid = sceneUuid};
        Status st = ModelAssetSerializer::saveProjectSetting(g_engineGlobal.resourceLoader, settingPath, setting);
        if (!st.ok()) {
            return InternalError("无法写入工程设置文件: " + settingPath + " (" + std::string(st.message()) + ")");
        }
    }

    ensureDefaultDemoModels(assetRoot);

    std::string prevAssetRoot = g_engineGlobal.dataManager->getAssetRoot();
    g_engineGlobal.dataManager->setAssetRoot(assetRoot);
    populateDefaultSceneEntities(sceneUuid);
    g_engineGlobal.dataManager->setAssetRoot(prevAssetRoot);

    return OkStatus();
}

void LauncherComposite::ensureDefaultDemoModels(const std::string& assetRoot) {
    importBuiltinModel(assetRoot, "floor_1", kDemoFloorModelUuid, resolveWorkspacePath("Assets/Models/floor_1.glb"));
    importBuiltinModel(assetRoot, "robot", kDemoRobotModelUuid, resolveWorkspacePath("Assets/Models/robot.glb"));
    importBuiltinModel(assetRoot, "ball", kDemoBallModelUuid, resolveWorkspacePath("Assets/ball.obj"));
    importBuiltinModel(assetRoot, "floor_material", kDemoFloorMatModelUuid, resolveWorkspacePath("Assets/Models/floor_material.glb"));
}

void LauncherComposite::importBuiltinModel(const std::string& assetRoot,
                                           const std::string& modelDirName,
                                           const std::string& modelUuid,
                                           const std::string& sourceModelPath) {
    if (!SAI::FileSystem::exists(sourceModelPath)) {
        return;
    }

    std::string targetDir = SAI::Path::join(assetRoot, "Models", modelDirName);
    if (SAI::FileSystem::containsAll(targetDir, {"model.uuid", "index.edit.dat"})) {
        return;
    }

    std::string sourceSubDir = SAI::Path::join(targetDir, "source");
    (void)SAI::FileSystem::createDirectories(sourceSubDir);

    if (!g_engineGlobal.rt) {
        return;
    }
    UniquePtr<ModelLoader> modelLoader(g_engineGlobal.rt->createModelLoader());
    if (!modelLoader) {
        return;
    }

    std::set<std::string> allPaths = modelLoader->resolveModePaths(sourceModelPath);
    allPaths.insert(sourceModelPath);
    std::string sourceBaseDir = SAI::Path::parentPath(sourceModelPath);

    for (const auto& filePath : allPaths) {
        if (SAI::FileSystem::exists(filePath)) {
            (void)SAI::FileSystem::copyFilePreservingRelative(filePath, sourceBaseDir, sourceSubDir);
        }
    }

    ModelInfo::SceneInfo sceneInfo = modelLoader->loadModelInfo(sourceModelPath);
    (void)ModelAssetSerializer::saveModelIndex(
        g_engineGlobal.resourceLoader,
        SAI::Path::join(targetDir, "index.edit.dat"),
        sceneInfo);

    ModelUuidFile uuidFileData;
    uuidFileData.uuid = modelUuid;
    uuidFileData.modelRelativePath = SAI::Path::join("source", SAI::Path::fileName(sourceModelPath));
    (void)ModelAssetSerializer::saveModelUuid(
        g_engineGlobal.resourceLoader,
        SAI::Path::join(targetDir, "model.uuid"),
        uuidFileData);

    int64_t timestamp = SAI::FileSystem::getLastWriteTimeSeconds(sourceModelPath);
    (void)ModelAssetSerializer::saveModelTimestamp(
        g_engineGlobal.resourceLoader,
        SAI::Path::join(targetDir, "timestamp.dat"),
        timestamp,
        sourceModelPath);
}

void LauncherComposite::populateDefaultSceneEntities(const std::string& sceneUuid) {
    g_engineGlobal.dataManager->loadScene(sceneUuid);
    g_engineGlobal.dataManager->openScene(sceneUuid);
    Scene::Scene* scene = g_engineGlobal.dataManager->getCurrentScene();
    if (scene) {
        SceneTrunk* trunk = scene->getLoadedTrunk("default");
        if (trunk && trunk->getRegistry()) {
            auto* reg = trunk->getRegistry();
            if (reg->view<CameraComponent>().empty()) {
                auto camEntity = reg->create();
                reg->emplace<NameTag>(camEntity, NameTag{"MainCamera"});
                reg->emplace<PositionComponent>(camEntity, PositionComponent{Vec3(2.8f, 2.2f, 2.8f)});
                reg->emplace<RotationComponent>(camEntity, RotationComponent{Vec3(-22.0f, -135.0f, 0.0f)});
                reg->emplace<CameraComponent>(camEntity, CameraComponent{
                    .active = true,
                    .fov = 60.0f,
                    .nearPlane = 0.1f,
                    .farPlane = 100.0f
                });

                auto lightEntity = reg->create();
                reg->emplace<NameTag>(lightEntity, NameTag{"DirectionalLight"});
                reg->emplace<PositionComponent>(lightEntity, PositionComponent{Vec3(4.0f, 8.0f, 4.0f)});
                reg->emplace<RotationComponent>(lightEntity, RotationComponent{Vec3(-45.0f, 45.0f, 0.0f)});
                DirectionalLightComponent dirLight{};
                dirLight.direction = Vec3(-0.5f, -1.0f, -0.6f);
                dirLight.color = Vec3(1.0f, 0.98f, 0.92f);
                dirLight.intensity = 1.8f;
                dirLight.enabled = true;
                reg->emplace<DirectionalLightComponent>(lightEntity, dirLight);
            } else {
                auto lightView = reg->view<DirectionalLightComponent>();
                for (auto entity : lightView) {
                    auto& dirLight = lightView.get<DirectionalLightComponent>(entity);
                    dirLight.direction = Vec3(-0.5f, -1.0f, -0.6f);
                    dirLight.color = Vec3(1.0f, 0.98f, 0.92f);
                    dirLight.intensity = 1.8f;
                    dirLight.enabled = true;
                }
            }

            if (reg->view<StaticMeshInstance>().empty()) {
                auto floorEntity = reg->create();
                reg->emplace<NameTag>(floorEntity, NameTag{"Floor"});
                reg->emplace<PositionComponent>(floorEntity, PositionComponent{Vec3(0.0f, 0.0f, 0.0f)});
                reg->emplace<RotationComponent>(floorEntity, RotationComponent{Vec3(0.0f, 0.0f, 0.0f)});
                reg->emplace<ScaleComponent>(floorEntity, ScaleComponent{Vec3(2.0f, 1.0f, 2.0f)});
                reg->emplace<StaticMeshInstance>(floorEntity, StaticMeshInstance{kDemoFloorModelUuid, "Object_0"});
                reg->emplace<DiffuseTextureComponent>(floorEntity, DiffuseTextureComponent{kDemoFloorModelUuid, "*0"});
                reg->emplace<NormalTextureComponent>(floorEntity, NormalTextureComponent{kDemoFloorModelUuid, "*2"});

                auto robotEntity = reg->create();
                reg->emplace<NameTag>(robotEntity, NameTag{"Robot"});
                reg->emplace<PositionComponent>(robotEntity, PositionComponent{Vec3(0.0f, 0.0f, 0.0f)});
                reg->emplace<RotationComponent>(robotEntity, RotationComponent{Vec3(0.0f, 45.0f, 0.0f)});
                reg->emplace<ScaleComponent>(robotEntity, ScaleComponent{Vec3(1.0f, 1.0f, 1.0f)});
                reg->emplace<StaticMeshInstance>(robotEntity, StaticMeshInstance{kDemoRobotModelUuid, "Bot_Bot_0"});
                reg->emplace<DiffuseTextureComponent>(robotEntity, DiffuseTextureComponent{kDemoRobotModelUuid, "*0"});
                reg->emplace<NormalTextureComponent>(robotEntity, NormalTextureComponent{kDemoRobotModelUuid, "*3"});

                auto sphereEntity = reg->create();
                reg->emplace<NameTag>(sphereEntity, NameTag{"Sphere"});
                reg->emplace<PositionComponent>(sphereEntity, PositionComponent{Vec3(-1.3f, 0.4f, 0.8f)});
                reg->emplace<RotationComponent>(sphereEntity, RotationComponent{Vec3(0.0f, 0.0f, 0.0f)});
                reg->emplace<ScaleComponent>(sphereEntity, ScaleComponent{Vec3(2.0f, 2.0f, 2.0f)});
                reg->emplace<StaticMeshInstance>(sphereEntity, StaticMeshInstance{kDemoBallModelUuid, "ball"});
            }
        }
        g_engineGlobal.dataManager->saveScene(sceneUuid);
    }
    g_engineGlobal.dataManager->closeScene(sceneUuid);
}

void LauncherComposite::scanProjects() {
    ZoneScoped();
    m_projects.clear();
    m_selectedProjectIndex = -1;

    addOrUpdateProjectEntry(resolveWorkspacePath("Example/DemoProject/project.mq"));
    scanDirectoryForProjects(resolveWorkspacePath("Example"));
    scanDirectoryForProjects(resolveWorkspacePath("Projects"));

    if (!m_projects.empty() && m_selectedProjectIndex < 0) {
        m_selectedProjectIndex = 0;
    }
}

void LauncherComposite::scanDirectoryForProjects(const std::string& rootDir) {
    auto* dataLoader = g_engineGlobal.dataManager ? g_engineGlobal.dataManager->getDataLoader() : nullptr;
    if (!dataLoader || !dataLoader->directoryExists(rootDir)) {
        return;
    }

    auto subDirs = dataLoader->getSubDirectories(rootDir);
    for (const auto& subDir : subDirs) {
        std::string candidateMq = subDir + "/project.mq";
        if (dataLoader->fileExists(candidateMq)) {
            addOrUpdateProjectEntry(candidateMq);
            continue;
        }
        std::string candidateLegacy = subDir + "/project.json";
        if (dataLoader->fileExists(candidateLegacy)) {
            addOrUpdateProjectEntry(candidateLegacy);
        }
    }
}

bool LauncherComposite::addOrUpdateProjectEntry(const std::string& projectFilePath) {
    if (!g_engineGlobal.resourceLoader) {
        return false;
    }

    std::string targetPath = projectFilePath;
    if (SAI::FileSystem::isDirectory(projectFilePath)) {
        std::string mqPath = SAI::Path::join(projectFilePath, Base::Foundation::kDefaultProjectFileName);
        std::string legacyPath = SAI::Path::join(projectFilePath, Base::Foundation::kLegacyProjectFileName);
        targetPath = SAI::FileSystem::exists(mqPath) ? mqPath : legacyPath;
    }

    std::string normalizedPath = SAI::Path::normalize(targetPath);
    auto* engine = Runtime::Engine::getInstance();
    if (!engine || !engine->foundation) {
        return false;
    }
    auto cfgRes = engine->foundation->loadProjectConfig(normalizedPath);
    if (!cfgRes.ok()) {
        return false;
    }

    ProjectConfig cfg = cfgRes.value();
    cfg.projectFilePath = normalizedPath;
    cfg.projectDirectory = SAI::Path::parentPath(normalizedPath);

    for (size_t i = 0; i < m_projects.size(); ++i) {
        if (m_projects[i].projectDirectory == cfg.projectDirectory) {
            m_projects[i] = cfg;
            m_selectedProjectIndex = static_cast<int>(i);
            return true;
        }
    }

    m_projects.push_back(cfg);
    m_selectedProjectIndex = static_cast<int>(m_projects.size()) - 1;
    return true;
}

void LauncherComposite::browseAndOpenProject() {
    const char* filterPatterns[2] = {"*.mq", "*.json"};
    const char* selectedFile = tinyfd_openFileDialog(
        "选择 MQEngine 工程文件 (project.mq)",
        "",
        2,
        filterPatterns,
        "MQEngine Project (project.mq)",
        0);

    if (!selectedFile) {
        return;
    }

    if (addOrUpdateProjectEntry(selectedFile)) {
        m_statusMessage = std::string("成功加载工程: ") + selectedFile;
        m_statusIsError = false;
    } else {
        m_statusMessage = std::string("无效的工程配置文件: ") + selectedFile;
        m_statusIsError = true;
    }
}

void LauncherComposite::createNewProject(const std::string& name,
                                         const std::string& parentDir,
                                         const ProjectScriptOptions& scriptOptions) {
    if (name.empty()) {
        m_statusMessage = "工程名称不能为空";
        m_statusIsError = true;
        return;
    }

    std::string relProjDir = parentDir.empty() ? name : (parentDir + "/" + name);
    std::string projDir = resolveWorkspacePath(relProjDir);
    auto st = initializeProjectDirectory(projDir, name, kDefaultDemoSceneUuid, scriptOptions);
    if (!st.ok()) {
        m_statusMessage = std::string("创建工程失败: ") + std::string(st.message());
        m_statusIsError = true;
        return;
    }

    addOrUpdateProjectEntry(projDir + "/project.mq");
    std::string langSummary = formatSelectedScriptLanguages(scriptOptions);
    m_statusMessage = "已成功创建工程: " + name + " (语言: " + langSummary + ")";
    m_statusIsError = false;
}

void LauncherComposite::launchProjectInApp(const std::string& appName, const ProjectConfig& config) {
    auto exeRes = locateExecutable(appName);
    if (!exeRes.ok()) {
        m_statusMessage = std::string(exeRes.status().message());
        m_statusIsError = true;
        spdlog::error("{}", m_statusMessage);
        return;
    }

    auto st = spawnProcess(exeRes.value(), config.projectFilePath, config.projectDirectory);
    if (!st.ok()) {
        m_statusMessage = std::string("启动失败: ") + std::string(st.message());
        m_statusIsError = true;
        spdlog::error("{}", m_statusMessage);
        return;
    }

    m_statusMessage = "已启动 " + appName + " -> " + config.projectName;
    m_statusIsError = false;
    spdlog::info("{}", m_statusMessage);
}

StatusOr<std::string> LauncherComposite::locateExecutable(const std::string& appName) const {
    std::string exeFileName = appName + ".exe";
    std::string appRelExe = SAI::Path::join(appName, exeFileName);

    SAI::PathSearcher searcher;
    searcher.addCandidatesFromExecutableDir({exeFileName})
            .addCandidatesFromExecutableAncestor(1, {appRelExe})
            .addCandidatesFromBase(m_workspaceRoot, {
                SAI::Path::join("Output/build-windows-debug/Applications", appRelExe),
                SAI::Path::join("Output/build-windows-release/Applications", appRelExe),
                SAI::Path::join("build-windows-debug/Applications", appRelExe),
                SAI::Path::join("Applications", appRelExe)
            });

    auto foundRes = searcher.findFirstFile();
    if (foundRes.ok()) {
        return foundRes.value();
    }

    return NotFoundError("未找到可执行程序 " + exeFileName + "，请先编译 " + appName + " 目标");
}

Status LauncherComposite::spawnProcess(const std::string& exePath,
                                       const std::string& projectFilePath,
                                       const std::string& workingDir) {
    std::string targetWorkDir = workingDir.empty() ? SAI::Path::parentPath(projectFilePath) : workingDir;
    return SAI::System::spawnDetachedProcess(exePath, {"--project", projectFilePath}, targetWorkDir);
}

} // namespace MQEngine
