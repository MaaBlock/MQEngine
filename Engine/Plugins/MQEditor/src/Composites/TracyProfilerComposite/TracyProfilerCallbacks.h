/**
 * @file TracyProfilerCallbacks.h
 * @brief 内嵌上游 tracy::View 所需的无锁主线程任务队列、字体槽位绑定与宿主窗口空操作回调桥接
 */
#ifndef MQENGINE_MQ_EDITOR_TRACY_PROFILER_CALLBACKS_H
#define MQENGINE_MQ_EDITOR_TRACY_PROFILER_CALLBACKS_H

#ifdef small
#undef small
#endif

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <Fonts.hpp>
#include <TracyTexture.hpp>
#include <imgui.h>

#include <functional>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace TracyBridge
        {
            /**
             * @brief 获取无锁主线程任务队列单例（供上游 tracy::View 后台工作线程投递主线程任务）
             */
            inline moodycamel::ConcurrentQueue<std::function<void()>>& getMainThreadTaskQueue()
            {
                static moodycamel::ConcurrentQueue<std::function<void()>> s_mainThreadTasks;
                return s_mainThreadTasks;
            }

            /**
             * @brief 传给 tracy::View 构造函数的 RunOnMainThread 回调
             */
            inline void runOnMainThread(const std::function<void()>& fn, bool force)
            {
                (void)force;
                getMainThreadTaskQueue().enqueue(fn);
            }

            /**
             * @brief 在编辑器 UI 主线程每帧消费并执行所有已排队的 tracy::View 回调任务
             */
            inline void drainMainThreadTasks()
            {
                std::function<void()> task;
                auto& queue = getMainThreadTaskQueue();
                while (queue.try_dequeue(task))
                {
                    if (task)
                    {
                        task();
                    }
                }
            }

            /**
             * @brief 将当前活跃 ImGui 字体绑定至上游 Tracy Profiler 全局字体槽位
             */
            inline void bindCurrentImGuiFonts()
            {
                ImFont* currentFont = ImGui::GetFont();
                g_fonts.normal = currentFont;
                g_fonts.mono = currentFont;
                g_fonts.bold = currentFont;
                g_fonts.boldItalic = currentFont;
                g_fonts.italic = currentFont;
                FontNormal = ImGui::GetFontSize();
                FontSmall = FontNormal * 0.75f;
                FontBig = FontNormal * 1.5f;
                tracy::InitTexture();
            }

            inline void noopSetTitle(const char*) {}
            inline void noopSetScale(int) {}
            inline void noopAttention() {}
        } // namespace TracyBridge

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_TRACY_PROFILER_CALLBACKS_H
