/**
 * @file IRmlUiHotReloadBridge.h
 * @brief 跨 Composite DLL 的轻量级纯虚文档热重载桥接接口（无 RmlUi 头文件与导出符号耦合）
 */
#ifndef MQENGINE_GAME_HTML_UI_HOT_RELOAD_BRIDGE_H
#define MQENGINE_GAME_HTML_UI_HOT_RELOAD_BRIDGE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <string>

namespace MQEngine::GameHtmlUI
{
    class IRmlUiHotReloadBridge
    {
    public:
        virtual ~IRmlUiHotReloadBridge() = default;
        virtual Status reloadDocumentFromDisk(const std::string& documentId = "") = 0;
    };
} // namespace MQEngine::GameHtmlUI

#endif // MQENGINE_GAME_HTML_UI_HOT_RELOAD_BRIDGE_H
