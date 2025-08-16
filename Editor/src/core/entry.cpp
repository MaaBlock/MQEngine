# include "EditorApplication.h"
using namespace MQEngine;

int main() {
    EditorApplication application;
    EngineScope engineScope(&application);
    Engine& engine = getEngine();
    engine.loop();
    return 0;
}
