#include "ShaderFileWatcher.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace MQEngine {

    ShaderFileWatcher::ShaderFileWatcher(FCT::NodeEnvironment* env, ShaderSnippetManager* manager)
        : m_env(env), m_manager(manager) {
        m_fsEvent.data = this;
    }

    ShaderFileWatcher::~ShaderFileWatcher() {
        stopWatching();
    }

    void ShaderFileWatcher::startWatching(const std::string& path) {
        if (m_isWatching) {
            stopWatching();
        }

        m_watchPath = path;
        int ret = uv_fs_event_init(m_env->loop(), &m_fsEvent);
        if (ret != 0) {
            spdlog::error("Failed to init fs event: {}", uv_strerror(ret));
            return;
        }
        
        m_fsEvent.data = this;

        ret = uv_fs_event_start(&m_fsEvent, onFileChange, path.c_str(), UV_FS_EVENT_RECURSIVE);
        if (ret != 0) {
            spdlog::error("Failed to start fs event watching {}: {}", path, uv_strerror(ret));
            return;
        }

        m_isWatching = true;
        spdlog::info("Started watching shader snippets at: {}", path);
    }

    void ShaderFileWatcher::stopWatching() {
        if (m_isWatching) {
            uv_fs_event_stop(&m_fsEvent);
            // uv_close handles are usually closed when the loop stops or explicitly.
            // Since we are tying this to the Engine's lifecycle which manages the loop,
            // stopping the event should be sufficient to stop callbacks.
            m_isWatching = false;
        }
    }

    void ShaderFileWatcher::onFileChange(uv_fs_event_t* handle, const char* filename, int events, int status) {
        auto* watcher = static_cast<ShaderFileWatcher*>(handle->data);
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
            std::filesystem::path p(filename);
            std::string ext = p.extension().string();
            if (ext != ".hlsl" && ext != ".meta") continue;

            std::string stem = p.stem().string();
            // Construct full path to check existence. 
            // m_watchPath is "./res/snippets/" usually.
            
            std::filesystem::path hlslPath = std::filesystem::path(m_watchPath) / (stem + ".hlsl");

            if (std::filesystem::exists(hlslPath)) {
                spdlog::info("ShaderFileWatcher: Loading '{}' (triggered by '{}')", stem, filename);
                m_manager->loadSnippet(stem);
            } else {
                spdlog::info("ShaderFileWatcher: Unloading '{}' (triggered by '{}')", stem, filename);
                m_manager->unloadSnippet(stem);
            }
        }
    }
} // MQEngine
