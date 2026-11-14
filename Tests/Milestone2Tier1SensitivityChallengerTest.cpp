#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <map>
#include <functional>

#include "Common/E2EAssert.h"
#include "Common/TestProfiles.h"
#include "Common/MockRhiContext.h"
#include <absl/status/status.h>
#include <absl/status/statusor.h>

using namespace MQEngine::Testing;

// ============================================================================
// 1. PassMerge Sensitivity Harness
// ============================================================================

struct PassMergeNode {
    std::string passName;
    uint32_t targetFboId{0};
    bool hasSampleDependencyOnPrevious{false};
    bool forceClear{false};
    uint32_t mrtCount{1};
};

struct MergedPassGroup {
    uint32_t targetFboId{0};
    uint32_t mrtCount{1};
    std::vector<std::string> subPasses;
};

std::vector<MergedPassGroup> mergePassSequence(const std::vector<PassMergeNode>& passes) {
    std::vector<MergedPassGroup> groups;
    for (const auto& pass : passes) {
        if (!groups.empty()) {
            auto& lastGroup = groups.back();
            if (lastGroup.targetFboId == pass.targetFboId &&
                lastGroup.mrtCount == pass.mrtCount &&
                !pass.hasSampleDependencyOnPrevious &&
                !pass.forceClear) {
                lastGroup.subPasses.push_back(pass.passName);
                continue;
            }
        }
        MergedPassGroup newGroup;
        newGroup.targetFboId = pass.targetFboId;
        newGroup.mrtCount = pass.mrtCount;
        newGroup.subPasses.push_back(pass.passName);
        groups.push_back(std::move(newGroup));
    }
    return groups;
}

bool runPassMergeSensitivityTests() {
    std::cout << "\n[TEST SUITE 1] PassMerge Sensitivity Verification" << std::endl;

    // Test 1.1: 3 兼容 Pass 正常合并为 1 组
    std::vector<PassMergeNode> passes = {
        PassMergeNode{"Opaque", 200, false, true, 1},
        PassMergeNode{"Skybox", 200, false, false, 1},
        PassMergeNode{"Transparent", 200, false, false, 1}
    };
    auto g1 = mergePassSequence(passes);
    E2E_ASSERT_EQ(g1.size(), 1u);
    E2E_ASSERT_EQ(g1[0].subPasses.size(), 3u);
    std::cout << "  [PASS] 1.1 Three sequential compatible passes merge into 1 group" << std::endl;

    // Test 1.2: 改变中间 Pass 的 FBO 目标 -> 应分裂为 3 组 (Pass 3 不能越过 Pass 2 与 Pass 1 合并)
    passes[1].targetFboId = 300;
    auto g2 = mergePassSequence(passes);
    E2E_ASSERT_EQ(g2.size(), 3u);
    E2E_ASSERT_EQ(g2[0].targetFboId, 200u);
    E2E_ASSERT_EQ(g2[1].targetFboId, 300u);
    E2E_ASSERT_EQ(g2[2].targetFboId, 200u);
    std::cout << "  [PASS] 1.2 FBO target change causes group split and prevents cross-pass merging" << std::endl;

    // Test 1.3: 引入对前序产物的采样依赖 -> 切断合并
    passes[1].targetFboId = 200; // 恢复 FBO
    passes[2].hasSampleDependencyOnPrevious = true; // 第 3 个 Pass 采样前序产物
    auto g3 = mergePassSequence(passes);
    E2E_ASSERT_EQ(g3.size(), 2u);
    E2E_ASSERT_EQ(g3[0].subPasses.size(), 2u);
    E2E_ASSERT_EQ(g3[1].subPasses.size(), 1u);
    std::cout << "  [PASS] 1.3 Sample dependency strictly prevents merge" << std::endl;

    // Test 1.4: 后续 Pass 强制清屏 -> 切断合并
    passes[2].hasSampleDependencyOnPrevious = false;
    passes[1].forceClear = true; // 第 2 个 Pass 强制清屏
    auto g4 = mergePassSequence(passes);
    E2E_ASSERT_EQ(g4.size(), 2u);
    E2E_ASSERT_EQ(g4[0].subPasses.size(), 1u);
    E2E_ASSERT_EQ(g4[1].subPasses.size(), 2u);
    std::cout << "  [PASS] 1.4 Subsequent force clear strictly prevents merge" << std::endl;

    // Test 1.5: MRT 输出数量不匹配 -> 切断合并
    passes[1].forceClear = false;
    passes[2].mrtCount = 2; // 第 3 个 Pass 输出 2 个 MRT
    auto g5 = mergePassSequence(passes);
    E2E_ASSERT_EQ(g5.size(), 2u);
    E2E_ASSERT_EQ(g5[0].subPasses.size(), 2u);
    E2E_ASSERT_EQ(g5[1].subPasses.size(), 1u);
    std::cout << "  [PASS] 1.5 Differing MRT count strictly prevents merge" << std::endl;

    // Test 1.6: 100 Pass 压力与敏锐度验证 (偶数兼容，奇数带采样依赖)
    std::vector<PassMergeNode> stressPasses;
    for (int i = 0; i < 100; ++i) {
        stressPasses.push_back(PassMergeNode{
            "Pass_" + std::to_string(i),
            100,
            (i % 2 == 1), // 奇数带依赖
            (i == 0),
            1
        });
    }
    auto g6 = mergePassSequence(stressPasses);
    // 0 与 1 不能合并 (1 带依赖) -> 0 单独一组
    // 1 带依赖新建组；2 不带依赖与 1 合并...
    // 预期每 2 个为一组 (除 0 单独一组): 1 + 99/2 + 1 = 51 组
    E2E_ASSERT_EQ(g6.size(), 51u);
    std::cout << "  [PASS] 1.6 100-pass stress test produces deterministic 51 groups" << std::endl;

    return true;
}

// ============================================================================
// 2. TBDR Invalidate Plan Sensitivity Harness
// ============================================================================

struct TbdrPassAttachment {
    uint32_t attachmentType{0};
    uint32_t lastPassIdx{0};
    bool isTransient{true};
    bool isExportTarget{false};
};

struct TbdrMultiPassEvaluator {
    std::vector<TbdrPassAttachment> attachments;

    std::vector<uint32_t> computeDiscardsForPass(uint32_t currentPassIdx) const {
        std::vector<uint32_t> discards;
        for (const auto& att : attachments) {
            if (att.isTransient && !att.isExportTarget && att.lastPassIdx == currentPassIdx) {
                discards.push_back(att.attachmentType);
            }
        }
        return discards;
    }
};

bool runTbdrInvalidatePlanSensitivityTests() {
    std::cout << "\n[TEST SUITE 2] TBDR Invalidate Plan Sensitivity Verification" << std::endl;

    TbdrMultiPassEvaluator evaluator;
    evaluator.attachments.push_back(TbdrPassAttachment{
        .attachmentType = 0x8D00, // GL_DEPTH_ATTACHMENT
        .lastPassIdx = 0,
        .isTransient = true,
        .isExportTarget = false
    });
    evaluator.attachments.push_back(TbdrPassAttachment{
        .attachmentType = 0x8CE0, // GL_COLOR_ATTACHMENT0 (MSAA)
        .lastPassIdx = 1,
        .isTransient = true,
        .isExportTarget = false
    });
    evaluator.attachments.push_back(TbdrPassAttachment{
        .attachmentType = 0x8CE0, // GL_COLOR_ATTACHMENT0 (Export target)
        .lastPassIdx = 1,
        .isTransient = false,
        .isExportTarget = true
    });

    // Test 2.1: 基线测试
    auto p0 = evaluator.computeDiscardsForPass(0);
    auto p1 = evaluator.computeDiscardsForPass(1);
    E2E_ASSERT_EQ(p0.size(), 1u);
    E2E_ASSERT_EQ(p0[0], 0x8D00u);
    E2E_ASSERT_EQ(p1.size(), 1u);
    E2E_ASSERT_EQ(p1[0], 0x8CE0u);
    std::cout << "  [PASS] 2.1 Baseline discard plan verified (Pass 0 depth, Pass 1 MSAA)" << std::endl;

    // Test 2.2: 突变 lastPassIdx: 将 Depth 的 lastPassIdx 改为 1
    evaluator.attachments[0].lastPassIdx = 1;
    auto p0_mut = evaluator.computeDiscardsForPass(0);
    auto p1_mut = evaluator.computeDiscardsForPass(1);
    E2E_ASSERT_EQ(p0_mut.size(), 0u); // Pass 0 不再丢弃
    E2E_ASSERT_EQ(p1_mut.size(), 2u); // Pass 1 同时丢弃 Depth 与 MSAA
    E2E_ASSERT_EQ(p1_mut[0], 0x8D00u);
    E2E_ASSERT_EQ(p1_mut[1], 0x8CE0u);
    std::cout << "  [PASS] 2.2 Mutating lastPassIdx shifts discard to Pass 1" << std::endl;

    // Test 2.3: 突变 isExportTarget: 将 MSAA 标记为导出目标 -> 不应被丢弃
    evaluator.attachments[0].lastPassIdx = 0; // 恢复 Depth
    evaluator.attachments[1].isExportTarget = true; // MSAA 变为导出目标
    auto p1_export = evaluator.computeDiscardsForPass(1);
    E2E_ASSERT_EQ(p1_export.size(), 0u);
    std::cout << "  [PASS] 2.3 Mutating isExportTarget protects attachment from discard" << std::endl;

    // Test 2.4: 突变 isTransient: 将 Depth 标记为非瞬态 -> 不应被丢弃
    evaluator.attachments[1].isExportTarget = false; // 恢复 MSAA
    evaluator.attachments[0].isTransient = false;    // Depth 为非瞬态
    auto p0_transient = evaluator.computeDiscardsForPass(0);
    E2E_ASSERT_EQ(p0_transient.size(), 0u);
    std::cout << "  [PASS] 2.4 Mutating isTransient false retains attachment in main memory" << std::endl;

    // Test 2.5: 跨多附件多通道组合 (Pass 2 丢弃 4 个瞬态缓冲)
    TbdrMultiPassEvaluator multiEvaluator;
    for (uint32_t attId = 0x8CE0; attId < 0x8CE4; ++attId) {
        multiEvaluator.attachments.push_back(TbdrPassAttachment{
            .attachmentType = attId,
            .lastPassIdx = 2,
            .isTransient = true,
            .isExportTarget = false
        });
    }
    auto p2_multi = multiEvaluator.computeDiscardsForPass(2);
    E2E_ASSERT_EQ(p2_multi.size(), 4u);
    std::cout << "  [PASS] 2.5 Multiple attachments correctly batched for Invalidate on Pass 2" << std::endl;

    return true;
}

// ============================================================================
// 3. Texture SubRegion Bounds Sensitivity Harness
// ============================================================================

struct TextureSubRegionValidator {
    uint32_t baseWidth{1024};
    uint32_t baseHeight{1024};
    uint32_t mipLevels{4};
    uint32_t arrayLayers{6};

    bool validate(uint32_t layer, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h) const {
        if (layer >= arrayLayers) return false;
        if (mip >= mipLevels) return false;
        uint32_t mipW = std::max(1u, baseWidth >> mip);
        uint32_t mipH = std::max(1u, baseHeight >> mip);
        if (x + w > mipW || y + h > mipH) return false;
        return true;
    }
};

bool runTextureSubRegionBoundsSensitivityTests() {
    std::cout << "\n[TEST SUITE 3] Texture SubRegion Bounds Sensitivity Verification" << std::endl;

    TextureSubRegionValidator validator{
        .baseWidth = 1024,
        .baseHeight = 1024,
        .mipLevels = 4,
        .arrayLayers = 6
    };

    // Test 3.1: 合法标称与精确边缘
    E2E_ASSERT_TRUE(validator.validate(0, 0, 0, 0, 1024, 1024)); // Mip 0 全尺寸
    E2E_ASSERT_TRUE(validator.validate(5, 3, 0, 0, 128, 128));   // Layer 5, Mip 3 (1024>>3 = 128)
    E2E_ASSERT_TRUE(validator.validate(2, 1, 256, 256, 256, 256)); // Mip 1 (512x512) 右下角恰好吻合
    std::cout << "  [PASS] 3.1 In-bounds nominal and exact boundary updates accepted" << std::endl;

    // Test 3.2: ArrayLayer 越界敏锐度
    E2E_ASSERT_FALSE(validator.validate(6, 0, 0, 0, 100, 100)); // layer == arrayLayers
    E2E_ASSERT_FALSE(validator.validate(7, 0, 0, 0, 100, 100)); // layer > arrayLayers
    E2E_ASSERT_FALSE(validator.validate(0xFFFFFFFFu, 0, 0, 0, 100, 100)); // layer UINT32_MAX
    std::cout << "  [PASS] 3.2 Out-of-bounds array layers strictly rejected" << std::endl;

    // Test 3.3: MipLevel 越界敏锐度
    E2E_ASSERT_FALSE(validator.validate(0, 4, 0, 0, 64, 64)); // mip == mipLevels
    E2E_ASSERT_FALSE(validator.validate(0, 5, 0, 0, 64, 64)); // mip > mipLevels
    E2E_ASSERT_FALSE(validator.validate(0, 0xFFFFFFFFu, 0, 0, 64, 64)); // mip UINT32_MAX
    std::cout << "  [PASS] 3.3 Out-of-bounds mip levels strictly rejected" << std::endl;

    // Test 3.4: 1 像素溢出敏锐度 (Off-by-one tests)
    // Mip 0 (1024x1024): x + w == 1024 (OK), x + w == 1025 (FAIL)
    E2E_ASSERT_TRUE(validator.validate(0, 0, 1023, 0, 1, 1));
    E2E_ASSERT_FALSE(validator.validate(0, 0, 1023, 0, 2, 1)); // 1023+2 = 1025 > 1024
    E2E_ASSERT_TRUE(validator.validate(0, 0, 0, 1023, 1, 1));
    E2E_ASSERT_FALSE(validator.validate(0, 0, 0, 1023, 1, 2)); // 1023+2 = 1025 > 1024

    // Mip 1 (512x512):
    E2E_ASSERT_TRUE(validator.validate(0, 1, 511, 511, 1, 1));
    E2E_ASSERT_FALSE(validator.validate(0, 1, 512, 0, 1, 1)); // 起始点越界

    // Mip 2 (256x256): 宽度溢出
    E2E_ASSERT_FALSE(validator.validate(1, 2, 0, 0, 257, 256));
    E2E_ASSERT_FALSE(validator.validate(1, 2, 0, 0, 256, 257));
    std::cout << "  [PASS] 3.4 Single-pixel overflow strictly triggers boundary rejection" << std::endl;

    return true;
}

// ============================================================================
// 4. WebJS TypedArray View & NBR Sensitivity Harness
// ============================================================================

struct WebJsTypedArrayView {
    uint8_t* bufferPtr{nullptr};
    size_t byteOffset{0};
    size_t byteLength{0};

    bool isValid() const noexcept {
        return bufferPtr != nullptr && byteLength > 0;
    }

    size_t size() const noexcept { return byteLength; }

    uint8_t get(size_t index) const {
        assert(index < byteLength && "TypedArray index out of bounds");
        return bufferPtr[byteOffset + index];
    }

    void set(size_t index, uint8_t value) {
        assert(index < byteLength && "TypedArray index out of bounds");
        bufferPtr[byteOffset + index] = value;
    }

    uint8_t& operator[](size_t index) {
        assert(index < byteLength && "TypedArray index out of bounds");
        return bufferPtr[byteOffset + index];
    }

    const uint8_t& operator[](size_t index) const {
        assert(index < byteLength && "TypedArray index out of bounds");
        return bufferPtr[byteOffset + index];
    }
};

class WebJsVmBridge {
public:
    explicit WebJsVmBridge(bool nbrCompatible = true)
        : m_nbrCompatible(nbrCompatible) {}

    WebJsTypedArrayView createTypedArrayView(uint8_t* buffer, size_t offset, size_t length) {
        if (!buffer || length == 0) {
            return WebJsTypedArrayView{};
        }
        return WebJsTypedArrayView{
            .bufferPtr = buffer,
            .byteOffset = offset,
            .byteLength = length
        };
    }

    bool isNbrCompatible() const noexcept { return m_nbrCompatible; }
    void setNbrCompatible(bool compatible) noexcept { m_nbrCompatible = compatible; }

private:
    bool m_nbrCompatible{true};
};

bool runWebJsSensitivityTests() {
    std::cout << "\n[TEST SUITE 4] WebJS TypedArray View & NBR Sensitivity Verification" << std::endl;

    WebJsVmBridge bridge;
    uint8_t wasmMemory[16] = { 10, 20, 30, 40, 50, 60, 70, 80 };

    // Test 4.1: 创建零拷贝视图并突变
    auto view = bridge.createTypedArrayView(wasmMemory, 2, 4);
    E2E_ASSERT_TRUE(view.isValid());
    E2E_ASSERT_EQ(view.size(), 4u);
    E2E_ASSERT_EQ(view.get(0), 30u);
    E2E_ASSERT_EQ(view.get(1), 40u);

    view.set(0, 99u);
    view[1] = 88u;
    E2E_ASSERT_EQ(wasmMemory[2], 99u);
    E2E_ASSERT_EQ(wasmMemory[3], 88u);

    // 验证视图外内存未被破坏
    E2E_ASSERT_EQ(wasmMemory[0], 10u);
    E2E_ASSERT_EQ(wasmMemory[1], 20u);
    E2E_ASSERT_EQ(wasmMemory[4], 50u);
    E2E_ASSERT_EQ(wasmMemory[5], 60u);

    // 验证反向：突变底层内存，视图立即可见
    wasmMemory[4] = 77u;
    E2E_ASSERT_EQ(view.get(2), 77u);
    std::cout << "  [PASS] 4.1 TypedArray bidirectional zero-copy mutation verified" << std::endl;

    // Test 4.2: 空指针与 0 长度视图防护
    auto nullView = bridge.createTypedArrayView(nullptr, 0, 10);
    E2E_ASSERT_FALSE(nullView.isValid());
    auto zeroView = bridge.createTypedArrayView(wasmMemory, 0, 0);
    E2E_ASSERT_FALSE(zeroView.isValid());
    std::cout << "  [PASS] 4.2 Null buffer and zero length protection verified" << std::endl;

    // Test 4.3: NBR 敏锐度验证 (非恒真存根)
    WebJsVmBridge defaultBridge;
    E2E_ASSERT_TRUE(defaultBridge.isNbrCompatible());

    WebJsVmBridge disabledBridge(false);
    E2E_ASSERT_FALSE(disabledBridge.isNbrCompatible());

    disabledBridge.setNbrCompatible(true);
    E2E_ASSERT_TRUE(disabledBridge.isNbrCompatible());

    disabledBridge.setNbrCompatible(false);
    E2E_ASSERT_FALSE(disabledBridge.isNbrCompatible());
    std::cout << "  [PASS] 4.3 NBR compatibility state toggle strictly verified" << std::endl;

    return true;
}

// ============================================================================
// 5. StorageBuffer Device Address & Offline Shader Table Sensitivity Harness
// ============================================================================

struct OfflineShaderBinaryPackage {
    std::string shaderName;
    uint32_t spirvMagic{0};
    std::vector<uint32_t> bytecode;
};

struct OfflinePrebakedShaderTable {
    std::map<std::string, OfflineShaderBinaryPackage> prebakedShaders;

    void registerShader(OfflineShaderBinaryPackage pkg) {
        prebakedShaders[pkg.shaderName] = std::move(pkg);
    }

    absl::StatusOr<const OfflineShaderBinaryPackage*> findValidShader(std::string_view name) const {
        auto it = prebakedShaders.find(std::string(name));
        if (it == prebakedShaders.end()) {
            return absl::NotFoundError("Prebaked shader not found in offline manifest");
        }
        if (it->second.spirvMagic != 0x07230203u) {
            return absl::InvalidArgumentError("Corrupted SPIR-V header magic");
        }
        return &it->second;
    }
};

struct RuntimeShaderCompilationEngine {
    bool hasShadercLinked{false};

    absl::Status compileShader(std::string_view glslSource) {
        if (!hasShadercLinked) {
            return absl::UnimplementedError("Runtime shaderc is stripped from minigame build");
        }
        return absl::OkStatus();
    }
};

bool runStorageAndShaderStrippingTests() {
    std::cout << "\n[TEST SUITE 5] StorageBuffer Address & Offline Prebaked Table Verification" << std::endl;

    // Test 5.1: StorageBuffer Device Address 敏锐度
    MockRhiContext minigameCtx{DouyinMinigameProfile()};
    MockRhiContext desktopCtx{DesktopProfile()};

    // 小游戏 Profile: 不支持 SSBO, 设备地址严格为 0
    uint64_t miniAddr1 = minigameCtx.getStorageBufferDeviceAddress(1);
    uint64_t miniAddr99 = minigameCtx.getStorageBufferDeviceAddress(99);
    E2E_ASSERT_EQ(miniAddr1, 0ull);
    E2E_ASSERT_EQ(miniAddr99, 0ull);

    // 桌面 Profile: 支持 SSBO, 设备地址必须非 0, 且 handle 0 必须为 0
    uint64_t deskAddr0 = desktopCtx.getStorageBufferDeviceAddress(0);
    uint64_t deskAddr1 = desktopCtx.getStorageBufferDeviceAddress(1);
    uint64_t deskAddr2 = desktopCtx.getStorageBufferDeviceAddress(2);
    E2E_ASSERT_EQ(deskAddr0, 0ull);
    E2E_ASSERT_NE(deskAddr1, 0ull);
    E2E_ASSERT_NE(deskAddr2, 0ull);
    E2E_ASSERT_NE(deskAddr1, deskAddr2);
    std::cout << "  [PASS] 5.1 StorageBuffer address sensitivity: 0 on Minigame, unique non-zero on Desktop" << std::endl;

    // Test 5.2: 离线预编译表与 SPIR-V 幻数敏锐度
    OfflinePrebakedShaderTable table;
    table.registerShader(OfflineShaderBinaryPackage{
        .shaderName = "ForwardPlus_Clustered_VS",
        .spirvMagic = 0x07230203u,
        .bytecode = { 0x07230203u, 0x00010300u, 0x000d000bu }
    });
    table.registerShader(OfflineShaderBinaryPackage{
        .shaderName = "Corrupted_Shader",
        .spirvMagic = 0xDEADBEEFu, // 损坏幻数
        .bytecode = { 0xDEADBEEFu }
    });

    auto validOr = table.findValidShader("ForwardPlus_Clustered_VS");
    E2E_ASSERT_TRUE(validOr.ok());

    auto corruptOr = table.findValidShader("Corrupted_Shader");
    E2E_ASSERT_FALSE(corruptOr.ok());
    E2E_ASSERT_EQ(corruptOr.status().code(), absl::StatusCode::kInvalidArgument);

    auto missingOr = table.findValidShader("NonExistent_Shader");
    E2E_ASSERT_FALSE(missingOr.ok());
    E2E_ASSERT_EQ(missingOr.status().code(), absl::StatusCode::kNotFound);
    std::cout << "  [PASS] 5.2 Offline shader table strictly validates SPIR-V magic and existence" << std::endl;

    // Test 5.3: 运行期编译引擎剥离敏锐度
    RuntimeShaderCompilationEngine strippedEngine{false};
    E2E_ASSERT_EQ(strippedEngine.compileShader("void main() {}").code(), absl::StatusCode::kUnimplemented);

    RuntimeShaderCompilationEngine fullEngine{true};
    E2E_ASSERT_TRUE(fullEngine.compileShader("void main() {}").ok());
    std::cout << "  [PASS] 5.3 Runtime compilation engine strictly verifies shaderc stripping" << std::endl;

    return true;
}

// ============================================================================
// Main Runner
// ============================================================================

int main(int argc, char** argv) {
    std::cout << "=========================================================" << std::endl;
    std::cout << "  Milestone TEST-M2 Gate 3: Challenger 1 Empirical Harness" << std::endl;
    std::cout << "=========================================================" << std::endl;

    bool p1 = runPassMergeSensitivityTests();
    bool p2 = runTbdrInvalidatePlanSensitivityTests();
    bool p3 = runTextureSubRegionBoundsSensitivityTests();
    bool p4 = runWebJsSensitivityTests();
    bool p5 = runStorageAndShaderStrippingTests();

    if (p1 && p2 && p3 && p4 && p5) {
        std::cout << "\n=========================================================" << std::endl;
        std::cout << "  ALL EMPIRICAL CHALLENGER SENSITIVITY TESTS PASSED!" << std::endl;
        std::cout << "=========================================================" << std::endl;
        return 0;
    } else {
        std::cerr << "\n>>> CHALLENGER TEST FAILED! <<<" << std::endl;
        return 1;
    }
}
