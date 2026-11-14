//
// Created by MaaBlock on 2026/11/8.
//

#ifndef MQENGINE_SPATIALCOMPONENTS_H
#define MQENGINE_SPATIALCOMPONENTS_H

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            struct Position {
                Vec3 value{0.0f, 0.0f, 0.0f};
            };
            struct Rotation {
                Vec4 value{0.0f, 0.0f, 0.0f, 1.0f};
            };

            struct Scale {
                Vec3 value{1.0f, 1.0f, 1.0f};
            };
        }
    }
}
#endif // MQENGINE_SPATIALCOMPONENTS_H
