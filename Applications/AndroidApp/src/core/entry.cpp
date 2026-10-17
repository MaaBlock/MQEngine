#include "AndroidApplication.h"

using namespace MQEngine;

#ifdef FCT_ANDROID
int main()
{
    AndroidApplication application;
    Engine engine;
    Engine::s_instance = &engine;
    engine.init(&application);
    engine.loop();
    engine.term();
    return 0;
}
#else
int main(int argc, char* argv[])
{
#ifdef WIN32
    system("chcp 65001");
#endif
    CommandLineArgs::instance().parse(argc, argv);
    std::locale::global(std::locale("zh_CN.UTF-8"));
    std::wcout.imbue(std::locale("zh_CN.UTF-8"));
    std::cout.imbue(std::locale("zh_CN.UTF-8"));
    AndroidApplication application;
    Engine engine;
    Engine::s_instance = &engine;
    engine.init(&application);
    engine.loop();
    engine.term();
    return 0;
}
#endif
