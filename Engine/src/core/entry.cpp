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
int main() {
    MQEngine::EngineScope engineScope;
    MQEngine::Engine& engine = MQEngine::getEngine();
    engine.loop();
    return 0;
}


