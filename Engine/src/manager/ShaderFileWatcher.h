#ifndef SHADERFILEWATCHER_H
#define SHADERFILEWATCHER_H

#include "../thirdparty/thirdparty.h"
#include <FCT_Node/NodeEnvironment.h>
#include "ShaderSnippetManager.h"
#include <uv.h>
#include <atomic>
#include <set>

namespace MQEngine {

    class ShaderFileWatcher {
    public:
        ShaderFileWatcher(FCT::NodeEnvironment* env, ShaderSnippetManager* manager);
        ~ShaderFileWatcher();

        void startWatching(const std::string& path);
        void stopWatching();
        void update();

    private:
        static void onFileChange(uv_fs_event_t* handle, const char* filename, int events, int status);
        void handleFileChange(const std::string& filename);

        FCT::NodeEnvironment* m_env;
        ShaderSnippetManager* m_manager;
        uv_fs_event_t m_fsEvent;
        bool m_isWatching = false;
        std::string m_watchPath;
        
        std::set<std::string> m_pendingFiles;
    };

} // MQEngine

#endif //SHADERFILEWATCHER_H
