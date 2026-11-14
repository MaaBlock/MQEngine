/**
 * @file thirdparty.h
 * @brief DouyinMiniGame 插件通用第三方与引擎基础聚合头
 */
#ifndef MQENGINE_DOUYINMINIGAME_THIRDPARTY_H
#define MQENGINE_DOUYINMINIGAME_THIRDPARTY_H

#ifdef MQ_DOUYIN_WEBJS_ONLY
    #include "thirdparty/webjs_thirdparty.h"
#else
    #include <MQEngine/EnginePCH.h>
    #include <MQEngine/thirdparty/thirdparty.h>
    #include <FCT/Context/ShaderCrossCompiler.h>
    #include <FCT/Context/TransientResourcePool.h>
#endif

#endif // MQENGINE_DOUYINMINIGAME_THIRDPARTY_H
