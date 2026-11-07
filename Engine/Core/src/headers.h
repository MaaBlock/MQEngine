/**
 *@file headers.h
 *@brief include all headers in this file for one who wants to use the engine
 */
#ifndef ENGINE_HEADERS_H
#define ENGINE_HEADERS_H
#ifdef _WIN32
    #ifdef BUILD_ENGINE
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
    #endif
#else
    #define ENGINE_API
#endif
#include "thirdparty/thirdparty.h"
#include "base/ConfigDocument.h"
#include "base/Foundation.h"
#include "base/MemoryManager.h"
#include "base/JobSystem.h"
#include "physics/data/PhysicsCommon.h"
#include "physics/manager/PhysicsScene.h"
#include "physics/system/PhysicsSystem.h"
#include "animation/system/SkeletalAnimationSystem.h"
#include "hid/data/KeyCode.h"
#include "hid/system/InputSystem.h"
#include "network/manager/NetworkManager.h"
#include "network/system/NetworkSyncSystem.h"
#include "render/core/TextureUtils.h"
#include "resource/core/ModelAssetSerializer.h"
#include "runtime/core/Engine.h"
#include "runtime/core/Entry.h"
#include "runtime/core/Runtime.h"
#include "runtime/manager/SystemManager.h"
#include "runtime/runtime.h"
#include "scene/core/Scene.h"
#include "scene/data/Component.h"
#include "scene/manager/DataManager.h"
#include "scene/system/ComponentReflection.h"
#include "script/core/IScriptBackend.h"
#include "script/core/NodeJsScriptBackend.h"
#include "script/core/PybindScriptBackend.h"
#include "script/system/ScriptSystem.h"
#include "Engine_reflection.gen.h"
#endif //HEADERS_H
