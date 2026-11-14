/**
 * @file TerrainApi.h
 * @brief Terrain 插件包 DLL 导出宏定义
 */
#ifndef MQENGINE_TERRAIN_API_H
#define MQENGINE_TERRAIN_API_H

#ifdef _WIN32
    #if defined(BUILD_TERRAINRUNTIMECOMPOSITE_COMPOSITE)
        #define TERRAIN_RUNTIME_COMPOSITE_API __declspec(dllexport)
    #else
        #define TERRAIN_RUNTIME_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_TERRAINEDITORCOMPOSITE_COMPOSITE)
        #define TERRAIN_EDITOR_COMPOSITE_API __declspec(dllexport)
    #else
        #define TERRAIN_EDITOR_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_PLUGIN_TERRAIN)
        #define TERRAIN_API __declspec(dllexport)
    #else
        #define TERRAIN_API __declspec(dllimport)
    #endif
#else
    #define TERRAIN_RUNTIME_COMPOSITE_API
    #define TERRAIN_EDITOR_COMPOSITE_API
    #define TERRAIN_API
#endif

#endif // MQENGINE_TERRAIN_API_H
