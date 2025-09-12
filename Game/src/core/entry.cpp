#include "GameApplication.h"
using namespace MQEngine;
int main() {
    std::locale::global(std::locale("zh_CN.UTF-8"));
    std::wcout.imbue(std::locale("zh_CN.UTF-8"));
    std::cout.imbue(std::locale("zh_CN.UTF-8"));
    GameApplication application;
    EngineScope engineScope(&application);
    Engine& engine = getEngine();
    engine.loop();
    return 0;
}
