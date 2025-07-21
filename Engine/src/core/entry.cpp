#include "../engineapi.h"
/**
* @brief Entrypoint for the program.
* @note The entrypoint has been replace to main in FCT.
*/
int main() {
    MQEngine::EngineScope engineScope();
    MQEngine::Engine& engine = MQEngine::getEngine();
    engine.loop();
    return 0;
}


