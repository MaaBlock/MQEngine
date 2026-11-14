/**
 * @file DebugSubsystem.h
 * @brief 调试诊断子域：内置控制台指令系统、可视化 Gizmo 调试绘制器与 Tracy 性能剖析钩子
 */
#ifndef MQENGINE_DOMAIN_DEBUG_DEBUGSUBSYSTEM_H
#define MQENGINE_DOMAIN_DEBUG_DEBUGSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <boost/lockfree/spsc_queue.hpp>

namespace MQEngine
{
    namespace Domain
    {
        namespace Debug
        {
            /**
             * @brief 调试线条基元
             */
            struct ENGINE_API GizmoLine
            {
                FCT::Vec3<> start{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> end{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> color{1.0f, 1.0f, 1.0f};
                float duration = 0.0f; // 存活持续时间 (秒)，0 表示仅本帧有效
            };

            /**
             * @brief 控制台指令处理函数类型
             */
            using ConsoleCommandHandler = std::function<std::string(const std::vector<std::string>&)>;

            /**
             * @brief 调试与诊断子系统核心类
             */
            class ENGINE_API DebugSubsystem
            {
            public:
                DebugSubsystem();
                ~DebugSubsystem();

                DebugSubsystem(const DebugSubsystem&) = delete;
                DebugSubsystem& operator=(const DebugSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                // ==================== 1. 可视化 Gizmo 调试绘制接口 ====================
                void drawLine(const FCT::Vec3<>& start, const FCT::Vec3<>& end, const FCT::Vec3<>& color = FCT::Vec3<>(1.0f, 1.0f, 1.0f), float duration = 0.0f);
                void drawBox(const FCT::Vec3<>& center, const FCT::Vec3<>& extents, const FCT::Vec3<>& color = FCT::Vec3<>(1.0f, 1.0f, 1.0f), float duration = 0.0f);
                void drawSphere(const FCT::Vec3<>& center, float radius, const FCT::Vec3<>& color = FCT::Vec3<>(1.0f, 1.0f, 1.0f), float duration = 0.0f);
                void drawFrustum(const FCT::Mat4& invViewProj, const FCT::Vec3<>& color = FCT::Vec3<>(1.0f, 1.0f, 0.0f), float duration = 0.0f);
                void drawFrustum(const FCT::Mat4& view, const FCT::Mat4& proj, const FCT::Vec3<>& color = FCT::Vec3<>(1.0f, 1.0f, 0.0f), float duration = 0.0f);

                /**
                 * @brief 获取当前帧全部待渲染的 Gizmo 线段
                 */
                std::vector<GizmoLine> getGizmoLines() const;

                /**
                 * @brief 清理已过期的 Gizmo 绘制基元
                 * @param deltaTime 逻辑帧流逝时间 (秒)
                 */
                void tickGizmos(float deltaTime);

                // ==================== 2. 内置控制台指令系统 ====================
                void registerCommand(const std::string& commandName, ConsoleCommandHandler handler, const std::string& helpDescription = "");
                void unregisterCommand(const std::string& commandName);
                std::string executeCommand(const std::string& commandLine);

                // ==================== 3. 控制台日志捕获与查询 ====================
                void log(const std::string& message);
                const std::vector<std::string>& getLogHistory() const noexcept { return m_logHistory; }
                void clearLogHistory();

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

            private:
                struct CommandEntry
                {
                    ConsoleCommandHandler handler;
                    std::string description;
                };

                std::atomic<bool> m_initialized{false};
                mutable boost::lockfree::spsc_queue<GizmoLine, boost::lockfree::capacity<4096>> m_incomingLines;
                mutable std::vector<GizmoLine> m_gizmoLines;
                std::unordered_map<std::string, CommandEntry> m_commands;
                std::vector<std::string> m_logHistory;
            };

        } // namespace Debug
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_DEBUG_DEBUGSUBSYSTEM_H
