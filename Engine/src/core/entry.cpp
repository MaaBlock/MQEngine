/**
 *@file entry.cpp
 *@brief The definition of entry point
 *@note The entry point has been unified in FCT library.
*/
#include "../engineapi.h"
#include "CommandLineArgs.h"
/**
* @brief Entrypoint for the program.
* @note The entry point has been unified in FCT library.
*/



namespace MQEngine
{
    extern CreateApplicationFn GCreateApplication;
}

#ifdef FCT_ANDROID
int ENGINE_API main()
#else
int ENGINE_API main(int argc, char* argv[])
#endif
{
    tracy::SetThreadName("Main / Logic Thread");
#ifdef WIN32
    system("chcp 65001");
#endif
#ifndef FCT_ANDROID
    MQEngine::CommandLineArgs::instance().parse(argc, argv);
#endif
    spdlog::info("main函数启动");
    std::filesystem::path current_path = std::filesystem::current_path();
    spdlog::info("当前工作目录：{}", current_path.string());
#ifndef FCT_ANDROID
    std::locale::global(std::locale("zh_CN.UTF-8"));
    std::wcout.imbue(std::locale("zh_CN.UTF-8"));
    std::cout.imbue(std::locale("zh_CN.UTF-8"));
#endif

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
