//
// Created by Administrator on 2025/7/21.
//

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
#endif //HEADERS_H
