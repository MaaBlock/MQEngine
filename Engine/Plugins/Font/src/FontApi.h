/**
 * @file FontApi.h
 * @brief Font 插件包 DLL 导出宏定义
 */
#ifndef MQENGINE_FONT_API_H
#define MQENGINE_FONT_API_H

#ifdef _WIN32
    #if defined(BUILD_FONTRUNTIMECOMPOSITE_COMPOSITE)
        #define FONT_RUNTIME_COMPOSITE_API __declspec(dllexport)
    #else
        #define FONT_RUNTIME_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_PLUGIN_Font) || defined(BUILD_PLUGIN_FONT) || defined(BUILD_FONTRUNTIMECOMPOSITE_COMPOSITE)
        #define FONT_API __declspec(dllexport)
    #else
        #define FONT_API __declspec(dllimport)
    #endif
#else
    #if defined(BUILD_FONTRUNTIMECOMPOSITE_COMPOSITE)
        #define FONT_RUNTIME_COMPOSITE_API __attribute__((visibility("default")))
    #else
        #define FONT_RUNTIME_COMPOSITE_API
    #endif

    #if defined(BUILD_PLUGIN_Font) || defined(BUILD_PLUGIN_FONT) || defined(BUILD_FONTRUNTIMECOMPOSITE_COMPOSITE)
        #define FONT_API __attribute__((visibility("default")))
    #else
        #define FONT_API
    #endif
#endif

#endif // MQENGINE_FONT_API_H
