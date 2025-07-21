//
// Created by Administrator on 2025/7/21.
//

#ifndef ENGINE_H
#define ENGINE_H
#include "systemmanager.h"

namespace MQEngine {
    class EngineScope;
    ENGINE_API class Engine
    {
    public:
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        void loop();
        static Engine& getInstance();
        friend class EngineScope;
    private:
        void init();
        void term();
        Engine() = default;
        ~Engine() = default;
        static Engine* s_instance;
        FCT::Runtime m_rt;
        SystemManager m_systemManager;
    };
    /**
     * @return engine version
     */
    ENGINE_API const char* getEngineVersion();
    /**
     * @return Engine instance
     */
    ENGINE_API Engine& getEngine();
}
#endif //ENGINE_H
