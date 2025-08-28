#include "./Uniform.h"
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
        m_application->init();
        m_cameraSystem = makeUnique<CameraSystem>(m_ctx,m_dataManager);
        m_techManager = makeUnique<TechManager>();
    }

    void Engine::settingUpTechs()
    {
        m_layout = new Layout(
            m_ctx,
            vertexLayout,
            pixelLayout,
            UniformSlot(
                "base",
                UniformVar{UniformType::MVPMatrix,"mvp"},
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
            ),
            CameraUniformSlot,
            m_shadowConstLayout,
            PassName("ObjectPass"),
            SamplerSlot{"shadowSampler"}
        );
        m_ps = m_layout->cachePixelShader(
            LoadStringFromStringResource(g_engineShaderObjectPixel,g_engineShaderObjectPixelSize)
        );
        m_vs = m_layout->cacheVertexShader(
            LoadStringFromStringResource(g_engineShaderObjectVertex,g_engineShaderObjectVertexSize)
            );
        m_shadowLayout = new Layout(
            m_ctx,
            vertexLayout,
            m_shadowConstLayout,
            PassName("ShadowMapPass")
            );
        m_vsShadow = m_shadowLayout->cacheVertexShader(
            LoadStringFromStringResource(g_engineShaderShadowVertex,g_engineShaderShadowVertexSize)
            );
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
                //Samples::sample_1
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

        //setting up mesh
        m_mesh = m_ctx->loadMesh(
            "ball.obj","ball",vertexLayout);
        m_floor = m_ctx->loadMesh(
            "ball.obj","floor",vertexLayout);

        m_baseUniform = m_layout->allocateUniform("base");
        m_shadowUniform = m_layout->allocateUniform("ShadowUniform");
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
            m_shadowLayout->begin();
            m_shadowLayout->bindUniform(m_shadowUniform);
            m_shadowLayout->bindVertexShader(m_vsShadow);
            m_shadowLayout->drawMesh(cmdBuf, m_mesh);
            m_shadowLayout->drawMesh(cmdBuf, m_floor);
            m_shadowLayout->end();
        });
        graph->subscribe("ObjectPass",[this](PassSubmitEvent env)
        {
            auto cmdBuf = env.cmdBuf;
            cmdBuf->viewport({0,0},{1024,768});
            cmdBuf->scissor({0,0},{1024,768});
            m_layout->begin();
            m_layout->bindSampler("shadowSampler",m_shadowSampler);
            m_cameraSystem->bind(m_layout);
            m_layout->bindUniform(m_baseUniform);
            m_layout->bindUniform(m_shadowUniform);
            m_layout->bindVertexShader(m_vs);
            m_layout->bindPixelShader(m_ps);
            m_layout->drawMesh(cmdBuf, m_mesh);
            m_layout->drawMesh(cmdBuf, m_floor);
            m_layout->end();
        });
        RenderCallBack::SubscribePass callback;
        callback.graph = graph;
        m_application->renderCallBackDispatcher.trigger(callback);

    }

    void Engine::initUniformValue()
    {
        //init base uniform value
        Mat4 view = Mat4::LookAt(Vec3(40,40,40), Vec3(0,0,0), Vec3(0,1,0));
        Mat4 proj = Mat4::Perspective(90, 800.0f / 600.0f, 0.1f, 250.0);;
        Mat4 modelMatrix = Mat4();
        Mat4 mvpMatrix = proj *  view  * modelMatrix;
        m_lightPos = Vec4(200,0,0,1);

        m_lightDistance = 40.0f;
        m_ambientColor[0] = m_ambientColor[1] = m_ambientColor[2] = 0.2f;
        m_diffuseColor[0] = m_diffuseColor[1] = m_diffuseColor[2] = 0.5f;
        m_specularColor[0] = m_specularColor[1] = m_specularColor[2] = 1.0f;
        m_shininess = 32.0f;
        m_constant = 1.0f;
        m_linearAttenuation = 0.09f;
        m_quadratic = 0.032f;
        m_cutOffAngle = 45.0f;

        m_baseUniform.setValue("mvp", mvpMatrix);
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
        Mat4::Ortho(-75.0f,75.0f,
            -75.0f, 75.0f,
            1.0f, 300.0f) * Mat4::LookAt(m_lightPos.xyz(),
                Vec3(0,0,0),
                m_lightPos.xyz().cross(Vec3(0,0,-1))
                //Vec3(0,1,0)
                ));
        m_shadowUniform.update();
        m_application->logicTick();
        m_cameraSystem->update();
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
