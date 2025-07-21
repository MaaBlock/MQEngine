/**
 * @file enginescope.h
 * @brief Engine scope for managing the engine instance
 */
#ifndef ENGINESCOPE_H
#define ENGINESCOPE_H
#include "../engine.h"
namespace MQEngine {
    class EngineScope {
    public:
        EngineScope(){
            Engine::s_instance = &m_engine;
            m_engine.init();
        }
        ~EngineScope(){
            m_engine.term();
            Engine::s_instance = nullptr;
        }
    private:
        Engine m_engine;
    };
}
#endif //ENGINESCOPE_H
