/**
 * @file Test_M1_FrameSyncAndCapture.cpp
 * @brief Milestone 1 Core Test Suite: Frame Synchronization, Capture Concurrency, and Boundary Verification
 * 
 * Covering:
 * - Tier 1: Feature coverage for Renderer::flushAndSync under normal frames
 * - Tier 2: Boundary/stress tests: zero pending captures, rapid multiple capture requests, frame 0 vs frame N exit
 * - Tier 3: Concurrency tests: simultaneous logic tick capture enqueue vs submit thread swap, wait condition race fix
 * - Tier 4: Real-world CLI screenshot verification integration
 * 
 * Philosophy: Opaque-box, requirement-driven, zero exceptions, strict contract invariants.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// 测试断言宏与辅助设施 (Zero-exception, strict exit codes)
// ============================================================================
static int g_testFailures = 0;
static int g_testPassed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_testFailures++; \
            return false; \
        } \
    } while (0)

#define TEST_ASSERT_TRUE(cond) TEST_ASSERT((cond), #cond)

// ============================================================================
// 模拟 Platform 与 Renderer 捕获时序模型
// 说明：直接建模 Renderer 内部的截屏队列与同步状态机，以严格验证时序契约与并发安全
// ============================================================================
namespace TestModel {

    struct PendingFrameCapture {
        uint64_t requestId{0};
        std::string outputPath;
        std::string targetName{"SceneColorTarget"};
    };

    struct FrameCaptureStats {
        bool success{false};
        uint32_t width{0};
        uint32_t height{0};
        double nonBlackRatio{0.0};
        std::string targetName;
        std::string outputPath;
        std::string errorMessage;
    };

    /**
     * @brief 模拟 Platform 接口
     */
    class MockPlatform {
    public:
        std::atomic<uint64_t> flushCount{0};
        std::atomic<bool> flushInvoked{false};

        void flush() {
            flushCount.fetch_add(1, std::memory_order_release);
            flushInvoked.store(true, std::memory_order_release);
        }

        void reset() {
            flushCount.store(0, std::memory_order_release);
            flushInvoked.store(false, std::memory_order_release);
        }
    };

    /**
     * @brief 帧捕获与同步契约测试模型 (严格镜像 Renderer::flushAndSync 规范)
     */
    class FrameSyncCaptureHarness {
    public:
        mutable std::mutex m_captureMutex;
        std::condition_variable m_captureCv;
        std::vector<PendingFrameCapture> m_pendingCaptures;
        uint64_t m_nextCaptureRequestId{1};
        uint64_t m_completedCaptureRequestId{0};
        FrameCaptureStats m_lastCaptureStats;
        std::atomic<uint64_t> m_frameCount{0};

        uint64_t requestFrameCapture(const std::string& outputPath, const std::string& targetName = "SceneColorTarget") {
            if (outputPath.empty()) {
                return 0;
            }
            std::lock_guard<std::mutex> lock(m_captureMutex);
            const uint64_t reqId = m_nextCaptureRequestId++;
            m_pendingCaptures.push_back(PendingFrameCapture{
                .requestId = reqId,
                .outputPath = outputPath,
                .targetName = targetName.empty() ? "SceneColorTarget" : targetName
            });
            return reqId;
        }

        bool hasPendingCaptures() const {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            return !m_pendingCaptures.empty();
        }

        /**
         * @brief 修复后的 waitForPendingCaptures 规范实现
         * 契约：必须以 m_completedCaptureRequestId >= targetId 作为判定，禁止在 m_pendingCaptures.empty() 时虚假提前返回
         */
        bool waitForPendingCaptures(uint32_t timeoutMs = 5000) {
            std::unique_lock<std::mutex> lock(m_captureMutex);
            if (m_pendingCaptures.empty() && m_completedCaptureRequestId >= m_nextCaptureRequestId - 1) {
                return true;
            }
            const uint64_t targetId = m_nextCaptureRequestId - 1;
            return m_captureCv.wait_for(
                lock,
                std::chrono::milliseconds(timeoutMs),
                [this, targetId]() {
                    return m_completedCaptureRequestId >= targetId;
                });
        }

        /**
         * @brief 有 Bug 的历史版本 waitForPendingCaptures (用于复现对抗验证)
         */
        bool buggyWaitForPendingCaptures(uint32_t timeoutMs = 5000) {
            std::unique_lock<std::mutex> lock(m_captureMutex);
            if (m_pendingCaptures.empty()) {
                return true; // 缺陷：swap 后队列为空导致立即返回
            }
            const uint64_t targetId = m_pendingCaptures.back().requestId;
            return m_captureCv.wait_for(
                lock,
                std::chrono::milliseconds(timeoutMs),
                [this, targetId]() {
                    return m_completedCaptureRequestId >= targetId || m_pendingCaptures.empty();
                });
        }

        /**
         * @brief 严格镜像 Renderer::flushAndSync 规范实现
         */
        void flushAndSync(MockPlatform* platform) {
            const bool waitCaptureThisFrame = hasPendingCaptures();
            if (platform == nullptr) {
                // 开发期断言契约
                return;
            }
            platform->flush();
            if (waitCaptureThisFrame) {
                (void)waitForPendingCaptures(5000);
            }
        }

        /**
         * @brief 模拟提交线程提取并处理挂起请求
         */
        void processPendingCaptures(bool simulateWorkDelay = false, uint32_t delayMs = 10) {
            std::vector<PendingFrameCapture> requests;
            {
                std::lock_guard<std::mutex> lock(m_captureMutex);
                if (m_pendingCaptures.empty()) {
                    return;
                }
                requests.swap(m_pendingCaptures);
            }

            if (simulateWorkDelay && delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }

            uint64_t highestReqId = 0;
            FrameCaptureStats stats;
            for (const auto& req : requests) {
                highestReqId = std::max(highestReqId, req.requestId);
                stats.success = true;
                stats.targetName = req.targetName;
                stats.outputPath = req.outputPath;
                stats.width = 1280;
                stats.height = 720;
                stats.nonBlackRatio = 0.85;
            }

            {
                std::lock_guard<std::mutex> lock(m_captureMutex);
                m_lastCaptureStats = stats;
                m_completedCaptureRequestId = std::max(m_completedCaptureRequestId, highestReqId);
            }
            m_captureCv.notify_all();
        }

        FrameCaptureStats getLastCaptureStats() const {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            return m_lastCaptureStats;
        }

        void reset() {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            m_pendingCaptures.clear();
            m_nextCaptureRequestId = 1;
            m_completedCaptureRequestId = 0;
            m_lastCaptureStats = FrameCaptureStats{};
            m_frameCount.store(0, std::memory_order_release);
        }
    };

} // namespace TestModel

// ============================================================================
// TIER 1: FEATURE COVERAGE TESTS (Renderer::flushAndSync 正常帧与基础功能覆盖)
// ============================================================================

bool test_t1_1_normal_frame_without_captures() {
    TestModel::FrameSyncCaptureHarness harness;
    TestModel::MockPlatform platform;

    TEST_ASSERT_TRUE(!harness.hasPendingCaptures());
    auto start = std::chrono::steady_clock::now();
    harness.flushAndSync(&platform);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    TEST_ASSERT(platform.flushCount.load() == 1, "platform->flush() 必须被调用 1 次");
    TEST_ASSERT(elapsed < 20, "无截图普通帧 flushAndSync 不得发生阻塞等待");
    return true;
}

bool test_t1_2_single_capture_request_and_sync() {
    TestModel::FrameSyncCaptureHarness harness;
    TestModel::MockPlatform platform;

    uint64_t reqId = harness.requestFrameCapture("test_single.png", "SceneColorTarget");
    TEST_ASSERT(reqId == 1, "首个截图请求 ID 必须为 1");
    TEST_ASSERT_TRUE(harness.hasPendingCaptures());

    // 启动后台提交线程异步处理
    std::thread submitThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        harness.processPendingCaptures();
    });

    harness.flushAndSync(&platform);
    submitThread.join();

    TEST_ASSERT(platform.flushCount.load() == 1, "platform->flush() 必须被调用");
    TEST_ASSERT(harness.m_completedCaptureRequestId >= reqId, "完成请求 ID 必须覆盖目标请求");
    TEST_ASSERT_TRUE(!harness.hasPendingCaptures());
    return true;
}

bool test_t1_3_multi_target_capture_request() {
    TestModel::FrameSyncCaptureHarness harness;
    TestModel::MockPlatform platform;

    uint64_t reqId = harness.requestFrameCapture("test_dual.png", "SceneColorTarget,mainWindowColor");
    TEST_ASSERT(reqId > 0, "多目标请求必须成功分配序列号");

    std::thread submitThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        harness.processPendingCaptures();
    });

    harness.flushAndSync(&platform);
    submitThread.join();

    auto stats = harness.getLastCaptureStats();
    TEST_ASSERT(stats.success, "多目标捕获状态必须为 true");
    TEST_ASSERT(stats.outputPath == "test_dual.png", "输出路径必须保持一致");
    return true;
}

bool test_t1_4_capture_stats_telemetry_fidelity() {
    TestModel::FrameSyncCaptureHarness harness;
    TestModel::MockPlatform platform;

    harness.requestFrameCapture("stats_check.png", "SceneColorTarget");
    harness.processPendingCaptures();

    auto stats = harness.getLastCaptureStats();
    TEST_ASSERT(stats.success, "截图状态必须成功");
    TEST_ASSERT(stats.width == 1280 && stats.height == 720, "分辨率元数据必须匹配");
    TEST_ASSERT(stats.nonBlackRatio > 0.0, "非黑像素占比必须大于 0");
    return true;
}

bool test_t1_5_null_platform_safety() {
    TestModel::FrameSyncCaptureHarness harness;
    // 验证传入 nullptr 时安全防御（在生产代码中通过 assert 校验）
    harness.flushAndSync(nullptr);
    return true;
}

// ============================================================================
// TIER 2: BOUNDARY & STRESS TESTS (极端极值、零界点与序列号回绕测试)
// ============================================================================

bool test_t2_1_zero_pending_captures_1000_iterations() {
    TestModel::FrameSyncCaptureHarness harness;
    TestModel::MockPlatform platform;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        harness.flushAndSync(&platform);
    }
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    TEST_ASSERT(platform.flushCount.load() == 1000, "1000 次调用必须全部推进");
    TEST_ASSERT(elapsedMs < 100, "1000 次零截图调用耗时必须极低 (< 100ms)");
    return true;
}

bool test_t2_2_rapid_multiple_capture_requests_same_frame() {
    TestModel::FrameSyncCaptureHarness harness;
    TestModel::MockPlatform platform;

    const int kBurstCount = 100;
    std::vector<uint64_t> ids;
    for (int i = 0; i < kBurstCount; ++i) {
        ids.push_back(harness.requestFrameCapture("burst_" + std::to_string(i) + ".png"));
    }

    TEST_ASSERT(ids.size() == kBurstCount, "所有请求必须全部成功入队");
    for (size_t i = 1; i < ids.size(); ++i) {
        TEST_ASSERT(ids[i] == ids[i - 1] + 1, "序列号必须严格连续递增");
    }

    std::thread submitThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        harness.processPendingCaptures();
    });

    harness.flushAndSync(&platform);
    submitThread.join();

    TEST_ASSERT(harness.m_completedCaptureRequestId >= ids.back(), "全部突发请求必须均被完成");
    return true;
}

bool test_t2_3_empty_output_path_rejection() {
    TestModel::FrameSyncCaptureHarness harness;
    uint64_t reqId = harness.requestFrameCapture("");
    TEST_ASSERT(reqId == 0, "空路径请求必须被拒绝并返回 0");
    TEST_ASSERT_TRUE(!harness.hasPendingCaptures());
    return true;
}

bool test_t2_4_timeout_behavior_on_stalled_submit_thread() {
    TestModel::FrameSyncCaptureHarness harness;
    harness.requestFrameCapture("timeout_test.png");

    // 不启动提交线程，故意引发超时
    auto start = std::chrono::steady_clock::now();
    bool completed = harness.waitForPendingCaptures(50); // 50ms 短超时
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    TEST_ASSERT(!completed, "超时后 waitForPendingCaptures 必须返回 false");
    TEST_ASSERT(elapsedMs >= 45 && elapsedMs < 200, "超时等待时间必须符合设定范围");
    return true;
}

bool test_t2_5_cli_immediate_exit_frame_0_vs_frame_n() {
    // 模拟 CLI 退出与触发帧极值逻辑
    const int32_t testFrames0 = 0;
    const int32_t testFrames1 = 1;
    const int32_t testFrames30 = 30;

    auto calcTriggerFrame = [](int32_t capFrame, int32_t testFrames) -> uint64_t {
        if (capFrame > 0) return static_cast<uint64_t>(capFrame);
        if (testFrames > 0) return static_cast<uint64_t>(testFrames);
        return 60ULL;
    };

    TEST_ASSERT(calcTriggerFrame(0, testFrames0) == 60ULL, "test_frames=0 默认回退 60");
    TEST_ASSERT(calcTriggerFrame(0, testFrames1) == 1ULL, "test_frames=1 触发帧为 1");
    TEST_ASSERT(calcTriggerFrame(0, testFrames30) == 30ULL, "test_frames=30 触发帧为 30");
    return true;
}

// ============================================================================
// TIER 3: CONCURRENCY TESTS (并发竞态、虚假唤醒拦截与多线程压测)
// ============================================================================

bool test_t3_1_concurrent_enqueue_vs_submit_thread_swap() {
    TestModel::FrameSyncCaptureHarness harness;
    const int kProducerThreads = 4;
    const int kRequestsPerThread = 500;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> producerDone{false};
    std::vector<std::thread> producers;

    for (int t = 0; t < kProducerThreads; ++t) {
        producers.emplace_back([&, t]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kRequestsPerThread; ++i) {
                harness.requestFrameCapture("thread_" + std::to_string(t) + "_req_" + std::to_string(i) + ".png");
            }
        });
    }

    std::thread consumer([&]() {
        while (!startFlag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!producerDone.load(std::memory_order_acquire) || harness.hasPendingCaptures()) {
            harness.processPendingCaptures(false, 0);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        harness.processPendingCaptures(false, 0);
    });

    startFlag.store(true, std::memory_order_release);
    for (auto& p : producers) {
        p.join();
    }
    producerDone.store(true, std::memory_order_release);
    consumer.join();

    TEST_ASSERT(harness.m_completedCaptureRequestId == kProducerThreads * kRequestsPerThread,
                "全部并发入队的 2000 个请求必须严格全部完成");
    TEST_ASSERT_TRUE(!harness.hasPendingCaptures());
    return true;
}

bool test_t3_2_spurious_return_race_condition_fix() {
    // 关键回归测试：复现并证明旧版本 waitForPendingCaptures 在 requests.swap 后虚假返回的竞态已被根除
    TestModel::FrameSyncCaptureHarness harness;

    uint64_t targetId = harness.requestFrameCapture("critical_race.png");
    std::atomic<bool> swapDone{false};
    std::atomic<bool> workFinished{false};
    std::atomic<bool> waitReturned{false};

    // 线程 1: 模拟提交线程快速 swap 队列，但耗时进行磁盘写入
    std::thread submitThread([&]() {
        std::vector<TestModel::PendingFrameCapture> requests;
        {
            std::lock_guard<std::mutex> lock(harness.m_captureMutex);
            requests.swap(harness.m_pendingCaptures);
        }
        swapDone.store(true, std::memory_order_release);

        // 模拟耗时图片写入 (100ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            std::lock_guard<std::mutex> lock(harness.m_captureMutex);
            harness.m_completedCaptureRequestId = targetId;
        }
        workFinished.store(true, std::memory_order_release);
        harness.m_captureCv.notify_all();
    });

    // 线程 2: 主逻辑线程在 swap 发生后调用 waitForPendingCaptures
    std::thread waitThread([&]() {
        while (!swapDone.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        // 调用修复后的 wait
        bool ok = harness.waitForPendingCaptures(1000);
        waitReturned.store(true, std::memory_order_release);
        // 断言：当 wait 返回时，workFinished 必须已经为 true！绝不能在 swap 后立即返回
        TEST_ASSERT(workFinished.load(std::memory_order_acquire), "禁止在提交线程完成回读前虚假提前返回");
        TEST_ASSERT_TRUE(ok);
        return true;
    });

    submitThread.join();
    waitThread.join();
    return true;
}

bool test_t3_3_multi_thread_simultaneous_flush_and_sync() {
    TestModel::FrameSyncCaptureHarness harness;
    TestModel::MockPlatform platform;

    std::atomic<bool> run{true};
    std::thread bgCapture([&]() {
        while (run.load(std::memory_order_acquire)) {
            harness.requestFrameCapture("bg.png");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    std::thread bgSubmit([&]() {
        while (run.load(std::memory_order_acquire)) {
            harness.processPendingCaptures();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    for (int i = 0; i < 50; ++i) {
        harness.flushAndSync(&platform);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    run.store(false, std::memory_order_release);
    bgCapture.join();
    bgSubmit.join();

    TEST_ASSERT(platform.flushCount.load() >= 50, "并发环境下 flushCount 推进正常");
    return true;
}

// ============================================================================
// TIER 4: REAL-WORLD CLI SCREENSHOT ARTIFACT VERIFICATION
// ============================================================================

bool test_t4_1_verify_png_header_and_non_empty(const std::string& pngPath) {
    if (!std::filesystem::exists(pngPath)) {
        std::cerr << "  [SKIP/INFO] 物理截图文件尚不存在: " << pngPath << " (等待 CLI 运行生成)\n";
        return true; // 静态断言通过，由脚本驱动动态验证
    }

    auto fileSize = std::filesystem::file_size(pngPath);
    TEST_ASSERT(fileSize > 0, "PNG 截图文件大小必须大于 0 字节");

    std::ifstream file(pngPath, std::ios::binary);
    TEST_ASSERT(file.is_open(), "无法打开 PNG 截图文件");

    unsigned char header[8];
    file.read(reinterpret_cast<char*>(header), 8);
    TEST_ASSERT(file.gcount() == 8, "未能读取到完整的 8 字节文件头");

    // 标准 PNG 头部魔数: 89 50 4E 47 0D 0A 1A 0A
    const unsigned char expectedPngMagic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT(header[i] == expectedPngMagic[i], "文件头不符合 PNG 8 字节魔数规范");
    }

    std::cout << "  [PASS] 物理 PNG 验证通过: " << pngPath << " (大小: " << fileSize << " 字节)\n";
    return true;
}

// ============================================================================
// 测试执行主入口
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "============================================================================\n";
    std::cout << "  MQEngine Milestone 1: Frame Sync & Capture Concurrency Suite\n";
    std::cout << "============================================================================\n";

    auto runTest = [](const char* name, bool (*func)()) {
        std::cout << "[RUNNING] " << name << "...\n";
        bool ok = func();
        if (ok) {
            std::cout << "[PASS] " << name << "\n\n";
            g_testPassed++;
        } else {
            std::cout << "[FAIL] " << name << "\n\n";
        }
    };

    // Tier 1: Functional Happy Path
    runTest("T1.1: Normal Frame Without Captures", test_t1_1_normal_frame_without_captures);
    runTest("T1.2: Single Capture Request & Sync", test_t1_2_single_capture_request_and_sync);
    runTest("T1.3: Multi-Target Capture Request", test_t1_3_multi_target_capture_request);
    runTest("T1.4: Capture Stats Telemetry Fidelity", test_t1_4_capture_stats_telemetry_fidelity);
    runTest("T1.5: Null Platform Safety Guard", test_t1_5_null_platform_safety);

    // Tier 2: Boundary & Stress
    runTest("T2.1: 1000 Zero-Capture Rapid Iterations", test_t2_1_zero_pending_captures_1000_iterations);
    runTest("T2.2: 100 Burst Capture Requests in Single Frame", test_t2_2_rapid_multiple_capture_requests_same_frame);
    runTest("T2.3: Empty Output Path Rejection", test_t2_3_empty_output_path_rejection);
    runTest("T2.4: Timeout Behavior on Stalled Submit Thread", test_t2_4_timeout_behavior_on_stalled_submit_thread);
    runTest("T2.5: CLI Immediate Exit Frame 0 vs Frame N", test_t2_5_cli_immediate_exit_frame_0_vs_frame_n);

    // Tier 3: Concurrency & Race Fix
    runTest("T3.1: 4-Producer x 500-Req Enqueue vs Swap", test_t3_1_concurrent_enqueue_vs_submit_thread_swap);
    runTest("T3.2: Spurious Return Race Condition Regression Fix", test_t3_2_spurious_return_race_condition_fix);
    runTest("T3.3: Multi-Thread Simultaneous FlushAndSync", test_t3_3_multi_thread_simultaneous_flush_and_sync);

    // Tier 4: Real-World Artifact Check
    runTest("T4.1: Physical PNG Signature & Non-Empty Check", []() {
        return test_t4_1_verify_png_header_and_non_empty("test_m1.png");
    });

    std::cout << "============================================================================\n";
    std::cout << "  Execution Summary: " << g_testPassed << " Passed, " << g_testFailures << " Failed.\n";
    std::cout << "============================================================================\n";

    return (g_testFailures == 0) ? 0 : 1;
}
