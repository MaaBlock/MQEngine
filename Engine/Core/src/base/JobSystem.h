#ifndef ENGINE_JOB_SYSTEM_H
#define ENGINE_JOB_SYSTEM_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "./CoreEngineManager.h"

namespace MQEngine {

    /**
     * @brief 引擎级多线程作业系统，托管全局工作窃取线程池与逻辑主线程延迟回调队列
     */
    class ENGINE_API JobSystem : public CoreEngineManager<JobSystem> {
    public:
        explicit JobSystem(size_t workerCount = 0);
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        void registerInfo() {}

        ThreadPool& pool() noexcept { return m_threadPool; }
        const ThreadPool& pool() const noexcept { return m_threadPool; }

        void shutdown();

        Status dispatch(
            std::function<void()> task,
            JobPriority priority = JobPriority::Normal,
            JobCounter* counter = nullptr);

        // ==================== 现代异步调度 API (Requirement R1 / R2) ====================
        template<typename Callable, typename... Args>
        auto submitAsync(JobPriority priority, Callable&& fn, Args&&... args)
            -> Computation<std::invoke_result_t<std::decay_t<Callable>, std::decay_t<Args>...>> {
            return m_threadPool.submitAsync(
                priority,
                std::forward<Callable>(fn),
                std::forward<Args>(args)...);
        }

        template<typename Callable, typename... Args>
        auto submitAsync(Callable&& fn, Args&&... args)
            -> Computation<std::invoke_result_t<std::decay_t<Callable>, std::decay_t<Args>...>> {
            return submitAsync(
                JobPriority::Normal,
                std::forward<Callable>(fn),
                std::forward<Args>(args)...);
        }

        template<typename Callable, typename... Args>
        auto submitComputation(JobPriority priority, Callable&& fn, Args&&... args)
            -> Computation<std::invoke_result_t<std::decay_t<Callable>, std::decay_t<Args>...>> {
            return submitAsync(
                priority,
                std::forward<Callable>(fn),
                std::forward<Args>(args)...);
        }

        template<typename Callable, typename... Args>
        auto submitComputation(Callable&& fn, Args&&... args)
            -> Computation<std::invoke_result_t<std::decay_t<Callable>, std::decay_t<Args>...>> {
            return submitAsync(
                JobPriority::Normal,
                std::forward<Callable>(fn),
                std::forward<Args>(args)...);
        }

        template<typename Callable, typename... Args>
        auto submit(Callable&& fn, Args&&... args)
            -> std::future<std::invoke_result_t<Callable, Args...>> {
            return m_threadPool.submit(std::forward<Callable>(fn), std::forward<Args>(args)...);
        }

        template<typename Callable, typename... Args>
        auto submitWithPriority(JobPriority priority, Callable&& fn, Args&&... args)
            -> std::future<std::invoke_result_t<Callable, Args...>> {
            return m_threadPool.submitWithPriority(
                priority,
                std::forward<Callable>(fn),
                std::forward<Args>(args)...);
        }

        template<typename IndexType, typename BodyFn>
        void parallelFor(
            IndexType begin,
            IndexType end,
            IndexType grainSize,
            BodyFn&& body,
            JobPriority priority = JobPriority::High) {
            m_threadPool.parallelFor(begin, end, grainSize, std::forward<BodyFn>(body), priority);
        }

        void wait(JobCounter& counter);
        void waitAll();

        void enqueueMainThread(std::function<void()> callback);
        size_t pumpMainThreadTasks(size_t maxTasks = 256);

        uint32_t getWorkerCount() const;
        uint64_t getPendingJobCount() const;
        uint64_t getCompletedJobCount() const;
        uint32_t getMainThreadQueueSize() const;

    private:
        ThreadPool m_threadPool;
        mutable std::mutex m_mainQueueMutex;
        std::deque<std::function<void()>> m_mainThreadQueue;
    };

} // namespace MQEngine

#endif // ENGINE_JOB_SYSTEM_H
