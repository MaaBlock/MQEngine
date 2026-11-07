#ifndef ENGINE_IPLUGIN_H
#define ENGINE_IPLUGIN_H

#include <concepts>
#include <string>
#include <vector>
#include "../../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "Composite.h"

namespace MQEngine
{
    namespace Runtime
    {
        /**
         * @brief 插件归属域枚举：区分引擎本体内置插件与用户项目扩展插件
         */
        enum class PluginScope {
            Engine, // 引擎本体插件（随引擎/编辑器启动驻留，切换工程不卸载）
            User    // 用户插件（随特定工程加载与卸载，持久化于 project.mq）
        };

        /**
         * @brief 模块化插件接口：作为功能组合体（Composite）的分类与内聚打包容器
         */
        class IPlugin {
        public:
            IPlugin() = default;
            IPlugin(const IPlugin&) = delete;
            IPlugin& operator=(const IPlugin&) = delete;
            IPlugin(IPlugin&&) noexcept = default;
            IPlugin& operator=(IPlugin&&) noexcept = default;
            virtual ~IPlugin() = default;

            /**
             * @brief 插件预加载注册阶段：注册下属 Composite 及其依赖关系
             */
            virtual void registerInfo() {}

            /**
             * @brief 插件装载阶段（提供默认空实现，支持插件仅作为 Composite 分类包使用）
             */
            virtual Status onLoad(Engine* engine) {
                (void)engine;
                return OkStatus();
            }

            /**
             * @brief 插件初始化阶段（提供默认空实现，具体功能推荐由下属 Composite 实现）
             */
            virtual Status onInit(Engine* engine) {
                (void)engine;
                return OkStatus();
            }

            /**
             * @brief 逻辑帧更新回调
             */
            virtual void onLogicTick(float dt) {
                (void)dt;
            }

            /**
             * @brief 渲染帧更新回调
             */
            virtual void onRenderTick() {}

            /**
             * @brief 插件卸载回调，清理资源
             */
            virtual void onUnload() {}

            /**
             * @brief 获取插件唯一名称
             */
            virtual std::string getName() const = 0;

            /**
             * @brief 获取插件版本号
             */
            virtual std::string getVersion() const = 0;

            /**
             * @brief 获取插件归属域（默认为用户插件，引擎本体插件可重写返回 PluginScope::Engine）
             */
            virtual PluginScope getScope() const { return PluginScope::User; }

            /**
             * @brief 获取当前插件包依赖的上游插件名称列表
             */
            virtual std::vector<std::string> getDependencies() const { return {}; }

            /**
             * @brief 向当前插件分类包注册一个功能组合体（Composite）
             */
            void addComposite(UniquePtr<Composite> composite) {
                if (composite) {
                    composite->registerInfo();
                    m_composites.push_back(std::move(composite));
                }
            }

            /**
             * @brief 就地构造并向当前插件分类包添加功能组合体
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
             * @brief 获取当前插件包收纳的全部功能组合体
             */
            const std::vector<UniquePtr<Composite>>& getComposites() const { return m_composites; }

            /**
             * @brief 热重载前序列化插件内部状态（可选重写）
             */
            virtual std::string onSerializeHotReloadState() const { return ""; }

            /**
             * @brief 热重载后恢复插件内部状态（可选重写）
             */
            virtual void onRestoreHotReloadState(const std::string& /*state*/) {}

        private:
            std::vector<UniquePtr<Composite>> m_composites;
        };
    } // namespace Runtime
} // namespace MQEngine

#endif // ENGINE_IPLUGIN_H
