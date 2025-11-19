//
// Created by MaaBlock on 2025/11/18.
//

#ifndef SCRIPTCACHESYSTEM_H
#define SCRIPTCACHESYSTEM_H

#include "ISystem.h"

namespace MQEngine {

    class DataManager;
    class ScriptSystem;

    class ENGINE_API ScriptCacheSystem : public ISystem {
    public:
        ScriptCacheSystem(DataManager* dataManager, ScriptSystem* scriptSystem);
        ~ScriptCacheSystem() override;

        void updateLogic() override;
        void updateRender() override;

    private:
        DataManager* m_dataManager;
        ScriptSystem* m_scriptSystem;
    };

} // MQEngine

#endif //SCRIPTCACHESYSTEM_H
