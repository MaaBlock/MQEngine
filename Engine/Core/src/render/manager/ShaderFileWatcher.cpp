#include "ShaderFileWatcher.h"
#include <spdlog/spdlog.h>

namespace MQEngine {

    ShaderFileWatcher::ShaderFileWatcher(FCT::JS::NodeEnvironment* env, ShaderSnippetManager* manager)
        : m_env(env), m_manager(manager) {
    }

    ShaderFileWatcher::~ShaderFileWatcher() {
        stopWatching();
    }

    void ShaderFileWatcher::startWatching(const std::string& path) {
        stopWatching();
        if (!m_env || !m_env->loop()) {
            return;
        }

        m_watchPath = path;
        m_fsEvent = new uv_fs_event_t{};
        int ret = uv_fs_event_init(m_env->loop(), m_fsEvent);
        if (ret != 0) {
            spdlog::error("Failed to init fs event: {}", uv_strerror(ret));
            delete m_fsEvent;
            m_fsEvent = nullptr;
            return;
        }
        
        m_fsEvent->data = this;

        ret = uv_fs_event_start(m_fsEvent, onFileChange, path.c_str(), UV_FS_EVENT_RECURSIVE);
        if (ret != 0) {
            spdlog::error("Failed to start fs event watching {}: {}", path, uv_strerror(ret));
            m_fsEvent->data = nullptr;
            uv_close(reinterpret_cast<uv_handle_t*>(m_fsEvent), [](uv_handle_t* h) {
                delete reinterpret_cast<uv_fs_event_t*>(h);
            });
            m_fsEvent = nullptr;
            return;
        }

        m_isWatching = true;
        spdlog::info("Started watching shader snippets at: {}", path);
    }

    void ShaderFileWatcher::stopWatching() {
        if (!m_fsEvent) {
            m_isWatching = false;
            return;
        }
        m_fsEvent->data = nullptr;
        if (m_isWatching) {
            uv_fs_event_stop(m_fsEvent);
            m_isWatching = false;
        }
        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(m_fsEvent))) {
            uv_close(reinterpret_cast<uv_handle_t*>(m_fsEvent), [](uv_handle_t* h) {
                delete reinterpret_cast<uv_fs_event_t*>(h);
            });
        }
        m_fsEvent = nullptr;
    }

    void ShaderFileWatcher::onFileChange(uv_fs_event_t* handle, const char* filename, int events, int status) {
        auto* watcher = static_cast<ShaderFileWatcher*>(handle->data);
        if (!watcher) {
            return;
        }
        if (status < 0) {
            spdlog::error("File watch error: {}", uv_strerror(status));
            return;
        }
        if (filename) {
            watcher->handleFileChange(filename);
        }
    }

    void ShaderFileWatcher::handleFileChange(const std::string& filename) {
        m_pendingFiles.insert(filename);
    }

    void ShaderFileWatcher::update() {
        if (m_pendingFiles.empty()) return;

        std::set<std::string> changes = std::move(m_pendingFiles);
        m_pendingFiles.clear();

        for (const auto& filename : changes) {
            if (!SAI::Path::hasAnyExtension(filename, {".hlsl", ".meta"})) {
                continue;
            }

            std::string stem = SAI::Path::stem(filename);
            std::string hlslPath = SAI::Path::join(m_watchPath, stem + ".hlsl");

            if (SAI::FileSystem::exists(hlslPath)) {
                spdlog::info("ShaderFileWatcher: Loading '{}' (triggered by '{}')", stem, filename);
                m_manager->loadSnippet(stem);
            } else {
                spdlog::info("ShaderFileWatcher: Unloading '{}' (triggered by '{}')", stem, filename);
                m_manager->unloadSnippet(stem);
            }
        }
    }
} // MQEngine
