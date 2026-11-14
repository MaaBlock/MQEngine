/**
 * @file ToastNotification.h
 * @brief 编辑器浮动通知卡片结构
 */
#ifndef MQENGINE_EDITOR_BASE_TOAST_NOTIFICATION_H
#define MQENGINE_EDITOR_BASE_TOAST_NOTIFICATION_H

#include "UiSignals.h"
#include <string>

namespace MQEngine
{
    namespace EditorBase
    {
        struct ToastNotification
        {
            std::string title;
            std::string message;
            LogEntry::Level level = LogEntry::Level::Info;
            float remainingSec = 2.8f;
            float totalSec = 2.8f;
        };
    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_TOAST_NOTIFICATION_H
