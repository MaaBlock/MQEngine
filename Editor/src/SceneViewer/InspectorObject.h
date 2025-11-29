#ifndef INSPECTOROBJECT_H
#define INSPECTOROBJECT_H

namespace MQEngine {
    class InspectorObject {
    public:
        virtual ~InspectorObject() = default;
        virtual void onInspectorGui() = 0;
    };
}

#endif // INSPECTOROBJECT_H
