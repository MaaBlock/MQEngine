#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>
#include <random>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/scene/system/CullingSystem.h>
#include <MQEngine/domain/debug/DebugSubsystem.h>
#include <MQEngine/host/manager/WorkHost.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/thirdparty/thirdparty.h>

using namespace MQEngine;
using namespace MQEngine::Domain::Scene;
using namespace MQEngine::Domain::Debug;
using namespace MQEngine::Host;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while(0)

// Helper to check if float is finite and not NaN
inline bool isFiniteVec3(const FCT::Vec3<>& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// -------------------------------------------------------------
// Test Suite 1: CullingSystem Gribb-Hartmann Plane Extraction
// -------------------------------------------------------------
bool test_GribbHartmann_Vulkan_NDC() {
    std::cout << "\n=== [Test 1] Gribb-Hartmann Vulkan NDC [0, 1] Plane Extraction ===" << std::endl;

    // Standard Forward-Z Perspective Projection: FOV=60 deg, Aspect=1.0, Near=1.0, Far=100.0
    // Camera at origin looking +Z: Eye=(0,0,0), Center=(0,0,1), Up=(0,1,0)
    const FCT::Mat4 view = FCT::Mat4::LookAt(FCT::Vec3<>(0.0f, 0.0f, 0.0f), FCT::Vec3<>(0.0f, 0.0f, 1.0f), FCT::Vec3<>(0.0f, 1.0f, 0.0f));
    const FCT::Mat4 proj = FCT::Mat4::Perspective(60.0f, 1.0f, 1.0f, 100.0f);
    const FCT::Mat4 vp = proj * view;

    Frustum frustum;
    frustum.extractFromViewProj(vp, /*reverseZ=*/false);

    // 1.1 Verify all 6 plane normals are normalized (|n| ~ 1.0)
    for (uint32_t i = 0; i < Frustum::Count; ++i) {
        const auto& plane = frustum.getPlane(static_cast<Frustum::PlaneIndex>(i));
        const float len = plane.normal.length();
        TEST_CHECK(std::abs(len - 1.0f) < 1e-4f, "Plane normal must be normalized to unit length");
        TEST_CHECK(std::isfinite(plane.distance), "Plane distance must be finite");
    }

    // 1.2 Point tests for Forward-Z:
    // Center point inside frustum (0, 0, 50)
    const FCT::Vec3<> centerInside(0.0f, 0.0f, 50.0f);
    TEST_CHECK(frustum.testPoint(centerInside), "Center point at z=50 must be inside frustum");
    for (uint32_t i = 0; i < Frustum::Count; ++i) {
        const float d = frustum.getPlane(static_cast<Frustum::PlaneIndex>(i)).signedDistance(centerInside);
        TEST_CHECK(d > 0.0f, "Center point must have positive signed distance for all planes");
    }

    // Near plane boundary: Near plane is at z=1.0. Point at z=1.0 should have signedDistance ~ 0
    const FCT::Vec3<> onNearPlane(0.0f, 0.0f, 1.0f);
    const float nearDist = frustum.getPlane(Frustum::Near).signedDistance(onNearPlane);
    TEST_CHECK(std::abs(nearDist) < 1e-3f, "Point on near plane must have signedDistance ~ 0");

    // Point in front of near plane (z=0.5 < 1.0) must fail Near plane test
    const FCT::Vec3<> beforeNear(0.0f, 0.0f, 0.5f);
    TEST_CHECK(!frustum.testPoint(beforeNear), "Point at z=0.5 must be culled by near plane");
    TEST_CHECK(frustum.getPlane(Frustum::Near).signedDistance(beforeNear) < 0.0f, "Signed distance before near plane must be negative");

    // Point behind camera (z=-5.0) must be culled
    const FCT::Vec3<> behindCamera(0.0f, 0.0f, -5.0f);
    TEST_CHECK(!frustum.testPoint(behindCamera), "Point behind camera must be culled");

    // Far plane boundary: Far plane is at z=100.0. Point at z=100.0 should have signedDistance ~ 0
    const FCT::Vec3<> onFarPlane(0.0f, 0.0f, 100.0f);
    const float farDist = frustum.getPlane(Frustum::Far).signedDistance(onFarPlane);
    TEST_CHECK(std::abs(farDist) < 1e-2f, "Point on far plane must have signedDistance ~ 0");

    // Point beyond far plane (z=150.0) must fail Far plane test
    const FCT::Vec3<> beyondFar(0.0f, 0.0f, 150.0f);
    TEST_CHECK(!frustum.testPoint(beyondFar), "Point beyond far plane must be culled");
    TEST_CHECK(frustum.getPlane(Frustum::Far).signedDistance(beyondFar) < 0.0f, "Signed distance beyond far plane must be negative");

    // Left/Right planes: at z=50, half width is 50 * tan(30 deg) = 50 * 0.57735 ~ 28.867
    const float halfWidth50 = 50.0f * std::tan(30.0f * 3.14159265f / 180.0f);
    const FCT::Vec3<> farLeft(-halfWidth50 - 5.0f, 0.0f, 50.0f);
    const FCT::Vec3<> farRight(halfWidth50 + 5.0f, 0.0f, 50.0f);
    TEST_CHECK(!frustum.testPoint(farLeft), "Point beyond left plane must be culled");
    TEST_CHECK(!frustum.testPoint(farRight), "Point beyond right plane must be culled");

    // 1.3 Reverse-Z Projection test:
    const FCT::Mat4 projRev = FCT::Mat4::PerspectiveReverseZ(60.0f, 1.0f, 1.0f, 100.0f);
    const FCT::Mat4 vpRev = projRev * view;
    Frustum frustumRev;
    frustumRev.extractFromViewProj(vpRev, /*reverseZ=*/true);

    TEST_CHECK(frustumRev.testPoint(centerInside), "Center point must be inside Reverse-Z frustum");
    TEST_CHECK(!frustumRev.testPoint(beforeNear), "Point before near plane must be culled in Reverse-Z");
    TEST_CHECK(!frustumRev.testPoint(beyondFar), "Point beyond far plane must be culled in Reverse-Z");

    // 1.4 Degenerate all-zero VP matrix
    FCT::Mat4 zeroMat{};
    for (int i = 0; i < 16; ++i) zeroMat.m[i] = 0.0f;
    Frustum zeroFrustum;
    zeroFrustum.extractFromViewProj(zeroMat, false);
    for (uint32_t i = 0; i < Frustum::Count; ++i) {
        const auto& p = zeroFrustum.getPlane(static_cast<Frustum::PlaneIndex>(i));
        TEST_CHECK(isFiniteVec3(p.normal), "Degenerate plane normal must be finite");
        TEST_CHECK(std::isfinite(p.distance), "Degenerate plane distance must be finite");
    }

    std::cout << "[PASS] Test 1: Gribb-Hartmann plane extraction verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test Suite 2: AABB and Sphere Intersection Corner Cases
// -------------------------------------------------------------
bool test_AABB_Sphere_CornerCases() {
    std::cout << "\n=== [Test 2] AABB and Sphere Intersection Corner Cases ===" << std::endl;

    const FCT::Mat4 view = FCT::Mat4::LookAt(FCT::Vec3<>(0.0f, 0.0f, 0.0f), FCT::Vec3<>(0.0f, 0.0f, 1.0f), FCT::Vec3<>(0.0f, 1.0f, 0.0f));
    const FCT::Mat4 proj = FCT::Mat4::Perspective(60.0f, 1.0f, 1.0f, 100.0f);
    const FCT::Mat4 vp = proj * view;

    Frustum frustum;
    frustum.extractFromViewProj(vp, false);

    // 2.1 Fully inside box and sphere
    TEST_CHECK(frustum.testAABB(FCT::Vec3<>(-2.0f, -2.0f, 20.0f), FCT::Vec3<>(2.0f, 2.0f, 24.0f)), "AABB fully inside must pass");
    TEST_CHECK(frustum.testSphere(FCT::Vec3<>(0.0f, 0.0f, 22.0f), 2.0f), "Sphere fully inside must pass");

    // 2.2 Box spanning near plane: crosses z=1.0 (e.g. z from 0.0 to 5.0)
    // Part is outside (behind near plane), but part is inside. MUST NOT be culled!
    TEST_CHECK(frustum.testAABB(FCT::Vec3<>(-1.0f, -1.0f, 0.0f), FCT::Vec3<>(1.0f, 1.0f, 5.0f)), "AABB spanning near plane must pass");
    TEST_CHECK(frustum.testSphere(FCT::Vec3<>(0.0f, 0.0f, 1.0f), 1.5f), "Sphere spanning near plane must pass");

    // 2.3 Box spanning far plane: crosses z=100.0 (e.g. z from 95.0 to 110.0)
    TEST_CHECK(frustum.testAABB(FCT::Vec3<>(-5.0f, -5.0f, 95.0f), FCT::Vec3<>(5.0f, 5.0f, 110.0f)), "AABB spanning far plane must pass");
    TEST_CHECK(frustum.testSphere(FCT::Vec3<>(0.0f, 0.0f, 100.0f), 6.0f), "Sphere spanning far plane must pass");

    // 2.4 Box spanning left plane
    const float halfWidth50 = 50.0f * std::tan(30.0f * 3.14159265f / 180.0f);
    TEST_CHECK(frustum.testAABB(FCT::Vec3<>(-halfWidth50 - 5.0f, -2.0f, 48.0f), FCT::Vec3<>(-halfWidth50 + 5.0f, 2.0f, 52.0f)), "AABB spanning left plane must pass");

    // 2.5 Box completely behind camera (z in [-50, -10])
    TEST_CHECK(!frustum.testAABB(FCT::Vec3<>(-10.0f, -10.0f, -50.0f), FCT::Vec3<>(10.0f, 10.0f, -10.0f)), "AABB completely behind camera must be culled");
    TEST_CHECK(!frustum.testSphere(FCT::Vec3<>(0.0f, 0.0f, -30.0f), 10.0f), "Sphere completely behind camera must be culled");

    // 2.6 Degenerate box where min == max (Single Point AABB)
    // Point inside:
    const FCT::Vec3<> ptInside(0.0f, 0.0f, 50.0f);
    TEST_CHECK(frustum.testAABB(ptInside, ptInside), "Degenerate AABB (min==max) inside frustum must pass");
    TEST_CHECK(frustum.testSphere(ptInside, 0.0f), "Degenerate Sphere (radius=0) inside frustum must pass");

    // Point outside:
    const FCT::Vec3<> ptOutside(0.0f, 0.0f, -10.0f);
    TEST_CHECK(!frustum.testAABB(ptOutside, ptOutside), "Degenerate AABB (min==max) outside frustum must be culled");
    TEST_CHECK(!frustum.testSphere(ptOutside, 0.0f), "Degenerate Sphere (radius=0) outside frustum must be culled");

    // 2.7 Extremely large coordinates
    // Universe-scale AABB covering [-1e15, 1e15]
    TEST_CHECK(frustum.testAABB(FCT::Vec3<>(-1e15f, -1e15f, -1e15f), FCT::Vec3<>(1e15f, 1e15f, 1e15f)), "Enormous AABB covering world must pass without overflow");

    // Ultra-distant AABB (at x=1e20)
    TEST_CHECK(!frustum.testAABB(FCT::Vec3<>(1e20f, 0.0f, 50.0f), FCT::Vec3<>(1e20f + 10.0f, 10.0f, 60.0f)), "Ultra-distant AABB must be culled");

    // 2.8 Inverted AABB where min > max (Adversarial stress)
    // Extents will be negative. The function must not crash or hang.
    const bool invertedResult = frustum.testAABB(FCT::Vec3<>(10.0f, 10.0f, 60.0f), FCT::Vec3<>(-10.0f, -10.0f, 40.0f));
    (void)invertedResult; // Just ensure no crash/exception

    std::cout << "[PASS] Test 2: AABB and Sphere intersection corner cases verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test Suite 3: Multithreaded WorkHost Chunked Culling
// -------------------------------------------------------------
bool test_WorkHost_ChunkedCulling() {
    std::cout << "\n=== [Test 3] Multithreaded WorkHost Chunked Culling Concurrency ===" << std::endl;

    WorkHost workHost;
    auto status = workHost.init(4, 2);
    TEST_CHECK(status.ok(), "WorkHost init must succeed");
    TEST_CHECK(workHost.isRunning(), "WorkHost must be running");

    CullingSystem cullingSys;
    cullingSys.setParallelEnabled(true);
    cullingSys.setChunkSize(32);

    const FCT::Mat4 view = FCT::Mat4::LookAt(FCT::Vec3<>(0.0f, 0.0f, 0.0f), FCT::Vec3<>(0.0f, 0.0f, 1.0f), FCT::Vec3<>(0.0f, 1.0f, 0.0f));
    const FCT::Mat4 proj = FCT::Mat4::Perspective(60.0f, 1.0f, 1.0f, 100.0f);
    const FCT::Mat4 vp = proj * view;

    Frustum frustum;
    frustum.extractFromViewProj(vp, false);

    // 3.1 Empty Scene Test (0 entities)
    {
        FCT::ECS::registry regEmpty;
        std::vector<FCT::ECS::entity> visibleSeq, visiblePar;
        CullingStats statsSeq{}, statsPar{};

        cullingSys.cullScene(regEmpty, frustum, visibleSeq, nullptr, &statsSeq);
        cullingSys.cullSceneParallel(regEmpty, frustum, visiblePar, nullptr, &statsPar);

        TEST_CHECK(visibleSeq.empty(), "Empty registry sequential must return 0 entities");
        TEST_CHECK(visiblePar.empty(), "Empty registry parallel must return 0 entities");
        TEST_CHECK(statsPar.totalEntities == 0, "Stats total entities must be 0");
    }

    // 3.2 Small Entity Count (< 64 threshold, e.g. 20 entities)
    {
        FCT::ECS::registry regSmall;
        for (int i = 0; i < 20; ++i) {
            auto e = regSmall.create();
            auto& world = regSmall.emplace<WorldTransform>(e);
            auto& mesh = regSmall.emplace<StaticMeshComponent>(e);

            mesh.visible = true;
            world.isVisible = true;

            if (i < 10) {
                // Inside frustum
                world.aabbMin = FCT::Vec3<>(-1.0f, -1.0f, 10.0f + static_cast<float>(i));
                world.aabbMax = FCT::Vec3<>( 1.0f,  1.0f, 12.0f + static_cast<float>(i));
            } else {
                // Outside frustum (behind camera)
                world.aabbMin = FCT::Vec3<>(-1.0f, -1.0f, -20.0f - static_cast<float>(i));
                world.aabbMax = FCT::Vec3<>( 1.0f,  1.0f, -18.0f - static_cast<float>(i));
            }
        }

        std::vector<FCT::ECS::entity> visibleSeq, visiblePar;
        cullingSys.cullScene(regSmall, frustum, visibleSeq);
        cullingSys.cullSceneParallel(regSmall, frustum, visiblePar);

        TEST_CHECK(visibleSeq.size() == 10, "Sequential small count visible must be 10");
        TEST_CHECK(visiblePar.size() == 10, "Parallel small count visible must be 10");
        TEST_CHECK(visibleSeq == visiblePar, "Small count parallel results must match sequential");
    }

    // 3.3 Large Entity Count (>= 128 entities, test 128, 512, 1024)
    const std::vector<int> counts = {128, 512, 1024};
    for (int totalCount : counts) {
        FCT::ECS::registry regLarge;
        int expectedVisible = 0;

        for (int i = 0; i < totalCount; ++i) {
            auto e = regLarge.create();
            auto& world = regLarge.emplace<WorldTransform>(e);
            auto& mesh = regLarge.emplace<StaticMeshComponent>(e);

            mesh.visible = true;
            world.isVisible = true;

            const int pattern = i % 4;
            if (pattern == 0) {
                // Visible inside frustum
                world.aabbMin = FCT::Vec3<>(-2.0f, -2.0f, 10.0f + static_cast<float>(i % 50));
                world.aabbMax = FCT::Vec3<>( 2.0f,  2.0f, 14.0f + static_cast<float>(i % 50));
                expectedVisible++;
            } else if (pattern == 1) {
                // Culled: behind camera
                world.aabbMin = FCT::Vec3<>(-2.0f, -2.0f, -50.0f - static_cast<float>(i % 50));
                world.aabbMax = FCT::Vec3<>( 2.0f,  2.0f, -46.0f - static_cast<float>(i % 50));
            } else if (pattern == 2) {
                // Culled: beyond far plane
                world.aabbMin = FCT::Vec3<>(-2.0f, -2.0f, 150.0f + static_cast<float>(i % 50));
                world.aabbMax = FCT::Vec3<>( 2.0f,  2.0f, 154.0f + static_cast<float>(i % 50));
            } else {
                // Culled: inside frustum volume, but mesh.visible = false
                world.aabbMin = FCT::Vec3<>(-2.0f, -2.0f, 10.0f + static_cast<float>(i % 50));
                world.aabbMax = FCT::Vec3<>( 2.0f,  2.0f, 14.0f + static_cast<float>(i % 50));
                mesh.visible = false;
            }
        }

        std::vector<FCT::ECS::entity> visibleSeq, visiblePar;
        CullingStats statsSeq{}, statsPar{};

        cullingSys.cullScene(regLarge, frustum, visibleSeq, nullptr, &statsSeq);
        cullingSys.cullSceneParallel(regLarge, frustum, visiblePar, nullptr, &statsPar);

        TEST_CHECK(visibleSeq.size() == static_cast<size_t>(expectedVisible), "Sequential visible count must match expected");
        TEST_CHECK(visiblePar.size() == static_cast<size_t>(expectedVisible), "Parallel visible count must match expected");
        TEST_CHECK(visibleSeq == visiblePar, "Parallel visible entity list must be IDENTICAL to sequential");
        TEST_CHECK(statsPar.totalEntities == static_cast<uint32_t>(totalCount), "Stats total entities must match");
        TEST_CHECK(statsPar.visibleEntities == static_cast<uint32_t>(expectedVisible), "Stats visible entities must match");

        std::cout << "  [Scale " << totalCount << "] Sequential: " << statsSeq.cullingDurationMs
                  << " ms, Parallel: " << statsPar.cullingDurationMs
                  << " ms, Visible: " << visiblePar.size() << "/" << totalCount << " entities" << std::endl;
    }

    // 3.4 Concurrency Stress Loop (50 iterations)
    std::cout << "  Running 50 parallel culling iterations to stress test thread safety..." << std::endl;
    FCT::ECS::registry regStress;
    for (int i = 0; i < 256; ++i) {
        auto e = regStress.create();
        auto& world = regStress.emplace<WorldTransform>(e);
        auto& mesh = regStress.emplace<StaticMeshComponent>(e);
        mesh.visible = (i % 2 == 0);
        world.isVisible = true;
        world.aabbMin = FCT::Vec3<>(-1.0f, -1.0f, 5.0f + static_cast<float>(i % 40));
        world.aabbMax = FCT::Vec3<>( 1.0f,  1.0f, 7.0f + static_cast<float>(i % 40));
    }

    for (int iter = 0; iter < 50; ++iter) {
        std::vector<FCT::ECS::entity> outVisible;
        cullingSys.cullSceneParallel(regStress, frustum, outVisible);
        TEST_CHECK(outVisible.size() == 128, "Stress test iteration visible count must be 128");
    }

    workHost.term();
    std::cout << "[PASS] Test 3: Multithreaded WorkHost chunked culling verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test Suite 4: DebugSubsystem::drawFrustum NDC-to-World Unprojection
// -------------------------------------------------------------
bool test_DebugSubsystem_drawFrustum() {
    std::cout << "\n=== [Test 4] DebugSubsystem::drawFrustum Unprojection & Singularity Protection ===" << std::endl;

    DebugSubsystem debugSys;
    auto status = debugSys.init();
    TEST_CHECK(status.ok(), "DebugSubsystem init must succeed");

    const FCT::Mat4 view = FCT::Mat4::LookAt(FCT::Vec3<>(0.0f, 0.0f, 0.0f), FCT::Vec3<>(0.0f, 0.0f, 1.0f), FCT::Vec3<>(0.0f, 1.0f, 0.0f));
    const FCT::Mat4 proj = FCT::Mat4::Perspective(60.0f, 1.0f, 1.0f, 100.0f);
    const FCT::Mat4 viewProj = proj * view;
    const FCT::Mat4 invViewProj = viewProj.inverse();

    // 4.1 Standard Frustum Drawing: exactly 12 lines generated
    debugSys.drawFrustum(invViewProj, FCT::Vec3<>(1.0f, 1.0f, 0.0f), 0.0f);
    auto lines = debugSys.getGizmoLines();
    TEST_CHECK(lines.size() == 12, "Standard drawFrustum must produce exactly 12 gizmo lines");

    // Verify all line coordinates are finite (no NaN, no Inf)
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        TEST_CHECK(isFiniteVec3(l.start), "Line start must be finite");
        TEST_CHECK(isFiniteVec3(l.end), "Line end must be finite");
        TEST_CHECK(isFiniteVec3(l.color), "Line color must be finite");
    }

    // 4.2 drawFrustum(view, proj) overload
    debugSys.tickGizmos(1.0f); // expire duration=0 lines
    TEST_CHECK(debugSys.getGizmoLines().empty(), "Tick must clear duration=0 lines");

    debugSys.drawFrustum(view, proj, FCT::Vec3<>(0.0f, 1.0f, 1.0f), 0.0f);
    lines = debugSys.getGizmoLines();
    TEST_CHECK(lines.size() == 12, "Overloaded drawFrustum(view, proj) must produce exactly 12 lines");

    // 4.3 Singular & Near-Singular Matrix Protection (|w| < 10^-6)
    debugSys.tickGizmos(1.0f);

    // Case A: All-zero matrix
    FCT::Mat4 zeroMat{};
    for (int i = 0; i < 16; ++i) zeroMat.m[i] = 0.0f;
    debugSys.drawFrustum(zeroMat);
    lines = debugSys.getGizmoLines();
    TEST_CHECK(lines.empty(), "All-zero matrix must produce 0 lines due to |w| < 1e-6 guard");

    // Case B: Matrix where w is exactly 0 for NDC corners
    FCT::Mat4 zeroWMat{};
    zeroWMat.m[0] = 1.0f; zeroWMat.m[5] = 1.0f; zeroWMat.m[10] = 1.0f; zeroWMat.m[15] = 0.0f;
    debugSys.drawFrustum(zeroWMat);
    lines = debugSys.getGizmoLines();
    TEST_CHECK(lines.empty(), "Zero-w matrix must produce 0 lines due to |w| < 1e-6 guard");

    // Case C: Near-singular matrix where |w| = 1e-7 < 1e-6
    FCT::Mat4 nearSingularMat{};
    for (int i = 0; i < 16; ++i) nearSingularMat.m[i] = 0.0f;
    nearSingularMat.m[15] = 1e-7f; // worldH.w will be 1e-7
    debugSys.drawFrustum(nearSingularMat);
    lines = debugSys.getGizmoLines();
    TEST_CHECK(lines.empty(), "Near-singular matrix (|w| = 1e-7) must be rejected by |w| < 1e-6 guard");

    // Case D: Matrix with |w| = 1e-5 > 1e-6 (should produce 12 lines safely)
    FCT::Mat4 smallWMat{};
    smallWMat.m[0] = 1.0f; smallWMat.m[5] = 1.0f; smallWMat.m[10] = 1.0f; smallWMat.m[15] = 1e-5f;
    debugSys.drawFrustum(smallWMat);
    lines = debugSys.getGizmoLines();
    TEST_CHECK(lines.size() == 12, "Matrix with |w| = 1e-5 must produce 12 lines");
    for (const auto& l : lines) {
        TEST_CHECK(isFiniteVec3(l.start), "Line start must be finite");
        TEST_CHECK(isFiniteVec3(l.end), "Line end must be finite");
    }

    debugSys.term();
    std::cout << "[PASS] Test 4: DebugSubsystem::drawFrustum unprojection and singularity handling verified." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Main Runner
// -------------------------------------------------------------
int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "   MQEngine Milestone 1 Empirical Stress Test Suite      " << std::endl;
    std::cout << "=========================================================" << std::endl;

    bool allPassed = true;

    if (!test_GribbHartmann_Vulkan_NDC()) allPassed = false;
    if (!test_AABB_Sphere_CornerCases()) allPassed = false;
    if (!test_WorkHost_ChunkedCulling()) allPassed = false;
    if (!test_DebugSubsystem_drawFrustum()) allPassed = false;

    std::cout << "\n=========================================================" << std::endl;
    if (allPassed) {
        std::cout << "   >>> ALL MILESTONE 1 EMPIRICAL TESTS PASSED! <<<      " << std::endl;
        std::cout << "=========================================================" << std::endl;
        return 0;
    } else {
        std::cerr << "   >>> MILESTONE 1 EMPIRICAL TESTS FAILED! <<<          " << std::endl;
        std::cout << "=========================================================" << std::endl;
        return 1;
    }
}
