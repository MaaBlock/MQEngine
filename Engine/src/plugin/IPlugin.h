#ifndef ENGINE_IPLUGIN_H
#define ENGINE_IPLUGIN_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include <string>

namespace MQEngine {

struct PluginContext;

/**
 * @brief 模块化插件纯虚接口，定义标准生命周期
 */
class ENGINE_API IPlugin {
public:
    virtual ~IPlugin() = default;

    /**
     * @brief 插件装载阶段，初始化插件数据结构、注册静态元数据
     */
    virtual Status onLoad(PluginContext& ctx) = 0;

    /**
     * @brief 插件初始化阶段，向管线与渲染图注册 Pass、Tech、System
     */
    virtual Status onInit(PluginContext& ctx) = 0;

    /**
     * @brief 逻辑帧更新回调
     */
    virtual void onLogicTick(float dt) = 0;

    /**
     * @brief 渲染帧更新回调
     */
    virtual void onRenderTick() = 0;

    /**
     * @brief 插件卸载回调，清理资源
     */
    virtual void onUnload() = 0;

    /**
     * @brief 获取插件唯一名称
     */
    virtual std::string getName() const = 0;

    /**
     * @brief 获取插件版本号
     */
    virtual std::string getVersion() const = 0;
};

} // namespace MQEngine

#endif // ENGINE_IPLUGIN_H
