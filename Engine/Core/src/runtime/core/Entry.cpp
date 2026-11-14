#include <MQEngine/engineapi.h>
#include <MQEngine/runtime/core/Runtime.h>

using namespace MQEngine;

int ENGINE_API main(int argc, char* argv[])
{
    SetThreadName("Main / Logic Thread");
    SetupUtf8();
    spdlog::flush_on(spdlog::level::trace);
    MQEngine::Runtime::Runtime rt;
    rt.init(argc, argv);
    rt.run();
    rt.term();

    return 0;
}
