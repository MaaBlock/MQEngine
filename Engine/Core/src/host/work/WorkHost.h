/**
 * @file WorkHost.h
 * @brief 双轨并发宿主与单线程无锁内联降级调度器 (PRD 03 §2, FEAT-21, FEAT-22)
 */
#ifndef MQENGINE_HOST_MANAGER_WORKHOST_H
#define MQENGINE_HOST_MANAGER_WORKHOST_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace MQEngine
{
    namespace Host
    {
        /**
         * @brief 工作线程宿主（WorkHost）：
         * 1. 专用异步 I/O 线程组（文件 I/O、网络 I/O、FCT_Node 轮询）
         * 2. 基于 bcpp::concurrency::blocking_work_contract_group 的无锁工作契约池
         * 3. 单线程无锁时间片内联排空降级模式（小游戏无 SharedArrayBuffer 稳妥保底）
         */
        class ENGINE_API WorkHost : public CoreEngineManager<WorkHost>
        {
        public:
            enum class Mode
            {
                SingleThreadInline,    // 单线程时间片内联排空模式 (小游戏稳妥降级)
                MultiThreadedWorkers   // 多线程工作线程组模式 (支持 -pthread / Desktop)
            };

            WorkHost();
            ~WorkHost();

            WorkHost(const WorkHost&) = delete;
            WorkHost& operator=(const WorkHost&) = delete;

            void registerInfo();
            Status init(Mode mode, uint32_t workerCount = 0, uint32_t ioThreadCount = 1);
            Status init(uint32_t workerCount = 0, uint32_t ioThreadCount = 1);
            void term() noexcept;

            void sync();
            void waitAll();
            void drainInline(uint32_t maxBudgetMicroseconds = 2000);

            bool isRunning() const noexcept { return m_running.load(std::memory_order_acquire); }
            Mode getMode() const noexcept { return m_mode; }

            /**
             * @brief 根据平台并发能力计算推荐 Worker 线程数（Wasm pthread 钳位至 2~3，不支持 pthread 时返回 0 降级）
             */
            static uint32_t resolveRecommendedWorkerCount(
                uint32_t hardwareConcurrency,
                bool pthreadSupported = true,
                bool isMiniGameRuntime = false) noexcept;

            // ==================== 1. Work Contract 调度接口 ====================

            template <typename WorkFunction>
            [[nodiscard]] bcpp::work_contract createContract(
                WorkFunction&& workFn,
                bcpp::work_contract::initial_state initialState = bcpp::work_contract::initial_state::unscheduled)
            {
                assert(m_running.load(std::memory_order_acquire) && "WorkHost 未处于运行状态");
                return m_contractGroup.create_contract(std::forward<WorkFunction>(workFn), initialState);
            }

            template <typename WorkFunction, typename ReleaseFunction>
            [[nodiscard]] bcpp::work_contract createContract(
                WorkFunction&& workFn,
                ReleaseFunction&& releaseFn,
                bcpp::work_contract::initial_state initialState = bcpp::work_contract::initial_state::unscheduled)
            {
                assert(m_running.load(std::memory_order_acquire) && "WorkHost 未处于运行状态");
                return m_contractGroup.create_contract(
                    std::forward<WorkFunction>(workFn),
                    std::forward<ReleaseFunction>(releaseFn),
                    initialState);
            }

            /**
             * @brief 安全调度契约（WorkHost 已终止或契约无效时返回 false）
             */
            bool scheduleContract(bcpp::work_contract& contract) noexcept;

            bool executeNextContract();

            Status postTask(std::function<void()> task);
            void postCpuTask(std::function<void()> task) { (void)postTask(std::move(task)); }

            // ==================== 2. 专用异步 I/O 线程组接口 ====================

            Status postIoTask(std::function<void()> ioTask);

            Status submitAsyncFileRead(
                const std::string& path,
                std::function<void(StatusOr<std::string>)> onComplete);

            void asyncReadAndDispatch(
                std::string_view path,
                bcpp::work_contract contract,
                std::function<void(StatusOr<std::vector<uint8_t>>)> onReadComplete);

            void registerIoPoller(std::string_view name, std::function<void()> poller);
            void unregisterIoPoller(std::string_view name);

            // ==================== 3. 主线程队列与兼容性接口 ====================

            void enqueueMainThread(std::function<void()> callback);
            size_t pumpMainThreadTasks(size_t maxTasks = 256);

            uint32_t getWorkerCount() const noexcept { return static_cast<uint32_t>(m_workerThreads.size()); }
            uint32_t getIoThreadCount() const noexcept { return static_cast<uint32_t>(m_ioThreads.size()); }

        private:
            void workerThreadLoop(uint32_t workerIndex);
            void ioThreadLoop(uint32_t ioThreadIndex);
            void processPendingIoTasks();
            void pollIoSources();

        private:
            struct PollerNode
            {
                std::string name;
                std::function<void()> poller;
                std::atomic<bool> active{true};
                PollerNode* next = nullptr;
            };

            std::atomic<bool> m_running{false};
            std::atomic<bool> m_initialized{false};
            Mode m_mode{Mode::MultiThreadedWorkers};

            bcpp::concurrency::blocking_work_contract_group<512> m_contractGroup;
            std::vector<std::thread> m_workerThreads;

            std::vector<std::thread> m_ioThreads;
            boost::lockfree::queue<std::function<void()>*> m_ioTasks{1024};
            std::atomic<uint32_t> m_pendingIoCount{0};

            std::atomic<PollerNode*> m_ioPollersHead{nullptr};
            boost::lockfree::queue<std::function<void()>*> m_mainThreadQueue{1024};

            boost::lockfree::queue<std::function<void()>*> m_generalTaskQueue{1024};
            bcpp::work_contract m_generalTaskContract;
            std::mutex m_retainedContractsMutex;
            std::deque<bcpp::work_contract> m_retainedContracts;
        };

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_MANAGER_WORKHOST_H
