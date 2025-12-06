#ifndef SHADERSNIPPETBROWSER_H
#define SHADERSNIPPETBROWSER_H

#include "../thirdparty/thirdparty.h"
#include "../imgui/ContentBrowser.h"
#include <filesystem>

namespace MQEngine {
    class ShaderSnippetBrowser : public IContentProvider {
    public:
        ShaderSnippetBrowser() = default;
        ~ShaderSnippetBrowser() override = default;

        void init() override;
        void render() override;
        std::string getName() const override { return "着色器片段"; }

    private:
        void openInVsCode(const std::filesystem::path& path);
        void createSnippet(const std::string& name);
        
        // m_snippetsDir is now handled by EngineGlobal.shaderSnippetManager
        std::string m_selectedSnippet;
        char m_newSnippetName[128] = "";
    };
}
#endif // SHADERSNIPPETBROWSER_H
