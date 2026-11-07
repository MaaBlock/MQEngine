#include "CesiumTaskProcessor.h"

namespace MQEngine {

CesiumTaskProcessor::CesiumTaskProcessor(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = 2;
    }
    m_workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this]() {
            workerLoop();
        });
    }
}

CesiumTaskProcessor::~CesiumTaskProcessor() {
    shutdown();
}

void CesiumTaskProcessor::shutdown() {
    bool expected = false;
    if (!m_stop.compare_exchange_strong(expected, true)) {
        return;
    }
    m_condition.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
}

void CesiumTaskProcessor::startTask(std::function<void()> f) {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_stop.load()) {
            return;
        }
        m_tasks.push(std::move(f));
    }
    m_condition.notify_one();
}

void CesiumTaskProcessor::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this]() {
                return m_stop.load() || !m_tasks.empty();
            });
            if (m_stop.load() && m_tasks.empty()) {
                return;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        if (task) {
            task();
        }
    }
}

} // namespace MQEngine
