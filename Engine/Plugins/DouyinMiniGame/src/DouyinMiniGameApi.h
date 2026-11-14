/**
 * @file DouyinMiniGameApi.h
 * @brief DouyinMiniGame 插件三大物理隔离 Composite 动态库导出符号宏定义
 */
#ifndef MQENGINE_DOUYINMINIGAME_API_H
#define MQENGINE_DOUYINMINIGAME_API_H

#ifdef _WIN32
    #ifdef BUILD_DOUYINPACKAGEREDITORCOMPOSITE_COMPOSITE
        #define DOUYIN_PACKAGER_EDITOR_API __declspec(dllexport)
    #else
        #define DOUYIN_PACKAGER_EDITOR_API __declspec(dllimport)
    #endif

    #ifdef BUILD_DOUYINPACKAGERCLICOMPOSITE_COMPOSITE
        #define DOUYIN_PACKAGER_CLI_API __declspec(dllexport)
    #else
        #define DOUYIN_PACKAGER_CLI_API __declspec(dllimport)
    #endif

    #ifdef BUILD_DOUYINPLATFORMRUNTIMECOMPOSITE_COMPOSITE
        #define DOUYIN_PLATFORM_RUNTIME_API __declspec(dllexport)
    #else
        #define DOUYIN_PLATFORM_RUNTIME_API __declspec(dllimport)
    #endif
#else
    #define DOUYIN_PACKAGER_EDITOR_API __attribute__((visibility("default")))
    #define DOUYIN_PACKAGER_CLI_API __attribute__((visibility("default")))
    #define DOUYIN_PLATFORM_RUNTIME_API __attribute__((visibility("default")))
#endif

#endif // MQENGINE_DOUYINMINIGAME_API_H
