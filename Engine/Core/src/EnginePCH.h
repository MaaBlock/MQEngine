//
// Created by Administrator on 2025/8/20.
//

#ifndef PCH_H
#define PCH_H
#ifdef _WIN32
    #ifdef BUILD_ENGINE
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
    #endif
#else
    #define ENGINE_API
#endif
#endif //PCH_H
