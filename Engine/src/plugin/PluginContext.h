#ifndef ENGINE_PLUGINCONTEXT_H
#define ENGINE_PLUGINCONTEXT_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include <string>
#include <vector>

namespace FCT {
    class RenderGraph;
    struct PassDesc;
}

namespace MQEngine {

class Engine;
class TechManager;
class SystemManager;
class ResourceLoader;
class DataManager;
class ComponentReflection;
class Tech;
class ISystem;

/**
 * @brief 插件运行上下文环境，持有引擎核心子系统指针并提供四大子系统动态注册 API
 */
struct ENGINE_API PluginContext {
    Engine* engine = nullptr;
    FCT::Window* wnd = nullptr;
    FCT::Context* rhiContext = nullptr;
    FCT::RenderGraph* renderGraph = nullptr;
    TechManager* techManager = nullptr;
    SystemManager* systemManager = nullptr;
    ResourceLoader* resourceLoader = nullptr;
    DataManager* dataManager = nullptr;
    ComponentReflection* componentReflection = nullptr;
    FCT::JS::NodeEnvironment* nodeEnv = nullptr;

    /**
     * @brief 向 TechManager 动态注册自定义渲染 Technique
     */
    Status registerTech(const std::string& passName, Tech&& tech);

    /**
     * @brief 向 RenderGraph 动态注册自定义 Pass
     */
    Status registerPass(const FCT::PassDesc& desc);

    /**
     * @brief 向 SystemManager 动态注册自定义 ECS 系统
     */
    void registerSystem(const std::string& name, ISystem* system,
                        const std::vector<std::string>& pre = {},
                        const std::vector<std::string>& succ = {});

    /**
     * @brief 向组件反射元数据与组件注册中心动态注册自定义组件
     */
    template<typename T>
    void registerComponent(const std::string& name);
};

} // namespace MQEngine

#include "../manager/PipelineRegistries.h"
#include "../system/ComponentReflection.h"

namespace MQEngine {

template<typename T>
inline void PluginContext::registerComponent(const std::string& name) {
    ComponentRegistry::instance().registerComponent<T>(name);
    if (componentReflection) {
        componentReflection->registerCustomComponent<T>(name);
    }
}

} // namespace MQEngine

#endif // ENGINE_PLUGINCONTEXT_H
