#ifndef EDITOR_SHADERGRAPH_H
#define EDITOR_SHADERGRAPH_H

#include "../thirdparty/thirdparty.h"
#include <imnodes.h>

namespace MQEngine {
    class ShaderGraph {
    public:
        ShaderGraph();
        ~ShaderGraph();

        void render();

    private:
        ImNodesEditorContext* m_context = nullptr;
    };
}

#endif // EDITOR_SHADERGRAPH_H
