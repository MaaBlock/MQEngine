#include "EditorApplication.h"
#include "../command/UndoManager.h"

using namespace FCT;
namespace MQEngine {
    EditorGlobal g_editorGlobal;

    EditorApplication::EditorApplication() = default;

    EditorApplication::~EditorApplication() {
        g_editorGlobal.undoManager = nullptr;
    }

    static Runtime::Application* CreateEditorApplication()
    {
        return new EditorApplication();
    }

    class ApplicationRegistrar
    {
    public:
        ApplicationRegistrar()
        {
            Runtime::Runtime::RegisterApplicationFactory(CreateEditorApplication);
        }
    };

    static ApplicationRegistrar g_registrar;
}
