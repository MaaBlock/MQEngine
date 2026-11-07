#ifndef ENGINE_USERCODEMANAGER_H
#define ENGINE_USERCODEMANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../EnginePCH.h"
#include "../../base/CoreEngineManager.h"
#include "../../base/SingleQueueEventSystem.h"
#include "../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../interface/Composite.h"
#include "../interface/NativeCode.h"
#include "PluginManager.h"

namespace MQEngine {

class ResourceLoader;

namespace Runtime {

/**
 * @brief 用户代码模块生命周期状态枚举
 */
enum class UserCodeState {
    Unloaded,
    Preloaded,
    Loaded,
    Initialized,
    Failed
};

/**
 * @brief 用户代码模块注册记录条目
 */
struct UserCodeEntry {
    std::string name;
    std::shared_ptr<NativeCode> userCode;
    UserCodeState state = UserCodeState::Unloaded;
    std::string errorMessage;
    bool isDynamic = false;
    bool isScript = false;
    std::string modulePath;
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
 * @brief 用户代码工厂函数类型
 */
using UserCodeFactory = std::function<std::shared_ptr<NativeCode>()>;

/**
 * @brief 用户代码管理器，位于 PluginManager 之上，负责 UserCode.dll 与项目用户代码模块的预加载注册、Composite Tag 拓扑激发、帧更新、热卸载与热重载
 */
class ENGINE_API UserCodeManager : public CoreEngineManager<UserCodeManager> {
public:
    using CompositeToken = std::string;
    using CompositeGraph = FCT::TokenGraph<CompositeToken, CompositeEntry*>;

    struct Request {
        struct Load {
            std::string userCodeIdent;
            std::string baseDir;
        };
        struct Unload {
            std::string moduleName;
        };
        struct Reload {
            std::string moduleName;
        };
    };

    UserCodeManager();
    virtual ~UserCodeManager();

    UserCodeManager(const UserCodeManager&) = delete;
    UserCodeManager& operator=(const UserCodeManager&) = delete;

    void registerInfo() {}

    void requestLoadUserCode(const std::string& userCodeIdent, const std::string& baseDir = "");
    void requestUnloadUserCode(const std::string& moduleName);
    void requestReloadUserCode(const std::string& moduleName = "");

    /**
     * @brief 线程汇聚点（及 loop 前）统一消费挂起的 UserCode 预加载/卸载/热重载请求，并在全部预加载完成后统一执行 TokenGraph 拓扑排序激发
     */
    void syncTicker();
    void flushRequests() { syncTicker(); }

    bool isAutoHotReloadEnabled() const noexcept { return m_autoHotReload; }
    void setAutoHotReloadEnabled(bool enabled) noexcept { m_autoHotReload = enabled; }
    void checkHotReload(Engine* engine = nullptr);
    std::vector<std::string> getLoadedModuleNames() const { return getAllUserCodeNames(); }

    /**
     * @brief 静态注册已有用户代码实例
     */
    Status registerUserCode(std::shared_ptr<NativeCode> userCode);

    /**
     * @brief 注册用户代码工厂函数
     */
    Status registerUserCodeFactory(const std::string& name, UserCodeFactory factory);

    /**
     * @brief 预加载指定用户代码模块（执行 registerInfo 收集下属 Composite 及其依赖关系，暂不触发 onLoad/onInit）
     */
    Status preloadUserCode(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 预加载 UserCode.dll 共享库
     */
    Status preloadDynamicUserCode(const std::string& libraryPath, Engine* engine = nullptr);

    /**
     * @brief 预加载以 JS/TS 编写的用户代码模块
     */
    Status preloadScriptUserCode(const std::string& scriptOrDirPath, Engine* engine = nullptr);

    /**
     * @brief 根据标识符自动解析并预加载用户代码模块（收集全部 Composite 注册信息，待所有模块预加载完毕后统一调用 refreshComposites 拓扑排序激发）
     */
    Status resolveAndPreloadUserCode(const std::string& userCodeIdent,
                                     const std::string& baseDir = "",
                                     Engine* engine = nullptr);

    /**
     * @brief 动态加载 UserCode.dll 共享库并刷新 TokenGraph
     */
    Status loadDynamicUserCode(const std::string& libraryPath, Engine* engine = nullptr);

    /**
     * @brief 加载以 JS/TS 编写的用户代码模块并刷新 TokenGraph
     */
    Status loadScriptUserCode(const std::string& scriptOrDirPath, Engine* engine = nullptr);

    /**
     * @brief 根据标识符自动解析、预加载并刷新 TokenGraph 激发
     */
    Status resolveAndLoadUserCode(const std::string& userCodeIdent,
                                  const std::string& baseDir = "",
                                  Engine* engine = nullptr);

    /**
     * @brief 执行指定用户代码模块的 onLoad 阶段
     */
    Status loadUserCode(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 执行指定用户代码模块的 onInit 阶段并触发下属 Composite 按 Tag 拓扑激发
     */
    Status initUserCode(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 加载并初始化指定用户代码模块
     */
    Status loadAndInitUserCode(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 加载所有已注册用户代码模块
     */
    Status loadAll(Engine* engine = nullptr);

    /**
     * @brief 初始化所有已加载用户代码模块
     */
    Status initAll(Engine* engine = nullptr);

    /**
     * @brief 重新评估所有已预加载/已注册 UserCode Composite 的 Tag 匹配状态，存入 TokenGraph 拓扑排序并按序激发/卸载
     */
    Status refreshComposites(Engine* engine = nullptr);

    /**
     * @brief 获取当前 TokenGraph 拓扑排序后的激活 UserCode Composite 名称序列
     */
    std::vector<std::string> getActiveCompositeOrder() const;

    /**
     * @brief 获取指定 UserCode Composite 条目详情
     */
    const CompositeEntry* getCompositeEntry(const std::string& compositeName) const;

    /**
     * @brief 逻辑帧 Tick 调度（在引擎核心系统与插件 Tick 之后调用）
     */
    void logicTick(float dt);

    /**
     * @brief 渲染帧 Tick 调度（在引擎核心系统与插件渲染 Tick 之后调用）
     */
    void renderTick();

    /**
     * @brief 卸载指定用户代码模块（自动清理下属 Composite、已注册的 System 与 ECS 组件池，并释放影子 DLL）
     */
    Status unloadUserCode(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 热重载指定用户代码模块（保留 ECS 组件字段快照与模块/Composite 内部状态）
     */
    Status reloadUserCode(const std::string& name, Engine* engine = nullptr);

    /**
     * @brief 热重载所有已加载的用户代码模块
     */
    Status reloadAll(Engine* engine = nullptr);

    /**
     * @brief 检查所有已加载动态/脚本用户代码模块的文件修改时间戳，自动热重载发生变更的模块
     * @return 本次成功热重载的模块数量
     */
    int checkAndReloadModified(Engine* engine = nullptr);

    /**
     * @brief 按加载逆序卸载全部用户代码模块（在插件卸载前调用）
     */
    void unloadAll();

    /**
     * @brief 检查是否存在指定名称的用户代码模块
     */
    bool hasUserCode(const std::string& name) const;

    /**
     * @brief 检查当前是否有任何已加载的用户代码模块
     */
    bool hasAnyLoadedUserCode() const { return !m_loadOrder.empty(); }

    /**
     * @brief 获取指定名称的用户代码实例
     */
    std::shared_ptr<NativeCode> getUserCode(const std::string& name) const;

    /**
     * @brief 按具体类型获取用户代码实例
     */
    template<typename T>
    std::shared_ptr<T> getUserCodeAs(const std::string& name) const {
        auto base = getUserCode(name);
        return base ? std::static_pointer_cast<T>(base) : nullptr;
    }

    /**
     * @brief 获取用户代码模块当前状态
     */
    UserCodeState getUserCodeState(const std::string& name) const;

    /**
     * @brief 获取所有已注册/已加载的用户代码模块名称
     */
    std::vector<std::string> getAllUserCodeNames() const;

    /**
     * @brief 获取用户代码注册条目详情
     */
    const UserCodeEntry* getUserCodeEntry(const std::string& name) const;

private:
    mutable std::recursive_mutex m_mutex;
    std::unordered_map<std::string, UserCodeEntry> m_userCodes;
    std::unordered_map<std::string, UserCodeFactory> m_factories;
    std::unordered_map<std::string, CompositeEntry> m_composites;
    CompositeGraph m_compositeGraph;
    std::vector<std::string> m_loadOrder;
    uint64_t m_shadowCounter = 0;
    bool m_autoHotReload = true;
    bool m_pendingGraphRefresh = false;
    SingleQueueEventSystem m_requestQueue;

    Status ensureUserCodeInstance(const std::string& name);
    Status ensureOwnerUserCodeLoaded(UserCodeEntry& entry, Engine* engine);
    Status ensureOwnerUserCodeInitialized(UserCodeEntry& entry, Engine* engine);
    bool isCompositeTagsMatched(const std::vector<std::string>& requiredTags, Engine* engine) const;
    void registerUserCodeComposites(UserCodeEntry& entry);
    void unloadUserCodeComposites(UserCodeEntry& entry, Engine* engine);
    void unloadSingleComposite(CompositeEntry& compEntry, Engine* engine);
    void cleanupRegistrations(std::vector<std::string>& components,
                              std::vector<std::string>& systems,
                              std::vector<uint64_t>& services,
                              Engine* engine);
    void cleanupUserCodeRegistrations(UserCodeEntry& entry, Engine* engine);
    void freeDynamicLibrary(UserCodeEntry& entry, ResourceLoader* resourceLoader = nullptr);
    std::string createShadowCopy(const std::string& resolvedPath, ResourceLoader* resourceLoader);
    std::string resolveDynamicLibraryPath(const std::string& inputPath, const std::string& baseDir = "") const;
    std::string resolveScriptUserCodePath(const std::string& inputPath, const std::string& baseDir = "") const;
};

} // namespace Runtime
} // namespace MQEngine

#endif // ENGINE_USERCODEMANAGER_H
