#include "../engineapi.h"
#include "g_engineShaderObjectPixel.h"
#include "g_engineShaderObjectVertex.h"
#include "g_engineShaderShadowVertex.h"
namespace FCT
{
    std::string LoadStringFromStringResource(const unsigned char* resource, size_t size)
    {
        return std::string(reinterpret_cast<const char*>(resource), size);
    }
    constexpr UniformSlot LightUniformSlot {
        "LightUniform",
        UniformVar{UniformType::Vec4,"lightPos"},
        UniformVar{UniformType::Vec4,"viewPos"},
        UniformVar{UniformType::Vec4,"lightDirection"},
        UniformVar{UniformType::Int,"lightType"},
        UniformVar{UniformType::Vec3,"ambientColor"},
        UniformVar{UniformType::Vec3,"diffuseColor"},
        UniformVar{UniformType::Vec3,"specularColor"},
        UniformVar{UniformType::Float,"shininess"},
        UniformVar{UniformType::Float,"constant"},
        UniformVar{UniformType::Float,"linearAttenuation"},
        UniformVar{UniformType::Float,"quadratic"},
        UniformVar{UniformType::Float,"cutOff"}
    };

    UniformSlot ShadowUniformSlot = {
        "ShadowUniform",
        UniformVar{UniformType::MVPMatrix,"lightMvp"},
    };
    
    constexpr UniformSlot ModelUniformSlot {
        "ModelUniform",
        UniformVar{UniformType::ModelMatrix,"modelMatrix"}
    };
}
namespace MQEngine
{
    EngineGlobal g_engineGlobal;
    void Engine::settingUpEnv()
    {
        m_systemManager.init();
        m_wnd = m_rt.createWindow(800,600,m_application->renderConfig().windowTitle);
        m_ctx = m_rt.createContext();
        m_ctx->create();
        m_wnd->bind(m_ctx);
        m_dataManager->loadRes();
        m_autoViewport = m_wnd->getModule<WindowModule::AutoViewport>();
        m_application->global.wnd = m_wnd;
        m_application->global.ctx = m_ctx;
        m_application->global.dataManager = m_dataManager;
        m_application->global.runtime = &m_rt;
        g_engineGlobal.ctx = m_ctx;
        m_application->init();
        m_cameraSystem = makeUnique<CameraSystem>(m_ctx,m_dataManager);
        m_meshRenderSystem = makeUnique<MeshRenderSystem>(m_ctx,m_dataManager);
        m_techManager = makeUnique<TechManager>();
    }

    void Engine::settingUpTechs()
    {
        m_techManager->addTech("ObjectPass", {
            .name = "BasicTech",
            .vs_source = LoadStringFromStringResource(g_engineShaderObjectVertex,g_engineShaderObjectVertexSize),
            .ps_source = LoadStringFromStringResource(g_engineShaderObjectPixel,g_engineShaderObjectPixelSize),
            .vertexLayouts = {
                vertexLayout
            },
            .pixelLayout = pixelLayout,
            .uniformSlots = {
                LightUniformSlot,
                CameraUniformSlot,
                ShadowUniformSlot,
                ModelUniformSlot,
            },
            .samplerSlots = {
                SamplerSlot{"shadowSampler"}
            },
        });
        m_techManager->addTech("ShadowMapPass", {
            .name = "ShadowTech",
            .vs_source = LoadStringFromStringResource(
                g_engineShaderShadowVertex, g_engineShaderShadowVertexSize),
            .vertexLayouts = {
                vertexLayout
            },
            .pixelLayout = vertexLayout,
            .uniformSlots = {
                ShadowUniformSlot,
                ModelUniformSlot
            }
        });
    }

    void Engine::settingUpPass()
    {
        auto graph = m_ctx->getModule<RenderGraph>();
        graph->addPass(
            "ShadowMapPass",
            EnablePassClear(ClearType::depth,1.0f),
            DepthStencil("DepthFromLigth0Image",
                2048,2048,
                Format::D32_SFLOAT
                ));
        graph->addPass(
            "ObjectPass",
            Texture("DepthFromLigth0Image"),
            EnablePassClear(ClearType::color | ClearType::depthStencil,
                Vec4(0,0,0,1)),
            Target("SceneColorTarget",1024, 768, Format::R8G8B8A8_UNORM),
            Target("PosTarget"),
            Target("RetTarget"),
            DepthStencil("SceneDepthTarget",Format::D32_SFLOAT)
            );
        {
            RenderCallBack::SettingUpPass callback;
            callback.graph = graph;
            m_application->renderCallBackDispatcher.trigger(callback);
        }
        graph->compile();
        {
            RenderCallBack::KeepImage callback;
            callback.graph = graph;
            m_application->renderCallBackDispatcher.trigger(callback);
        }
    }

    void Engine::settingUpResources()
    {
        m_shadowSampler = m_ctx->createResource<Sampler>();
        m_shadowSampler->setShadowMap();
        m_shadowSampler->create();

        m_baseUniform = Uniform(m_ctx,LightUniformSlot);
        // m_layout->allocateUniform("LightUniform");
        //m_shadowUniform = m_layout->allocateUniform("ShadowUniform");
        m_shadowUniform = Uniform(m_ctx,ShadowUniformSlot);
        m_meshModelUniform = Uniform(m_ctx,ModelUniformSlot);
        m_floorModelUniform = Uniform(m_ctx,ModelUniformSlot);
    }


    void Engine::settingUpSync()
    {
        auto &syncGraph = m_ctx->syncTickers();
        RenderCallBack::SettingSync callback{
            .graph = syncGraph
        };
        m_application->renderCallBackDispatcher.trigger(callback);
        syncGraph.update();
    }

    void Engine::settingUpSubmitTicker()
    {
        auto graph = m_ctx->getModule<RenderGraph>();
        graph->subscribe("ShadowMapPass",[this](PassSubmitEvent env)
        {
            auto cmdBuf = env.cmdBuf;
            cmdBuf->viewport(FCT::Vec2(0, 0), FCT::Vec2(2048, 2048));
            cmdBuf->scissor(FCT::Vec2(0, 0), FCT::Vec2(2048, 2048));
            auto techs = m_techManager->getTechsForPass(env.passName);
            for (auto tech : techs)
            {
                auto layout = m_techManager->getLayoutForTech(tech->name);
                layout->begin();
                layout->bindUniform(m_shadowUniform);
                layout->bindVertexShader(tech->vs_ref);
                
                // 渲染MeshRenderSystem收集到的所有mesh
                const auto& renderData = m_meshRenderSystem->getRenderData();
                for (const auto& meshData : renderData)
                {
                    if (meshData.mesh)
                    {
                        // 暂时为每个mesh绑定默认的单位矩阵ModelUniform
                        layout->bindUniform(m_meshModelUniform);
                        layout->drawMesh(cmdBuf, meshData.mesh);
                    }
                }
            }
        });
        graph->subscribe("ObjectPass",[this](PassSubmitEvent env)
        {
            auto cmdBuf = env.cmdBuf;
            cmdBuf->viewport({0,0},{1024,768});
            cmdBuf->scissor({0,0},{1024,768});
            auto techs = m_techManager->getTechsForPass(env.passName);
            for (auto tech : techs)
            {
                auto layout = m_techManager->getLayoutForTech(tech->name);
                layout->begin();
                layout->bindSampler("shadowSampler",m_shadowSampler);
                m_cameraSystem->bind(layout);
                layout->bindUniform(m_baseUniform);
                layout->bindUniform(m_shadowUniform);
                layout->bindVertexShader(tech->vs_ref);
                layout->bindPixelShader(tech->ps_ref);
                
                // 渲染MeshRenderSystem收集到的所有mesh
                const auto& renderData = m_meshRenderSystem->getRenderData();
                for (const auto& meshData : renderData)
                {
                    if (meshData.mesh)
                    {
                        // 暂时为每个mesh绑定默认的单位矩阵ModelUniform
                        layout->bindUniform(m_meshModelUniform);
                        layout->drawMesh(cmdBuf, meshData.mesh);
                    }
                }

                layout->end();
            }
        });
        RenderCallBack::SubscribePass callback;
        callback.graph = graph;
        m_application->renderCallBackDispatcher.trigger(callback);

    }

    void Engine::initUniformValue()
    {
        //init base uniform value
        m_lightPos = Vec4(20,0,0,1);

        m_lightDistance = 40.0f;
        m_ambientColor[0] = m_ambientColor[1] = m_ambientColor[2] = 0.2f;
        m_diffuseColor[0] = m_diffuseColor[1] = m_diffuseColor[2] = 0.5f;
        m_specularColor[0] = m_specularColor[1] = m_specularColor[2] = 1.0f;
        m_shininess = 32.0f;
        m_constant = 1.0f;
        m_linearAttenuation = 0.09f;
        m_quadratic = 0.032f;
        m_cutOffAngle = 45.0f;

        m_baseUniform.setValue("viewPos", Vec4(40.0,40.0,40.0,1.0));
        m_baseUniform.setValue("ambientColor", m_ambientColor);
        m_baseUniform.setValue("diffuseColor", m_diffuseColor);
        m_baseUniform.setValue("specularColor", m_specularColor);
        m_baseUniform.setValue("shininess", m_shininess);
        m_baseUniform.setValue("constant", m_constant);
        m_baseUniform.setValue("linearAttenuation", m_linearAttenuation);
        m_baseUniform.setValue("quadratic", m_quadratic);
        m_baseUniform.setValue("cutOff", cos(m_cutOffAngle * 3.1415926535f / 180.0f));

        m_baseUniform.update();

        //init shadow uniform value
        m_shadowUniform.setValue("lightMvp",
            Mat4::Ortho(-25.0f,25.0f,
                -25.0f, 25.0f,
                1.0f, 25.0f) * Mat4::LookAt(m_lightPos.xyz(),
                Vec3(0,0,0),
                m_lightPos.xyz().cross(Vec3(0,0,-1))));
        m_shadowUniform.update();
        
        //init model uniform values
        m_meshModelUniform.setValue("modelMatrix", FCT::Mat4());
        m_meshModelUniform.update();
        m_floorModelUniform.setValue("modelMatrix", FCT::Mat4());
        m_floorModelUniform.update();
    }


    void Engine::logicTick()
    {
        static auto lastFrameTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime).count() / 1000000.0f;
        lastFrameTime = currentTime;
        Mat4 mat;
        mat.rotateZ(deltaTime * 90);
        m_lightPos = mat * m_lightPos;
        m_baseUniform.setValue("lightPos", m_lightPos);
        m_baseUniform.setValue("lightDirection", (-m_lightPos).normalize());
        m_baseUniform.setValue("lightType",m_lightType);
        m_baseUniform.update();
        m_shadowUniform.setValue("lightMvp",
        Mat4::Ortho(-25.0f,25.0f,
            -25.0f, 25.0f,
            1.0f, 50.0f) * Mat4::LookAt(m_lightPos.xyz(),
                Vec3(0,0,0),
                m_lightPos.xyz().cross(Vec3(0,0,-1))
                //Vec3(0,1,0)
                ));
        m_shadowUniform.update();
        m_application->logicTick();
        m_cameraSystem->update();
        m_meshRenderSystem->update();
        m_ctx->flush();
    }

    void Engine::init(Application* application)
    {
        m_dataManager = new DataManager();
        g_engineGlobal.dataManager = m_dataManager;
        m_application = application;
        settingUpEnv();
        settingUpPass();
        settingUpTechs();
        settingUpResources();
        initUniformValue();
        settingUpSubmitTicker();
        settingUpSync();
    }

    void Engine::loop()
    {
        while (m_wnd->isRunning())
        {
            logicTick();
        }
    }

    void Engine::term()
    {
        m_ctx->release();
        m_wnd->release();
        m_systemManager.term();
    }

    Engine& Engine::getInstance() {
        return *s_instance;
    }
}
