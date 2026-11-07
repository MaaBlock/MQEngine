#ifndef MQENGINE_HOMELBM_COMPUTE_BACKEND_H
#define MQENGINE_HOMELBM_COMPUTE_BACKEND_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmComponents.h"

namespace MQEngine {

/**
 * @brief 管理 FCT_Compute (LuisaCompute) 设备生命周期、D3D12 RHI 同设备零拷贝互操作与运行时热切换
 */
class HOME_LBM_API HomeLbmComputeBackend {
public:
    HomeLbmComputeBackend();
    ~HomeLbmComputeBackend();

    HomeLbmComputeBackend(const HomeLbmComputeBackend&) = delete;
    HomeLbmComputeBackend& operator=(const HomeLbmComputeBackend&) = delete;

    /**
     * @brief 初始化或切换到底层硬件计算后端
     */
    Status initialize(LbmComputeBackendMode mode, FCT::Context* rhiContext);

    /**
     * @brief 同步并释放当前计算流与设备句柄
     */
    void shutdown();

    /**
     * @brief 检测指定硬件计算后端在当前机器上是否已安装且存在可用物理设备
     */
    bool isBackendAvailable(LbmComputeBackendMode mode) const;

    /**
     * @brief 获取指定后端对应的 LuisaCompute 后端标识名 ("dx" / "cuda" / "vk")
     */
    static std::string_view toBackendIdent(LbmComputeBackendMode mode) noexcept;

    /**
     * @brief 获取人类可读的当前后端状态描述
     */
    std::string getActiveBackendDisplayName() const;

    bool isValid() const noexcept { return m_device.valid() && m_stream.valid(); }
    bool isSharingRhiDx12Device() const noexcept { return m_isSharingRhiDx12Device; }
    LbmComputeBackendMode activeMode() const noexcept { return m_activeMode; }

    FCT::Compute::ComputeDevice& device() noexcept { return m_device; }
    const FCT::Compute::ComputeDevice& device() const noexcept { return m_device; }

    FCT::Compute::ComputeStream& stream() noexcept { return m_stream; }
    const FCT::Compute::ComputeStream& stream() const noexcept { return m_stream; }

    /**
     * @brief 将 FCT::SingleBufferImage 零拷贝注册为 FCT_Compute 外部可写纹理（仅在同设备 DX12 模式下可用）
     */
    template<FCT::Format F>
        requires FCT::Compute::SupportedFormatConcept<F>
    StatusOr<FCT::Compute::FormatImage<F>> importExternalRhiImage(FCT::SingleBufferImage* rhiImage);

private:
    void ensureContextCreated() const;
    StatusOr<void*> extractSharedD3D12Resource(
        FCT::SingleBufferImage* rhiImage,
        uint32_t& outInitState,
        uint32_t& outWidth,
        uint32_t& outHeight) const;

private:
    mutable UniquePtr<FCT::Compute::ComputeContext> m_context;
    FCT::Compute::ComputeDevice m_device;
    FCT::Compute::ComputeStream m_stream;
    LbmComputeBackendMode m_activeMode = LbmComputeBackendMode::MatchRHI_DX12;
    bool m_isSharingRhiDx12Device = false;
    std::string m_activeDeviceHardwareName;
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_COMPUTE_BACKEND_H
