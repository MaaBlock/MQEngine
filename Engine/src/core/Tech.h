
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "../Thirdparty/thirdparty.h"

namespace MQEngine
{
    class DataManager;

    // Tech 定义了一套完整的渲染技术
    struct Tech
    {
        std::string name;
        std::string vs_source;
        std::string ps_source;

        ShaderRef vs_ref;
        ShaderRef ps_ref;

        std::vector<FCT::VertexLayout> vertexLayouts;
        FCT::PixelLayout pixelLayout;
        std::vector<FCT::UniformSlot> uniformSlots;
        std::vector<FCT::SamplerSlot> samplerSlots;
        std::vector<FCT::TextureSlot> textureSlots;
    };


    // TechManager 负责管理Tech、Layout和Shader的生命周期
    class TechManager
    {
    public:
        TechManager();

        void addTech(const std::string& passName, Tech&& tech);

        const std::vector<Tech*>& getTechsForPass(const std::string& passName);

        FCT::Layout* getLayoutForTech(const std::string& techName);

    private:
        struct LayoutKey
        {
            size_t vertexLayoutHash;
            size_t pixelLayoutHash;

            bool operator<(const LayoutKey& other) const
            {
                if (vertexLayoutHash < other.vertexLayoutHash) return true;
                if (vertexLayoutHash > other.vertexLayoutHash) return false;
                return pixelLayoutHash < other.pixelLayoutHash;
            }
        };

        Context* m_ctx;
        std::map<std::string, Tech> m_techs;
        std::map<std::string, std::vector<Tech*>> m_passTechs;
        std::map<LayoutKey, std::unique_ptr<FCT::Layout>> m_layouts;
        std::map<std::string, FCT::Layout*> m_techToLayoutMap;
        DataManager* m_dataManager;
    };
}
