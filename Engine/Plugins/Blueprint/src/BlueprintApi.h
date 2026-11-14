/**
 * @file BlueprintApi.h
 * @brief Blueprint 插件包 DLL 导出宏定义
 */
#ifndef MQENGINE_BLUEPRINT_API_H
#define MQENGINE_BLUEPRINT_API_H

#ifdef _WIN32
    #if defined(BUILD_BLUEPRINTRUNTIMECOMPOSITE_COMPOSITE)
        #define BLUEPRINT_RUNTIME_COMPOSITE_API __declspec(dllexport)
    #else
        #define BLUEPRINT_RUNTIME_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_BLUEPRINTEDITORCOMPOSITE_COMPOSITE)
        #define BLUEPRINT_EDITOR_COMPOSITE_API __declspec(dllexport)
    #else
        #define BLUEPRINT_EDITOR_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_PLUGIN_BLUEPRINT)
        #define BLUEPRINT_API __declspec(dllexport)
    #else
        #define BLUEPRINT_API __declspec(dllimport)
    #endif
#else
    #define BLUEPRINT_RUNTIME_COMPOSITE_API
    #define BLUEPRINT_EDITOR_COMPOSITE_API
    #define BLUEPRINT_API
#endif

#endif // MQENGINE_BLUEPRINT_API_H
