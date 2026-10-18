#include "LogWindow.h"

#define TEXT(str) (const char*)u8##str

namespace MQEngine
{
    namespace
    {
        thread_local bool g_inSpdlogDispatch = false;

        /**
         * @brief 无锁 spdlog Sink，直接将日志推入 MPMC 无锁队列
         */
        class LockFreeEditorSink : public spdlog::sinks::sink
        {
        public:
            explicit LockFreeEditorSink(LogWindow* owner)
                : m_owner(owner)
            {
            }

            void log(const spdlog::details::log_msg& msg) override
            {
                LogWindow* owner = m_owner.load(std::memory_order_acquire);
                if (!owner)
                    return;

                g_inSpdlogDispatch = true;
                EditorLogLevel level = convertLevel(msg.level);
                std::string timestamp = formatLogTime(msg.time);
                std::string text(msg.payload.data(), msg.payload.size());
                owner->enqueueLog(level, std::move(timestamp), std::move(text));
                g_inSpdlogDispatch = false;
            }

            void flush() override {}
            void set_pattern(const std::string&) override {}
            void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

            void detach()
            {
                m_owner.store(nullptr, std::memory_order_release);
            }

        private:
            static EditorLogLevel convertLevel(spdlog::level::level_enum lvl)
            {
                switch (lvl)
                {
                case spdlog::level::trace:
                case spdlog::level::debug:
                    return EditorLogLevel::Debug;
                case spdlog::level::warn:
                    return EditorLogLevel::Warn;
                case spdlog::level::err:
                case spdlog::level::critical:
                    return EditorLogLevel::Error;
                default:
                    return EditorLogLevel::Info;
                }
            }

            static std::string formatLogTime(spdlog::log_clock::time_point tp)
            {
                auto timeT = spdlog::log_clock::to_time_t(tp);
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
                std::tm tmBuf{};
#ifdef _WIN32
                localtime_s(&tmBuf, &timeT);
#else
                localtime_r(&timeT, &tmBuf);
#endif
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                              tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, static_cast<int>(ms.count()));
                return std::string(buf);
            }

            std::atomic<LogWindow*> m_owner{nullptr};
        };

        /**
         * @brief 线程局部行缓冲的无锁流转发器，用于捕获 FCT::fout / std::cout 输出
         */
        class LockFreeStreamTeeBuf : public std::streambuf
        {
        public:
            LockFreeStreamTeeBuf(std::streambuf* origBuf, LogWindow* owner, std::string(*timeFn)())
                : m_origBuf(origBuf), m_owner(owner), m_timeFn(timeFn)
            {
            }

            void detach()
            {
                m_owner.store(nullptr, std::memory_order_release);
            }

        protected:
            int overflow(int c) override
            {
                if (c == traits_type::eof())
                    return traits_type::not_eof(c);

                char ch = static_cast<char>(c);
                appendChars(&ch, 1);
                return m_origBuf ? m_origBuf->sputc(ch) : c;
            }

            std::streamsize xsputn(const char* s, std::streamsize n) override
            {
                if (n > 0)
                    appendChars(s, static_cast<size_t>(n));
                return m_origBuf ? m_origBuf->sputn(s, n) : n;
            }

            int sync() override
            {
                return m_origBuf ? m_origBuf->pubsync() : 0;
            }

        private:
            void appendChars(const char* s, size_t n)
            {
                if (g_inSpdlogDispatch)
                    return;

                LogWindow* owner = m_owner.load(std::memory_order_acquire);
                if (!owner)
                    return;

                thread_local std::string tlsLineBuffer;
                for (size_t i = 0; i < n; ++i)
                {
                    char ch = s[i];
                    if (ch == '\r')
                        continue;
                    if (ch == '\n')
                    {
                        if (!tlsLineBuffer.empty())
                        {
                            owner->enqueueLog(EditorLogLevel::Info, m_timeFn(), std::move(tlsLineBuffer));
                            tlsLineBuffer.clear();
                        }
                    }
                    else
                    {
                        tlsLineBuffer.push_back(ch);
                    }
                }
            }

            std::streambuf* m_origBuf = nullptr;
            std::atomic<LogWindow*> m_owner{nullptr};
            std::string(*m_timeFn)() = nullptr;
        };
    }

    LogWindow::LogWindow()
    {
        installLogHooks();
    }

    LogWindow::~LogWindow()
    {
        uninstallLogHooks();
    }

    void LogWindow::installLogHooks()
    {
        auto sink = std::make_shared<LockFreeEditorSink>(this);
        m_spdlogSink = sink;
        if (auto logger = spdlog::default_logger())
        {
            auto& sinks = logger->sinks();
            if (std::find(sinks.begin(), sinks.end(), m_spdlogSink) == sinks.end())
                sinks.push_back(m_spdlogSink);
        }
        Engine::addLogSink(m_spdlogSink);

        m_origCoutBuf = std::cout.rdbuf();
        if (m_origCoutBuf)
        {
            auto tee = std::make_unique<LockFreeStreamTeeBuf>(m_origCoutBuf, this, &LogWindow::formatCurrentTime);
            std::cout.rdbuf(tee.get());
            m_coutTeeBuf = std::move(tee);
        }
    }

    void LogWindow::uninstallLogHooks()
    {
        if (m_spdlogSink)
        {
            static_cast<LockFreeEditorSink*>(m_spdlogSink.get())->detach();
            Engine::removeLogSink(m_spdlogSink);
            if (auto logger = spdlog::default_logger())
            {
                auto& sinks = logger->sinks();
                sinks.erase(std::remove(sinks.begin(), sinks.end(), m_spdlogSink), sinks.end());
            }
            m_spdlogSink.reset();
        }

        if (m_coutTeeBuf)
        {
            static_cast<LockFreeStreamTeeBuf*>(m_coutTeeBuf.get())->detach();
            if (m_origCoutBuf)
            {
                std::cout.rdbuf(m_origCoutBuf);
                m_origCoutBuf = nullptr;
            }
            m_coutTeeBuf.reset();
        }
    }

    void LogWindow::enqueueLog(EditorLogLevel level, std::string timestamp, std::string message)
    {
        m_pendingQueue.enqueue(EditorLogEntry{
            .level = level,
            .timestamp = std::move(timestamp),
            .message = std::move(message)
        });
    }

    void LogWindow::drainPendingLogs()
    {
        EditorLogEntry item;
        bool added = false;
        while (m_pendingQueue.try_dequeue(item))
        {
            switch (item.level)
            {
            case EditorLogLevel::Debug: ++m_debugCount; break;
            case EditorLogLevel::Info:  ++m_infoCount;  break;
            case EditorLogLevel::Warn:  ++m_warnCount;  break;
            case EditorLogLevel::Error: ++m_errorCount; break;
            }
            m_entries.push_back(std::move(item));
            added = true;

            if (m_entries.size() > m_maxEntries)
            {
                const auto& dropped = m_entries.front();
                switch (dropped.level)
                {
                case EditorLogLevel::Debug: if (m_debugCount > 0) --m_debugCount; break;
                case EditorLogLevel::Info:  if (m_infoCount > 0)  --m_infoCount;  break;
                case EditorLogLevel::Warn:  if (m_warnCount > 0)  --m_warnCount;  break;
                case EditorLogLevel::Error: if (m_errorCount > 0) --m_errorCount; break;
                }
                m_entries.pop_front();
            }
        }

        if (added && m_autoScroll)
            m_scrollToBottom = true;
    }

    void LogWindow::render()
    {
        ZoneScoped();
        drainPendingLogs();

        if (ImGui::Begin(TEXT("日志")))
        {
            renderToolbar();
            ImGui::Separator();
            renderLogList();
        }
        ImGui::End();
    }

    void LogWindow::renderToolbar()
    {
        if (ImGui::Button(TEXT("清空")))
        {
            m_entries.clear();
            m_debugCount = 0;
            m_infoCount = 0;
            m_warnCount = 0;
            m_errorCount = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT("复制全部")))
        {
            std::string allText;
            for (const auto& entry : m_entries)
            {
                if (!matchesFilter(entry))
                    continue;
                allText += "[" + entry.timestamp + "] [" + getLevelTag(entry.level) + "] " + entry.message + "\n";
            }
            ImGui::SetClipboardText(allText.c_str());
        }
        ImGui::SameLine();
        ImGui::Checkbox(TEXT("自动滚动"), &m_autoScroll);

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        char labelBuf[64];
        std::snprintf(labelBuf, sizeof(labelBuf), "Debug (%u)", m_debugCount);
        ImGui::Checkbox(labelBuf, &m_showDebug);
        ImGui::SameLine();

        std::snprintf(labelBuf, sizeof(labelBuf), "Info (%u)", m_infoCount);
        ImGui::Checkbox(labelBuf, &m_showInfo);
        ImGui::SameLine();

        std::snprintf(labelBuf, sizeof(labelBuf), "Warn (%u)", m_warnCount);
        ImGui::Checkbox(labelBuf, &m_showWarn);
        ImGui::SameLine();

        std::snprintf(labelBuf, sizeof(labelBuf), "Error (%u)", m_errorCount);
        ImGui::Checkbox(labelBuf, &m_showError);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##LogFilter", TEXT("搜索日志..."), m_searchBuffer, sizeof(m_searchBuffer));
    }

    void LogWindow::renderLogList()
    {
        ImGuiWindowFlags childFlags = ImGuiWindowFlags_HorizontalScrollbar;
        if (ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), ImGuiChildFlags_None, childFlags))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                const auto& entry = m_entries[i];
                if (!matchesFilter(entry))
                    continue;

                ImGui::PushID(static_cast<int>(i));
                ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, getLevelColor(entry.level));
                ImGui::TextUnformatted(getLevelTag(entry.level));
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.message.c_str());
                ImGui::PopStyleColor();

                if (ImGui::BeginPopupContextItem("LogEntryCtx"))
                {
                    if (ImGui::MenuItem(TEXT("复制此条日志")))
                    {
                        std::string line = "[" + entry.timestamp + "] " + getLevelTag(entry.level) + " " + entry.message;
                        ImGui::SetClipboardText(line.c_str());
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopStyleVar();

            if (m_scrollToBottom || (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
            {
                ImGui::SetScrollHereY(1.0f);
                m_scrollToBottom = false;
            }
        }
        ImGui::EndChild();
    }

    bool LogWindow::matchesFilter(const EditorLogEntry& entry) const
    {
        switch (entry.level)
        {
        case EditorLogLevel::Debug: if (!m_showDebug) return false; break;
        case EditorLogLevel::Info:  if (!m_showInfo)  return false; break;
        case EditorLogLevel::Warn:  if (!m_showWarn)  return false; break;
        case EditorLogLevel::Error: if (!m_showError) return false; break;
        }

        if (m_searchBuffer[0] != '\0')
        {
            if (entry.message.find(m_searchBuffer) == std::string::npos &&
                entry.timestamp.find(m_searchBuffer) == std::string::npos)
            {
                return false;
            }
        }
        return true;
    }

    ImVec4 LogWindow::getLevelColor(EditorLogLevel level)
    {
        switch (level)
        {
        case EditorLogLevel::Debug:
            return ImVec4(0.62f, 0.72f, 0.82f, 1.0f);
        case EditorLogLevel::Warn:
            return ImVec4(0.98f, 0.78f, 0.25f, 1.0f);
        case EditorLogLevel::Error:
            return ImVec4(0.96f, 0.36f, 0.36f, 1.0f);
        default:
            return ImVec4(0.90f, 0.92f, 0.94f, 1.0f);
        }
    }

    const char* LogWindow::getLevelTag(EditorLogLevel level)
    {
        switch (level)
        {
        case EditorLogLevel::Debug: return "[DEBUG]";
        case EditorLogLevel::Warn:  return "[WARN] ";
        case EditorLogLevel::Error: return "[ERROR]";
        default:                    return "[INFO] ";
        }
    }

    std::string LogWindow::formatCurrentTime()
    {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &timeT);
#else
        localtime_r(&timeT, &tmBuf);
#endif
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                      tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, static_cast<int>(ms.count()));
        return std::string(buf);
    }
}
