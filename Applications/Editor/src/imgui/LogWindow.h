#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    enum class EditorLogLevel : uint8_t
    {
        Debug = 0,
        Info,
        Warn,
        Error
    };

    struct EditorLogEntry
    {
        EditorLogLevel level = EditorLogLevel::Info;
        std::string timestamp;
        std::string message;
    };

    /**
     * @brief 编辑器日志窗口，采用无锁并发队列收集 spdlog 与标准输出流日志
     */
    class LogWindow
    {
    public:
        LogWindow();
        ~LogWindow();

        void render();
        void enqueueLog(EditorLogLevel level, std::string timestamp, std::string message);

    private:
        void installLogHooks();
        void uninstallLogHooks();
        void drainPendingLogs();
        void renderToolbar();
        void renderLogList();
        bool matchesFilter(const EditorLogEntry& entry) const;
        static ImVec4 getLevelColor(EditorLogLevel level);
        static const char* getLevelTag(EditorLogLevel level);
        static std::string formatCurrentTime();

        moodycamel::ConcurrentQueue<EditorLogEntry> m_pendingQueue;
        std::deque<EditorLogEntry> m_entries;
        std::shared_ptr<spdlog::sinks::sink> m_spdlogSink;
        std::streambuf* m_origCoutBuf = nullptr;
        std::unique_ptr<std::streambuf> m_coutTeeBuf;

        size_t m_maxEntries = 5000;
        uint32_t m_debugCount = 0;
        uint32_t m_infoCount = 0;
        uint32_t m_warnCount = 0;
        uint32_t m_errorCount = 0;

        bool m_showDebug = true;
        bool m_showInfo = true;
        bool m_showWarn = true;
        bool m_showError = true;
        bool m_autoScroll = true;
        bool m_scrollToBottom = false;
        char m_searchBuffer[256] = {};
    };
}

#endif // LOGWINDOW_H
