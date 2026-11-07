#ifndef MQENGINE_HOMELBM_SYSTEM_H
#define MQENGINE_HOMELBM_SYSTEM_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmComponents.h"
#include "HomeLbmSolver.h"
#include "HomeLbmRenderer.h"
#include "HomeLbmCubeTool.h"

namespace MQEngine {

/**
 * @brief 供渲染提交线程 (ImGui) 无锁只读消费的 HOME-LBM 帧快照
 */
struct HomeLbmUiSnapshot {
    uint64_t sequence = 0;
    bool hasDomain = false;
    LbmDomainComponent domain{};
    HomeLbmTelemetry telemetry{};
    bool solverInitialized = false;
    bool dxAvailable = false;
    bool cudaAvailable = false;
    bool vkAvailable = false;
    bool sharingRhiDx12 = false;
    std::string activeBackendName;
    std::string statusMessage = "就绪";
    HomeLbmCameraView cameraView{};
    std::vector<AmrBlockWireframeBox> amrWireframeBoxes;
    std::vector<LbmGpuPrimitive> sceneColliderPrimitives;
};

/**
 * @brief 从 ImGui 渲染提交线程投递到逻辑线程执行的无锁 UI 指令包
 */
struct HomeLbmUiPendingCommand {
    bool createDefaultDomain = false;
    bool hasDomainPatch = false;
    LbmDomainComponent patchedDomain{};
    bool hasPresetRequest = false;
    LbmPresetScenario requestedPreset = LbmPresetScenario::WindTunnelKarmanVortex;
    bool hasBackendSwitchRequest = false;
    LbmComputeBackendMode requestedBackendMode = LbmComputeBackendMode::MatchRHI_DX12;
    bool requestDiagnostics = false;
    bool hasBoxCommit = false;
    FCT::Vec3<> committedMinBounds = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    FCT::Vec3<> committedMaxBounds = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    bool hasStatusMessage = false;
    std::string statusMessage;

    [[nodiscard]] bool hasAnyAction() const noexcept {
        return createDefaultDomain || hasDomainPatch || hasPresetRequest ||
               hasBackendSwitchRequest || requestDiagnostics || hasBoxCommit || hasStatusMessage;
    }
};

/**
 * @brief 自适应 HOME-LBM 空间模拟域 ECS 系统（负责自动收集域内碰撞体/网格体素化、推进 GPU 求解与体积合成）
 */
class HOME_LBM_API HomeLbmSystem : public ISystem {
public:
    HomeLbmSystem(FCT::Context* rhiContext, DataManager* dataManager);
    ~HomeLbmSystem() override;

    Status initialize();
    void shutdown();

    void updateLogic() override;
    void updateRender() override;

    /**
     * @brief 查找当前主场景活跃的 LbmDomainComponent 实体；若不存在且 createIfMissing 为 true 则创建
     */
    bool findPrimaryDomainEntity(
        entt::registry*& outRegistry,
        entt::entity& outEntity,
        bool createIfMissing = false);

    /**
     * @brief 应用三大经典物理实验预设（卡门涡街风洞 / 三维溃坝自由液面 / 热浮力湍流烟羽）
     */
    void applyPresetToDomain(LbmDomainComponent& domain, LbmPresetScenario preset);

    /**
     * @brief 立即请求热切换计算后端（保留全部三维流体状态）
     */
    Status requestBackendSwitch(LbmComputeBackendMode targetMode);

    /**
     * @brief 切换计算后端模式别名（供外部服务/脚本统一调度）
     */
    Status setComputeBackendMode(LbmComputeBackendMode targetMode) {
        return requestBackendSwitch(targetMode);
    }

    /**
     * @brief 回读流场并执行守恒诊断
     */
    Status evaluateFieldDiagnostics(const LbmDomainComponent& domain);

    /**
     * @brief 手动单步/多步推进流场
     */
    Status stepManual(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives,
        int32_t steps = 1);

    bool isSolverInitialized() const noexcept { return m_solver.isInitialized(); }

    /**
     * @brief 对当前场景中的碰撞体与地面执行射线求交，供 UE5 拉框建盒吸附表面高度（逻辑线程使用）
     */
    bool raycastSceneColliders(const LbmRay3D& ray, FCT::Vec3<>& outHitPoint) const;

    /**
     * @brief 基于无锁快照中的碰撞体图元与地面执行射线求交（供 ImGui 渲染提交线程安全调用）
     */
    bool raycastSnapshotColliders(const LbmRay3D& ray, FCT::Vec3<>& outHitPoint) const;

    /**
     * @brief 解析当前活跃相机视图；若无活跃相机则根据模拟域包围盒生成环绕观察视角
     */
    HomeLbmCameraView resolveActiveCameraView(
        const LbmDomainComponent& domain,
        float fallbackAspect = 16.0f / 9.0f) const;

    /**
     * @brief 收集与给定模拟域相交的全部障碍物、发射源及场景碰撞体几何基元
     */
    void gatherDomainPrimitives(
        const LbmDomainComponent& domain,
        entt::entity domainEntity,
        std::vector<LbmGpuPrimitive>& outPrimitives) const;

    /**
     * @brief 获取当前已发布的无锁 UI 快照
     */
    const HomeLbmUiSnapshot& currentUiSnapshot() const noexcept {
        return m_uiSnapshots[m_publishedUiSnapshotIdx.load(std::memory_order_acquire) & 1u];
    }

    /**
     * @brief 从 UI 线程无锁投递操作指令，在下一帧逻辑线程开头统一消费
     */
    void submitUiCommand(HomeLbmUiPendingCommand cmd);

    HomeLbmSolver& solver() noexcept { return m_solver; }
    const HomeLbmSolver& solver() const noexcept { return m_solver; }

    HomeLbmRenderer& renderer() noexcept { return m_renderer; }
    HomeLbmCubeTool& cubeTool() noexcept { return m_cubeTool; }
    const HomeLbmCubeTool& cubeTool() const noexcept { return m_cubeTool; }

    const std::string& lastStatusMessage() const noexcept { return m_lastStatusMessage; }
    void setStatusMessage(std::string msg) { m_lastStatusMessage = std::move(msg); }

private:
    void consumePendingUiCommands();
    void publishUiSnapshot(entt::registry* domainReg, entt::entity domainEntity);
    void collectSceneCollidersInRegistry(
        entt::registry* registry,
        entt::entity domainEntity,
        const FCT::Vec3<>& domMin,
        const FCT::Vec3<>& domMax,
        std::vector<LbmGpuPrimitive>& outPrimitives) const;

private:
    FCT::Context* m_rhiContext = nullptr;
    DataManager* m_dataManager = nullptr;

    HomeLbmSolver m_solver;
    HomeLbmRenderer m_renderer;
    HomeLbmCubeTool m_cubeTool;
    std::vector<LbmGpuPrimitive> m_renderPrimitives;

    std::vector<LbmGpuPrimitive> m_framePrimitives;
    FCT::Vec3<> m_lastMinBounds = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    FCT::Vec3<> m_lastMaxBounds = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    bool m_hasTrackedBounds = false;
    std::string m_lastStatusMessage = "就绪";

    // Note: 双缓冲无锁 UI 快照与单生产者-单消费者无锁指令槽，彻底消除跨线程锁与 ECS 竞态
    HomeLbmUiSnapshot m_uiSnapshots[2];
    std::atomic<uint32_t> m_publishedUiSnapshotIdx{0};
    uint64_t m_snapshotSequence = 0;

    HomeLbmUiPendingCommand m_pendingUiCommands[2];
    std::atomic<uint32_t> m_uiCmdWriteIdx{0};
    std::atomic<bool> m_hasPendingUiCmd{false};
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_SYSTEM_H
