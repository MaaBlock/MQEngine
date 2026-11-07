#ifndef MQENGINE_HOMELBM_SERVICE_H
#define MQENGINE_HOMELBM_SERVICE_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmComponents.h"

namespace MQEngine {

class HomeLbmSystem;

/**
 * @brief 面向脚本系统 (JS/TS & Python) 与外部控制器的自适应 HOME-LBM 服务接口（支持 mq_reflect 零注解静态反射）
 */
class HOME_LBM_API HomeLbmService {
public:
    explicit HomeLbmService(HomeLbmSystem* system = nullptr) noexcept;
    ~HomeLbmService() = default;

    void bindSystem(HomeLbmSystem* system) noexcept { m_system = system; }

    bool resetSimulation();
    bool stepSimulation(int32_t steps);
    bool setPaused(bool paused);
    bool isPaused() const;

    bool setComputeBackend(const std::string& backendIdent);
    std::string getActiveBackendName() const;
    std::string getActiveDeviceName() const;

    bool applyPreset(int32_t presetIndex);
    bool setDomainBounds(FCT::Vec3<> minBounds, FCT::Vec3<> maxBounds);
    FCT::Vec3<> getDomainMinBounds() const;
    FCT::Vec3<> getDomainMaxBounds() const;

    int32_t getStepCount() const;
    int32_t getActiveBlockCount() const;
    int32_t getTotalActiveCells() const;
    float getTotalLiquidMass() const;
    float getLiquidMassDriftPercent() const;
    float getMaxSpeed() const;
    float getMaxVorticity() const;

private:
    HomeLbmSystem* m_system = nullptr;
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_SERVICE_H
