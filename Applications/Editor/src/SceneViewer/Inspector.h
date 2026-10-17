#ifndef INSPECTOR_H
#define INSPECTOR_H

#include "../thirdparty/thirdparty.h"
#include "../core/Global.h"
#include "InspectorObject.h"

namespace MQEngine {
    class Inspector {
    public:
        Inspector();
        void render();
        void setTarget(InspectorObject* target)
        {
            m_currentObject = target;
        }
        InspectorObject* getCurrentObject() const { return m_currentObject; }
    private:
        InspectorObject* m_currentObject = nullptr;
    };
}

#endif // INSPECTOR_H
