#ifndef ENTITYINSPECTOR_H
#define ENTITYINSPECTOR_H
#include "../thirdparty/thirdparty.h"
#include "../core/Global.h"
namespace MQEngine {

    class EntityInspector {
    public:
        EntityInspector();
        void render();

    private:
        void renderEntityDetails();
        void renderComponents(entt::registry* registry);
    };

} // MQEngine

#endif //ENTITYINSPECTOR_H
