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
        
        template<typename ComponentType>
        bool tryRenderComponent(entt::registry* registry, entt::entity entity);
    };

} // MQEngine

#endif //ENTITYINSPECTOR_H
