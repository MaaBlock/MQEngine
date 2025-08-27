#include "Tech.h"
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
        auto techName = tech.name;
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
        // 1. 检查是否已经为这个Tech准备好了Layout
        auto mapIt = m_techToLayoutMap.find(techName);
        if (mapIt != m_techToLayoutMap.end())
        {
            return mapIt->second;
        }

        // 2. 如果没有，则查找Tech的定义
        auto techIt = m_techs.find(techName);
        if (techIt == m_techs.end())
        {
            // 或者抛出异常，取决于你的错误处理策略
            return nullptr;
        }
        Tech& tech = techIt->second;

        // 3. 计算Layout的唯一键 (Key)
        // 使用 boost::hash_combine 组合所有vertexLayout的哈希值
        size_t vertexLayoutsHash = 0;
        for (const auto& vl : tech.vertexLayouts)
        {
            boost::hash_combine(vertexLayoutsHash, vl.getHash());
        }

        LayoutKey key = { vertexLayoutsHash, tech.pixelLayout.getHash() };

        // 4. 查找或创建共享的Layout对象
        auto layoutIt = m_layouts.find(key);
        if (layoutIt == m_layouts.end())
        {
            // 如果Layout不存在，则创建一个新的
            auto newLayout = std::make_unique<FCT::Layout>(m_ctx, tech.vertexLayouts, tech.pixelLayout);
            layoutIt = m_layouts.emplace(key, std::move(newLayout)).first;
        }
        Layout* layout = layoutIt->second.get();

        // 5. 动态地将此Tech所需的Slots和Shader添加到Layout中
        // Layout内部应处理重复添加的情况，确保幂等性
        for (const auto& slot : tech.uniformSlots) { layout->addUniformSlot(slot); }
        for (const auto& slot : tech.samplerSlots) { layout->addSamplerSlot(slot); }
        for (const auto& slot : tech.textureSlots)
        {
            layout->addTextureSlot(slot);
        }

        if (!tech.vs_source.empty())
        {
            tech.vs_ref = layout->cacheVertexShader(tech.vs_source);
        }
        if (!tech.ps_source.empty())
        {
            tech.ps_ref = layout->cachePixelShader(tech.ps_source);
        }

        // 6. 缓存Tech到Layout的映射关系并返回
        m_techToLayoutMap[techName] = layout;
        return layout;
    }
}