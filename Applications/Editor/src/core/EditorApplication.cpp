#include "EditorApplication.h"

using namespace FCT;
namespace MQEngine {
    EditorGlobal g_editorGlobal;

    static Application* CreateEditorApplication()
    {
        return new EditorApplication();
    }

    class ApplicationRegistrar
    {
    public:
        ApplicationRegistrar()
        {
            Engine::RegisterApplicationFactory(CreateEditorApplication);
        }
    };

    static ApplicationRegistrar g_registrar;
}
