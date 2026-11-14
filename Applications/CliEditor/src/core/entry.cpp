/**
 * @file entry.cpp
 * @brief CliEditor 应用程序入口注册器
 */
#include "CliEditorApplication.h"
#include <MQEngine/runtime/core/Runtime.h>

namespace MQEngine
{
    static Runtime::Application* CreateCliEditorApplication()
    {
        BindModuleSharedMemoryTracker();
        return FCT_NEW(CliEditorApplication);
    }

    namespace
    {
        class CliEditorApplicationRegistrar
        {
        public:
            CliEditorApplicationRegistrar()
            {
                Runtime::Runtime::RegisterApplicationFactory(CreateCliEditorApplication);
            }
        };
        static CliEditorApplicationRegistrar g_cliEditorRegistrar;
    } // namespace
} // namespace MQEngine
