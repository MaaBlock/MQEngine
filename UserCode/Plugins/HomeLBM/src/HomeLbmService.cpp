#include "HomeLbmService.h"
#include "HomeLbmSystem.h"

namespace MQEngine {

HomeLbmService::HomeLbmService(HomeLbmSystem* system) noexcept
    : m_system(system) {}

bool HomeLbmService::resetSimulation() {
    if (!m_system) {
        return false;
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, true) || !reg) {
        return false;
    }
    reg->get<LbmDomainComponent>(entity).resetRequested = true;
    return true;
}

bool HomeLbmService::stepSimulation(int32_t steps) {
    if (!m_system || steps <= 0) {
        return false;
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, false) || !reg) {
        return false;
    }
    const auto& domain = reg->get<LbmDomainComponent>(entity);
    std::vector<LbmGpuPrimitive> prims;
    m_system->gatherDomainPrimitives(domain, entity, prims);
    return m_system->stepManual(domain, prims, steps).ok();
}

bool HomeLbmService::setPaused(bool paused) {
    if (!m_system) {
        return false;
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, false) || !reg) {
        return false;
    }
    reg->get<LbmDomainComponent>(entity).paused = paused;
    return true;
}

bool HomeLbmService::isPaused() const {
    if (!m_system) {
        return true;
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, false) || !reg) {
        return true;
    }
    return reg->get<LbmDomainComponent>(entity).paused;
}

bool HomeLbmService::setComputeBackend(const std::string& backendIdent) {
    if (!m_system) {
        return false;
    }
    LbmComputeBackendMode target = LbmComputeBackendMode::MatchRHI_DX12;
    if (backendIdent == "cuda" || backendIdent == "CUDA") {
        target = LbmComputeBackendMode::CUDA;
    } else if (backendIdent == "vk" || backendIdent == "vulkan" || backendIdent == "Vulkan") {
        target = LbmComputeBackendMode::Vulkan;
    }
    return m_system->requestBackendSwitch(target).ok();
}

std::string HomeLbmService::getActiveBackendName() const {
    if (!m_system || !m_system->solver().isInitialized()) {
        return "Uninitialized";
    }
    return std::string(HomeLbmComputeBackend::toBackendIdent(
        m_system->solver().backend().activeMode()));
}

std::string HomeLbmService::getActiveDeviceName() const {
    if (!m_system || !m_system->solver().isInitialized()) {
        return "None";
    }
    return m_system->solver().backend().getActiveBackendDisplayName();
}

bool HomeLbmService::applyPreset(int32_t presetIndex) {
    if (!m_system) {
        return false;
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, true) || !reg) {
        return false;
    }
    const auto clamped = static_cast<LbmPresetScenario>(std::clamp(presetIndex, 0, 2));
    m_system->applyPresetToDomain(reg->get<LbmDomainComponent>(entity), clamped);
    return true;
}

bool HomeLbmService::setDomainBounds(FCT::Vec3<> minBounds, FCT::Vec3<> maxBounds) {
    if (!m_system) {
        return false;
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, true) || !reg) {
        return false;
    }
    auto& domain = reg->get<LbmDomainComponent>(entity);
    domain.minBounds = FCT::Vec3<>(
        std::min(minBounds.x, maxBounds.x),
        std::min(minBounds.y, maxBounds.y),
        std::min(minBounds.z, maxBounds.z));
    domain.maxBounds = FCT::Vec3<>(
        std::max(minBounds.x, maxBounds.x),
        std::max(minBounds.y, maxBounds.y),
        std::max(minBounds.z, maxBounds.z));
    if (domain.maxBounds.x - domain.minBounds.x < 0.25f) domain.maxBounds.x = domain.minBounds.x + 0.25f;
    if (domain.maxBounds.y - domain.minBounds.y < 0.25f) domain.maxBounds.y = domain.minBounds.y + 0.25f;
    if (domain.maxBounds.z - domain.minBounds.z < 0.25f) domain.maxBounds.z = domain.minBounds.z + 0.25f;
    domain.resetRequested = true;
    return true;
}

FCT::Vec3<> HomeLbmService::getDomainMinBounds() const {
    if (!m_system) {
        return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, false) || !reg) {
        return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    }
    return reg->get<LbmDomainComponent>(entity).minBounds;
}

FCT::Vec3<> HomeLbmService::getDomainMaxBounds() const {
    if (!m_system) {
        return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    }
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!m_system->findPrimaryDomainEntity(reg, entity, false) || !reg) {
        return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    }
    return reg->get<LbmDomainComponent>(entity).maxBounds;
}

int32_t HomeLbmService::getStepCount() const {
    return m_system ? m_system->solver().telemetry().stepCount : 0;
}

int32_t HomeLbmService::getActiveBlockCount() const {
    return m_system ? m_system->solver().telemetry().totalActiveBlocks : 0;
}

int32_t HomeLbmService::getTotalActiveCells() const {
    return m_system ? m_system->solver().telemetry().totalActiveCells : 0;
}

float HomeLbmService::getTotalLiquidMass() const {
    return m_system ? m_system->solver().telemetry().totalLiquidMass : 0.0f;
}

float HomeLbmService::getLiquidMassDriftPercent() const {
    return m_system ? m_system->solver().telemetry().liquidMassDriftPercent : 0.0f;
}

float HomeLbmService::getMaxSpeed() const {
    return m_system ? m_system->solver().telemetry().maxSpeed : 0.0f;
}

float HomeLbmService::getMaxVorticity() const {
    return m_system ? m_system->solver().telemetry().maxVorticity : 0.0f;
}

} // namespace MQEngine
