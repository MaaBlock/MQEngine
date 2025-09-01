#include "Tech.h"
#include "Tech.hpp"
#include <numeric> // For std::accumulate

#include "EngineGlobal.h"
#include "../data/Component.h"
#include "engine.h"


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
        
        // 自动订阅Pass
        subscribeToPass(passName);
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

    void TechManager::subscribeToPass(const std::string& passName)
    {
        if (m_subscribedPasses.find(passName) != m_subscribedPasses.end())
        {
            return;
        }
        m_subscribedPasses.insert(passName);

        auto ctx = g_engineGlobal.ctx;
        auto graph = ctx->getModule<FCT::RenderGraph>();

        ctx->pipeHub().passPipe.subscribe<PassInfo>(passName, [this, passName](PassInfo& passInfo) {
            m_passOutputInfos[passName] = passInfo.outputInfo;
        });

        graph->subscribe(passName, [this, passName](PassSubmitEvent env)
        {
            auto cmdBuf = env.cmdBuf;

            auto it = m_passOutputInfos.find(passName);
            if (it != m_passOutputInfos.end())
            {
                const auto& outputInfo = it->second;

                if (outputInfo.isWindow) {
                    if (outputInfo.window) {
                        auto autoViewport = outputInfo.window->getModule<WindowModule::AutoViewport>();
                        if (autoViewport) {
                            autoViewport->submit(cmdBuf);
                        } else {
                            cmdBuf->viewport(FCT::Vec2(0, 0), FCT::Vec2(outputInfo.width, outputInfo.height));
                            cmdBuf->scissor(FCT::Vec2(0, 0), FCT::Vec2(outputInfo.width, outputInfo.height));
                        }
                    }
                } else {
                    cmdBuf->viewport(FCT::Vec2(0, 0), FCT::Vec2(outputInfo.width, outputInfo.height));
                    cmdBuf->scissor(FCT::Vec2(0, 0), FCT::Vec2(outputInfo.width, outputInfo.height));
                }
            }

            auto techs = this->getTechsForPass(env.passName);
            for (auto tech : techs)
            {
                auto layout = this->getLayoutForTech(tech->getName());
                layout->begin();

                tech->executeBindCallback(layout);
                
                layout->bindVertexShader(tech->getVertexShaderRef());
                if (!tech->getPixelShaderSource().empty())
                {
                    layout->bindPixelShader(tech->getPixelShaderRef());
                }

                const auto& registries = g_engineGlobal.dataManager->currentRegistries();
                for (auto* registry : registries)
                {
                    const auto& filter = tech->getComponentFilter();

                    entt::runtime_view runtime_view{};

                    for (const auto& type_info : filter.include_types)
                    {
                        auto* storage = registry->storage(type_info.hash());
                        if (storage)
                        {
                            runtime_view.iterate(*storage);
                        }
                    }

                    for (const auto& type_info : filter.exclude_types)
                    {
                        auto* storage = registry->storage(type_info.hash());
                        if (storage)
                        {
                            runtime_view.exclude(*storage);
                        }
                    }

                    for (auto entity : runtime_view)
                    {
                        tech->executeEntityOperationCallback(*registry, entity, layout, env.cmdBuf);
                    }
                }

                layout->end();
            }
        });
    }


}