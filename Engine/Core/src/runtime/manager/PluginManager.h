#ifndef ENGINE_PLUGINMANAGER_H
#define ENGINE_PLUGINMANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../EnginePCH.h"
#include "../../base/CoreEngineManager.h"
#include "../../base/SingleQueueEventSystem.h"
#include "../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../interface/Composite.h"
#include "../interface/Plugin.h"

namespace MQEngine {

class ResourceLoader;

namespace Runtime {

/**
 * @brief 插件与组合体生命周期状态枚举
 */
enum class PluginState {
    Unloaded,
    Preloaded,
    Loaded,
    Initialized,
    Failed
};

/**
 * @brief 功能组合体（Composite）注册记录条目
 */
struct CompositeEntry {
    std::string name;
    std::string ownerPluginName;
    Composite* composite = nullptr;
    PluginState state = PluginState::Unloaded;
    std::string errorMessage;
    std::vector<std::string> registeredComponents;
    std::vector<std::string> registeredSystems;
    std::vector<uint64_t> registeredServices;
};

/**
 * @brief 插件分类包注册记录条目
 */
struct PluginEntry {
    std::string name;
    std::shared_ptr<IPlugin> plugin;
    PluginScope scope = PluginScope::User;
    PluginState state = PluginState::Unloaded;
    std::string errorMessage;
    bool isDynamic = false;
    bool isScript = false;
    std::string dynamicLibraryPath;
    std::string sourcePath;
    std::string shadowLibraryPath;
    void* libraryHandle = nullptr;
    int64_t lastWriteTimeSeconds = 0;
    uint32_t reloadCount = 0;
    std::vector<std::string> compositeNames;
    std::vector<std::string> registeredComponents;
    std::vector<std::string> registeredSystems;
    std::vector<uint64_t> registeredServices;
};

/**
 * @brief 插件工厂类型（支持按需惰性实例化）
 */
using PluginFactory = std::function<std::shared_ptr<IPlugin>()>;

/**
 * @brief 插件与组合体管理器：负责插件分类包预加载注册、Tag 包含匹配过滤、TokenGraph 拓扑排序调度与热重载
 */
class ENGINE_API PluginManager : public CoreEngineManager<PluginManager> {
public:
    using CompositeToken = std::string;
    using CompositeGraph = FCT::TokenGraph<CompositeToken, CompositeEntry*>;

    struct Request {
        struct Load {
            std::string pluginIdent;
            std::string baseDir;
        };
        struct Unload {
            std::string pluginName;
        };
        struct Reload {
            std::string pluginName;
        };
        struct SetTags {
            std::vector<std::string> tags;
        };
    };

    PluginManager();
    virtual ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    void registerInfo();

    void requestLoadPlugin(const std::string& pluginIdent, const std::string& baseDir = "");
    void requestUnloadPlugin(const std::string& pluginName);
    void requestReloadPlugin(const std::string& pluginName);
    void requestSetTags(const std::vector<std::string>& tags);

    /**
     * @brief 线程汇聚点（及 loop 前）统一消费挂起的插件预加载/卸载/热重载请求，并在全部预加载完成后统一执行 TokenGraph 拓扑排序与激发
     */
    void syncTicker();
    void flushRequests() { syncTicker(); }

    bool isAutoHotReloadEnabled() const noexcept { return m_autoHotReload; }
    void setAutoHotReloadEnabled(bool enabled) noexcept { m_autoHotReload = enabled; }
    void checkHotReload(Engine* engine = nullptr);

    /**
     * @brief 静态注册已有插件实例
     */
    Status registerPlugin(std::shared_ptr<IPlugin> plugin);

    /**
     * @brief 注册插件工厂函数
     */
    Status registerPluginFactory(const std::string& name, PluginFactory factory);

    /**
     * @brief 预加载指定插件（执行 registerInfo 收集下属 Composite 及其依赖关系，暂不触发 onLoad/onInit）
     */
    Status preloadPlugin(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 预加载动态共享库/DLL 插件（仅装载影子 DLL 与注册 Composite 元数据，等待全局 TokenGraph 拓扑排序后统一激发）
     */
    Status preloadDynamicPlugin(const std::string& libraryPath, Engine* engine = nullptr);

    /**
     * @brief 预加载 JS / TS / Python 脚本插件（仅解析清单与注册 Composite 元数据）
     */
    Status preloadScriptPlugin(const std::string& scriptOrDirPath, Engine* engine = nullptr);

    /**
     * @brief 根据插件标识符自动解析并预加载插件（收集全部 Composite 注册信息，待所有插件预加载完毕后统一调用 refreshComposites 拓扑排序激发）
     */
    Status resolveAndPreloadPlugin(const std::string& pluginIdent,
                                   const std::string& baseDir = "",
                                   Engine* engine = nullptr);

    /**
     * @brief 动态加载共享库/DLL 插件并刷新 TokenGraph
     */
    Status loadDynamicPlugin(const std::string& libraryPath, Engine* engine = nullptr);

    /**
     * @brief 加载 JS / TS / Python 脚本插件并刷新 TokenGraph
     */
    Status loadScriptPlugin(const std::string& scriptOrDirPath, Engine* engine = nullptr);

    /**
     * @brief 根据插件标识符自动解析、预加载并刷新 TokenGraph 激发
     */
    Status resolveAndLoadPlugin(const std::string& pluginIdent,
                                const std::string& baseDir = "",
                                Engine* engine = nullptr);

    /**
     * @brief 加载指定插件及其下属满足 Tag 条件的 Composite
     */
    Status loadPlugin(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 初始化指定插件并触发 Composite TokenGraph 拓扑排序与按序激发
     */
    Status initPlugin(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 加载并初始化指定插件
     */
    Status loadAndInitPlugin(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 加载所有已注册插件
     */
    Status loadAll(Engine* engine = nullptr);

    /**
     * @brief 初始化所有已加载插件并完成全局 Composite 拓扑激发
     */
    Status initAll(Engine* engine = nullptr);

    /**
     * @brief 设置当前宿主激活的 Tag 列表（覆盖已有 Tag）
     */
    void setTags(const std::vector<std::string>& tags);

    /**
     * @brief 设置激活 Tag 列表并立即重新评估 TokenGraph 激发/卸载匹配的 Composite
     */
    Status setTagsAndRefresh(const std::vector<std::string>& tags, Engine* engine = nullptr);

    /**
     * @brief 追加单个激活 Tag
     */
    void addTag(const std::string& tag);

    /**
     * @brief 追加单个激活 Tag 并立即刷新 Composite 激发状态
     */
    Status addTagAndRefresh(const std::string& tag, Engine* engine = nullptr);

    /**
     * @brief 移除单个激活 Tag
     */
    void removeTag(const std::string& tag);

    /**
     * @brief 移除单个激活 Tag 并立即刷新（卸载不再满足包含条件的 Composite）
     */
    Status removeTagAndRefresh(const std::string& tag, Engine* engine = nullptr);

    /**
     * @brief 查询指定 Tag 是否处于激活状态
     */
    bool hasTag(const std::string& tag) const;

    /**
     * @brief 获取当前所有激活 Tag 列表
     */
    std::vector<std::string> getActiveTags() const;

    /**
     * @brief 包含匹配判定：检查 requiredTags 是否为当前激活 Tag 集合的子集（requiredTags ⊆ m_activeTags）
     */
    bool isTagsMatched(const std::vector<std::string>& requiredTags) const;

    /**
     * @brief 重新评估所有已预加载/已注册 Composite 的 Tag 匹配状态，存入 TokenGraph 进行拓扑排序并按拓扑序执行 onLoad 与 onInit
     */
    Status refreshComposites(Engine* engine = nullptr);

    /**
     * @brief 获取当前 TokenGraph 拓扑排序后的激活 Composite 名称序列
     */
    std::vector<std::string> getActiveCompositeOrder() const;

    /**
     * @brief 获取指定 Composite 条目详情
     */
    const CompositeEntry* getCompositeEntry(const std::string& compositeName) const;

    /**
     * @brief 注册跨插件原始服务指针
     */
    void registerRawService(uint64_t serviceTypeId, void* instance);

    /**
     * @brief 注销跨插件原始服务指针
     */
    void unregisterRawService(uint64_t serviceTypeId);

    /**
     * @brief 查询跨插件原始服务指针
     */
    void* getRawService(uint64_t serviceTypeId) const;

    /**
     * @brief 获取当前已注册的全部跨插件服务 TypeId 列表
     */
    std::vector<uint64_t> getRegisteredServiceIds() const;

    /**
     * @brief 注册强类型跨插件服务接口
     */
    template <typename ServiceInterface>
    void registerService(ServiceInterface* instance) {
        constexpr uint64_t typeId = static_cast<uint64_t>(entt::type_hash<std::decay_t<ServiceInterface>>::value());
        registerRawService(typeId, static_cast<void*>(instance));
    }

    /**
     * @brief 注销强类型跨插件服务接口
     */
    template <typename ServiceInterface>
    void unregisterService() {
        constexpr uint64_t typeId = static_cast<uint64_t>(entt::type_hash<std::decay_t<ServiceInterface>>::value());
        unregisterRawService(typeId);
    }

    /**
     * @brief 按接口类型获取跨插件服务实例
     */
    template <typename ServiceInterface>
    ServiceInterface* getService() const {
        constexpr uint64_t typeId = static_cast<uint64_t>(entt::type_hash<std::decay_t<ServiceInterface>>::value());
        return static_cast<ServiceInterface*>(getRawService(typeId));
    }

    /**
     * @brief 逻辑帧 Tick 调度（按 TokenGraph 拓扑序调度各 Composite）
     */
    void logicTick(float dt);

    /**
     * @brief 渲染帧 Tick 调度（按 TokenGraph 拓扑序调度各 Composite）
     */
    void renderTick();

    /**
     * @brief 卸载指定插件（自动按拓扑逆序卸载其下属 Composite、清理 System/Component/Service 并释放影子 DLL）
     */
    Status unloadPlugin(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 热重载指定插件（保留 ECS 组件字段快照与插件/Composite 自定义状态）
     */
    Status reloadPlugin(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 检查所有已加载动态/脚本插件的文件修改时间戳，自动热重载发生变更的插件
     * @return 本次成功热重载的插件数量
     */
    int checkAndReloadModified(Engine* engine = nullptr);

    /**
     * @brief 卸载所有插件（按加载与拓扑逆序）
     */
    void unloadAll();

    /**
     * @brief 仅卸载指定归属域的插件（例如关闭工程时仅卸载 PluginScope::User 插件，保留 PluginScope::Engine 本体插件）
     */
    void unloadPluginsByScope(PluginScope scope);

    /**
     * @brief 检查插件是否存在
     */
    bool hasPlugin(const std::string& name) const;

    /**
     * @brief 获取插件实例
     */
    std::shared_ptr<IPlugin> getPlugin(const std::string& name) const;

    /**
     * @brief 按具体类型获取插件实例
     */
    template<typename T>
    std::shared_ptr<T> getPluginAs(const std::string& name) const {
        auto base = getPlugin(name);
        return base ? std::static_pointer_cast<T>(base) : nullptr;
    }

    /**
     * @brief 获取插件生命周期状态
     */
    PluginState getPluginState(const std::string& name) const;

    /**
     * @brief 获取所有已注册/已加载的插件名称列表
     */
    std::vector<std::string> getAllPluginNames() const;

    /**
     * @brief 获取指定归属域（Engine 本体插件 / User 用户插件）的插件名称列表
     */
    std::vector<std::string> getPluginNamesByScope(PluginScope scope) const;

    /**
     * @brief 获取处于指定状态的插件名称列表
     */
    std::vector<std::string> getPluginsInState(PluginState state) const;

    /**
     * @brief 获取插件条目详情
     */
    const PluginEntry* getPluginEntry(const std::string& name) const;

private:
    mutable std::recursive_mutex m_mutex;
    std::unordered_map<std::string, PluginEntry> m_plugins;
    std::unordered_map<std::string, PluginFactory> m_factories;
    std::unordered_map<std::string, CompositeEntry> m_composites;
    CompositeGraph m_compositeGraph;
    std::unordered_set<std::string> m_activeTags;
    std::unordered_map<uint64_t, void*> m_services;
    std::vector<std::string> m_loadOrder;
    uint64_t m_shadowCounter = 0;
    bool m_autoHotReload = true;
    bool m_pendingGraphRefresh = false;
    SingleQueueEventSystem m_requestQueue;

    Status ensurePluginInstance(const std::string& name);
    Status preloadPluginDependencies(const IPlugin& plugin, Engine* engine);
    Status ensureOwnerPluginLoaded(PluginEntry& entry, Engine* engine);
    Status ensureOwnerPluginInitialized(PluginEntry& entry, Engine* engine);
    void registerPluginComposites(PluginEntry& entry);
    void unloadPluginComposites(PluginEntry& entry, Engine* engine);
    void unloadSingleComposite(CompositeEntry& compEntry, Engine* engine);
    void cleanupRegistrations(std::vector<std::string>& components,
                              std::vector<std::string>& systems,
                              std::vector<uint64_t>& services,
                              Engine* engine);
    void cleanupPluginRegistrations(PluginEntry& entry, Engine* engine);
    void freeDynamicLibrary(PluginEntry& entry, ResourceLoader* resourceLoader = nullptr);
    std::string createShadowCopy(const std::string& resolvedPath, ResourceLoader* resourceLoader);
    std::string resolveDynamicLibraryPath(const std::string& inputPath, const std::string& baseDir = "") const;
    std::string resolveScriptPluginPath(const std::string& inputPath, const std::string& baseDir = "") const;
};

} // namespace Runtime
} // namespace MQEngine

#endif // ENGINE_PLUGINMANAGER_H
