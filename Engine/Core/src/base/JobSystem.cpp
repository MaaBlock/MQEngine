#include "JobSystem.h"

namespace MQEngine {

    JobSystem::JobSystem(size_t workerCount)
        : m_threadPool(workerCount, "MQJobWorker") {
        ThreadPool::setSharedInstance(&m_threadPool);
    }

    JobSystem::~JobSystem() {
        shutdown();
    }

    void JobSystem::shutdown() {
        if (&ThreadPool::globalInstance() == &m_threadPool) {
            ThreadPool::setSharedInstance(nullptr);
        }
        m_threadPool.shutdown();
        std::lock_guard<std::mutex> lock(m_mainQueueMutex);
        m_mainThreadQueue.clear();
    }

    Status JobSystem::dispatch(std::function<void()> task, JobPriority priority, JobCounter* counter) {
        return m_threadPool.dispatch(std::move(task), priority, counter);
    }

    void JobSystem::wait(JobCounter& counter) {
        m_threadPool.wait(counter);
    }

    void JobSystem::waitAll() {
        m_threadPool.waitIdle();
    }

    void JobSystem::enqueueMainThread(std::function<void()> callback) {
        if (!callback) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mainQueueMutex);
        m_mainThreadQueue.push_back(std::move(callback));
    }

    size_t JobSystem::pumpMainThreadTasks(size_t maxTasks) {
        ZoneScoped();
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(m_mainQueueMutex);
            if (m_mainThreadQueue.empty()) {
                return 0;
            }
            const size_t fetchCount = maxTasks == 0
                ? m_mainThreadQueue.size()
                : std::min(maxTasks, m_mainThreadQueue.size());
            batch.reserve(fetchCount);
            for (size_t i = 0; i < fetchCount; ++i) {
                batch.push_back(std::move(m_mainThreadQueue.front()));
                m_mainThreadQueue.pop_front();
            }
        }

        for (auto& task : batch) {
            if (task) {
                task();
            }
        }
        return batch.size();
    }

    uint32_t JobSystem::getWorkerCount() const {
        return static_cast<uint32_t>(m_threadPool.workerCount());
    }

    uint64_t JobSystem::getPendingJobCount() const {
        return static_cast<uint64_t>(m_threadPool.pendingTaskCount());
    }

    uint64_t JobSystem::getCompletedJobCount() const {
        return m_threadPool.completedTaskCount();
    }

    uint32_t JobSystem::getMainThreadQueueSize() const {
        std::lock_guard<std::mutex> lock(m_mainQueueMutex);
        return static_cast<uint32_t>(m_mainThreadQueue.size());
    }

} // namespace MQEngine
