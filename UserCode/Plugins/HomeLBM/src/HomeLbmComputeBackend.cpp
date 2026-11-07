#include "HomeLbmComputeBackend.h"

#include <Context/D3D12_Context.h>
#include <RHI/D3D12_Image.h>
#include <luisa/backends/ext/dx_config_ext.h>
#include <luisa/backends/ext/native_resource_ext.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MQEngine {

namespace {

std::filesystem::path resolveExecutablePath() noexcept {
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(buffer);
    }
#endif
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path("MQEngine.exe") : (cwd / "MQEngine.exe");
}

bool hasLuisaBackendModules(const std::filesystem::path& dir) noexcept {
    std::error_code ec;
    return std::filesystem::exists(dir / "luisa-backend-dx.dll", ec) ||
           std::filesystem::exists(dir / "luisa-backend-cuda.dll", ec) ||
           std::filesystem::exists(dir / "luisa-backend-vk.dll", ec) ||
           std::filesystem::exists(dir / "libluisa-backend-cuda.so", ec) ||
           std::filesystem::exists(dir / "libluisa-backend-vk.so", ec);
}

std::filesystem::path resolveLuisaRuntimePath() noexcept {
    const std::filesystem::path exePath = resolveExecutablePath();
    const std::filesystem::path exeDir = exePath.parent_path();
    if (!exeDir.empty() && hasLuisaBackendModules(exeDir)) {
        return exePath;
    }

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    const std::array<std::filesystem::path, 3> fallbackDirs = {
        exeDir / ".." / "bin",
        exeDir / ".." / ".." / "bin",
        ec ? std::filesystem::path("bin") : (cwd / "bin")
    };
    for (const auto& candidate : fallbackDirs) {
        if (!candidate.empty() && hasLuisaBackendModules(candidate)) {
            std::error_code canonEc;
            const auto canonicalDir = std::filesystem::weakly_canonical(candidate, canonEc);
            return (canonEc ? candidate : canonicalDir) / "MQEngine.exe";
        }
    }
    return exePath;
}

#ifdef _WIN32
class SharedRhiD3D12ConfigExt : public luisa::compute::DirectXDeviceConfigExt {
public:
    SharedRhiD3D12ConfigExt(
        ID3D12Device* device,
        IDXGIAdapter1* adapter,
        IDXGIFactory2* factory) noexcept
        : m_device(device),
          m_adapter(adapter),
          m_factory(factory) {}

    luisa::optional<ExternalDevice> CreateExternalDevice() noexcept override {
        if (!m_device || !m_adapter || !m_factory) {
            return {};
        }
        ExternalDevice ext{};
        ext.device = m_device;
        ext.adapter = m_adapter;
        ext.factory = m_factory;
        return ext;
    }

    bool UseDRED() const noexcept override { return false; }
    bool UseEnhancedBarrier() const noexcept override { return false; }

private:
    ID3D12Device* m_device = nullptr;
    IDXGIAdapter1* m_adapter = nullptr;
    IDXGIFactory2* m_factory = nullptr;
};

struct LuisaDxNativeTextureDesc {
    D3D12_RESOURCE_STATES initState;
    DXGI_FORMAT customFormat;
    bool allowUav;
};
static_assert(sizeof(LuisaDxNativeTextureDesc) == 12, "LuisaDxNativeTextureDesc 布局必须与 lc::dx::NativeTextureDesc 完全一致");
#endif

} // namespace

HomeLbmComputeBackend::HomeLbmComputeBackend() = default;

HomeLbmComputeBackend::~HomeLbmComputeBackend() {
    shutdown();
}

std::string_view HomeLbmComputeBackend::toBackendIdent(LbmComputeBackendMode mode) noexcept {
    switch (mode) {
        case LbmComputeBackendMode::MatchRHI_DX12: return "dx";
        case LbmComputeBackendMode::CUDA:          return "cuda";
        case LbmComputeBackendMode::Vulkan:        return "vk";
    }
    return "dx";
}

void HomeLbmComputeBackend::ensureContextCreated() const {
    if (!m_context) {
        m_context = makeUnique<FCT::Compute::ComputeContext>(resolveLuisaRuntimePath());
    }
}

bool HomeLbmComputeBackend::isBackendAvailable(LbmComputeBackendMode mode) const {
    ensureContextCreated();
    const std::string_view ident = toBackendIdent(mode);
    const auto& lcCtx = m_context->backendImpl();

    bool installed = false;
    for (const auto& name : lcCtx.installed_backends()) {
        if (std::string_view(name.data(), name.size()) == ident) {
            installed = true;
            break;
        }
    }
    if (!installed) {
        return false;
    }

    auto deviceNames = lcCtx.backend_device_names(luisa::string_view(ident.data(), ident.size()));
    return !deviceNames.empty();
}

Status HomeLbmComputeBackend::initialize(LbmComputeBackendMode mode, FCT::Context* rhiContext) {
    ensureContextCreated();
    const std::string_view ident = toBackendIdent(mode);

    if (!isBackendAvailable(mode)) {
        return UnavailableError(StrCat("计算后端不可用或未检测到支持的物理设备: ", ident));
    }

    shutdown();
    ensureContextCreated();

    auto& lcCtx = m_context->backendImpl();
    auto deviceNames = lcCtx.backend_device_names(luisa::string_view(ident.data(), ident.size()));
    m_activeDeviceHardwareName = deviceNames.empty()
        ? std::string(ident)
        : std::string(deviceNames.front().data(), deviceNames.front().size());

#ifdef _WIN32
    if (mode == LbmComputeBackendMode::MatchRHI_DX12 && rhiContext != nullptr) {
        auto* d3d12Ctx = dynamic_cast<FCT::D3D12_Context*>(rhiContext);
        if (d3d12Ctx && d3d12Ctx->getDevice() && d3d12Ctx->getAdapter() && d3d12Ctx->getFactory()) {
            luisa::compute::DeviceConfig devCfg{};
            devCfg.device_index = 0u;
            devCfg.extension = luisa::make_unique<SharedRhiD3D12ConfigExt>(
                d3d12Ctx->getDevice(),
                d3d12Ctx->getAdapter(),
                d3d12Ctx->getFactory());

            luisa::compute::Device nativeDev = lcCtx.create_device("dx", &devCfg);
            if (!nativeDev) {
                return InternalError("共享 FCT::D3D12_Context 创建 LuisaCompute DX12 设备失败");
            }

            m_device = FCT::Compute::ComputeDevice(std::move(nativeDev));
            // Note: 使用 GRAPHICS (DIRECT) 队列，支持外部 D3D12 纹理在 PIXEL_SHADER_RESOURCE 与 UAV 之间无拷贝转换
            m_stream = m_device.createStream(true);
            m_activeMode = mode;
            m_isSharingRhiDx12Device = true;
            return OkStatus();
        }
    }
#endif

    auto devOr = m_context->createDevice(ident, 0u);
    CHECK_STATUS(devOr);

    m_device = std::move(*devOr);
    const bool useGraphicsQueue = (mode == LbmComputeBackendMode::MatchRHI_DX12);
    m_stream = m_device.createStream(useGraphicsQueue);
    if (!m_stream.valid()) {
        m_device = FCT::Compute::ComputeDevice{};
        return InternalError(StrCat("创建计算流失败: ", ident));
    }

    m_activeMode = mode;
    m_isSharingRhiDx12Device = false;
    return OkStatus();
}

void HomeLbmComputeBackend::shutdown() {
    if (m_stream.valid()) {
        m_stream.synchronize();
        m_stream = FCT::Compute::ComputeStream{};
    }
    if (m_device.valid()) {
        m_device = FCT::Compute::ComputeDevice{};
    }
    m_isSharingRhiDx12Device = false;
}

std::string HomeLbmComputeBackend::getActiveBackendDisplayName() const {
    if (!isValid()) {
        return "Uninitialized";
    }
    switch (m_activeMode) {
        case LbmComputeBackendMode::MatchRHI_DX12:
            return m_isSharingRhiDx12Device
                ? StrCat("DX12 (RHI 零拷贝共享 | ", m_activeDeviceHardwareName, ")")
                : StrCat("DX12 (独立设备 | ", m_activeDeviceHardwareName, ")");
        case LbmComputeBackendMode::CUDA:
            return StrCat("CUDA (", m_activeDeviceHardwareName, ")");
        case LbmComputeBackendMode::Vulkan:
            return StrCat("Vulkan (", m_activeDeviceHardwareName, ")");
    }
    return "Unknown";
}

StatusOr<void*> HomeLbmComputeBackend::extractSharedD3D12Resource(
    FCT::SingleBufferImage* rhiImage,
    uint32_t& outInitState,
    uint32_t& outWidth,
    uint32_t& outHeight) const {
    if (!m_isSharingRhiDx12Device || !isValid()) {
        return FailedPreconditionError("当前计算后端未与 D3D12 RHI 共享同一物理设备");
    }
    if (!rhiImage || !rhiImage->currentImage()) {
        return InvalidArgumentError("传入的 RHI 图像对象为空");
    }
#ifdef _WIN32
    auto* d3d12Image = dynamic_cast<FCT::RHI::D3D12_Image*>(rhiImage->currentImage());
    if (!d3d12Image || !d3d12Image->getResource()) {
        return FailedPreconditionError("无法从 SingleBufferImage 提取底层 ID3D12Resource");
    }
    outInitState = static_cast<uint32_t>(d3d12Image->getResourceState());
    outWidth = d3d12Image->width();
    outHeight = d3d12Image->height();
    return static_cast<void*>(d3d12Image->getResource());
#else
    (void)outInitState;
    (void)outWidth;
    (void)outHeight;
    return UnavailableError("当前平台不支持 D3D12 外部纹理互操作");
#endif
}

template<FCT::Format F>
    requires FCT::Compute::SupportedFormatConcept<F>
StatusOr<FCT::Compute::FormatImage<F>> HomeLbmComputeBackend::importExternalRhiImage(FCT::SingleBufferImage* rhiImage) {
    uint32_t initState = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    auto resPtrOr = extractSharedD3D12Resource(rhiImage, initState, width, height);
    CHECK_STATUS(resPtrOr);

#ifdef _WIN32
    auto* nativeExt = static_cast<luisa::compute::NativeResourceExt*>(
        m_device.backendImpl().impl()->extension(luisa::compute::NativeResourceExt::name));
    if (!nativeExt) {
        return UnavailableError("LuisaCompute DX12 设备未暴露 NativeResourceExt 扩展");
    }

    LuisaDxNativeTextureDesc nativeDesc{};
    nativeDesc.initState = static_cast<D3D12_RESOURCE_STATES>(initState);
    nativeDesc.customFormat = DXGI_FORMAT_UNKNOWN;
    nativeDesc.allowUav = true;

    using ElemT = typename FCT::Compute::FormatTraits<F>::ElementType;
    constexpr auto storage = FCT::Compute::FormatImage<F>::backendStorage();
    auto lcImg = nativeExt->create_native_image<ElemT>(
        *resPtrOr,
        width,
        height,
        storage,
        1u,
        &nativeDesc);
    if (!lcImg) {
        return InternalError("注册外部 D3D12 纹理到 LuisaCompute 失败");
    }
    return FCT::Compute::FormatImage<F>(std::move(lcImg));
#else
    return UnavailableError("当前平台不支持 D3D12 外部纹理互操作");
#endif
}

template HOME_LBM_API StatusOr<FCT::Compute::FormatImage<FCT::Format::R16G16B16A16_SFLOAT>>
HomeLbmComputeBackend::importExternalRhiImage<FCT::Format::R16G16B16A16_SFLOAT>(FCT::SingleBufferImage*);

template HOME_LBM_API StatusOr<FCT::Compute::FormatImage<FCT::Format::R32_SFLOAT>>
HomeLbmComputeBackend::importExternalRhiImage<FCT::Format::R32_SFLOAT>(FCT::SingleBufferImage*);

} // namespace MQEngine
