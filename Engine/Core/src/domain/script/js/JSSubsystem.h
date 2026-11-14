/**
 * @file JSSubsystem.h
 * @brief JavaScript/TypeScript 脚本子域：基于 FCT_Node (V8 + libuv) 虚拟机环境，
 *        在 WorkHost 中注册 Dedicated I/O 事件循环轮询源，
 *        通过 FCT::NB 自动反射绑定 ECS 组件与核心引擎子系统接口
 */
#ifndef MQENGINE_DOMAIN_SCRIPT_JS_JSSUBSYSTEM_H
#define MQENGINE_DOMAIN_SCRIPT_JS_JSSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <string>
#include <vector>
#include <atomic>

namespace MQEngine
{
    namespace Domain
    {
        namespace Script
        {
            /**
             * @brief JavaScript/TypeScript 脚本运行时子系统（严格无锁单线程调度）
             */
            class ENGINE_API JSSubsystem
            {
            public:
                JSSubsystem();
                ~JSSubsystem();

                JSSubsystem(const JSSubsystem&) = delete;
                JSSubsystem& operator=(const JSSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                /**
                 * @brief 轮询 V8/libuv 事件循环，由 WorkHost Dedicated I/O 线程定时触发
                 */
                void pollEvents();

                /**
                 * @brief 执行一段 JavaScript 源码
                 * @param source JS 源码字符串
                 * @param filename 脚本文件名或标识
                 */
                Status evaluateScript(const std::string& source, const std::string& filename = "anonymous.js");

                /**
                 * @brief 加载并执行 JS/TS 模块文件
                 * @param modulePath 模块文件路径
                 */
                Status loadModule(const std::string& modulePath);

                /**
                 * @brief 获取底层 NodeEnvironment 实例指针
                 */
                FCT::JS::NodeEnvironment* getNodeEnvironment() const noexcept { return m_nodeEnv.get(); }

                /**
                 * @brief 获取导出的 Native Binding Package
                 */
                FCT::NB::Package* getEnginePackage() const noexcept { return m_enginePackage.get(); }

                /**
                 * @brief 检查运行时是否已就绪
                 */
                bool isRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

            private:
                void setupEnginePackage();
                void registerWithWorkHost();
                void unregisterFromWorkHost() noexcept;

            private:
                UniquePtr<FCT::JS::NodeEnvironment> m_nodeEnv;
                UniquePtr<FCT::NB::Package> m_enginePackage;
                std::atomic<bool> m_running{false};
            };

        } // namespace Script
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCRIPT_JS_JSSUBSYSTEM_H
