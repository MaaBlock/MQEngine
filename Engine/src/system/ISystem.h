//
// Created by MaaBlock on 2025/10/25.
//

#ifndef ISYSTEM_H
#define ISYSTEM_H

namespace MQEngine {
    class ENGINE_API ISystem
    {
    public:
        virtual void updateLogic() = 0;
        virtual void updateRender() = 0;
    };
} // MQEngine

#endif //ISYSTEM_H
