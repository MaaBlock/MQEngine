/**
 * @file WorkHost.cpp
 * @brief 双轨并发宿主与单线程无锁内联降级调度器实现
 */
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/runtime/core/Engine.h>
#include <algorithm>
#include <chrono>

namespace MQEngine
{
    namespace Host
    {
        WorkHost::WorkHost() = default;

        WorkHost::~WorkHost()
        {
            term();
        }

        void WorkHost::registerInfo()
        {
        }

        uint32_t WorkHost::resolveRecommendedWorkerCount(
            uint32_t hardwareConcurrency,
            bool pthreadSupported,
            bool isMiniGameRuntime) noexcept
        {
            if (!pthreadSupported)
            {
                return 0u;
            }

            if (isMiniGameRuntime)
            {
                if (hardwareConcurrency <= 2u)
                {
                    return 2u;
                }
                return std::clamp(hardwareConcurrency - 1u, 2u, 3u);
            }

            return (hardwareConcurrency > 2u) ? (hardwareConcurrency - 2u) : 1u;
        }

        Status WorkHost::init(uint32_t workerCount, uint32_t ioThreadCount)
        {
            Mode mode = Mode::MultiThreadedWorkers;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
            mode = Mode::SingleThreadInline;
#else
            if (workerCount == 0 && ioThreadCount == 0)
            {
                mode = Mode::SingleThreadInline;
            }
#endif
            return init(mode, workerCount, ioThreadCount);
        }

        Status WorkHost::init(Mode mode, uint32_t workerCount, uint32_t ioThreadCount)
        {
            if (m_initialized.load(std::memory_order_acquire))
            {
                return OkStatus();
            }

            m_mode = mode;
            m_running.store(true, std::memory_order_release);

            m_generalTaskContract = m_contractGroup.create_contract([this]() {
                std::function<void()>* task = nullptr;
                if (m_generalTaskQueue.pop(task))
                {
                    if (task)
                    {
                        if (*task)
                        {
                            (*task)();
                        }
                        FCT_DELETE(task);
                    }
                    if (!m_generalTaskQueue.empty() && m_running.load(std::memory_order_acquire))
                    {
                        m_generalTaskContract.schedule();
                    }
                }
            });

            if (m_mode == Mode::MultiThreadedWorkers)
            {
                if (workerCount == 0)
                {
                    const uint32_t hw = std::thread::hardware_concurrency();
#if defined(__EMSCRIPTEN__)
                    workerCount = resolveRecommendedWorkerCount(hw, true, true);
#else
                    workerCount = resolveRecommendedWorkerCount(hw, true, false);
#endif
                }
                if (ioThreadCount == 0)
                {
                    ioThreadCount = 1;
                }

                m_workerThreads.reserve(workerCount);
                for (uint32_t i = 0; i < workerCount; ++i)
                {
                    m_workerThreads.emplace_back(&WorkHost::workerThreadLoop, this, i);
                }

                m_ioThreads.reserve(ioThreadCount);
                for (uint32_t i = 0; i < ioThreadCount; ++i)
                {
                    m_ioThreads.emplace_back(&WorkHost::ioThreadLoop, this, i);
                }
            }

            m_initialized.store(true, std::memory_order_release);
            return OkStatus();
        }

        void WorkHost::term() noexcept
        {
            if (!m_initialized.load(std::memory_order_acquire))
            {
                return;
            }

            m_running.store(false, std::memory_order_release);

            for (auto& t : m_ioThreads)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
            m_ioThreads.clear();

            {
                std::lock_guard<std::mutex> lock(m_retainedContractsMutex);
                for (auto& contract : m_retainedContracts)
                {
                    if (contract.is_valid())
                    {
                        contract.release();
                    }
                }
                m_retainedContracts.clear();
            }

            if (m_generalTaskContract.is_valid())
            {
                m_generalTaskContract.release();
            }
            m_contractGroup.stop();

            for (auto& t : m_workerThreads)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
            m_workerThreads.clear();

            std::function<void()>* task = nullptr;
            while (m_generalTaskQueue.pop(task))
            {
                FCT_DELETE(task);
            }
            while (m_ioTasks.pop(task))
            {
                FCT_DELETE(task);
            }
            while (m_mainThreadQueue.pop(task))
            {
                FCT_DELETE(task);
            }
            m_pendingIoCount.store(0, std::memory_order_relaxed);

            PollerNode* currPoller = m_ioPollersHead.exchange(nullptr, std::memory_order_acq_rel);
            while (currPoller)
            {
                PollerNode* next = currPoller->next;
                FCT_DELETE(currPoller);
                currPoller = next;
            }

            m_initialized.store(false, std::memory_order_release);
        }

        void WorkHost::workerThreadLoop(uint32_t workerIndex)
        {
            const std::string threadName = "WorkHost_Worker_" + std::to_string(workerIndex);
            FCT::SetThreadName(threadName.c_str());
            bcpp::concurrency::signal_id hint{};
            while (m_running.load(std::memory_order_acquire))
            {
                if (!m_contractGroup.execute_next_contract(hint))
                {
                    break;
                }
            }
        }

        void WorkHost::ioThreadLoop(uint32_t ioThreadIndex)
        {
            const std::string threadName = "WorkHost_IO_" + std::to_string(ioThreadIndex);
            FCT::SetThreadName(threadName.c_str());
            while (m_running.load(std::memory_order_acquire))
            {
                processPendingIoTasks();
                pollIoSources();

                if (m_pendingIoCount.load(std::memory_order_relaxed) == 0 && m_running.load(std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }

        void WorkHost::processPendingIoTasks()
        {
            std::function<void()>* task = nullptr;
            while (m_ioTasks.pop(task))
            {
                if (task)
                {
                    if (*task)
                    {
                        (*task)();
                    }
                    FCT_DELETE(task);
                    m_pendingIoCount.fetch_sub(1, std::memory_order_relaxed);
                }
            }
        }

        void WorkHost::pollIoSources()
        {
            PollerNode* curr = m_ioPollersHead.load(std::memory_order_acquire);
            while (curr)
            {
                if (curr->active.load(std::memory_order_acquire) && curr->poller)
                {
                    curr->poller();
                }
                curr = curr->next;
            }
        }

        bool WorkHost::scheduleContract(bcpp::work_contract& contract) noexcept
        {
            if (!m_running.load(std::memory_order_acquire) || !contract.is_valid())
            {
                return false;
            }
            contract.schedule();
            return true;
        }

        Status WorkHost::postTask(std::function<void()> task)
        {
            if (!m_running.load(std::memory_order_acquire))
            {
                return FailedPreconditionError("WorkHost 未启动");
            }
            auto* holder = FCT_NEW(std::function<void()>, std::move(task));
            m_generalTaskQueue.push(holder);
            m_generalTaskContract.schedule();
            return OkStatus();
        }

        Status WorkHost::postIoTask(std::function<void()> ioTask)
        {
            if (!m_running.load(std::memory_order_acquire))
            {
                return FailedPreconditionError("WorkHost 未启动");
            }
            auto* holder = FCT_NEW(std::function<void()>, std::move(ioTask));
            m_pendingIoCount.fetch_add(1, std::memory_order_relaxed);
            m_ioTasks.push(holder);
            return OkStatus();
        }

        Status WorkHost::submitAsyncFileRead(
            const std::string& path,
            std::function<void(StatusOr<std::string>)> onComplete)
        {
            if (!m_running.load(std::memory_order_acquire))
            {
                return FailedPreconditionError("WorkHost 未启动");
            }

            struct ReadContext
            {
                std::string filePath;
                StatusOr<std::string> result;
                std::function<void(StatusOr<std::string>)> completionCallback;
                bcpp::work_contract completionContract;
            };

            auto ctx = std::make_shared<ReadContext>();
            ctx->filePath = path;
            ctx->completionCallback = std::move(onComplete);

            ctx->completionContract = createContract([ctx]() {
                if (ctx->completionCallback)
                {
                    ctx->completionCallback(std::move(ctx->result));
                }
                ctx->completionContract.release();
            });

            return postIoTask([ctx]() {
                ResourceLoader* loader = nullptr;
                if (auto* resMod = Resource::Resource::InstancePtr())
                {
                    loader = resMod->getResourceLoader();
                }
                else if (auto* engine = Runtime::Engine::getInstance())
                {
                    loader = engine->resource.getResourceLoader();
                }

                bool loaded = false;
                if (loader && loader->exists(ctx->filePath))
                {
                    auto loadRes = loader->loadFile(ctx->filePath);
                    if (loadRes.ok())
                    {
                        ctx->result = std::move(loadRes);
                        loaded = true;
                    }
                }
                if (!loaded)
                {
                    auto bytesOr = FCT::SAI::FileSystem::readBinaryFile(ctx->filePath);
                    if (bytesOr.ok())
                    {
                        ctx->result = std::string(
                            reinterpret_cast<const char*>(bytesOr->data()),
                            bytesOr->size());
                    }
                    else
                    {
                        ctx->result = bytesOr.status();
                    }
                }
                ctx->completionContract.schedule();
            });
        }

        void WorkHost::asyncReadAndDispatch(
            std::string_view path,
            bcpp::work_contract contract,
            std::function<void(StatusOr<std::vector<uint8_t>>)> onReadComplete)
        {
            struct BinaryReadContext
            {
                std::string filePath;
                StatusOr<std::vector<uint8_t>> result;
                std::function<void(StatusOr<std::vector<uint8_t>>)> completionCallback;
                bcpp::work_contract downstreamContract;
            };

            auto ctx = std::make_shared<BinaryReadContext>();
            ctx->filePath = std::string(path);
            ctx->completionCallback = std::move(onReadComplete);
            ctx->downstreamContract = std::move(contract);

            (void)postIoTask([this, ctx]() {
                ResourceLoader* loader = nullptr;
                if (auto* resMod = Resource::Resource::InstancePtr())
                {
                    loader = resMod->getResourceLoader();
                }
                else if (auto* engine = Runtime::Engine::getInstance())
                {
                    loader = engine->resource.getResourceLoader();
                }

                bool loaded = false;
                if (loader && loader->exists(ctx->filePath))
                {
                    auto strRes = loader->loadFile(ctx->filePath);
                    if (strRes.ok())
                    {
                        const std::string& str = *strRes;
                        ctx->result = std::vector<uint8_t>(str.begin(), str.end());
                        loaded = true;
                    }
                }
                if (!loaded)
                {
                    ctx->result = FCT::SAI::FileSystem::readBinaryFile(ctx->filePath);
                }

                if (ctx->completionCallback)
                {
                    ctx->completionCallback(std::move(ctx->result));
                }
                if (ctx->downstreamContract.is_valid() && m_running.load(std::memory_order_acquire))
                {
                    std::lock_guard<std::mutex> lock(m_retainedContractsMutex);
                    if (m_retainedContracts.size() >= 256u)
                    {
                        m_retainedContracts.pop_front();
                    }
                    m_retainedContracts.push_back(std::move(ctx->downstreamContract));
                    m_retainedContracts.back().schedule();
                }
            });
        }

        void WorkHost::registerIoPoller(std::string_view name, std::function<void()> poller)
        {
            PollerNode* curr = m_ioPollersHead.load(std::memory_order_acquire);
            while (curr)
            {
                if (curr->name == name)
                {
                    curr->poller = std::move(poller);
                    curr->active.store(true, std::memory_order_release);
                    return;
                }
                curr = curr->next;
            }

            auto* newNode = FCT_NEW(PollerNode);
            newNode->name = std::string(name);
            newNode->poller = std::move(poller);
            newNode->active.store(true, std::memory_order_relaxed);
            newNode->next = m_ioPollersHead.load(std::memory_order_relaxed);
            while (!m_ioPollersHead.compare_exchange_weak(
                newNode->next,
                newNode,
                std::memory_order_release,
                std::memory_order_relaxed))
            {
            }
        }

        void WorkHost::unregisterIoPoller(std::string_view name)
        {
            PollerNode* curr = m_ioPollersHead.load(std::memory_order_acquire);
            while (curr)
            {
                if (curr->name == name)
                {
                    curr->active.store(false, std::memory_order_release);
                    return;
                }
                curr = curr->next;
            }
        }

        bool WorkHost::executeNextContract()
        {
            if (!m_running.load(std::memory_order_acquire))
            {
                return false;
            }
            return m_contractGroup.try_execute_next_contract();
        }

        void WorkHost::enqueueMainThread(std::function<void()> callback)
        {
            if (!m_running.load(std::memory_order_acquire))
            {
                return;
            }
            auto* holder = FCT_NEW(std::function<void()>, std::move(callback));
            m_mainThreadQueue.push(holder);
        }

        size_t WorkHost::pumpMainThreadTasks(size_t maxTasks)
        {
            std::function<void()>* task = nullptr;
            size_t count = 0;
            while (count < maxTasks && m_mainThreadQueue.pop(task))
            {
                if (task)
                {
                    if (*task)
                    {
                        (*task)();
                    }
                    FCT_DELETE(task);
                }
                ++count;
            }
            return count;
        }

        void WorkHost::sync()
        {
            if (!m_initialized.load(std::memory_order_acquire) || !m_running.load(std::memory_order_acquire))
            {
                return;
            }

            if (m_mode == Mode::SingleThreadInline)
            {
                // 单线程无锁内联排空：循环执行全部就绪契约（含嵌套递归调度契约）、I/O 与主线程回调直至静默
                constexpr size_t kMaxSafetyIterations = 65536;
                for (size_t iter = 0; iter < kMaxSafetyIterations; ++iter)
                {
                    bool progressed = false;
                    while (m_contractGroup.try_execute_next_contract())
                    {
                        progressed = true;
                    }

                    std::function<void()>* ioTask = nullptr;
                    while (m_ioTasks.pop(ioTask))
                    {
                        if (ioTask)
                        {
                            if (*ioTask)
                            {
                                (*ioTask)();
                            }
                            FCT_DELETE(ioTask);
                            m_pendingIoCount.fetch_sub(1, std::memory_order_relaxed);
                            progressed = true;
                        }
                    }

                    pollIoSources();

                    if (pumpMainThreadTasks(256) > 0)
                    {
                        progressed = true;
                    }

                    if (!progressed)
                    {
                        break;
                    }
                }
                return;
            }

            pumpMainThreadTasks(256);
        }

        void WorkHost::drainInline(uint32_t maxBudgetMicroseconds)
        {
            if (!m_running.load(std::memory_order_acquire))
            {
                return;
            }

            const auto startTime = std::chrono::steady_clock::now();
            const auto maxBudget = std::chrono::microseconds(maxBudgetMicroseconds);

            do
            {
                bool workDone = false;
                if (m_contractGroup.try_execute_next_contract())
                {
                    workDone = true;
                }

                std::function<void()>* ioTask = nullptr;
                if (m_ioTasks.pop(ioTask))
                {
                    if (ioTask)
                    {
                        if (*ioTask)
                        {
                            (*ioTask)();
                        }
                        FCT_DELETE(ioTask);
                        m_pendingIoCount.fetch_sub(1, std::memory_order_relaxed);
                        workDone = true;
                    }
                }

                pollIoSources();

                if (pumpMainThreadTasks(16) > 0)
                {
                    workDone = true;
                }

                if (!workDone)
                {
                    break;
                }
            } while (std::chrono::steady_clock::now() - startTime < maxBudget);
        }

        void WorkHost::waitAll()
        {
            ZoneScopedN("WorkHost::waitAll");
            if (!m_initialized.load(std::memory_order_acquire))
            {
                return;
            }

            if (m_mode == Mode::SingleThreadInline)
            {
                sync();
                return;
            }

            pumpMainThreadTasks(1024);

            while (m_contractGroup.try_execute_next_contract())
            {
                std::this_thread::yield();
            }

            const auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() < timeout)
            {
                if (m_pendingIoCount.load(std::memory_order_relaxed) == 0)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            while (m_contractGroup.try_execute_next_contract())
            {
                std::this_thread::yield();
            }
            pumpMainThreadTasks(1024);
        }

    } // namespace Host
} // namespace MQEngine
