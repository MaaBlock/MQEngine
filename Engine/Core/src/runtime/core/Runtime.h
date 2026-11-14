#ifndef MQENGINE_RUNTIME_CORE_RUNTIME_H
#define MQENGINE_RUNTIME_CORE_RUNTIME_H

#include <cstdint>
#include <string>
#include <vector>
#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/classdef.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/runtime/classdef.h>
#include <MQEngine/runtime/core/CreateApplication.h>

namespace MQEngine
{
    namespace Runtime
    {
        /**
         * @brief 引擎运行时总控类：负责模块生命周期流程管理，
         *        通过编译期拓扑图 StaticTokenGraph 驱动模块初始化与逆序销毁
         */
        class ENGINE_API Runtime
        {
        public:
            Runtime() = default;
            ~Runtime() = default;

            Runtime(const Runtime&) = delete;
            Runtime& operator=(const Runtime&) = delete;

            static void RegisterApplicationFactory(CreateApplicationFn fn);

            void init(int argc, char* argv[]);
            void run();
            void term();

        private:
            void prepare();
            void loop();
            void finalize();

            void registerModules();
            void initModules();
            void termModules();
            void unregisterModules();

        private:
            UniquePtr<Engine> m_engine;
        };
    } // namespace Runtime
} // namespace MQEngine

#endif // MQENGINE_RUNTIME_CORE_RUNTIME_H
