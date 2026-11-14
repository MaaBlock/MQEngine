/**
 * @file MQEditorApi.h
 * @brief MQEditor 插件包下各独立 Composite DLL 的导出宏定义
 */
#ifndef MQENGINE_MQ_EDITOR_API_H
#define MQENGINE_MQ_EDITOR_API_H

#ifdef _WIN32
    #if defined(BUILD_SCENEVIEWERCOMPOSITE_COMPOSITE)
        #define SCENE_VIEWER_COMPOSITE_API __declspec(dllexport)
    #else
        #define SCENE_VIEWER_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_ASSETBROWSERCOMPOSITE_COMPOSITE)
        #define ASSET_BROWSER_COMPOSITE_API __declspec(dllexport)
    #else
        #define ASSET_BROWSER_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_SHADEREDITORCOMPOSITE_COMPOSITE)
        #define SHADER_EDITOR_COMPOSITE_API __declspec(dllexport)
    #else
        #define SHADER_EDITOR_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_RENDERGRAPHEDITORCOMPOSITE_COMPOSITE)
        #define RENDER_GRAPH_EDITOR_COMPOSITE_API __declspec(dllexport)
    #else
        #define RENDER_GRAPH_EDITOR_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_TRACYPROFILERCOMPOSITE_COMPOSITE)
        #define TRACY_PROFILER_COMPOSITE_API __declspec(dllexport)
    #else
        #define TRACY_PROFILER_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_REPLAYTIMELINECOMPOSITE_COMPOSITE)
        #define REPLAY_TIMELINE_COMPOSITE_API __declspec(dllexport)
    #else
        #define REPLAY_TIMELINE_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_NTCBAKERCOMPOSITE_COMPOSITE)
        #define NTC_BAKER_COMPOSITE_API __declspec(dllexport)
    #else
        #define NTC_BAKER_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_PROJECTPACKAGING_COMPOSITE)
        #define PROJECT_PACKAGING_COMPOSITE_API __declspec(dllexport)
    #else
        #define PROJECT_PACKAGING_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_MQ_EDITOR_PLUGIN) || defined(BUILD_MQEDITOR_PLUGIN)
        #define MQ_EDITOR_API __declspec(dllexport)
    #else
        #define MQ_EDITOR_API __declspec(dllimport)
    #endif
#else
    #define SCENE_VIEWER_COMPOSITE_API
    #define ASSET_BROWSER_COMPOSITE_API
    #define SHADER_EDITOR_COMPOSITE_API
    #define RENDER_GRAPH_EDITOR_COMPOSITE_API
    #define TRACY_PROFILER_COMPOSITE_API
    #define REPLAY_TIMELINE_COMPOSITE_API
    #define NTC_BAKER_COMPOSITE_API
    #define PROJECT_PACKAGING_COMPOSITE_API
    #define MQ_EDITOR_API
#endif

#endif // MQENGINE_MQ_EDITOR_API_H
