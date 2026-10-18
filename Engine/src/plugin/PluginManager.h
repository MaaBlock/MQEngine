#ifndef ENGINE_PLUGINMANAGER_H
#define ENGINE_PLUGINMANAGER_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "IPlugin.h"
#include "PluginContext.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace MQEngine {

/**
 * @brief 插件生命周期状态枚举
 */
enum class PluginState {
    Unloaded,
    Loaded,
    Initialized,
    Failed
};

/**
 * @brief 插件注册记录条目
 */
struct PluginEntry {
    std::string name;
    std::shared_ptr<IPlugin> plugin;
    PluginState state = PluginState::Unloaded;
    std::string errorMessage;
    bool isDynamic = false;
    bool isScript = false;
    std::string dynamicLibraryPath;
    void* libraryHandle = nullptr;
};

/**
 * @brief 插件工厂类型（支持按需惰性实例化）
 */
using PluginFactory = std::function<std::shared_ptr<IPlugin>()>;

/**
 * @brief 插件管理器，负责模块化插件的静态注册、动态加载与全生命周期调度
 */
class ENGINE_API PluginManager {
public:
    PluginManager();
    virtual ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    /**
     * @brief 静态注册已有插件实例
     */
    Status registerPlugin(std::shared_ptr<IPlugin> plugin);

    /**
     * @brief 注册插件工厂函数
     */
    Status registerPluginFactory(const std::string& name, PluginFactory factory);

    /**
     * @brief 动态加载共享库/DLL 插件
     */
    Status loadDynamicPlugin(const std::string& libraryPath, PluginContext& ctx);

    /**
     * @brief 加载 JS / TS 脚本插件（支持 .js / .ts 文件或包含 plugin.json / index.ts / index.js 的目录）
     */
    Status loadScriptPlugin(const std::string& scriptOrDirPath, PluginContext& ctx);

    /**
     * @brief 根据插件标识符自动解析并加载静态插件、动态库插件 (.dll) 或脚本插件 (.js/.ts)
     */
    Status resolveAndLoadPlugin(const std::string& pluginIdent,
                                const std::string& baseDir,
                                PluginContext& ctx);

    /**
     * @brief 加载指定插件 (执行 onLoad)
     */
    Status loadPlugin(const std::string& name, PluginContext& ctx);

    /**
     * @brief 初始化指定插件 (执行 onInit)
     */
    Status initPlugin(const std::string& name, PluginContext& ctx);

    /**
     * @brief 加载并初始化指定插件
     */
    Status loadAndInitPlugin(const std::string& name, PluginContext& ctx);

    /**
     * @brief 加载所有已注册插件
     */
    Status loadAll(PluginContext& ctx);

    /**
     * @brief 初始化所有已加载插件
     */
    Status initAll(PluginContext& ctx);

    /**
     * @brief 逻辑帧 Tick 调度
     */
    void logicTick(float dt);

    /**
     * @brief 渲染帧 Tick 调度
     */
    void renderTick();

    /**
     * @brief 卸载指定插件
     */
    Status unloadPlugin(const std::string& name);

    /**
     * @brief 卸载所有插件（按加载逆序）
     */
    void unloadAll();

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
     * @brief 获取处于指定状态的插件名称列表
     */
    std::vector<std::string> getPluginsInState(PluginState state) const;

    /**
     * @brief 获取插件条目详情
     */
    const PluginEntry* getPluginEntry(const std::string& name) const;

private:
    std::unordered_map<std::string, PluginEntry> m_plugins;
    std::unordered_map<std::string, PluginFactory> m_factories;
    std::vector<std::string> m_loadOrder;

    Status ensurePluginInstance(const std::string& name);
    void freeDynamicLibrary(PluginEntry& entry);
    std::string resolveDynamicLibraryPath(const std::string& inputPath, const std::string& baseDir = "") const;
    std::string resolveScriptPluginPath(const std::string& inputPath, const std::string& baseDir = "") const;
};

} // namespace MQEngine

#endif // ENGINE_PLUGINMANAGER_H
