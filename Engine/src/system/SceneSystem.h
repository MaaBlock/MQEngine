#ifndef SCENESYSTEM_H
#define SCENESYSTEM_H
#include "./ISystem.h"
namespace MQEngine {
    class SceneSystem : public ISystem {
    public:
        void updateLogic() override;
        void updateRender() override;

    private:

    };

} // MQEngine

#endif //SCENESYSTEM_H
