/**
 * @file DebugSubsystem.cpp
 * @brief 调试诊断子域实现
 */
#include <MQEngine/domain/debug/DebugSubsystem.h>
#include <sstream>
#include <cmath>

namespace MQEngine
{
    namespace Domain
    {
        namespace Debug
        {
            DebugSubsystem::DebugSubsystem() = default;

            DebugSubsystem::~DebugSubsystem()
            {
                term();
            }

            void DebugSubsystem::registerInfo()
            {
            }

            Status DebugSubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                // 注册内置控制台基础指令
                registerCommand("help", [this](const std::vector<std::string>&) {
                    std::string out = "Available commands:\n";
                    for (const auto& [name, entry] : m_commands) {
                        out += "  " + name + " : " + entry.description + "\n";
                    }
                    return out;
                }, "列出全部已注册控制台指令");

                registerCommand("clear", [this](const std::vector<std::string>&) {
                    clearLogHistory();
                    return "日志已清空";
                }, "清空控制台日志历史");

                registerCommand("echo", [](const std::vector<std::string>& args) {
                    std::string out;
                    for (size_t i = 1; i < args.size(); ++i) {
                        out += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                    return out;
                }, "原样输出传入参数");

                m_initialized.store(true, std::memory_order_release);
                return OkStatus();
            }

            void DebugSubsystem::term() noexcept
            {
                if (!m_initialized.load(std::memory_order_relaxed)) {
                    return;
                }

                m_gizmoLines.clear();
                m_commands.clear();
                m_logHistory.clear();
                m_initialized.store(false, std::memory_order_release);
            }

            void DebugSubsystem::drawLine(
                const FCT::Vec3<>& start,
                const FCT::Vec3<>& end,
                const FCT::Vec3<>& color,
                float duration)
            {
                (void)m_incomingLines.push(GizmoLine{start, end, color, duration});
            }

            void DebugSubsystem::drawBox(
                const FCT::Vec3<>& center,
                const FCT::Vec3<>& extents,
                const FCT::Vec3<>& color,
                float duration)
            {
                FCT::Vec3<> min = center - extents;
                FCT::Vec3<> max = center + extents;

                // 12条包围盒线框
                drawLine({min.x, min.y, min.z}, {max.x, min.y, min.z}, color, duration);
                drawLine({max.x, min.y, min.z}, {max.x, max.y, min.z}, color, duration);
                drawLine({max.x, max.y, min.z}, {min.x, max.y, min.z}, color, duration);
                drawLine({min.x, max.y, min.z}, {min.x, min.y, min.z}, color, duration);

                drawLine({min.x, min.y, max.z}, {max.x, min.y, max.z}, color, duration);
                drawLine({max.x, min.y, max.z}, {max.x, max.y, max.z}, color, duration);
                drawLine({max.x, max.y, max.z}, {min.x, max.y, max.z}, color, duration);
                drawLine({min.x, max.y, max.z}, {min.x, min.y, max.z}, color, duration);

                drawLine({min.x, min.y, min.z}, {min.x, min.y, max.z}, color, duration);
                drawLine({max.x, min.y, min.z}, {max.x, min.y, max.z}, color, duration);
                drawLine({max.x, max.y, min.z}, {max.x, max.y, max.z}, color, duration);
                drawLine({min.x, max.y, min.z}, {min.x, max.y, max.z}, color, duration);
            }

            void DebugSubsystem::drawSphere(
                const FCT::Vec3<>& center,
                float radius,
                const FCT::Vec3<>& color,
                float duration)
            {
                constexpr int segments = 16;
                constexpr float pi2 = 6.28318530718f;

                for (int i = 0; i < segments; ++i) {
                    float a0 = (static_cast<float>(i) / segments) * pi2;
                    float a1 = (static_cast<float>(i + 1) / segments) * pi2;

                    // XY 圆环
                    drawLine(center + FCT::Vec3<>(std::cos(a0) * radius, std::sin(a0) * radius, 0.0f),
                             center + FCT::Vec3<>(std::cos(a1) * radius, std::sin(a1) * radius, 0.0f),
                             color, duration);
                    // XZ 圆环
                    drawLine(center + FCT::Vec3<>(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius),
                             center + FCT::Vec3<>(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius),
                             color, duration);
                }
            }

            void DebugSubsystem::drawFrustum(
                const FCT::Mat4& invViewProj,
                const FCT::Vec3<>& color,
                float duration)
            {
                // Note: 在 Vulkan NDC 空间中，X/Y 范围为 [-1, 1]，Z 深度范围为 [0, 1]（近平面 Z=0，远平面 Z=1）
                const FCT::Vec4 ndcCorners[8] = {
                    // 近平面 4 顶点（逆时针：左下、右下、右上、左上）
                    {-1.0f, -1.0f, 0.0f, 1.0f},
                    { 1.0f, -1.0f, 0.0f, 1.0f},
                    { 1.0f,  1.0f, 0.0f, 1.0f},
                    {-1.0f,  1.0f, 0.0f, 1.0f},
                    // 远平面 4 顶点（逆时针：左下、右下、右上、左上）
                    {-1.0f, -1.0f, 1.0f, 1.0f},
                    { 1.0f, -1.0f, 1.0f, 1.0f},
                    { 1.0f,  1.0f, 1.0f, 1.0f},
                    {-1.0f,  1.0f, 1.0f, 1.0f}
                };

                FCT::Vec3<> worldCorners[8];
                for (int i = 0; i < 8; ++i) {
                    const FCT::Vec4 worldPoint4 = invViewProj * ndcCorners[i];
                    if (std::abs(worldPoint4.w) < 1e-6f) {
                        return; // 投影矩阵奇异，跳过绘制以防产生无效坐标
                    }
                    worldCorners[i] = FCT::Vec3<>(
                        worldPoint4.x / worldPoint4.w,
                        worldPoint4.y / worldPoint4.w,
                        worldPoint4.z / worldPoint4.w);
                }

                // 近裁剪面 4 条边
                drawLine(worldCorners[0], worldCorners[1], color, duration);
                drawLine(worldCorners[1], worldCorners[2], color, duration);
                drawLine(worldCorners[2], worldCorners[3], color, duration);
                drawLine(worldCorners[3], worldCorners[0], color, duration);

                // 远裁剪面 4 条边
                drawLine(worldCorners[4], worldCorners[5], color, duration);
                drawLine(worldCorners[5], worldCorners[6], color, duration);
                drawLine(worldCorners[6], worldCorners[7], color, duration);
                drawLine(worldCorners[7], worldCorners[4], color, duration);

                // 连接近远平面的 4 条侧边棱线
                drawLine(worldCorners[0], worldCorners[4], color, duration);
                drawLine(worldCorners[1], worldCorners[5], color, duration);
                drawLine(worldCorners[2], worldCorners[6], color, duration);
                drawLine(worldCorners[3], worldCorners[7], color, duration);
            }

            void DebugSubsystem::drawFrustum(
                const FCT::Mat4& view,
                const FCT::Mat4& proj,
                const FCT::Vec3<>& color,
                float duration)
            {
                const FCT::Mat4 viewProj = proj * view;
                drawFrustum(viewProj.inverse(), color, duration);
            }

            std::vector<GizmoLine> DebugSubsystem::getGizmoLines() const
            {
                GizmoLine incoming;
                while (m_incomingLines.pop(incoming)) {
                    m_gizmoLines.push_back(incoming);
                }
                return m_gizmoLines;
            }

            void DebugSubsystem::tickGizmos(float deltaTime)
            {
                GizmoLine incoming;
                while (m_incomingLines.pop(incoming)) {
                    m_gizmoLines.push_back(incoming);
                }
                std::vector<GizmoLine> alive;
                alive.reserve(m_gizmoLines.size());

                for (auto& line : m_gizmoLines) {
                    if (line.duration > 0.0f) {
                        line.duration -= deltaTime;
                        if (line.duration > 0.0f) {
                            alive.push_back(line);
                        }
                    }
                }
                m_gizmoLines = std::move(alive);
            }

            void DebugSubsystem::registerCommand(
                const std::string& commandName,
                ConsoleCommandHandler handler,
                const std::string& helpDescription)
            {
                m_commands[commandName] = {std::move(handler), helpDescription};
            }

            void DebugSubsystem::unregisterCommand(const std::string& commandName)
            {
                m_commands.erase(commandName);
            }

            std::string DebugSubsystem::executeCommand(const std::string& commandLine)
            {
                log("> " + commandLine);

                std::istringstream stream(commandLine);
                std::vector<std::string> args;
                std::string token;
                while (stream >> token) {
                    args.push_back(token);
                }

                if (args.empty()) {
                    return "";
                }

                auto it = m_commands.find(args[0]);
                if (it == m_commands.end()) {
                    std::string err = "未知指令: " + args[0];
                    log(err);
                    return err;
                }

                std::string result = it->second.handler(args);
                if (!result.empty()) {
                    log(result);
                }
                return result;
            }

            void DebugSubsystem::log(const std::string& message)
            {
                m_logHistory.push_back(message);
                if (m_logHistory.size() > 1000) {
                    m_logHistory.erase(m_logHistory.begin(), m_logHistory.begin() + 100);
                }
            }

            void DebugSubsystem::clearLogHistory()
            {
                m_logHistory.clear();
            }

        } // namespace Debug
    } // namespace Domain
} // namespace MQEngine
