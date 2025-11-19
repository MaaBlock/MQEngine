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
#include "core/engine.h"
#include "core/entry.h"
#include "manager/SystemManager.h"
#include "core/TextureUtils.h"
#include "data/DataManager.h"
#include "system/ScriptSystem.h"
#include "data/Scene.h"
#include "data/Component.h"
#endif //HEADERS_H
