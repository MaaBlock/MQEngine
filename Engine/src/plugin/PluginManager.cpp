#include "PluginManager.h"
#include <spdlog/spdlog.h>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MQEngine {

using absl::AlreadyExistsError;
using absl::FailedPreconditionError;

using CreatePluginFunc = IPlugin* (*)();

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    unloadAll();
}

Status PluginManager::registerPlugin(std::shared_ptr<IPlugin> plugin) {
    if (!plugin) {
        return InvalidArgumentError("registerPlugin: 插件指针为空");
    }

    std::string name = plugin->getName();
    if (name.empty()) {
        return InvalidArgumentError("registerPlugin: 插件名称为空");
    }

    if (m_plugins.find(name) != m_plugins.end()) {
        return AlreadyExistsError(StrCat("插件已存在: ", name));
    }

    PluginEntry entry;
    entry.name = name;
    entry.plugin = plugin;
    entry.state = PluginState::Unloaded;
    m_plugins.emplace(name, std::move(entry));

    return OkStatus();
}

Status PluginManager::registerPluginFactory(const std::string& name, PluginFactory factory) {
    if (name.empty()) {
        return InvalidArgumentError("registerPluginFactory: 插件名称为空");
    }
    if (!factory) {
        return InvalidArgumentError("registerPluginFactory: 工厂函数为空");
    }

    m_factories[name] = std::move(factory);
    return OkStatus();
}

Status PluginManager::ensurePluginInstance(const std::string& name) {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end() && it->second.plugin) {
        return OkStatus();
    }

    auto factoryIt = m_factories.find(name);
    if (factoryIt != m_factories.end()) {
        auto instance = factoryIt->second();
        if (!instance) {
            return InternalError(StrCat("插件工厂创建插件失败: ", name));
        }
        PluginEntry entry;
        entry.name = name;
        entry.plugin = instance;
        entry.state = PluginState::Unloaded;
        m_plugins[name] = std::move(entry);
        return OkStatus();
    }

    return NotFoundError(StrCat("未找到已注册插件或插件工厂: ", name));
}

Status PluginManager::loadPlugin(const std::string& name, PluginContext& ctx) {
    auto ensureRes = ensurePluginInstance(name);
    if (!ensureRes.ok()) {
        return ensureRes;
    }

    auto& entry = m_plugins[name];
    if (entry.state == PluginState::Loaded || entry.state == PluginState::Initialized) {
        return OkStatus();
    }

    Status status = entry.plugin->onLoad(ctx);
    if (!status.ok()) {
        entry.state = PluginState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("插件 [{}] onLoad 失败: {}", name, status.message());
        return status;
    }

    entry.state = PluginState::Loaded;
    if (std::find(m_loadOrder.begin(), m_loadOrder.end(), name) == m_loadOrder.end()) {
        m_loadOrder.push_back(name);
    }

    spdlog::info("插件 [{}] 成功加载 (版本: {})", name, entry.plugin->getVersion());
    return OkStatus();
}

Status PluginManager::initPlugin(const std::string& name, PluginContext& ctx) {
    auto it = m_plugins.find(name);
    if (it == m_plugins.end()) {
        return NotFoundError(StrCat("未找到插件: ", name));
    }

    auto& entry = it->second;
    if (entry.state == PluginState::Unloaded) {
        Status loadRes = loadPlugin(name, ctx);
        if (!loadRes.ok()) {
            return loadRes;
        }
    }

    if (entry.state == PluginState::Initialized) {
        return OkStatus();
    }

    if (entry.state != PluginState::Loaded) {
        return FailedPreconditionError(StrCat("插件 [", name, "] 处于非就绪状态: ", static_cast<int>(entry.state)));
    }

    Status status = entry.plugin->onInit(ctx);
    if (!status.ok()) {
        entry.state = PluginState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("插件 [{}] onInit 失败: {}", name, status.message());
        return status;
    }

    entry.state = PluginState::Initialized;
    spdlog::info("插件 [{}] 成功初始化", name);
    return OkStatus();
}

Status PluginManager::loadAndInitPlugin(const std::string& name, PluginContext& ctx) {
    Status loadRes = loadPlugin(name, ctx);
    if (!loadRes.ok()) {
        return loadRes;
    }
    return initPlugin(name, ctx);
}

Status PluginManager::loadAll(PluginContext& ctx) {
    std::vector<std::string> namesToLoad;
    for (const auto& pair : m_plugins) {
        namesToLoad.push_back(pair.first);
    }
    for (const auto& pair : m_factories) {
        if (std::find(namesToLoad.begin(), namesToLoad.end(), pair.first) == namesToLoad.end()) {
            namesToLoad.push_back(pair.first);
        }
    }

    for (const auto& name : namesToLoad) {
        Status status = loadPlugin(name, ctx);
        if (!status.ok()) {
            return status;
        }
    }
    return OkStatus();
}

Status PluginManager::initAll(PluginContext& ctx) {
    for (const auto& name : m_loadOrder) {
        Status status = initPlugin(name, ctx);
        if (!status.ok()) {
            return status;
        }
    }
    return OkStatus();
}

void PluginManager::logicTick(float dt) {
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() && it->second.state == PluginState::Initialized) {
            it->second.plugin->onLogicTick(dt);
        }
    }
}

void PluginManager::renderTick() {
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() && it->second.state == PluginState::Initialized) {
            it->second.plugin->onRenderTick();
        }
    }
}

Status PluginManager::unloadPlugin(const std::string& name) {
    auto it = m_plugins.find(name);
    if (it == m_plugins.end()) {
        return NotFoundError(StrCat("未找到插件: ", name));
    }

    auto& entry = it->second;
    if (entry.plugin && (entry.state == PluginState::Initialized || entry.state == PluginState::Loaded)) {
        entry.plugin->onUnload();
        spdlog::info("插件 [{}] 已卸载", name);
    }

    entry.state = PluginState::Unloaded;
    freeDynamicLibrary(entry);

    auto orderIt = std::find(m_loadOrder.begin(), m_loadOrder.end(), name);
    if (orderIt != m_loadOrder.end()) {
        m_loadOrder.erase(orderIt);
    }

    return OkStatus();
}

void PluginManager::unloadAll() {
    std::vector<std::string> reverseOrder = m_loadOrder;
    std::reverse(reverseOrder.begin(), reverseOrder.end());

    for (const auto& name : reverseOrder) {
        (void)unloadPlugin(name);
    }
    m_loadOrder.clear();
}

Status PluginManager::loadDynamicPlugin(const std::string& libraryPath, PluginContext& ctx) {
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(libraryPath.c_str());
    if (!handle) {
        DWORD err = GetLastError();
        return NotFoundError(StrCat("加载动态链接库失败: ", libraryPath, ", Windows错误码: ", err));
    }

    auto createFn = reinterpret_cast<CreatePluginFunc>(GetProcAddress(handle, "CreatePlugin"));
    if (!createFn) {
        FreeLibrary(handle);
        return InvalidArgumentError(StrCat("动态库未导出 CreatePlugin 函数: ", libraryPath));
    }

    IPlugin* rawPlugin = createFn();
    if (!rawPlugin) {
        FreeLibrary(handle);
        return InternalError(StrCat("CreatePlugin 返回空指针: ", libraryPath));
    }

    std::string pluginName = rawPlugin->getName();
    auto pluginPtr = std::shared_ptr<IPlugin>(rawPlugin);

    PluginEntry entry;
    entry.name = pluginName;
    entry.plugin = pluginPtr;
    entry.state = PluginState::Unloaded;
    entry.isDynamic = true;
    entry.dynamicLibraryPath = libraryPath;
    entry.libraryHandle = handle;

    m_plugins[pluginName] = std::move(entry);

    Status status = loadPlugin(pluginName, ctx);
    if (!status.ok()) {
        return status;
    }

    return initPlugin(pluginName, ctx);
#else
    return UnimplementedError("非Windows平台的动态插件加载尚未配置");
#endif
}

void PluginManager::freeDynamicLibrary(PluginEntry& entry) {
#ifdef _WIN32
    if (entry.isDynamic && entry.libraryHandle) {
        entry.plugin.reset();
        FreeLibrary(static_cast<HMODULE>(entry.libraryHandle));
        entry.libraryHandle = nullptr;
    }
#endif
}

bool PluginManager::hasPlugin(const std::string& name) const {
    return m_plugins.find(name) != m_plugins.end() || m_factories.find(name) != m_factories.end();
}

std::shared_ptr<IPlugin> PluginManager::getPlugin(const std::string& name) const {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return it->second.plugin;
    }
    return nullptr;
}

PluginState PluginManager::getPluginState(const std::string& name) const {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return it->second.state;
    }
    return PluginState::Unloaded;
}

std::vector<std::string> PluginManager::getAllPluginNames() const {
    std::vector<std::string> names;
    names.reserve(m_plugins.size() + m_factories.size());
    for (const auto& pair : m_plugins) {
        names.push_back(pair.first);
    }
    for (const auto& pair : m_factories) {
        if (std::find(names.begin(), names.end(), pair.first) == names.end()) {
            names.push_back(pair.first);
        }
    }
    return names;
}

std::vector<std::string> PluginManager::getPluginsInState(PluginState state) const {
    std::vector<std::string> names;
    for (const auto& pair : m_plugins) {
        if (pair.second.state == state) {
            names.push_back(pair.first);
        }
    }
    return names;
}

const PluginEntry* PluginManager::getPluginEntry(const std::string& name) const {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace MQEngine
