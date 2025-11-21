#ifndef SKYBOXCACHESYSTEM_H
#define SKYBOXCACHESYSTEM_H
#include "./ISystem.h"

namespace MQEngine {
    class DataManager;
    class ENGINE_API SkyboxCacheSystem : public ISystem {
    public:
        SkyboxCacheSystem(FCT::Context* ctx, DataManager* dataManager);
        ~SkyboxCacheSystem() override = default;

        void updateLogic() override;
        void updateRender() override {}

    private:
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
    };
}
#endif //SKYBOXCACHESYSTEM_H
