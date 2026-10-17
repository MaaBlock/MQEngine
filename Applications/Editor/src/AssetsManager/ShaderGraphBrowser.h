#ifndef SHADERGRAPHBROWSER_H
#define SHADERGRAPHBROWSER_H

#include "../imgui/ContentBrowser.h"
#include <filesystem>

namespace MQEngine {
    class ShaderGraph;

    class ShaderGraphBrowser : public IContentProvider {
    public:
        ShaderGraphBrowser(ShaderGraph* shaderGraph);
        ~ShaderGraphBrowser() override = default;

        void init() override;
        void render() override;
        std::string getName() const override { return "Shader Graphs"; }

    private:
        void renderGraphList();
        void createNewGraph(const std::string& name);
        void loadGraph(const std::string& name);
        
        ShaderGraph* m_shaderGraph;
        std::filesystem::path m_rootPath = "./res/shadergraph/";
        char m_newGraphName[128] = "NewGraph";
        std::string m_selectedGraph;
    };
}
#endif // SHADERGRAPHBROWSER_H