#ifndef SHADERGRAPHBROWSER_H
#define SHADERGRAPHBROWSER_H

#include "../imgui/ContentBrowser.h"

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
        std::string m_rootPath = "./Assets/ShaderGraph/";
        char m_newGraphName[128] = "NewGraph";
        std::string m_selectedGraph;
    };
}
#endif // SHADERGRAPHBROWSER_H