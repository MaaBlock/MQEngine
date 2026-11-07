#ifndef ENGINE_ICOMPOSITE_H
#define ENGINE_ICOMPOSITE_H

#include <string>
#include <vector>
#include "../../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../classdef.h"

namespace MQEngine {
    namespace Runtime {

        /**
         * @brief 功能组合体（Composite）预加载阶段静态注册信息，声明唯一标识、依赖前驱/后继与激发所需的 Tag
         */
        struct CompositeRegisterInfo {
            std::string name;
            std::vector<std::string> requiredTags;
            std::vector<std::string> predecessors;
            std::vector<std::string> successors;
        };

        /**
         * @brief 插件与用户代码内的功能组合体接口，作为 Tag 包含匹配激发与 TokenGraph 拓扑排序调度的最小功能单元
         */
        class Composite {
        public:
            CompositeRegisterInfo compositeRegisterInfo;

            Composite() = default;
            Composite(const Composite&) = delete;
            Composite& operator=(const Composite&) = delete;
            Composite(Composite&&) noexcept = default;
            Composite& operator=(Composite&&) noexcept = default;
            virtual ~Composite() = default;

            /**
             * @brief 组合体预加载注册阶段：填充 compositeRegisterInfo（名称、所需 Tag、TokenGraph 前驱与后继依赖）
             */
            virtual void registerInfo() {}

            /**
             * @brief 获取组合体唯一 Token 名称（作为 FCT::TokenGraph 的节点标识）
             */
            virtual std::string getName() const { return compositeRegisterInfo.name; }

            /**
             * @brief 声明激发该组合体所需的 Tag 集合（采用包含匹配：RequiredTags ⊆ ActiveTags 时入图激发）
             */
            virtual std::vector<std::string> getRequiredTags() const { return compositeRegisterInfo.requiredTags; }

            /**
             * @brief 声明 TokenGraph 拓扑前驱组合体 Token 列表（当前组合体将在这些前驱加载/初始化之后执行）
             */
            virtual std::vector<std::string> getPredecessors() const { return compositeRegisterInfo.predecessors; }

            /**
             * @brief 声明 TokenGraph 拓扑后继组合体 Token 列表（当前组合体将在这些后继加载/初始化之前执行）
             */
            virtual std::vector<std::string> getSuccessors() const { return compositeRegisterInfo.successors; }

            /**
             * @brief 组合体装载阶段，注册静态元数据或服务接口
             */
            virtual Status onLoad(Engine* engine) {
                (void)engine;
                return OkStatus();
            }

            /**
             * @brief 组合体初始化阶段，挂载 Pass、Tech、System 或 UI 模块
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
             * @brief 组合体卸载回调，清理挂载的资源与服务
             */
            virtual void onUnload() {}

            /**
             * @brief 热重载前序列化组合体内部状态（可选重写）
             */
            virtual std::string onSerializeHotReloadState() const { return ""; }

            /**
             * @brief 热重载后恢复组合体内部状态（可选重写）
             */
            virtual void onRestoreHotReloadState(const std::string& state) {
                (void)state;
            }
        };

    } // namespace Runtime
} // namespace MQEngine

#endif // ENGINE_ICOMPOSITE_H
