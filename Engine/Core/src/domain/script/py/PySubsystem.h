/**
 * @file PySubsystem.h
 * @brief Python 脚本子域：基于 FCT_PyBri 的嵌入式 CPython 解释器环境与 GIL 生命周期管理，
 *        通过反射零样板绑定 ECS 组件与核心业务子系统
 */
#ifndef MQENGINE_DOMAIN_SCRIPT_PY_PYSUBSYSTEM_H
#define MQENGINE_DOMAIN_SCRIPT_PY_PYSUBSYSTEM_H

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
             * @brief Python 脚本运行时子系统（严格无锁单线程调度）
             */
            class ENGINE_API PySubsystem
            {
            public:
                PySubsystem();
                ~PySubsystem();

                PySubsystem(const PySubsystem&) = delete;
                PySubsystem& operator=(const PySubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                /**
                 * @brief 执行一段 Python 代码字符串
                 */
                Status executeScript(const std::string& script);

                /**
                 * @brief 执行指定的 Python 脚本文件
                 */
                Status executeFile(const std::string& filePath);

                /**
                 * @brief 检查 Python 运行时是否处于活跃可用状态
                 */
                bool isAvailable() const noexcept { return m_available.load(std::memory_order_acquire); }

#ifndef __ANDROID__
                FCT::PyBri::PyEnvironment* getPyEnvironment() const noexcept { return m_pyEnv.get(); }
                FCT::PyBri::Package* getEnginePackage() const noexcept { return m_enginePackage.get(); }
#endif

            private:
                void setupPyBriEnginePackage();

            private:
#ifndef __ANDROID__
                UniquePtr<FCT::PyBri::PyEnvironment> m_pyEnv;
                UniquePtr<FCT::PyBri::Package> m_enginePackage;
#endif
                std::atomic<bool> m_available{false};
            };

        } // namespace Script
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCRIPT_PY_PYSUBSYSTEM_H
