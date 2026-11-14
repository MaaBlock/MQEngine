/**
 * @file headers.h
 * @brief MQEngine v2 核心引擎公共聚合头文件
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

#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/base/base.h>
#include <MQEngine/resource/resource.h>
#include <MQEngine/host/host.h>
#include <MQEngine/asset/asset.h>
#include <MQEngine/domain/domain.h>
#include <MQEngine/runtime/runtime.h>

#endif // ENGINE_HEADERS_H
