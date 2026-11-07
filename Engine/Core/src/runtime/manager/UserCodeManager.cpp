#include "UserCodeManager.h"
#include <algorithm>
#include "../core/EngineGlobal.h"
#include "../core/Engine.h"
#include "../core/Runtime.h"
#include "../../scene/manager/DataManager.h"
#include "../../resource/core/DiskResourceLoader.h"
#include "../../resource/core/ResourceLoader.h"
#include "../../render/manager/PipelineRegistries.h"
#include "SystemManager.h"
#include "../../scene/system/ComponentReflection.h"
#include "../../script/system/ScriptSystem.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MQEngine {
namespace Runtime {

using absl::AlreadyExistsError;
using absl::FailedPreconditionError;

using CreateUserCodeFunc = NativeCode* (*)();

namespace {

Engine* ResolveEngine(Engine* provided) {
    return provided ? provided : Engine::getInstance();
}

ResourceLoader* ResolveEngineResourceLoader(Engine* engine) {
    if (engine && engine->resource && engine->resource->getResourceLoader()) {
        return engine->resource->getResourceLoader();
    }
    return g_engineGlobal.resourceLoader;
}

ResourceLoader* ResolveLoaderOrFallback(ResourceLoader* provided, UniquePtr<DiskResourceLoader>& fallbackHolder) {
    if (provided) {
        return provided;
    }
    if (g_engineGlobal.resourceLoader) {
        return g_engineGlobal.resourceLoader;
    }
    fallbackHolder = makeUnique<DiskResourceLoader>();
    return fallbackHolder.get();
}

template <typename T>
void MergeUniqueItems(std::vector<T>& dst, const std::vector<T>& src) {
    for (const auto& item : src) {
        if (std::find(dst.begin(), dst.end(), item) == dst.end()) {
            dst.push_back(item);
        }
    }
}

template <typename T>
void AppendDiffItems(std::vector<T>& dst, const std::vector<T>& before, const std::vector<T>& after) {
    for (const auto& item : after) {
        if (std::find(before.begin(), before.end(), item) == before.end() &&
            std::find(dst.begin(), dst.end(), item) == dst.end()) {
            dst.push_back(item);
        }
    }
}

class ScriptUserCodeAdapter;

/**
 * @brief 脚本 UserCode 默认组合体，将脚本用户代码模块统一纳入 TokenGraph 拓扑排序与调度
 */
class ScriptUserCodeComposite : public Composite {
public:
    ScriptUserCodeComposite(ScriptUserCodeAdapter* owner, std::string name)
        : m_owner(owner), m_name(std::move(name)) {}

    void registerInfo() override {
        compositeRegisterInfo = CompositeRegisterInfo{
            .name = m_name,
            .requiredTags = {"Runtime"},
        };
    }

    Status onLoad(Engine* engine) override;
    Status onInit(Engine* engine) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

private:
    ScriptUserCodeAdapter* m_owner = nullptr;
    std::string m_name;
};

/**
 * @brief JS/TS 脚本用户代码适配器，支持在 Project 下通过脚本定义 NativeCode 模块生命周期
 */
class ScriptUserCodeAdapter : public NativeCode {
public:
    ScriptUserCodeAdapter(std::string name, std::string version, std::string entryPath, std::string moduleDir)
        : m_name(std::move(name))
        , m_version(std::move(version))
        , m_entryPath(std::move(entryPath))
        , m_moduleDir(std::move(moduleDir)) {}

    ~ScriptUserCodeAdapter() override {
        if (m_isLoaded) {
            unloadScript();
        }
    }

    void registerInfo() override {
        if (getComposites().empty()) {
            emplaceComposite<ScriptUserCodeComposite>(this, m_name);
        }
    }

    std::string getName() const override { return m_name; }
    std::string getVersion() const override { return m_version; }

    Status loadScript(Engine* engine) {
        if (m_isLoaded) {
            return OkStatus();
        }
        ScriptSystem* ss = (engine && engine->script && engine->script->getScriptSystem())
            ? engine->script->getScriptSystem()
            : g_engineGlobal.scriptSystem;
        if (!ss) {
            return FailedPreconditionError(StrCat("加载脚本 UserCode [", m_name, "] 失败: ScriptSystem 未初始化"));
        }

        CHECK_STATUS(ss->executePluginScript(
            m_name,
            m_version,
            m_entryPath,
            m_moduleDir,
            ResolveEngineResourceLoader(engine)));

        m_isLoaded = true;
        return ss->invokePluginLifecycle(m_name, "onLoad");
    }

    Status initScript(Engine* engine) {
        ScriptSystem* ss = (engine && engine->script && engine->script->getScriptSystem())
            ? engine->script->getScriptSystem()
            : g_engineGlobal.scriptSystem;
        if (!ss) {
            return FailedPreconditionError(StrCat("初始化脚本 UserCode [", m_name, "] 失败: ScriptSystem 未就绪"));
        }
        return ss->invokePluginLifecycle(m_name, "onInit");
    }

    void tickScriptLogic(float dt) {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!m_isLoaded || !ss) {
            return;
        }
        std::string dtJson = "{\"dt\":" + std::to_string(dt) + "}";
        (void)ss->invokePluginLifecycle(m_name, "onLogicTick", dtJson);
    }

    void tickScriptRender() {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!m_isLoaded || !ss) {
            return;
        }
        (void)ss->invokePluginLifecycle(m_name, "onRenderTick");
    }

    void unloadScript() {
        if (!m_isLoaded) {
            return;
        }
        if (g_engineGlobal.scriptSystem) {
            g_engineGlobal.scriptSystem->unloadPluginScript(m_name);
        }
        m_isLoaded = false;
    }

private:
    std::string m_name;
    std::string m_version;
    std::string m_entryPath;
    std::string m_moduleDir;
    bool m_isLoaded = false;
};

Status ScriptUserCodeComposite::onLoad(Engine* engine) {
    return m_owner ? m_owner->loadScript(engine) : OkStatus();
}

Status ScriptUserCodeComposite::onInit(Engine* engine) {
    return m_owner ? m_owner->initScript(engine) : OkStatus();
}

void ScriptUserCodeComposite::onLogicTick(float dt) {
    if (m_owner) {
        m_owner->tickScriptLogic(dt);
    }
}

void ScriptUserCodeComposite::onRenderTick() {
    if (m_owner) {
        m_owner->tickScriptRender();
    }
}

void ScriptUserCodeComposite::onUnload() {
    if (m_owner) {
        m_owner->unloadScript();
    }
}

} // namespace

UserCodeManager::UserCodeManager() {
    m_requestQueue.subscribe<Request::Load>([this](Request::Load& req) {
        Engine* engine = Engine::getInstance();
        std::string baseDir = req.baseDir;
        if (baseDir.empty() && engine && engine->foundation &&
            engine->foundation->isProjectOpen()) {
            baseDir = engine->foundation->getProjectDirectory();
        }
        Status st = resolveAndPreloadUserCode(req.userCodeIdent, baseDir, engine);
        if (st.ok()) {
            m_pendingGraphRefresh = true;
        }
        return st;
    });

    m_requestQueue.subscribe<Request::Unload>([this](Request::Unload& req) {
        return unloadUserCode(req.moduleName, Engine::getInstance());
    });

    m_requestQueue.subscribe<Request::Reload>([this](Request::Reload& req) {
        Engine* engine = Engine::getInstance();
        if (req.moduleName.empty()) {
            return reloadAll(engine);
        }
        return reloadUserCode(req.moduleName, engine);
    });
}

void UserCodeManager::requestLoadUserCode(const std::string& userCodeIdent, const std::string& baseDir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_requestQueue.enqueue(Request::Load{userCodeIdent, baseDir});
}

void UserCodeManager::requestUnloadUserCode(const std::string& moduleName) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_requestQueue.enqueue(Request::Unload{moduleName});
}

void UserCodeManager::requestReloadUserCode(const std::string& moduleName) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_requestQueue.enqueue(Request::Reload{moduleName});
}

void UserCodeManager::syncTicker() {
    ZoneScoped();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    while (!m_requestQueue.empty()) {
        auto st = m_requestQueue.processNext();
        if (!st.ok()) {
            spdlog::error("UserCodeManager::syncTicker 处理请求失败: {}", st.message());
        }
    }
    if (m_pendingGraphRefresh) {
        m_pendingGraphRefresh = false;
        auto st = refreshComposites(Engine::getInstance());
        if (!st.ok()) {
            spdlog::error("UserCodeManager::syncTicker 拓扑排序激发 Composite 失败: {}", st.message());
        }
    }
}

void UserCodeManager::checkHotReload(Engine* engine) {
    if (!m_autoHotReload) {
        return;
    }
    (void)checkAndReloadModified(ResolveEngine(engine));
}

UserCodeManager::~UserCodeManager() {
    unloadAll();
}

Status UserCodeManager::registerUserCode(std::shared_ptr<NativeCode> userCode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!userCode) {
        return InvalidArgumentError("registerUserCode: 用户代码实例指针为空");
    }

    std::string name = userCode->getName();
    if (name.empty()) {
        return InvalidArgumentError("registerUserCode: 用户代码模块名称为空");
    }

    if (m_userCodes.find(name) != m_userCodes.end()) {
        return AlreadyExistsError(StrCat("用户代码模块已存在: ", name));
    }

    userCode->registerInfo();

    UserCodeEntry entry;
    entry.name = name;
    entry.userCode = std::move(userCode);
    entry.state = UserCodeState::Unloaded;
    registerUserCodeComposites(entry);
    m_userCodes.emplace(name, std::move(entry));
    return OkStatus();
}

Status UserCodeManager::registerUserCodeFactory(const std::string& name, UserCodeFactory factory) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (name.empty()) {
        return InvalidArgumentError("registerUserCodeFactory: 名称为空");
    }
    if (!factory) {
        return InvalidArgumentError("registerUserCodeFactory: 工厂函数为空");
    }
    m_factories[name] = std::move(factory);
    return OkStatus();
}

Status UserCodeManager::ensureUserCodeInstance(const std::string& name) {
    auto it = m_userCodes.find(name);
    if (it != m_userCodes.end() && it->second.userCode) {
        return OkStatus();
    }

    auto factoryIt = m_factories.find(name);
    if (factoryIt != m_factories.end()) {
        auto instance = factoryIt->second();
        if (!instance) {
            return InternalError(StrCat("用户代码工厂创建实例失败: ", name));
        }
        instance->registerInfo();
        UserCodeEntry entry;
        entry.name = name;
        entry.userCode = std::move(instance);
        entry.state = UserCodeState::Unloaded;
        registerUserCodeComposites(entry);
        m_userCodes[name] = std::move(entry);
        return OkStatus();
    }

    return NotFoundError(StrCat("未找到已注册的用户代码模块或工厂: ", name));
}

bool UserCodeManager::isCompositeTagsMatched(const std::vector<std::string>& requiredTags, Engine* engine) const {
    if (requiredTags.empty()) {
        return true;
    }
    engine = ResolveEngine(engine);
    if (engine && engine->host && engine->host->getPluginManager()) {
        return engine->host->getPluginManager()->isTagsMatched(requiredTags);
    }
    return true;
}

void UserCodeManager::registerUserCodeComposites(UserCodeEntry& entry) {
    if (!entry.userCode) {
        return;
    }
    for (const auto& compPtr : entry.userCode->getComposites()) {
        if (!compPtr) {
            continue;
        }
        compPtr->registerInfo();
        std::string compName = compPtr->getName();
        if (compName.empty()) {
            continue;
        }
        if (std::find(entry.compositeNames.begin(), entry.compositeNames.end(), compName) == entry.compositeNames.end()) {
            entry.compositeNames.push_back(compName);
        }
        auto existingIt = m_composites.find(compName);
        if (existingIt == m_composites.end()) {
            CompositeEntry compEntry;
            compEntry.name = compName;
            compEntry.ownerPluginName = entry.name;
            compEntry.composite = compPtr.get();
            compEntry.state = PluginState::Unloaded;
            m_composites.emplace(compName, std::move(compEntry));
        } else {
            existingIt->second.ownerPluginName = entry.name;
            existingIt->second.composite = compPtr.get();
        }
    }
}

Status UserCodeManager::preloadUserCode(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    CHECK_STATUS(ensureUserCodeInstance(name));

    auto& entry = m_userCodes[name];
    if (entry.state == UserCodeState::Preloaded ||
        entry.state == UserCodeState::Loaded ||
        entry.state == UserCodeState::Initialized) {
        return OkStatus();
    }

    entry.userCode->registerInfo();
    registerUserCodeComposites(entry);
    entry.state = UserCodeState::Preloaded;

    if (std::find(m_loadOrder.begin(), m_loadOrder.end(), name) == m_loadOrder.end()) {
        m_loadOrder.push_back(name);
    }

    spdlog::info("UserCode [{}] 已完成预加载与 Composite 注册 (版本: {}, Composite 数量: {})",
                 name, entry.userCode->getVersion(), entry.compositeNames.size());
    return OkStatus();
}

Status UserCodeManager::ensureOwnerUserCodeLoaded(UserCodeEntry& entry, Engine* engine) {
    if (entry.state == UserCodeState::Loaded || entry.state == UserCodeState::Initialized) {
        return OkStatus();
    }
    if (!entry.userCode) {
        return FailedPreconditionError(StrCat("UserCode 实例为空: ", entry.name));
    }

    ComponentReflection* compRefl = (engine && engine->foundation)
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    SystemManager* sysMgr = (engine && engine->host)
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;
    PluginManager* pm = (engine && engine->host)
        ? engine->host->getPluginManager()
        : nullptr;

    std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
    std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
    std::vector<uint64_t> beforeServices = pm ? pm->getRegisteredServiceIds() : std::vector<uint64_t>{};

    Status status = entry.userCode->onLoad(engine);

    if (compRefl) {
        AppendDiffItems(entry.registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
    }
    if (sysMgr) {
        AppendDiffItems(entry.registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
        sysMgr->flushRequests();
    }
    if (pm) {
        AppendDiffItems(entry.registeredServices, beforeServices, pm->getRegisteredServiceIds());
    }

    if (!status.ok()) {
        entry.state = UserCodeState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("UserCode [{}] onLoad 失败: {}", entry.name, status.message());
        return status;
    }

    entry.state = UserCodeState::Loaded;
    registerUserCodeComposites(entry);

    if (std::find(m_loadOrder.begin(), m_loadOrder.end(), entry.name) == m_loadOrder.end()) {
        m_loadOrder.push_back(entry.name);
    }
    return OkStatus();
}

Status UserCodeManager::ensureOwnerUserCodeInitialized(UserCodeEntry& entry, Engine* engine) {
    if (entry.state == UserCodeState::Initialized) {
        return OkStatus();
    }
    if (entry.state == UserCodeState::Unloaded || entry.state == UserCodeState::Preloaded) {
        CHECK_STATUS(ensureOwnerUserCodeLoaded(entry, engine));
    }
    if (entry.state != UserCodeState::Loaded) {
        return FailedPreconditionError(StrCat("UserCode [", entry.name, "] 处于非就绪状态: ", static_cast<int>(entry.state)));
    }

    ComponentReflection* compRefl = (engine && engine->foundation)
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    SystemManager* sysMgr = (engine && engine->host)
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;
    PluginManager* pm = (engine && engine->host)
        ? engine->host->getPluginManager()
        : nullptr;

    std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
    std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
    std::vector<uint64_t> beforeServices = pm ? pm->getRegisteredServiceIds() : std::vector<uint64_t>{};

    Status status = entry.userCode->onInit(engine);

    if (compRefl) {
        AppendDiffItems(entry.registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
    }
    if (sysMgr) {
        AppendDiffItems(entry.registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
        sysMgr->flushRequests();
    }
    if (pm) {
        AppendDiffItems(entry.registeredServices, beforeServices, pm->getRegisteredServiceIds());
    }

    if (!status.ok()) {
        entry.state = UserCodeState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("UserCode [{}] onInit 失败: {}", entry.name, status.message());
        return status;
    }

    registerUserCodeComposites(entry);
    entry.state = UserCodeState::Initialized;
    return OkStatus();
}

Status UserCodeManager::refreshComposites(Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);

    ComponentReflection* compRefl = (engine && engine->foundation)
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    SystemManager* sysMgr = (engine && engine->host)
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;
    PluginManager* pm = (engine && engine->host)
        ? engine->host->getPluginManager()
        : nullptr;

    // 1. 先检查已入图但因 Tag 变更不再满足包含匹配条件的 Composite，按逆拓扑序卸载并移出 TokenGraph
    auto currentOrder = m_compositeGraph.order();
    for (auto it = currentOrder.rbegin(); it != currentOrder.rend(); ++it) {
        CompositeEntry* compEntry = *it;
        if (!compEntry || !compEntry->composite) {
            continue;
        }
        if (!isCompositeTagsMatched(compEntry->composite->getRequiredTags(), engine)) {
            unloadSingleComposite(*compEntry, engine);
            m_compositeGraph.removeNode(compEntry->name);
        }
    }

    // 2. 将所有归属 UserCode 已预加载/已加载且满足 Tag 包含匹配的 Composite 节点加入 TokenGraph
    for (auto& [compName, compEntry] : m_composites) {
        if (!compEntry.composite) {
            continue;
        }
        auto ucIt = m_userCodes.find(compEntry.ownerPluginName);
        if (ucIt == m_userCodes.end() ||
            (ucIt->second.state != UserCodeState::Preloaded &&
             ucIt->second.state != UserCodeState::Loaded &&
             ucIt->second.state != UserCodeState::Initialized)) {
            continue;
        }

        if (isCompositeTagsMatched(compEntry.composite->getRequiredTags(), engine)) {
            FCT::Vector<std::string> preds;
            for (const auto& p : compEntry.composite->getPredecessors()) {
                preds.push_back(p);
            }
            FCT::Vector<std::string> succs;
            for (const auto& s : compEntry.composite->getSuccessors()) {
                succs.push_back(s);
            }
            m_compositeGraph.addNode(compName, &compEntry, std::move(preds), std::move(succs));
        }
    }

    // 3. 在所有 UserCode 预加载注册完成后，统一对 TokenGraph 执行拓扑排序
    Status topoStatus = m_compositeGraph.update();
    if (!topoStatus.ok()) {
        spdlog::error("UserCodeManager: Composite TokenGraph 拓扑排序失败（检测到循环依赖）: {}", topoStatus.message());
        return topoStatus;
    }

    auto sortedComposites = m_compositeGraph.order();

    // 4. 按拓扑正序确保所属 UserCode 已 onLoad，并执行未加载 Composite 的 onLoad
    for (CompositeEntry* compEntry : sortedComposites) {
        if (!compEntry || !compEntry->composite || compEntry->state != PluginState::Unloaded) {
            continue;
        }
        auto ucIt = m_userCodes.find(compEntry->ownerPluginName);
        if (ucIt != m_userCodes.end()) {
            CHECK_STATUS(ensureOwnerUserCodeLoaded(ucIt->second, engine));
        }

        std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
        std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
        std::vector<uint64_t> beforeServices = pm ? pm->getRegisteredServiceIds() : std::vector<uint64_t>{};

        Status loadSt = compEntry->composite->onLoad(engine);

        if (compRefl) {
            AppendDiffItems(compEntry->registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
        }
        if (sysMgr) {
            AppendDiffItems(compEntry->registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
        }
        if (pm) {
            AppendDiffItems(compEntry->registeredServices, beforeServices, pm->getRegisteredServiceIds());
        }

        if (!loadSt.ok()) {
            compEntry->state = PluginState::Failed;
            compEntry->errorMessage = std::string(loadSt.message());
            spdlog::error("UserCode Composite [{}] (所属模块: {}) onLoad 失败: {}",
                          compEntry->name, compEntry->ownerPluginName, loadSt.message());
            return loadSt;
        }
        compEntry->state = PluginState::Loaded;
    }

    // 5. 按拓扑正序确保所属 UserCode 已 onInit，并执行已加载 Composite 的 onInit
    for (CompositeEntry* compEntry : sortedComposites) {
        if (!compEntry || !compEntry->composite || compEntry->state != PluginState::Loaded) {
            continue;
        }
        auto ucIt = m_userCodes.find(compEntry->ownerPluginName);
        if (ucIt != m_userCodes.end()) {
            CHECK_STATUS(ensureOwnerUserCodeInitialized(ucIt->second, engine));
        }

        std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
        std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
        std::vector<uint64_t> beforeServices = pm ? pm->getRegisteredServiceIds() : std::vector<uint64_t>{};

        Status initSt = compEntry->composite->onInit(engine);

        if (compRefl) {
            AppendDiffItems(compEntry->registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
        }
        if (sysMgr) {
            AppendDiffItems(compEntry->registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
            sysMgr->flushRequests();
        }
        if (pm) {
            AppendDiffItems(compEntry->registeredServices, beforeServices, pm->getRegisteredServiceIds());
        }

        if (!initSt.ok()) {
            compEntry->state = PluginState::Failed;
            compEntry->errorMessage = std::string(initSt.message());
            spdlog::error("UserCode Composite [{}] (所属模块: {}) onInit 失败: {}",
                          compEntry->name, compEntry->ownerPluginName, initSt.message());
            return initSt;
        }
        compEntry->state = PluginState::Initialized;
        spdlog::info("UserCode Composite [{}] (模块: {}) 已按拓扑序激发初始化",
                     compEntry->name, compEntry->ownerPluginName);
    }

    // 6. 对于已预加载但无匹配 Composite 的模块，确保其完成基础生命周期
    for (const auto& name : m_loadOrder) {
        auto it = m_userCodes.find(name);
        if (it != m_userCodes.end() &&
            (it->second.state == UserCodeState::Preloaded || it->second.state == UserCodeState::Loaded)) {
            CHECK_STATUS(ensureOwnerUserCodeInitialized(it->second, engine));
        }
    }

    return OkStatus();
}

std::vector<std::string> UserCodeManager::getActiveCompositeOrder() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> result;
    for (CompositeEntry* entry : m_compositeGraph.order()) {
        if (entry && entry->state == PluginState::Initialized) {
            result.push_back(entry->name);
        }
    }
    return result;
}

const CompositeEntry* UserCodeManager::getCompositeEntry(const std::string& compositeName) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_composites.find(compositeName);
    return it != m_composites.end() ? &it->second : nullptr;
}

Status UserCodeManager::loadUserCode(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadUserCode(name, engine));
    auto& entry = m_userCodes[name];
    return ensureOwnerUserCodeLoaded(entry, engine);
}

Status UserCodeManager::initUserCode(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    auto it = m_userCodes.find(name);
    if (it == m_userCodes.end()) {
        return NotFoundError(StrCat("未找到 UserCode 模块: ", name));
    }
    CHECK_STATUS(ensureOwnerUserCodeInitialized(it->second, engine));
    return refreshComposites(engine);
}

Status UserCodeManager::loadAndInitUserCode(const std::string& name, Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadUserCode(name, engine));
    return refreshComposites(engine);
}

Status UserCodeManager::loadAll(Engine* engine) {
    engine = ResolveEngine(engine);
    std::vector<std::string> namesToLoad;
    for (const auto& pair : m_userCodes) {
        namesToLoad.push_back(pair.first);
    }
    for (const auto& pair : m_factories) {
        if (std::find(namesToLoad.begin(), namesToLoad.end(), pair.first) == namesToLoad.end()) {
            namesToLoad.push_back(pair.first);
        }
    }

    for (const auto& name : namesToLoad) {
        CHECK_STATUS(preloadUserCode(name, engine));
    }
    return OkStatus();
}

Status UserCodeManager::initAll(Engine* engine) {
    engine = ResolveEngine(engine);
    return refreshComposites(engine);
}

void UserCodeManager::logicTick(float dt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& name : m_loadOrder) {
        auto it = m_userCodes.find(name);
        if (it != m_userCodes.end() && it->second.state == UserCodeState::Initialized && it->second.userCode) {
            it->second.userCode->onLogicTick(dt);
        }
    }
    for (CompositeEntry* compEntry : m_compositeGraph.order()) {
        if (compEntry && compEntry->composite && compEntry->state == PluginState::Initialized) {
            compEntry->composite->onLogicTick(dt);
        }
    }
}

void UserCodeManager::renderTick() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& name : m_loadOrder) {
        auto it = m_userCodes.find(name);
        if (it != m_userCodes.end() && it->second.state == UserCodeState::Initialized && it->second.userCode) {
            it->second.userCode->onRenderTick();
        }
    }
    for (CompositeEntry* compEntry : m_compositeGraph.order()) {
        if (compEntry && compEntry->composite && compEntry->state == PluginState::Initialized) {
            compEntry->composite->onRenderTick();
        }
    }
}

void UserCodeManager::cleanupRegistrations(std::vector<std::string>& components,
                                           std::vector<std::string>& systems,
                                           std::vector<uint64_t>& services,
                                           Engine* engine) {
    engine = ResolveEngine(engine);
    SystemManager* sysMgr = (engine && engine->host && engine->host->getSystemManager())
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;
    DataManager* dataMgr = (engine && engine->scene && engine->scene->getDataManager())
        ? engine->scene->getDataManager()
        : g_engineGlobal.dataManager;
    ComponentReflection* compRefl = (engine && engine->foundation && engine->foundation->getComponentReflection())
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    PluginManager* pm = (engine && engine->host && engine->host->getPluginManager())
        ? engine->host->getPluginManager()
        : nullptr;

    if (sysMgr && !systems.empty()) {
        for (const auto& sysName : systems) {
            sysMgr->requestRemoveSystem(sysName);
        }
        sysMgr->flushRequests();
    }
    systems.clear();

    if (!components.empty()) {
        if (compRefl && dataMgr) {
            compRefl->purgeComponentPools(dataMgr->currentRegistries(), components);
        }
        for (const auto& compName : components) {
            ComponentRegistry::instance().unregisterComponent(compName);
            if (compRefl) {
                compRefl->unregisterComponent(compName);
            }
        }
        components.clear();
    }

    if (pm && !services.empty()) {
        for (uint64_t serviceId : services) {
            pm->unregisterRawService(serviceId);
        }
    }
    services.clear();
}

void UserCodeManager::cleanupUserCodeRegistrations(UserCodeEntry& entry, Engine* engine) {
    cleanupRegistrations(entry.registeredComponents, entry.registeredSystems, entry.registeredServices, engine);
}

void UserCodeManager::unloadSingleComposite(CompositeEntry& compEntry, Engine* engine) {
    if (compEntry.composite &&
        (compEntry.state == PluginState::Initialized || compEntry.state == PluginState::Loaded)) {
        compEntry.composite->onUnload();
    }
    cleanupRegistrations(
        compEntry.registeredComponents,
        compEntry.registeredSystems,
        compEntry.registeredServices,
        engine);
    compEntry.state = PluginState::Unloaded;
}

void UserCodeManager::unloadUserCodeComposites(UserCodeEntry& entry, Engine* engine) {
    if (entry.compositeNames.empty()) {
        return;
    }

    auto sortedOrder = m_compositeGraph.order();
    for (auto it = sortedOrder.rbegin(); it != sortedOrder.rend(); ++it) {
        CompositeEntry* compEntry = *it;
        if (compEntry && compEntry->ownerPluginName == entry.name) {
            unloadSingleComposite(*compEntry, engine);
            m_compositeGraph.removeNode(compEntry->name);
        }
    }

    for (const auto& compName : entry.compositeNames) {
        auto compIt = m_composites.find(compName);
        if (compIt != m_composites.end()) {
            unloadSingleComposite(compIt->second, engine);
            m_compositeGraph.removeNode(compName);
            m_composites.erase(compIt);
        }
    }
    entry.compositeNames.clear();
    (void)m_compositeGraph.update();
}

Status UserCodeManager::unloadUserCode(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    auto it = m_userCodes.find(name);
    if (it == m_userCodes.end()) {
        return NotFoundError(StrCat("未找到 UserCode 模块: ", name));
    }

    auto& entry = it->second;
    unloadUserCodeComposites(entry, engine);

    if (entry.userCode && (entry.state == UserCodeState::Initialized || entry.state == UserCodeState::Loaded)) {
        entry.userCode->onUnload();
        spdlog::info("UserCode [{}] 已卸载", name);
    }

    cleanupUserCodeRegistrations(entry, engine);
    entry.state = UserCodeState::Unloaded;
    freeDynamicLibrary(entry, ResolveEngineResourceLoader(engine));

    auto orderIt = std::find(m_loadOrder.begin(), m_loadOrder.end(), name);
    if (orderIt != m_loadOrder.end()) {
        m_loadOrder.erase(orderIt);
    }

    return OkStatus();
}

void UserCodeManager::unloadAll() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> reverseOrder = m_loadOrder;
    std::reverse(reverseOrder.begin(), reverseOrder.end());

    for (const auto& name : reverseOrder) {
        (void)unloadUserCode(name, Engine::getInstance());
    }
    m_loadOrder.clear();
}

std::string UserCodeManager::createShadowCopy(const std::string& resolvedPath, ResourceLoader* resourceLoader) {
    UniquePtr<DiskResourceLoader> fallbackLoader;
    resourceLoader = ResolveLoaderOrFallback(resourceLoader, fallbackLoader);

    std::string parentDir = SAI::Path::parentPath(resolvedPath);
    std::string stem = SAI::Path::stem(resolvedPath);
    std::string ext = SAI::Path::extension(resolvedPath);
    if (ext.empty()) {
        ext = ".dll";
    }

    uint32_t pid = 0;
#ifdef _WIN32
    pid = static_cast<uint32_t>(GetCurrentProcessId());
#endif
    ++m_shadowCounter;

    std::string shadowFileName = StrCat(stem, ".__hotreload_", pid, "_", m_shadowCounter, ext);
    std::string shadowPath = SAI::Path::normalize(SAI::Path::join(parentDir, shadowFileName));
    if (resourceLoader->copyFile(resolvedPath, shadowPath, true).ok()) {
        return shadowPath;
    }
    return "";
}

std::string UserCodeManager::resolveDynamicLibraryPath(const std::string& inputPath, const std::string& baseDir) const {
    if (SAI::FileSystem::isFile(inputPath)) {
        return SAI::Path::normalize(inputPath);
    }

    std::string rawFileName = SAI::Path::fileName(inputPath);
    std::string stemName = SAI::Path::stem(inputPath);
    std::string dllFileName = SAI::Path::ensureExtension(rawFileName, ".dll", {".dll", ".so"});
    if (!SAI::Path::hasAnyExtension(rawFileName, {".dll", ".so"})) {
        stemName = rawFileName;
    }

    std::string userCodeSubPath = SAI::Path::join("UserCode", dllFileName);
    std::string binSubPath = SAI::Path::join("bin", dllFileName);
    std::string stemSubPath = SAI::Path::join(stemName, dllFileName);
    std::string debugUserCodePath = SAI::Path::join("Output/build-windows-debug/UserCode", dllFileName);
    std::string releaseUserCodePath = SAI::Path::join("Output/build-windows-release/UserCode", dllFileName);
    std::string debugStemPath = SAI::Path::join("Output/build-windows-debug", stemName, dllFileName);
    std::string releaseStemPath = SAI::Path::join("Output/build-windows-release", stemName, dllFileName);

    SAI::PathSearcher searcher;
    searcher.addCandidatesFromBase(baseDir, {inputPath, dllFileName, userCodeSubPath, binSubPath})
            .addCandidatesFromExecutableDir({dllFileName, userCodeSubPath, stemSubPath})
            .addCandidatesFromExecutableAncestor(2, {userCodeSubPath, stemSubPath})
            .addCandidatesUpwardFromCurrentDir(6, {
                inputPath,
                dllFileName,
                userCodeSubPath,
                debugUserCodePath,
                releaseUserCodePath,
                debugStemPath,
                releaseStemPath
            });

    return searcher.resolveFirstFile(inputPath);
}

std::string UserCodeManager::resolveScriptUserCodePath(const std::string& inputPath, const std::string& baseDir) const {
    auto isValidScriptFile = [](const std::string& candidate) -> bool {
        return SAI::FileSystem::isFile(candidate) &&
               SAI::Path::hasAnyExtension(candidate, {".ts", ".js"});
    };

    if (isValidScriptFile(inputPath)) {
        return SAI::Path::normalize(inputPath);
    }

    SAI::PathSearcher searcher;
    searcher.addCandidatesFromBase(baseDir, {
        inputPath,
        SAI::Path::join("UserCode", inputPath),
        SAI::Path::join("Assets/Scripts", inputPath),
        SAI::Path::join("Assets/Scripts/ts", inputPath),
        SAI::Path::join("Assets/Scripts/js", inputPath)
    });

    return searcher.resolveFirstMatching(isValidScriptFile);
}

Status UserCodeManager::preloadDynamicUserCode(const std::string& libraryPath, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
#ifdef _WIN32
    UniquePtr<DiskResourceLoader> fallbackLoader;
    ResourceLoader* loader = ResolveLoaderOrFallback(ResolveEngineResourceLoader(engine), fallbackLoader);

    std::string resolvedPath = resolveDynamicLibraryPath(libraryPath);
    if (!loader->exists(resolvedPath)) {
        return NotFoundError(StrCat("未找到 UserCode 动态库文件: ", resolvedPath));
    }

    int64_t writeTime = loader->getLastWriteTimeSeconds(resolvedPath);
    std::string shadowPath = createShadowCopy(resolvedPath, loader);
    std::string loadTargetPath = shadowPath.empty() ? resolvedPath : shadowPath;

    HMODULE handle = LoadLibraryA(loadTargetPath.c_str());
    if (!handle) {
        DWORD err = GetLastError();
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return NotFoundError(StrCat("加载 UserCode 动态链接库失败: ", loadTargetPath, ", Windows错误码: ", err));
    }

    auto createFn = reinterpret_cast<CreateUserCodeFunc>(GetProcAddress(handle, "CreateUserCode"));
    if (!createFn) {
        FreeLibrary(handle);
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return InvalidArgumentError(StrCat("UserCode 动态库未导出 CreateUserCode 函数: ", resolvedPath));
    }

    NativeCode* rawUserCode = createFn();
    if (!rawUserCode) {
        FreeLibrary(handle);
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return InternalError(StrCat("CreateUserCode 返回空指针: ", resolvedPath));
    }

    std::string moduleName = rawUserCode->getName();
    auto existingIt = m_userCodes.find(moduleName);
    if (existingIt != m_userCodes.end() &&
        (existingIt->second.state == UserCodeState::Preloaded ||
         existingIt->second.state == UserCodeState::Loaded ||
         existingIt->second.state == UserCodeState::Initialized)) {
        delete rawUserCode;
        FreeLibrary(handle);
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return OkStatus();
    }

    uint32_t prevReloadCount = (existingIt != m_userCodes.end()) ? existingIt->second.reloadCount : 0;
    auto userCodePtr = std::shared_ptr<NativeCode>(rawUserCode);

    UserCodeEntry entry;
    entry.name = moduleName;
    entry.userCode = userCodePtr;
    entry.state = UserCodeState::Unloaded;
    entry.isDynamic = true;
    entry.modulePath = resolvedPath;
    entry.sourcePath = resolvedPath;
    entry.shadowLibraryPath = shadowPath;
    entry.libraryHandle = handle;
    entry.lastWriteTimeSeconds = writeTime;
    entry.reloadCount = prevReloadCount;

    m_userCodes[moduleName] = std::move(entry);
    return preloadUserCode(moduleName, engine);
#else
    (void)libraryPath;
    (void)engine;
    return UnimplementedError("非Windows平台的动态 UserCode 加载尚未配置");
#endif
}

Status UserCodeManager::preloadScriptUserCode(const std::string& scriptOrDirPath, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    UniquePtr<DiskResourceLoader> fallbackLoader;
    ResourceLoader* loader = ResolveLoaderOrFallback(ResolveEngineResourceLoader(engine), fallbackLoader);

    std::string resolvedPath = resolveScriptUserCodePath(scriptOrDirPath);
    if (resolvedPath.empty()) {
        resolvedPath = scriptOrDirPath;
    }

    if (!loader->exists(resolvedPath)) {
        return NotFoundError(StrCat("未找到脚本 UserCode 文件: ", resolvedPath));
    }

    std::string normalizedPath = SAI::Path::normalize(resolvedPath);
    std::string moduleName = SAI::Path::stem(normalizedPath);
    auto existingIt = m_userCodes.find(moduleName);
    if (existingIt != m_userCodes.end() &&
        (existingIt->second.state == UserCodeState::Preloaded ||
         existingIt->second.state == UserCodeState::Loaded ||
         existingIt->second.state == UserCodeState::Initialized)) {
        return OkStatus();
    }

    uint32_t prevReloadCount = (existingIt != m_userCodes.end()) ? existingIt->second.reloadCount : 0;
    std::string moduleDir = SAI::Path::normalize(SAI::Path::parentPath(normalizedPath));
    auto scriptUserCode = std::make_shared<ScriptUserCodeAdapter>(moduleName, "1.0.0", normalizedPath, moduleDir);

    UserCodeEntry entry;
    entry.name = moduleName;
    entry.userCode = scriptUserCode;
    entry.state = UserCodeState::Unloaded;
    entry.isScript = true;
    entry.modulePath = normalizedPath;
    entry.sourcePath = normalizedPath;
    entry.lastWriteTimeSeconds = loader->getLastWriteTimeSeconds(normalizedPath);
    entry.reloadCount = prevReloadCount;

    m_userCodes[moduleName] = std::move(entry);
    return preloadUserCode(moduleName, engine);
}

Status UserCodeManager::resolveAndPreloadUserCode(const std::string& userCodeIdent,
                                                  const std::string& baseDir,
                                                  Engine* engine) {
    engine = ResolveEngine(engine);
    auto existingIt = m_userCodes.find(userCodeIdent);
    if (existingIt != m_userCodes.end() && existingIt->second.userCode) {
        return preloadUserCode(userCodeIdent, engine);
    }
    if (m_factories.find(userCodeIdent) != m_factories.end()) {
        return preloadUserCode(userCodeIdent, engine);
    }

    std::string scriptCandidate = resolveScriptUserCodePath(userCodeIdent, baseDir);
    if (!scriptCandidate.empty()) {
        return preloadScriptUserCode(scriptCandidate, engine);
    }

    if (SAI::Path::hasAnyExtension(userCodeIdent, {".ts", ".js"})) {
        return preloadScriptUserCode(userCodeIdent, engine);
    }

    std::string dllCandidate = resolveDynamicLibraryPath(userCodeIdent, baseDir);
    if (SAI::FileSystem::exists(dllCandidate) || SAI::Path::hasAnyExtension(userCodeIdent, {".dll", ".so"})) {
        return preloadDynamicUserCode(dllCandidate, engine);
    }

    return NotFoundError(StrCat("无法解析或找到 UserCode 模块: ", userCodeIdent));
}

Status UserCodeManager::loadDynamicUserCode(const std::string& libraryPath, Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadDynamicUserCode(libraryPath, engine));
    return refreshComposites(engine);
}

Status UserCodeManager::loadScriptUserCode(const std::string& scriptOrDirPath, Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadScriptUserCode(scriptOrDirPath, engine));
    return refreshComposites(engine);
}

Status UserCodeManager::resolveAndLoadUserCode(const std::string& userCodeIdent,
                                               const std::string& baseDir,
                                               Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(resolveAndPreloadUserCode(userCodeIdent, baseDir, engine));
    return refreshComposites(engine);
}

Status UserCodeManager::reloadUserCode(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    auto it = m_userCodes.find(name);
    if (it == m_userCodes.end()) {
        return resolveAndLoadUserCode(name, "", engine);
    }

    UserCodeEntry& entry = it->second;
    std::string sourcePath = entry.sourcePath.empty() ? entry.modulePath : entry.sourcePath;
    bool isDynamic = entry.isDynamic;
    bool isScript = entry.isScript;
    uint32_t nextReloadCount = entry.reloadCount + 1;

    std::string customState;
    if (entry.userCode && (entry.state == UserCodeState::Initialized || entry.state == UserCodeState::Loaded)) {
        customState = entry.userCode->onSerializeHotReloadState();
    }

    std::unordered_map<std::string, std::string> compositeStates;
    std::vector<std::string> allComponentsToCapture = entry.registeredComponents;
    for (const auto& compName : entry.compositeNames) {
        auto compIt = m_composites.find(compName);
        if (compIt != m_composites.end() && compIt->second.composite) {
            MergeUniqueItems(allComponentsToCapture, compIt->second.registeredComponents);
            if (compIt->second.state == PluginState::Initialized || compIt->second.state == PluginState::Loaded) {
                std::string compState = compIt->second.composite->onSerializeHotReloadState();
                if (!compState.empty()) {
                    compositeStates[compName] = std::move(compState);
                }
            }
        }
    }

    ComponentHotReloadSnapshot compSnapshot;
    ComponentReflection* compRefl = (engine && engine->foundation && engine->foundation->getComponentReflection())
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    DataManager* dataMgr = (engine && engine->scene && engine->scene->getDataManager())
        ? engine->scene->getDataManager()
        : g_engineGlobal.dataManager;

    if (compRefl && dataMgr && !allComponentsToCapture.empty()) {
        compSnapshot = compRefl->captureSnapshot(dataMgr->currentRegistries(), allComponentsToCapture);
    }

    CHECK_STATUS(unloadUserCode(name, engine));

    Status reloadStatus = OkStatus();
    if (isDynamic) {
        reloadStatus = loadDynamicUserCode(sourcePath, engine);
    } else if (isScript) {
        reloadStatus = loadScriptUserCode(sourcePath, engine);
    } else {
        reloadStatus = loadAndInitUserCode(name, engine);
    }

    if (!reloadStatus.ok()) {
        spdlog::error("UserCode [{}] 热重载失败: {}", name, reloadStatus.message());
        return reloadStatus;
    }

    auto reloadedIt = m_userCodes.find(name);
    if (reloadedIt != m_userCodes.end()) {
        reloadedIt->second.reloadCount = nextReloadCount;
        if (compRefl && !compSnapshot.empty()) {
            compRefl->restoreSnapshot(compSnapshot);
        }
        if (reloadedIt->second.userCode && !customState.empty()) {
            reloadedIt->second.userCode->onRestoreHotReloadState(customState);
        }
        for (const auto& [compName, savedState] : compositeStates) {
            auto compIt = m_composites.find(compName);
            if (compIt != m_composites.end() && compIt->second.composite) {
                compIt->second.composite->onRestoreHotReloadState(savedState);
            }
        }
    }

    spdlog::info("UserCode [{}] 热重载完成 (第 {} 次重载)", name, nextReloadCount);
    return OkStatus();
}

Status UserCodeManager::reloadAll(Engine* engine) {
    engine = ResolveEngine(engine);
    std::vector<std::string> names = getAllUserCodeNames();
    for (const auto& name : names) {
        CHECK_STATUS(reloadUserCode(name, engine));
    }
    return OkStatus();
}

int UserCodeManager::checkAndReloadModified(Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    UniquePtr<DiskResourceLoader> fallbackLoader;
    ResourceLoader* loader = ResolveLoaderOrFallback(ResolveEngineResourceLoader(engine), fallbackLoader);

    std::vector<std::string> modifiedModules;
    for (const auto& name : m_loadOrder) {
        auto it = m_userCodes.find(name);
        if (it == m_userCodes.end()) {
            continue;
        }
        const UserCodeEntry& entry = it->second;
        if ((!entry.isDynamic && !entry.isScript) || entry.state != UserCodeState::Initialized) {
            continue;
        }
        std::string watchPath = entry.sourcePath.empty() ? entry.modulePath : entry.sourcePath;
        if (watchPath.empty() || !loader->exists(watchPath)) {
            continue;
        }
        int64_t currentWriteTime = loader->getLastWriteTimeSeconds(watchPath);
        if (currentWriteTime > 0 && entry.lastWriteTimeSeconds > 0 &&
            currentWriteTime > entry.lastWriteTimeSeconds) {
            modifiedModules.push_back(name);
        }
    }

    int reloadedCount = 0;
    for (const auto& name : modifiedModules) {
        if (reloadUserCode(name, engine).ok()) {
            ++reloadedCount;
        }
    }
    return reloadedCount;
}

void UserCodeManager::freeDynamicLibrary(UserCodeEntry& entry, ResourceLoader* resourceLoader) {
    entry.userCode.reset();
#ifdef _WIN32
    const bool isShuttingDown = Runtime::instance().isShuttingDown();
    if (!isShuttingDown && entry.isDynamic && entry.libraryHandle) {
        FreeLibrary(static_cast<HMODULE>(entry.libraryHandle));
        entry.libraryHandle = nullptr;
    } else if (isShuttingDown) {
        entry.libraryHandle = nullptr;
    }
    if (!entry.shadowLibraryPath.empty()) {
        if (!isShuttingDown) {
            UniquePtr<DiskResourceLoader> fallbackLoader;
            ResourceLoader* loader = ResolveLoaderOrFallback(resourceLoader, fallbackLoader);
            (void)loader->unlink(entry.shadowLibraryPath);
        }
        entry.shadowLibraryPath.clear();
    }
#else
    (void)resourceLoader;
#endif
}

bool UserCodeManager::hasUserCode(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_userCodes.find(name) != m_userCodes.end() || m_factories.find(name) != m_factories.end();
}

std::shared_ptr<NativeCode> UserCodeManager::getUserCode(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_userCodes.find(name);
    if (it != m_userCodes.end()) {
        return it->second.userCode;
    }
    return nullptr;
}

UserCodeState UserCodeManager::getUserCodeState(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_userCodes.find(name);
    if (it != m_userCodes.end()) {
        return it->second.state;
    }
    return UserCodeState::Unloaded;
}

std::vector<std::string> UserCodeManager::getAllUserCodeNames() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> names;
    names.reserve(m_userCodes.size() + m_factories.size());
    for (const auto& pair : m_userCodes) {
        names.push_back(pair.first);
    }
    for (const auto& pair : m_factories) {
        if (std::find(names.begin(), names.end(), pair.first) == names.end()) {
            names.push_back(pair.first);
        }
    }
    return names;
}

const UserCodeEntry* UserCodeManager::getUserCodeEntry(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_userCodes.find(name);
    if (it != m_userCodes.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace Runtime
} // namespace MQEngine
