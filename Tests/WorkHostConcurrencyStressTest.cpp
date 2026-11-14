#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <fstream>
#include <set>
#include <mutex>
#include <cassert>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/manager/WorkHost.h>
#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>

using namespace MQEngine;
using namespace MQEngine::Host;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while(0)

// -------------------------------------------------------------
// Test 1: Basic Dual-Track Concurrency & Thread Separation
// -------------------------------------------------------------
bool Test1_DualTrackConcurrency()
{
    std::cout << "\n=== Running Test 1: Dual-Track Concurrency & Thread Separation ===" << std::endl;
    WorkHost workHost;
    auto status = workHost.init(4, 2);
    TEST_ASSERT(status.ok(), "WorkHost init failed");
    TEST_ASSERT(workHost.isRunning(), "WorkHost should be running");
    TEST_ASSERT(workHost.getWorkerCount() == 4, "Worker count should be 4");
    TEST_ASSERT(workHost.getIoThreadCount() == 2, "IO thread count should be 2");

    std::atomic<uint32_t> cpuTaskCount{0};
    std::mutex cpuThreadMutex;
    std::set<std::thread::id> cpuThreadIds;

    // Concurrently post 2,000 CPU tasks across 4 feeder threads
    constexpr int NUM_CPU_TASKS = 2000;
    std::vector<std::thread> cpuFeeders;
    for (int t = 0; t < 4; ++t) {
        cpuFeeders.emplace_back([&workHost, &cpuTaskCount, &cpuThreadMutex, &cpuThreadIds]() {
            for (int i = 0; i < NUM_CPU_TASKS / 4; ++i) {
                workHost.postCpuTask([&cpuTaskCount, &cpuThreadMutex, &cpuThreadIds]() {
                    cpuTaskCount.fetch_add(1, std::memory_order_relaxed);
                    std::lock_guard<std::mutex> lock(cpuThreadMutex);
                    cpuThreadIds.insert(std::this_thread::get_id());
                });
            }
        });
    }
    for (auto& f : cpuFeeders) f.join();

    // Concurrently post 500 I/O tasks across 2 feeder threads
    constexpr int NUM_IO_TASKS = 500;
    std::atomic<uint32_t> ioTaskCount{0};
    std::mutex ioThreadMutex;
    std::set<std::thread::id> ioThreadIds;

    std::vector<std::thread> ioFeeders;
    for (int t = 0; t < 2; ++t) {
        ioFeeders.emplace_back([&workHost, &ioTaskCount, &ioThreadMutex, &ioThreadIds]() {
            for (int i = 0; i < NUM_IO_TASKS / 2; ++i) {
                auto st = workHost.postIoTask([&ioTaskCount, &ioThreadMutex, &ioThreadIds]() {
                    ioTaskCount.fetch_add(1, std::memory_order_relaxed);
                    std::lock_guard<std::mutex> lock(ioThreadMutex);
                    ioThreadIds.insert(std::this_thread::get_id());
                });
                assert(st.ok());
            }
        });
    }
    for (auto& f : ioFeeders) f.join();

    // Wait for completion with timeout
    auto start = std::chrono::steady_clock::now();
    while ((cpuTaskCount.load(std::memory_order_relaxed) < NUM_CPU_TASKS ||
            ioTaskCount.load(std::memory_order_relaxed) < NUM_IO_TASKS) &&
           std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "  Executed CPU tasks: " << cpuTaskCount.load() << " / " << NUM_CPU_TASKS << std::endl;
    std::cout << "  Distinct CPU worker threads observed: " << cpuThreadIds.size() << std::endl;
    std::cout << "  Executed IO tasks: " << ioTaskCount.load() << " / " << NUM_IO_TASKS << std::endl;
    std::cout << "  Distinct IO threads observed: " << ioThreadIds.size() << std::endl;

    TEST_ASSERT(cpuTaskCount.load() == NUM_CPU_TASKS, "Not all CPU tasks executed");
    TEST_ASSERT(ioTaskCount.load() == NUM_IO_TASKS, "Not all IO tasks executed");

    // Ensure CPU worker thread IDs and IO thread IDs do not overlap
    for (const auto& id : ioThreadIds) {
        TEST_ASSERT(cpuThreadIds.find(id) == cpuThreadIds.end(), "IO thread ID overlaps with CPU worker thread ID!");
        TEST_ASSERT(id != std::this_thread::get_id(), "IO task ran on main thread!");
    }

    workHost.term();
    TEST_ASSERT(!workHost.isRunning(), "WorkHost should not be running after term");
    std::cout << "[PASS] Test 1: Dual-Track Concurrency verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test 2: Work Contract Capacity & Rapid Scheduling Stress
// -------------------------------------------------------------
bool Test2_ContractCapacityAndRapidSchedule()
{
    std::cout << "\n=== Running Test 2: Work Contract Capacity & Rapid Scheduling ===" << std::endl;
    WorkHost workHost;
    workHost.init(4, 1);

    // 2.1 Rapid scheduling of active contracts
    constexpr int NUM_CONTRACTS = 32;
    constexpr int RUNS_PER_CONTRACT = 1000;
    std::vector<bcpp::work_contract> contracts;
    std::vector<std::atomic<uint32_t>> contractCounters(NUM_CONTRACTS);

    for (int i = 0; i < NUM_CONTRACTS; ++i) {
        contractCounters[i].store(0);
        contracts.push_back(workHost.createContract([&, i]() {
            contractCounters[i].fetch_add(1, std::memory_order_relaxed);
        }));
        TEST_ASSERT(contracts.back().is_valid(), "Created contract must be valid");
    }

    // Concurrently trigger schedule across 4 scheduler threads
    std::vector<std::thread> schedulers;
    for (int s = 0; s < 4; ++s) {
        schedulers.emplace_back([&contracts, s]() {
            for (int r = 0; r < RUNS_PER_CONTRACT; ++r) {
                for (size_t c = s; c < contracts.size(); c += 4) {
                    contracts[c].schedule();
                }
                std::this_thread::yield();
            }
        });
    }
    for (auto& s : schedulers) s.join();

    // Give time for contracts to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    uint32_t totalRuns = 0;
    for (int i = 0; i < NUM_CONTRACTS; ++i) {
        totalRuns += contractCounters[i].load();
    }
    std::cout << "  Total contract runs completed under rapid scheduling: " << totalRuns << std::endl;
    TEST_ASSERT(totalRuns > 0, "Contracts should have executed");

    // Release contracts
    for (auto& c : contracts) {
        c.release();
    }

    // 2.2 Adversarial Contract Capacity Stress
    // Capacity of blocking_work_contract_group<512> is 512.
    // Note: m_generalTaskContract already occupies 1 slot!
    std::cout << "  Testing contract group capacity limit (512 slots)..." << std::endl;
    std::vector<bcpp::work_contract> bulkContracts;
    int successfullyCreated = 0;
    int failedCreations = 0;

    for (int i = 0; i < 600; ++i) {
        auto c = workHost.createContract([]() {});
        if (c.is_valid()) {
            successfullyCreated++;
            bulkContracts.push_back(std::move(c));
        } else {
            failedCreations++;
        }
    }

    std::cout << "  Successfully created: " << successfullyCreated << ", Rejected (capacity exhausted): " << failedCreations << std::endl;
    TEST_ASSERT(failedCreations > 0, "Capacity exhaustion must reject contracts beyond 512!");

    // Clean up bulk contracts
    for (auto& c : bulkContracts) {
        c.release();
    }

    workHost.term();
    std::cout << "[PASS] Test 2: Contract Capacity & Rapid Scheduling verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test 3: File Read-to-Contract Wake-up Dispatch
// -------------------------------------------------------------
bool Test3_FileReadToContractWakeup()
{
    std::cout << "\n=== Running Test 3: File Read-to-Contract Wake-up Dispatch ===" << std::endl;
    WorkHost workHost;
    workHost.init(2, 2);

    // 3.1 Missing file test
    std::cout << "  Testing asyncReadAndDispatch with non-existent file..." << std::endl;
    std::atomic<bool> callbackCalled{false};
    std::atomic<bool> contractExecuted{false};
    Status callbackStatus = OkStatus();

    auto downstreamContract = workHost.createContract([&contractExecuted]() {
        contractExecuted.store(true);
    });

    workHost.asyncReadAndDispatch(
        "non_existent_file_definitely_missing_12345.bin",
        std::move(downstreamContract),
        [&callbackCalled, &callbackStatus](StatusOr<std::vector<uint8_t>> res) {
            callbackStatus = res.status();
            callbackCalled.store(true);
        });

    auto start = std::chrono::steady_clock::now();
    while (!callbackCalled.load() &&
           std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    TEST_ASSERT(callbackCalled.load(), "Missing file callback must be called");
    TEST_ASSERT(!callbackStatus.ok(), "Missing file must report non-ok Status");
    std::cout << "  Missing file reported expected error: " << callbackStatus.ToString() << std::endl;

    // Give time for scheduled downstream contract to execute in CPU worker pool
    auto contractWaitStart = std::chrono::steady_clock::now();
    while (!contractExecuted.load() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - contractWaitStart).count() < 500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "  Downstream contract executed on failure: " << (contractExecuted.load() ? "YES" : "NO") << std::endl;

    // 3.2 Empty file test
    std::string tempEmptyFile = "test_empty_tmp.bin";
    {
        std::ofstream ofs(tempEmptyFile, std::ios::binary);
    } // create 0-byte file

    callbackCalled.store(false);
    contractExecuted.store(false);
    size_t receivedBytes = 999;

    auto downstreamContract2 = workHost.createContract([&contractExecuted]() {
        contractExecuted.store(true);
    });

    workHost.asyncReadAndDispatch(
        tempEmptyFile,
        std::move(downstreamContract2),
        [&callbackCalled, &receivedBytes](StatusOr<std::vector<uint8_t>> res) {
            if (res.ok()) {
                receivedBytes = res->size();
            }
            callbackCalled.store(true);
        });

    start = std::chrono::steady_clock::now();
    while (!callbackCalled.load() &&
           std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    TEST_ASSERT(callbackCalled.load(), "Empty file callback must be called");
    std::remove(tempEmptyFile.c_str());

    auto contractWaitStart2 = std::chrono::steady_clock::now();
    while (!contractExecuted.load() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - contractWaitStart2).count() < 500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "  Empty file downstream contract executed: " << (contractExecuted.load() ? "YES" : "NO") << std::endl;

    workHost.term();
    std::cout << "[PASS] Test 3: File Read-to-Contract Dispatch verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test 4: Poller Registry Stability & Concurrency
// -------------------------------------------------------------
bool Test4_PollerRegistryStability()
{
    std::cout << "\n=== Running Test 4: Poller Registry Stability & Concurrency ===" << std::endl;
    WorkHost workHost;
    workHost.init(2, 2);

    std::atomic<bool> stopStress{false};
    std::atomic<uint32_t> pollerExecutions{0};

    // Register initial poller
    workHost.registerIoPoller("test_poller_0", [&pollerExecutions]() {
        pollerExecutions.fetch_add(1, std::memory_order_relaxed);
    });

    // Concurrently register and unregister pollers while I/O thread is looping
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&workHost, &stopStress, t]() {
            int counter = 0;
            while (!stopStress.load(std::memory_order_relaxed)) {
                std::string name = "dyn_poller_" + std::to_string(t) + "_" + std::to_string(counter % 10);
                workHost.registerIoPoller(name, []() {});
                std::this_thread::yield();
                workHost.unregisterIoPoller(name);
                counter++;
            }
        });
    }

    // Let it stress for 200 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stopStress.store(true, std::memory_order_relaxed);
    for (auto& w : workers) w.join();

    workHost.unregisterIoPoller("test_poller_0");
    std::cout << "  Total poller executions observed: " << pollerExecutions.load() << std::endl;
    TEST_ASSERT(pollerExecutions.load() > 0, "Poller should have executed multiple times");

    workHost.term();
    std::cout << "[PASS] Test 4: Poller Registry Stability verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test 5: Clean Shutdown & Post-Shutdown Edge Conditions
// -------------------------------------------------------------
bool Test5_CleanShutdownAndPostShutdownEdges()
{
    std::cout << "\n=== Running Test 5: Clean Shutdown & Post-Shutdown Edge Conditions ===" << std::endl;
    WorkHost workHost;
    workHost.init(4, 2);

    // 5.1 Shutdown under heavy concurrent activity
    std::cout << "  5.1 Stressing concurrent task dispatch during WorkHost::term()..." << std::endl;
    std::atomic<bool> stormActive{true};
    std::vector<std::thread> stormThreads;

    for (int i = 0; i < 4; ++i) {
        stormThreads.emplace_back([&workHost, &stormActive]() {
            while (stormActive.load(std::memory_order_relaxed)) {
                workHost.postCpuTask([]() {});
                workHost.postIoTask([]() {});
            }
        });
    }

    // Let storm run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Call term while storm is ongoing
    auto termStart = std::chrono::steady_clock::now();
    workHost.term();
    auto termElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - termStart).count();
    std::cout << "  WorkHost::term() completed in: " << termElapsedMs << " ms" << std::endl;

    stormActive.store(false, std::memory_order_relaxed);
    for (auto& t : stormThreads) t.join();

    TEST_ASSERT(!workHost.isRunning(), "WorkHost must be stopped");
    TEST_ASSERT(termElapsedMs < 3000, "WorkHost::term() must complete in under 3 seconds (no blocked thread hangs)");

    // 5.2 Post-term calls must NOT crash
    std::cout << "  5.2 Testing post-shutdown calls..." << std::endl;
    auto ioStatus = workHost.postIoTask([]() {});
    TEST_ASSERT(!ioStatus.ok(), "postIoTask after term() must return error");

    auto cpuStatus = workHost.postTask([]() {});
    TEST_ASSERT(!cpuStatus.ok(), "postTask after term() must return error");

    workHost.postCpuTask([]() {}); // void version, must not crash

    auto readStatus = workHost.submitAsyncFileRead("foo", [](auto) {});
    TEST_ASSERT(!readStatus.ok(), "submitAsyncFileRead after term() must return error");

    // Double term() call must be safe and idempotent
    std::cout << "  5.3 Testing double term()..." << std::endl;
    workHost.term();
    workHost.term();

    // 5.4 Re-initialization challenge
    std::cout << "  5.4 Testing WorkHost re-initialization after term()..." << std::endl;
    auto reinitStatus = workHost.init(2, 1);
    TEST_ASSERT(reinitStatus.ok(), "Re-init returned error");
    std::cout << "  Re-init status: " << reinitStatus.ToString() << std::endl;

    std::atomic<bool> reinitTaskRan{false};
    workHost.postCpuTask([&reinitTaskRan]() {
        reinitTaskRan.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "  Task executed after re-initialization: " << (reinitTaskRan.load() ? "YES" : "NO") << std::endl;

    workHost.term();
    std::cout << "[PASS] Test 5: Clean Shutdown and Post-Shutdown verified." << std::endl;
    return true;
}

int main()
{
    std::cout << "============================================================" << std::endl;
    std::cout << "   MQEngine WorkHost Concurrency & Threading Stress Suite   " << std::endl;
    std::cout << "============================================================" << std::endl;

    bool allPassed = true;
    allPassed &= Test1_DualTrackConcurrency();
    allPassed &= Test2_ContractCapacityAndRapidSchedule();
    allPassed &= Test3_FileReadToContractWakeup();
    allPassed &= Test4_PollerRegistryStability();
    allPassed &= Test5_CleanShutdownAndPostShutdownEdges();

    std::cout << "\n============================================================" << std::endl;
    if (allPassed) {
        std::cout << "   >>> ALL EMPIRICAL WORKHOST TESTS PASSED SUCCESSFULLY <<<   " << std::endl;
    } else {
        std::cout << "   >>> SOME WORKHOST TESTS FAILED <<<   " << std::endl;
    }
    std::cout << "============================================================" << std::endl;

    return allPassed ? 0 : 1;
}
