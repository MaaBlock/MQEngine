//
// Created by MaaBlock on 2026/11/6.
//

#ifndef MQENGINE_ENGINE_H
#define MQENGINE_ENGINE_H

#include "../../EnginePCH.h"
#include "../../animation/core/Animator.h"
#include "../../base/Foundation.h"
#include "../../effect/core/Effector.h"
#include "../../hid/core/Input.h"
#include "../../network/core/Replication.h"
#include "../../physics/core/Simulator.h"
#include "../../platform/core/Platform.h"
#include "../../render/core/Renderer.h"
#include "../../resource/core/Repository.h"
#include "../../scene/core/World.h"
#include "../../script/core/Interpreter.h"
#include "../classdef.h"
#include "../thirdparty/thirdparty.h"
#include "./CreateApplication.h"
#include "./Host.h"

namespace MQEngine
{
    namespace Runtime
    {
        /**
         * @brief 引擎顶层模块聚合数据容器，持有全部核心模块值内嵌对象并向外部暴露；
         *        除 static Engine* getInstance() 外不包含任何成员函数，初始化与销毁生命周期完全由友元 Runtime 驱动管控
         */
        class ENGINE_API Engine
        {
        private:
            friend class Runtime;
            static Engine* s_instance;
        public:
            Engine();
            ~Engine();
            Engine(const Engine&) = delete;
            Engine& operator=(const Engine&) = delete;
            Engine(Engine&&) = delete;
            Engine& operator=(Engine&&) = delete;

            static Engine* getInstance()
            {
                return s_instance;
            }

            Application* application = nullptr;
            Base::Foundation foundation;
            Host host;
            Platform::Platform platform;
            Resource::Repository resource;
            Scene::World scene;
            Physics::Simulator physics;
            Animation::Animator animation;
            HID::Input hid;
            Network::Replication network;
            Script::Interpreter script;
            Effect::Effector effect;
            Render::Renderer renderer;

            bool headLess = false;
        };

    } // namespace Runtime

    ENGINE_API const char* getEngineVersion();
    ENGINE_API Runtime::Engine* getEngine();
    ENGINE_API void OutputDebugObject();
#ifdef FCT_DEBUG
    ENGINE_API std::vector<FCT::_fct_object_t*> GetDebugObject();
#endif
} // namespace MQEngine

#endif // MQENGINE_ENGINE_H
