#include "GameApplication.h"
using namespace MQEngine;
int main() {
    std::locale::global(std::locale("zh_CN.UTF-8"));
    std::wcout.imbue(std::locale("zh_CN.UTF-8"));
    std::cout.imbue(std::locale("zh_CN.UTF-8"));
    GameApplication application;
    Engine engine;
    engine.init(&application);
    engine.loop();
    engine.term();
    return 0;
}
