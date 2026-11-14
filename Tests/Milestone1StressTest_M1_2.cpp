#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <unordered_set>
#include <string>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/animation/AnimationSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>

using namespace MQEngine;

// =============================================================================
// Test 1: RendererSubsystem::requestFrameCapture Concurrency Stress
// =============================================================================
void testFrameCaptureConcurrencyStress()
{
    std::cout << "\n=== [Test 1] requestFrameCapture Concurrency Stress (Multi-threaded) ===" << std::endl;
    Domain::Renderer::RendererSubsystem renderer;

    constexpr int kThreadCount = 10;
    constexpr int kRequestsPerThread = 50;
    const int totalExpected = kThreadCount * kRequestsPerThread;

    std::vector<std::thread> threads;
    std::vector<std::vector<uint64_t>> threadReqIds(kThreadCount);
    std::atomic<bool> startFlag{false};

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&, t]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            threadReqIds[t].reserve(kRequestsPerThread);
            for (int r = 0; r < kRequestsPerThread; ++r) {
                std::string path = "Captures/test_frame_t" + std::to_string(t) + "_r" + std::to_string(r) + ".png";
                std::string target = (r % 3 == 0) ? "SceneColorTarget" : ((r % 3 == 1) ? "mainWindowColor" : "CustomTarget");
                uint64_t id = renderer.requestFrameCapture(path, target);
                threadReqIds[t].push_back(id);
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto& th : threads) {
        th.join();
    }

    // Verify all returned IDs are strictly positive and completely unique
    std::unordered_set<uint64_t> uniqueIds;
    for (int t = 0; t < kThreadCount; ++t) {
        for (uint64_t id : threadReqIds[t]) {
            assert(id > 0 && "Capture request ID must be > 0");
            auto [it, inserted] = uniqueIds.insert(id);
            assert(inserted && "Capture request IDs must be strictly unique without race condition collisions!");
        }
    }
    assert(static_cast<int>(uniqueIds.size()) == totalExpected);
    assert(renderer.hasPendingCaptures() && "Renderer must report hasPendingCaptures() == true");

    std::cout << "[PASS] Test 1: 500 concurrent requestFrameCapture calls succeeded with 100% unique IDs, zero collisions, zero crash!" << std::endl;
}

// =============================================================================
// Test 2: RendererSubsystem Timeout and Synchronization
// =============================================================================
void testFrameCaptureTimeoutAndSync()
{
    std::cout << "\n=== [Test 2] waitForPendingCaptures Timeout & Synchronization ===" << std::endl;
    Domain::Renderer::RendererSubsystem renderer;

    // 2.1 Empty queue: must return true immediately
    auto t0 = std::chrono::steady_clock::now();
    bool emptyRes = renderer.waitForPendingCaptures(500);
    auto t1 = std::chrono::steady_clock::now();
    auto emptyDuration = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    assert(emptyRes == true && "Empty queue must return true immediately!");
    assert(emptyDuration < 100 && "Empty queue wait must not block!");

    // 2.2 Enqueue one request without processing: wait with timeout 60ms
    uint64_t reqId = renderer.requestFrameCapture("Captures/timeout_test.png");
    assert(reqId > 0);

    auto t2 = std::chrono::steady_clock::now();
    bool timeoutRes = renderer.waitForPendingCaptures(60);
    auto t3 = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    assert(timeoutRes == false && "waitForPendingCaptures must return false on timeout!");
    assert(elapsedMs >= 40 && elapsedMs <= 500 && "waitForPendingCaptures must honor the specified timeout duration!");

    // 2.3 Simulated processing in background thread
    std::thread consumerThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        // processPendingCaptures with nullptr safely fails individual captures but advances m_completedCaptureId
        renderer.processPendingCaptures(nullptr, nullptr);
    });

    auto t4 = std::chrono::steady_clock::now();
    bool syncRes = renderer.waitForPendingCaptures(2000);
    auto t5 = std::chrono::steady_clock::now();
    auto syncElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t5 - t4).count();

    consumerThread.join();
    assert(syncRes == true && "waitForPendingCaptures must unblock and return true once processPendingCaptures runs!");
    assert(syncElapsedMs < 1000 && "waitForPendingCaptures must not wait full timeout when awakened!");
    assert(!renderer.hasPendingCaptures() && "hasPendingCaptures must be false after completion");

    std::cout << "[PASS] Test 2: Timeout (false), immediate unblock on empty, and thread sync on completion verified!" << std::endl;
}

// =============================================================================
// Test 3: ResourceLoader File Conformance
// =============================================================================
void testResourceLoaderConformance()
{
    std::cout << "\n=== [Test 3] ResourceLoader Disk Output Conformance ===" << std::endl;
    auto* res = Resource::Resource::InstancePtr();
    assert(res != nullptr && "Resource module must be available");
    auto* loader = res->getResourceLoader();
    assert(loader != nullptr && "ResourceLoader must be available");

    const std::string testArtifactPath = "Output/m1_capture_conformance_test.bin";
    const std::string dummyPayload = "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00";

    // Write file through ResourceLoader
    Status saveStatus = loader->saveFile(testArtifactPath, dummyPayload);
    assert(saveStatus.ok() && "ResourceLoader::saveFile must succeed");

    // Verify file actually arrived on physical disk
    assert(loader->exists(testArtifactPath) && "Saved file must physically exist on disk");
    assert(std::filesystem::exists(testArtifactPath) && "Physical filesystem check must confirm existence");

    // Read back through ResourceLoader
    auto readResult = loader->loadFile(testArtifactPath);
    assert(readResult.ok() && "ResourceLoader::loadFile must succeed");
    assert(readResult.value() == dummyPayload && "Payload content must match exactly");

    // Clean up test artifact
    (void)loader->unlink(testArtifactPath);
    assert(!loader->exists(testArtifactPath));

    std::cout << "[PASS] Test 3: ResourceLoader saveFile/loadFile/exists/unlink confirmed on physical disk!" << std::endl;
}

// =============================================================================
// Test 4: AnimationSubsystem Nonexistent Asset Paths Graceful Fallback
// =============================================================================
void testAnimationNonexistentAssetFallback()
{
    std::cout << "\n=== [Test 4] AnimationSubsystem Nonexistent Asset Fallback ===" << std::endl;
    Domain::Animation::AnimationSubsystem anim;
    Status s = anim.init();
    assert(s.ok());

    // 4.1 Directly request nonexistent skeleton asset
    auto skel = anim.getOrLoadSkeleton("Assets/Models/NonExistent_Ghost_Mesh_99999.fbx");
    assert(skel == nullptr && "Nonexistent skeleton asset must safely return nullptr without crash");

    // 4.2 Empty asset path
    auto emptySkel = anim.getOrLoadSkeleton("");
    assert(emptySkel == nullptr && "Empty asset path must return nullptr");

    // 4.3 ECS Entity with SkeletalAnimatorComponent pointing to nonexistent asset
    FCT::ECS::registry registry;
    auto ent = registry.create();
    auto& animator = registry.emplace<Domain::Animation::SkeletalAnimatorComponent>(ent);
    animator.skeletonAssetPath = "Assets/Models/NonExistent_Ghost_Mesh_99999.fbx";
    animator.clipName = "Idle";
    animator.isPlaying = true;

    // Run 60 frames of update
    for (int frame = 0; frame < 60; ++frame) {
        anim.update(0.01667f, registry);
    }

    // Verify BonePaletteComponent
    auto* palette = registry.try_get<Domain::Animation::BonePaletteComponent>(ent);
    assert(palette != nullptr && "BonePaletteComponent must be created");
    assert(palette->boneMatrices.size() == Domain::Animation::kMaxBones && "Palette must have kMaxBones size");
    assert(palette->hasBones == false && "hasBones must remain false for nonexistent skeleton");

    // Verify all 128 bone matrices are identity matrices
    for (size_t b = 0; b < Domain::Animation::kMaxBones; ++b) {
        const auto& mat = palette->boneMatrices[b];
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float expected = (row == col) ? 1.0f : 0.0f;
                assert(std::abs(mat.m[row * 4 + col] - expected) < 1e-5f && "Fallback bone matrix must be identity!");
            }
        }
    }

    anim.term();
    std::cout << "[PASS] Test 4: Nonexistent skeleton paths safely fallback to identity palette without crash!" << std::endl;
}

// =============================================================================
// Test 5: AnimationSubsystem Extreme Blend Weights
// =============================================================================
void testAnimationExtremeBlendWeights()
{
    std::cout << "\n=== [Test 5] AnimationSubsystem Extreme Blend Weights ===" << std::endl;
    Domain::Animation::AnimationSubsystem anim;
    Status s = anim.init();
    assert(s.ok());

    FCT::ECS::registry registry;
    auto ent = registry.create();
    auto& animator = registry.emplace<Domain::Animation::SkeletalAnimatorComponent>(ent);
    animator.clipName = "Walk";
    animator.targetClipName = "Run";
    animator.isPlaying = true;
    animator.isLoop = true;
    animator.blendDuration = 0.5f;

    // Test extreme weights: 0.0, 1.0, <0 (-1.0, -100.0), >1 (2.0, 100.0)
    std::vector<float> testWeights = {0.0f, 1.0f, -0.5f, -100.0f, 1.5f, 100.0f};

    for (float weight : testWeights) {
        animator.blendWeight = weight;
        animator.blendElapsed = weight * animator.blendDuration; // might be <0 or >duration
        // Advance clock
        anim.update(0.01667f, registry);

        auto* palette = registry.try_get<Domain::Animation::BonePaletteComponent>(ent);
        assert(palette != nullptr);
        assert(palette->boneMatrices.size() == Domain::Animation::kMaxBones);
        // Verify no memory violation or NaN corruption in bone matrices
        for (size_t b = 0; b < Domain::Animation::kMaxBones; ++b) {
            for (int i = 0; i < 16; ++i) {
                assert(!std::isnan(palette->boneMatrices[b].m[i]) && "Bone matrix element must not be NaN!");
                assert(!std::isinf(palette->boneMatrices[b].m[i]) && "Bone matrix element must not be Inf!");
            }
        }
    }

    anim.term();
    std::cout << "[PASS] Test 5: Extreme blend weights (0.0, 1.0, <0, >1) handled cleanly without NaN or crash!" << std::endl;
}

// =============================================================================
// Test 6: Bone Palette Size Bounds (Does not overflow kMaxBones = 128)
// =============================================================================
void testBonePaletteSizeBounds()
{
    std::cout << "\n=== [Test 6] BonePalette bounds test (kMaxBones = 128) ===" << std::endl;
    Domain::Animation::AnimationSubsystem anim;
    Status s = anim.init();
    assert(s.ok());

    FCT::ECS::registry registry;
    auto ent = registry.create();
    auto& animator = registry.emplace<Domain::Animation::SkeletalAnimatorComponent>(ent);

    // Create a synthetic ModelSkeletonData with 256 bones (> 128)
    auto largeSkeleton = std::make_shared<FCT::ModelSkeletonData>();
    largeSkeleton->bones.resize(256);
    for (size_t i = 0; i < 256; ++i) {
        largeSkeleton->bones[i].name = "Bone_" + std::to_string(i);
        largeSkeleton->bones[i].offsetMatrix.identity();
    }
    largeSkeleton->runtimeCachePrepared = true;
    animator.customSkeleton = largeSkeleton;
    animator.clipName = "Pose";
    animator.isPlaying = true;

    // Run update
    anim.update(0.01667f, registry);

    auto* palette = registry.try_get<Domain::Animation::BonePaletteComponent>(ent);
    assert(palette != nullptr);
    assert(palette->boneMatrices.size() == Domain::Animation::kMaxBones && "Palette size must be strictly kMaxBones (128)");
    assert(palette->boneMatrices.size() == 128);

    // Verify FCT::ModelSkeletonData::evaluateBlendedPose directly with maxBones = 128
    FCT::SkeletalBlendParams params;
    std::vector<FCT::Mat4> testPalette(256);
    largeSkeleton->evaluateBlendedPose(params, testPalette, nullptr, Domain::Animation::kMaxBones);
    assert(testPalette.size() == 128 && "evaluateBlendedPose must resize/clamp outBoneMatrices to maxBones (128)");

    anim.term();
    std::cout << "[PASS] Test 6: Palette size strictly bounded to kMaxBones (128), zero buffer overrun with 256 bones!" << std::endl;
}

int main()
{
    std::cout << "========================================================" << std::endl;
    std::cout << "MQEngine Milestone 1 Adversarial Challenger M1-2 Suite  " << std::endl;
    std::cout << "========================================================" << std::endl;

    // Instantiate Resource module to establish CoreEngineModule<Resource>::InstancePtr()
    Resource::Resource resourceModule;

    testFrameCaptureConcurrencyStress();
    testFrameCaptureTimeoutAndSync();
    testResourceLoaderConformance();
    testAnimationNonexistentAssetFallback();
    testAnimationExtremeBlendWeights();
    testBonePaletteSizeBounds();

    std::cout << "\n========================================================" << std::endl;
    std::cout << ">>> ALL CHALLENGER M1-2 ADVERSARIAL TESTS PASSED! <<<   " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
