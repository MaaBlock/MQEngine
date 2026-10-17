#include "GameApplication.h"
#include "Engine/core/CommandLineArgs.h"
using namespace MQEngine;
int main(int argc, char* argv[]) {
#ifdef WIN32
    system("chcp 65001");
#endif
    CommandLineArgs::instance().parse(argc, argv);
    std::locale::global(std::locale("zh_CN.UTF-8"));
    std::wcout.imbue(std::locale("zh_CN.UTF-8"));
    std::cout.imbue(std::locale("zh_CN.UTF-8"));
    GameApplication application;
    Engine engine;
    Engine::s_instance = &engine;
    engine.init(&application);
    engine.loop();
    engine.term();
    return 0;
}
