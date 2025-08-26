#ifndef ENTITYINSPECTOR_H
#define ENTITYINSPECTOR_H
#include "../Thirdparty/thirdparty.h"
namespace MQEngine {

    class EntityInspector {
    public:
        EntityInspector();
        void inspectEntity(Scene* scene,const std::string& trunk,entt::entity entity);
        void render();

    private:
        Scene* m_currentEntityScene = nullptr;
        std::string m_currentEntityTrunk;
        entt::entity m_currentEntity = entt::null;
    };

} // MQEngine

#endif //ENTITYINSPECTOR_H
