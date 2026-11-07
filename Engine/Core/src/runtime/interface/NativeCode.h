#ifndef ENGINE_IUSERCODE_H
#define ENGINE_IUSERCODE_H

#include <concepts>
#include <string>
#include <vector>
#include "../../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "Composite.h"

namespace MQEngine {
namespace Runtime {

/**
 * @brief 用户代码模块接口，位于 Engine 与 Plugin 层之上，承载项目级业务逻辑、自定义组件、系统扩展与功能组合体（Composite）
 */
class ENGINE_API NativeCode {
public:
    NativeCode() = default;
    NativeCode(const NativeCode&) = delete;
    NativeCode& operator=(const NativeCode&) = delete;
    NativeCode(NativeCode&&) noexcept = default;
    NativeCode& operator=(NativeCode&&) noexcept = default;
    virtual ~NativeCode() = default;

    /**
     * @brief 用户代码预加载注册阶段：注册下属 Composite 及其依赖关系
     */
    virtual void registerInfo() {}

    /**
     * @brief 用户代码装载阶段（提供默认空实现，支持用户代码仅作为 Composite 分类包使用）
     */
    virtual Status onLoad(Engine* engine) {
        (void)engine;
        return OkStatus();
    }

    /**
     * @brief 用户代码初始化阶段（提供默认空实现，具体功能推荐由下属 Composite 实现）
     */
    virtual Status onInit(Engine* engine) {
        (void)engine;
        return OkStatus();
    }

    /**
     * @brief 逻辑帧更新回调（在引擎内置系统与插件逻辑 Tick 之后执行）
     */
    virtual void onLogicTick(float dt) {
        (void)dt;
    }

    /**
     * @brief 渲染帧更新回调（在引擎内置系统与插件渲染 Tick 之后执行）
     */
    virtual void onRenderTick() {}

    /**
     * @brief 用户代码卸载回调（在插件卸载与工程关闭之前先行执行）
     */
    virtual void onUnload() {}

    /**
     * @brief 获取用户代码模块名称
     */
    virtual std::string getName() const = 0;

    /**
     * @brief 获取用户代码模块版本号
     */
    virtual std::string getVersion() const = 0;

    /**
     * @brief 向当前用户代码模块注册一个功能组合体（Composite）
     */
    void addComposite(UniquePtr<Composite> composite) {
        if (composite) {
            composite->registerInfo();
            m_composites.push_back(std::move(composite));
        }
    }

    /**
     * @brief 就地构造并向当前用户代码模块添加功能组合体
     */
    template <typename CompositeType, typename... Args>
        requires std::derived_from<CompositeType, Composite>
    CompositeType* emplaceComposite(Args&&... args) {
        auto comp = makeUnique<CompositeType>(std::forward<Args>(args)...);
        CompositeType* rawPtr = comp.get();
        addComposite(std::move(comp));
        return rawPtr;
    }

    /**
     * @brief 获取当前用户代码模块收纳的全部功能组合体
     */
    const std::vector<UniquePtr<Composite>>& getComposites() const { return m_composites; }

    /**
     * @brief 热重载前序列化用户代码模块内部状态（可选重写）
     */
    virtual std::string onSerializeHotReloadState() const { return ""; }

    /**
     * @brief 热重载后恢复用户代码模块内部状态（可选重写）
     */
    virtual void onRestoreHotReloadState(const std::string& /*state*/) {}

private:
    std::vector<UniquePtr<Composite>> m_composites;
};

} // namespace Runtime
} // namespace MQEngine

#endif // ENGINE_IUSERCODE_H
