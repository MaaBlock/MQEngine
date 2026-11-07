#include "TechManager.h"
#include "../core/Tech.h"
#include "../core/Tech.hpp"
#include "../core/VertexLayouts.h"
#include "./PipelineRegistries.h"
#include <numeric>

#include "../../runtime/core/EngineGlobal.h"
#include "../../scene/data/Component.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/manager/SystemManager.h"

using namespace FCT;
namespace MQEngine
{
    TechManager::TechManager()
    {
        m_ctx = g_engineGlobal.ctx;
        m_dataManager = g_engineGlobal.dataManager;
        ensureInitialized();

        m_requestQueue.subscribe<Request::AddTech>([this](Request::AddTech& req) {
            return addTech(req.passName, std::move(req.tech));
        });

        m_requestQueue.subscribe<Request::AddTechDesc>([this](Request::AddTechDesc& req) {
            InitializePipelineRegistries();
            auto techRes = buildTech(req.techDesc);
            CHECK_STATUS(techRes);
            return addTech(req.techDesc.pass, std::move(techRes.value()));
        });

        m_requestQueue.subscribe<Request::BuildPipeline>([this](Request::BuildPipeline& req) {
            return buildFromPipeline(req.config);
        });
    }

    TechManager::~TechManager()
    {
        term();
    }

    void TechManager::init(Context* ctx, DataManager* dataManager)
    {
        if (ctx) {
            m_ctx = ctx;
        }
        if (dataManager) {
            m_dataManager = dataManager;
        }
        ensureInitialized();
    }

    void TechManager::ensureInitialized()
    {
        if (!m_ctx) {
            m_ctx = g_engineGlobal.ctx;
        }
        if (!m_dataManager) {
            m_dataManager = g_engineGlobal.dataManager;
        }
        if (m_ctx && !m_defaultDepthStencilState) {
            m_defaultDepthStencilState = m_ctx->createResource<FCT::DepthStencilState>();
            if (m_defaultDepthStencilState) {
                m_defaultDepthStencilState->depthCompareOp(FCT::CompareOp::LessOrEqual);
                m_defaultDepthStencilState->create();
            }
        }
        if (m_ctx && !m_reverseZDepthStencilState) {
            m_reverseZDepthStencilState = m_ctx->createResource<FCT::DepthStencilState>();
            if (m_reverseZDepthStencilState) {
                m_reverseZDepthStencilState->depthCompareOp(FCT::CompareOp::GreaterOrEqual);
                m_reverseZDepthStencilState->create();
            }
        }
    }

    void TechManager::term()
    {
        m_techToLayoutMap.clear();
        m_layouts.clear();
        m_passTechs.clear();
        m_techs.clear();
        m_subscribedPasses.clear();
        m_passOutputInfos.clear();
        m_rhiPasses.clear();

        if (m_defaultDepthStencilState) {
            m_defaultDepthStencilState->release();
            m_defaultDepthStencilState = nullptr;
        }
        if (m_reverseZDepthStencilState) {
            m_reverseZDepthStencilState->release();
            m_reverseZDepthStencilState = nullptr;
        }
    }

    void TechManager::setReverseZ(bool reverseZ)
    {
        m_reverseZ = reverseZ;
        auto it = m_rhiPasses.find("ObjectPass");
        if (it != m_rhiPasses.end() && it->second) {
            it->second->setClearDepth(m_reverseZ ? 0.0f : 1.0f);
        }
    }

    Status TechManager::addTech(const std::string& passName, Tech&& tech)
    {
        ZoneScoped();
        ensureInitialized();
        const std::string techName = tech.getName();
        if (techName.empty())
            return InvalidArgumentError("Tech名字不能为空");
        tech.setPassName(passName);
        auto valid = tech.valid();
        CHECK_STATUS(valid);
        if (m_techs.count(techName))
        {
            Tech* oldPtr = m_techs[techName].get();
            for (auto& [pass, vec] : m_passTechs)
                std::erase(vec, oldPtr);
            m_techToLayoutMap.erase(techName);
        }
        auto techUniquePtr = makeUnique<Tech>(std::move(tech));
        Tech* techRawPtr = techUniquePtr.get();
        m_techs[techName] = std::move(techUniquePtr);
        m_passTechs[passName].push_back(techRawPtr);
        auto ret = subscribeToPass(passName);
        CHECK_STATUS(ret);
        return OkStatus();
    }

    StatusOr<Tech> TechManager::buildTech(const PipelineTechDesc& techDesc) const
    {
        Tech tech;
        tech.setName(techDesc.name);
        tech.setPassName(techDesc.pass);

        auto vlRes = LayoutRegistry::instance().getVertexLayout(techDesc.vertexLayout);
        if (vlRes.ok()) {
            tech.addVertexLayout(vlRes.value());
        } else {
            tech.addVertexLayout(StandardMeshVertexLayout);
        }

        auto plRes = LayoutRegistry::instance().getPixelLayout(techDesc.pixelLayout);
        if (plRes.ok()) {
            tech.setPixelLayout(plRes.value());
        } else {
            auto defaultPl = LayoutRegistry::instance().getPixelLayout("StandardPixelLayout");
            if (defaultPl.ok()) {
                tech.setPixelLayout(defaultPl.value());
            }
        }

        if (!techDesc.vertexShader.empty()) {
            auto vsRes = ShaderSourceRegistry::instance().getShader(techDesc.vertexShader);
            CHECK_STATUS(vsRes);
            tech.setVertexShaderSource(vsRes.value());
        }

        if (!techDesc.pixelShader.empty()) {
            auto psRes = ShaderSourceRegistry::instance().getShader(techDesc.pixelShader);
            CHECK_STATUS(psRes);
            tech.setPixelShaderSource(psRes.value());
        }

        bool hasModelUniform = false;
        bool hasBoneUniform = false;
        for (const auto& slotName : techDesc.uniformSlots) {
            if (slotName == "ModelUniform" || slotName == "ModelUniformSlot") {
                hasModelUniform = true;
            }
            if (slotName == "BoneUniform" || slotName == "BoneUniformSlot") {
                hasBoneUniform = true;
            }
            auto slotRes = UniformSlotRegistry::instance().getSlot(slotName);
            if (slotRes.ok()) {
                tech.addUniformSlot(slotRes.value());
            } else {
                spdlog::warn("Technique {} 找不到指定 UniformSlot: {}", techDesc.name, slotName);
            }
        }
        if (hasModelUniform && !hasBoneUniform && techDesc.name != "SkyboxTech") {
            tech.addUniformSlot(BoneUniformSlot);
        }

        for (const auto& samplerName : techDesc.samplerSlots) {
            tech.addSamplerSlot(SamplerSlot(samplerName.c_str()));
        }

        for (const auto& texName : techDesc.textureSlots) {
            if (texName == "skyboxTexture") {
                tech.addTextureSlot(TextureSlot{texName, TextureType::TextureCube});
            } else {
                tech.addTextureSlot(TextureSlot{texName, TextureType::Texture2D});
            }
        }

        ComponentFilter filter;
        for (const auto& compName : techDesc.componentFilter.include) {
            auto infoRes = ComponentRegistry::instance().getTypeInfo(compName);
            if (infoRes.ok()) {
                filter.include_types.push_back(infoRes.value());
            } else {
                spdlog::warn("Technique {} 组件包含过滤器找不到类型: {}", techDesc.name, compName);
            }
        }
        for (const auto& compName : techDesc.componentFilter.exclude) {
            auto infoRes = ComponentRegistry::instance().getTypeInfo(compName);
            if (infoRes.ok()) {
                filter.exclude_types.push_back(infoRes.value());
            } else {
                spdlog::warn("Technique {} 组件排除过滤器找不到类型: {}", techDesc.name, compName);
            }
        }
        tech.setComponentFilter(filter);

        if (!techDesc.entityCallback.empty()) {
            auto cbRes = EntityCallbackRegistry::instance().getCallback(techDesc.entityCallback);
            if (cbRes.ok()) {
                tech.setEntityOperationCallback(cbRes.value());
            } else {
                spdlog::warn("Technique {} 找不到指定 EntityCallback: {}", techDesc.name, techDesc.entityCallback);
            }
        }

        SystemManager* sysMgr = g_engineGlobal.systemManager;
        if (!sysMgr) {
            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            sysMgr = engine->host->getSystemManager();
        }
        assert(sysMgr != nullptr && "SystemManager 不能为空");
        for (const auto& sysName : techDesc.bindedSystems) {
            auto* binded = dynamic_cast<BindedSystem*>(sysMgr->findSystem(sysName));
            if (binded) {
                tech.addBindedSystem(binded);
            } else {
                spdlog::warn("Technique {} 找不到绑定系统: {}", techDesc.name, sysName);
            }
        }

        return tech;
    }

    Status TechManager::buildFromPipeline(const PipelineConfig& config)
    {
        ZoneScoped();
        ensureInitialized();
        InitializePipelineRegistries();

        for (const auto& techDesc : config.techniques) {
            auto techRes = buildTech(techDesc);
            CHECK_STATUS(techRes);
            auto ret = addTech(techDesc.pass, std::move(techRes.value()));
            CHECK_STATUS(ret);
        }
        return OkStatus();
    }

    void TechManager::requestAddTech(const std::string& passName, Tech&& tech)
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.enqueue(Request::AddTech{passName, std::move(tech)});
    }

    void TechManager::requestAddTechDesc(const PipelineTechDesc& techDesc)
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.enqueue(Request::AddTechDesc{techDesc});
    }

    void TechManager::requestBuildFromPipeline(const PipelineConfig& config)
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requestQueue.enqueue(Request::BuildPipeline{config});
    }

    void TechManager::syncTicker()
    {
        ZoneScoped();
        std::lock_guard<std::mutex> lock(m_requestMutex);
        ensureInitialized();
        while (!m_requestQueue.empty()) {
            auto st = m_requestQueue.processNext();
            if (!st.ok()) {
                spdlog::error("TechManager::syncTicker 处理请求失败: {}", st.message());
            }
        }
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
        ZoneScoped();
        ensureInitialized();
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
        Tech& tech = *techIt->second;

        size_t vertexLayoutsHash = 0;
        for (const auto& vl : tech.getVertexLayouts())
        {
            boost::hash_combine(vertexLayoutsHash, vl.getHash());
        }

        LayoutKey key = { techName, tech.getPassName(), vertexLayoutsHash, tech.getPixelLayout().getHash() };

        auto layoutIt = m_layouts.find(key);
        if (layoutIt == m_layouts.end())
        {
            auto newLayout = makeUnique<FCT::Layout>(m_ctx, PassName(tech.getPassName()), tech.getVertexLayouts(), tech.getPixelLayout());
            layoutIt = m_layouts.emplace(key, std::move(newLayout)).first;
        }
        Layout* layout = layoutIt->second.get();

        for (const auto& slot : tech.getUniformSlots()) { layout->addUniformSlot(slot); }
        
        for (const auto& slot : tech.getSamplerSlots())
        {
            layout->addSamplerSlot(slot);
        }
        
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

    Status TechManager::subscribeToPass(const std::string& passName)
    {
        if (m_subscribedPasses.find(passName) != m_subscribedPasses.end())
            return OkStatus();
        m_subscribedPasses.insert(passName);

        auto ctx = m_ctx ? m_ctx : g_engineGlobal.ctx;
        if (!ctx) {
            return FailedPreconditionError("TechManager::subscribeToPass: Context 为空");
        }
        auto graph = ctx->getModule<FCT::RenderGraph>();

        ctx->pipeHub().passPipe.subscribe<PassInfo>(passName, [this, passName](PassInfo& passInfo) {
            m_passOutputInfos[passName] = passInfo.outputInfo;
            m_rhiPasses[passName] = &passInfo.pass;
            if (passName == "ObjectPass") {
                passInfo.pass.setClearDepth(m_reverseZ ? 0.0f : 1.0f);
            }
        });

        graph->subscribe(passName, [this, passName](PassSubmitEvent env)
        {
            ZoneScopedN("TechManager::PassSubmit");
            ZoneName(passName.c_str(), passName.size());
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
                ZoneScopedN("TechManager::ExecuteTech");
                ZoneName(tech->getName().c_str(), tech->getName().size());
                auto layout = this->getLayoutForTech(tech->getName());
                layout->begin();
                if (passName == "ObjectPass") {
                    layout->bindDepthStencilState(m_reverseZ ? m_reverseZDepthStencilState : m_defaultDepthStencilState);
                } else if (m_defaultDepthStencilState) {
                    layout->bindDepthStencilState(m_defaultDepthStencilState);
                }

                auto bindedSystem = tech->getSystems();
                for (auto* system : bindedSystem)
                {
                    system->bindUniforms(layout);
                    system->bindResources(layout);
                }
                
                layout->bindVertexShader(tech->getVertexShaderRef());
                if (!tech->getPixelShaderSource().empty())
                {
                    layout->bindPixelShader(tech->getPixelShaderRef());
                }

                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (dm) {
                    const auto& registries = dm->currentRegistries();
                    for (auto* registry : registries)
                    {
                        const auto& filter = tech->getComponentFilter();

                        entt::runtime_view runtime_view{};
                        bool hasEmpty = false;
                        for (const auto& type_info : filter.include_types)
                        {
                            auto* storage = registry->storage(type_info.hash());
                            if (storage)
                            {
                                runtime_view.iterate(*storage);
                            } else
                            {
                                hasEmpty = true;
                            }
                        }
                        if (hasEmpty)
                        {
                            continue;
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
                }

                layout->end();
            }
        });
        return OkStatus();
    }

}
