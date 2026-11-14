#include "EditorApplication.h"
#include <MQEngine/runtime/core/Runtime.h>

namespace MQEngine
{
    static Runtime::Application* CreateEditorApplication()
    {
        BindModuleSharedMemoryTracker();
        return FCT_NEW(EditorApplication);
    }

    namespace
    {
        class EditorApplicationRegistrar
        {
        public:
            EditorApplicationRegistrar()
            {
                Runtime::Runtime::RegisterApplicationFactory(CreateEditorApplication);
            }
        };
        static EditorApplicationRegistrar g_editorRegistrar;
    } // namespace
} // namespace MQEngine
