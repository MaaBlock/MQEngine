#ifndef MQENGINE_EDITOR_UI_PLUGIN_API_H
#define MQENGINE_EDITOR_UI_PLUGIN_API_H

#ifdef _WIN32
    #ifdef BUILD_EDITORUIPLUGIN_PLUGIN
        #define EDITOR_UI_PLUGIN_API __declspec(dllexport)
    #else
        #define EDITOR_UI_PLUGIN_API __declspec(dllimport)
    #endif
#else
    #define EDITOR_UI_PLUGIN_API
#endif

#endif // MQENGINE_EDITOR_UI_PLUGIN_API_H
