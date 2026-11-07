#ifndef MQENGINE_CESIUM_TASK_PROCESSOR_H
#define MQENGINE_CESIUM_TASK_PROCESSOR_H

#include "thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief Cesium 异步工作线程池调度器，负责后台瓦片下载、Draco 解压与顶点网格生成任务
 */
class TILES3D_PLUGIN_API CesiumTaskProcessor : public CesiumAsync::ITaskProcessor {
public:
    explicit CesiumTaskProcessor(size_t numThreads = 4);
    ~CesiumTaskProcessor() override;

    void startTask(std::function<void()> f) override;
    void shutdown();

private:
    void workerLoop();

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stop{false};
};

} // namespace MQEngine

#endif // MQENGINE_CESIUM_TASK_PROCESSOR_H
