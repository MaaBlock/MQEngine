/**
 * @file GameHtmlUiApi.h
 * @brief GameHtmlUI 插件包下各独立 Composite DLL 的导出宏定义
 */
#ifndef MQENGINE_GAME_HTML_UI_API_H
#define MQENGINE_GAME_HTML_UI_API_H

#ifdef _WIN32
    #if defined(BUILD_RMLUICOMPOSITE_COMPOSITE)
        #define RML_UI_COMPOSITE_API __declspec(dllexport)
    #else
        #define RML_UI_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_HTMLUIEDITORCOMPOSITE_COMPOSITE)
        #define HTML_UI_EDITOR_COMPOSITE_API __declspec(dllexport)
    #else
        #define HTML_UI_EDITOR_COMPOSITE_API __declspec(dllimport)
    #endif

    #if defined(BUILD_GAME_HTML_UI_PLUGIN) || defined(BUILD_GAMEHTMLUI_PLUGIN)
        #define GAME_HTML_UI_API __declspec(dllexport)
    #else
        #define GAME_HTML_UI_API __declspec(dllimport)
    #endif
#else
    #define RML_UI_COMPOSITE_API
    #define HTML_UI_EDITOR_COMPOSITE_API
    #define GAME_HTML_UI_API
#endif

#endif // MQENGINE_GAME_HTML_UI_API_H
