/**
 * @file EditorBaseApi.h
 * @brief EditorBase 插件包下各独立 Composite DLL 的导出宏定义
 */
#ifndef MQENGINE_EDITOR_BASE_API_H
#define MQENGINE_EDITOR_BASE_API_H

#ifdef _WIN32
    #if defined(BUILD_IMGUICOMPOSITE_COMPOSITE)
        #define IMGUI_COMPOSITE_API __declspec(dllexport)
    #else
        #define IMGUI_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_EDITORUICOMPOSITE_COMPOSITE)
        #define EDITOR_UI_COMPOSITE_API __declspec(dllexport)
    #else
        #define EDITOR_UI_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_EDITOR_BASE_PLUGIN) || defined(BUILD_EDITORBASE_PLUGIN)
        #define EDITOR_BASE_API __declspec(dllexport)
    #else
        #define EDITOR_BASE_API __declspec(dllimport)
    #endif
#else
    #define IMGUI_COMPOSITE_API
    #define EDITOR_UI_COMPOSITE_API
    #define EDITOR_BASE_API
#endif

#endif // MQENGINE_EDITOR_BASE_API_H
