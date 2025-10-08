/**
 *@file entry.cpp
 *@brief The definition of entry point
 *@note The entry point has been unified in FCT library.
*/
#include "../engineapi.h"
/**
* @brief Entrypoint for the program.
* @note The entry point has been unified in FCT library.
*/



namespace MQEngine
{
    extern CreateApplicationFn GCreateApplication;
}

int ENGINE_API main()
{
    std::locale::global(std::locale("zh_CN.UTF-8"));
    std::wcout.imbue(std::locale("zh_CN.UTF-8"));
    std::cout.imbue(std::locale("zh_CN.UTF-8"));

    auto application = MQEngine::GCreateApplication();

    MQEngine::Engine engine;
    MQEngine::Engine::s_instance = &engine;

    engine.init(application);
    engine.loop();
    engine.term();

    delete application;
    application = nullptr;

    return 0;
}
