#include "../engineapi.h"
#include "./Uniform.h"

namespace MQEngine
{
    void Engine::settingUpEnv()
    {
        m_systemManager.init();
        m_wnd = m_rt.createWindow(800,600,m_application->renderConfig().windowTitle);
        m_ctx = m_rt.createContext();
        m_ctx->create();
        m_wnd->bind(m_ctx);
        m_autoViewport = m_wnd->getModule<WindowModule::AutoViewport>();
        m_application->global.wnd = m_wnd;
        m_application->global.ctx = m_ctx;
        m_application->init();
    }


    void Engine::settingUpLayout()
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
            m_shadowConstLayout,
            PassName("ObjectPass"),
            SamplerSlot{"shadowSampler"}
        );
        m_ps = m_layout->allocatePixelShader(
            R"(
float calculateShadow(float4 shadowPos, float3 normal, float3 lightDir) {
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    //projCoords = projCoords * 0.5 + 0.5;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    float currentDepth = projCoords.z;

    float cosTheta = dot(normalize(normal), normalize(lightDir));
    cosTheta = clamp(cosTheta, 0.0, 1.0);

    float bias = max(0.025 * (1.0 - dot(normal, lightDir)), 0.0005);

    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2048.0, 2048.0);

    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float2 offset = float2(x, y) * texelSize;
            float closestDepth = DepthFromLigth0Image.Sample(shadowSampler, projCoords.xy + offset).r;
            shadow += (currentDepth - bias) <= closestDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;  // 9个采样点的平均值

    return shadow;
}

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    float4 lightDir;
    float attenuation = 1.0;
    switch(lightType) {
    case 0:
        float distance = length(sIn.srcpos - lightPos);
        lightDir = normalize(lightPos - sIn.srcpos);
        attenuation = 1.0 / (constant + linearAttenuation * distance +
                quadratic * (distance * distance));
        break;
    case 1:
        lightDir = -lightDirection;
        break;
    case 2:
        lightDir = normalize(lightPos - sIn.srcpos);
        if (dot(-lightDir, lightDirection) < cutOff) {
            attenuation = 0;
        }
        break;
    }
    float4 viewDir = normalize(viewPos - sIn.srcpos);
    float3 halfDir = normalize(viewDir + lightDir).xyz;
    float3 diff = max(dot(sIn.normal.xyz, lightDir.xyz), 0.0) * diffuseColor;
    float3 spec = pow(max(dot(sIn.normal, halfDir), 0.0), shininess) * specularColor;
    float3 ambi = ambientColor;
    float shadow = calculateShadow(lightMvp * sIn.srcpos, sIn.normal.xyz, lightDir.xyz);
    float3 finalColor = (sIn.color.xyz * (ambi +  shadow * (spec  + diff) * attenuation));
    sOut.target0 = float4(finalColor, 1.0);
    float3 projCoords = sIn.shadowPos.xyz / sIn.shadowPos.w;

    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;

    sOut.target1 = float4(projCoords.x,projCoords.y,projCoords.z,1);

    float closestDepth = DepthFromLigth0Image.Sample(shadowSampler, projCoords.xy).r;
    float currentDepth = projCoords.z;
    sOut.target2 = float4(closestDepth, currentDepth, abs(currentDepth - closestDepth) * 10.0, 1.0);
    return sOut;
})");
        m_vs = m_layout->allocateVertexShader((R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = mvp * sIn.position;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.srcpos = sIn.position;
    sOut.shadowPos = lightMvp * sIn.position;
    return sOut;
}
)"));
        m_shadowLayout = new Layout(
            m_ctx,
            vertexLayout,
            m_shadowConstLayout,
            PassName("ShadowMapPass")
            );
        m_vsShadow = m_shadowLayout->allocateVertexShader(R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = lightMvp * sIn.position ;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    return sOut;
}
)");
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
        m_ctx->flush();
    }

    void Engine::init(Application* application)
    {
        m_application = application;
        settingUpEnv();
        settingUpPass();
        settingUpLayout();
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
