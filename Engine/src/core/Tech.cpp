#include "Tech.h"
#include "Tech.hpp"
#include <numeric> // For std::accumulate

#include "EngineGlobal.h"


using namespace FCT;
namespace MQEngine
{
    TechManager::TechManager()
    {
        m_ctx = g_engineGlobal.ctx;
        m_dataManager = g_engineGlobal.dataManager;
    }

    void TechManager::addTech(const std::string& passName, Tech&& tech)
    {
        tech.setPassName(passName);
        auto techName = tech.getName();
        auto it = m_techs.find(techName);
        if (it == m_techs.end())
        {
            it = m_techs.emplace(techName, std::move(tech)).first;
        }
        m_passTechs[passName].push_back(&it->second);
    }

    const std::vector<Tech*>& TechManager::getTechsForPass(const std::string& passName)
    {
        static std::vector<Tech*> empty;
        auto it = m_passTechs.find(passName);
        if (it != m_passTechs.end())
        {
            return it->second;
        }
        return empty;
    }

    Layout* TechManager::getLayoutForTech(const std::string& techName)
    {
        auto mapIt = m_techToLayoutMap.find(techName);
        if (mapIt != m_techToLayoutMap.end())
        {
            return mapIt->second;
        }

        auto techIt = m_techs.find(techName);
        if (techIt == m_techs.end())
        {
            return nullptr;
        }
        Tech& tech = techIt->second;

        size_t vertexLayoutsHash = 0;
        for (const auto& vl : tech.getVertexLayouts())
        {
            boost::hash_combine(vertexLayoutsHash, vl.getHash());
        }

        LayoutKey key = { tech.getPassName(), vertexLayoutsHash, tech.getPixelLayout().getHash() };

        auto layoutIt = m_layouts.find(key);
        if (layoutIt == m_layouts.end())
        {
            auto newLayout = std::make_unique<FCT::Layout>(m_ctx,PassName(tech.getPassName()), tech.getVertexLayouts(), tech.getPixelLayout());
            layoutIt = m_layouts.emplace(key, std::move(newLayout)).first;
        }
        Layout* layout = layoutIt->second.get();

        for (const auto& slot : tech.getUniformSlots()) { layout->addUniformSlot(slot); }
        for (const auto& slot : tech.getSamplerSlots()) { layout->addSamplerSlot(slot); }
        for (const auto& slot : tech.getTextureSlots())
        {
            layout->addTextureSlot(slot);
        }

        if (!tech.getVertexShaderSource().empty())
        {
            tech.setVertexShaderRef(layout->cacheVertexShader(tech.getVertexShaderSource()));
        }
        if (!tech.getPixelShaderSource().empty())
        {
            tech.setPixelShaderRef(layout->cachePixelShader(tech.getPixelShaderSource()));
        }

        m_techToLayoutMap[techName] = layout;
        return layout;
    }
}