//
// Created by MaaBlock on 2025/10/25.
//

#ifndef RESOURCEACTIVESYSTEM_H
#define RESOURCEACTIVESYSTEM_H

#include "../../runtime/system/ISystem.h"
#include <FCT_Node/concurrentqueue.h>

namespace MQEngine {
    class DataManager;
    struct CacheResource;

    class ENGINE_API ResourceActiveSystem : public ISystem {
    public:
        explicit ResourceActiveSystem(DataManager* dataManager);

        void setDataManager(DataManager* dataManager) noexcept { m_dataManager = dataManager; }

        void updateLogic() override;

        void updateRender() override;

        void requestActive(CacheResource* resource);
        void requestActive(bool* visibleFlag);

        template<typename T>
        void requestActive(T* resource) {
            if (resource) {
                requestActive(&resource->visible);
            }
        }

    private:
        moodycamel::ConcurrentQueue<bool*> m_activeQueue;
        DataManager* m_dataManager;
    };

} // MQEngine

#endif //RESOURCEACTIVESYSTEM_H
