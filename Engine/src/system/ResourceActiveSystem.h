//
// Created by MaaBlock on 2025/10/25.
//

#ifndef RESOURCEACTIVESYSTEM_H
#define RESOURCEACTIVESYSTEM_H

#include "ISystem.h"
#include <FCT_Node/concurrentqueue.h>

namespace MQEngine {
    class DataManager;
    struct CacheResource;

    class ENGINE_API ResourceActiveSystem final : public ISystem {
    public:
        explicit ResourceActiveSystem(DataManager* dataManager);

        void updateLogic() override;

        void updateRender() override;

        void requestActive(CacheResource* resource);

    private:
        moodycamel::ConcurrentQueue<CacheResource*> m_activeQueue;
        DataManager* m_dataManager;
    };

} // MQEngine

#endif //RESOURCEACTIVESYSTEM_H
