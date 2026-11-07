#ifndef MQENGINE_SCRIPT_CORE_INTERPRETER_H
#define MQENGINE_SCRIPT_CORE_INTERPRETER_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../system/ScriptCacheSystem.h"
#include "../system/ScriptSystem.h"

namespace MQEngine
{
    namespace Script
    {
        class ENGINE_API Interpreter : public CoreEngineModule<Interpreter>
        {
        public:
            Interpreter();
            ~Interpreter();

            Interpreter(const Interpreter&) = delete;
            Interpreter& operator=(const Interpreter&) = delete;

            void registerInfo();
            Status init();
            void term();

            ScriptSystem* getScriptSystem() const noexcept { return scriptSystem.get(); }
            ScriptCacheSystem* getScriptCacheSystem() const noexcept { return scriptCacheSystem.get(); }

        public:
            UniquePtr<ScriptSystem> scriptSystem;
            UniquePtr<ScriptCacheSystem> scriptCacheSystem;

        private:
            bool m_initialized = false;
        };

    } // namespace Script
} // namespace MQEngine

#endif // MQENGINE_SCRIPT_CORE_INTERPRETER_H
