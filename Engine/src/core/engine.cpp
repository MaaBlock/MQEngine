#include "../engineapi.h"
#include "./Uniform.h"
#define TEXT(str) (const char*)u8##str

namespace MQEngine
{
    void Engine::settingUpShaders()
    {
        //settting up shder
        m_vs = m_ctx->createResource<ContextResource::VertexShader>();
        m_vs->addLayout(0,vertexLayout);
        m_vs->pixelLayout(pixelLayout);
        m_vs->addUniform(constLayout);
        m_vs->addUniform(m_shadowConstLayout);
        m_vs->code(R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = mul(mvp,sIn.position);
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.srcpos = sIn.position;
    sOut.shadowPos = mul(lightMvp, sIn.position);
    return sOut;
}
)");
        m_vs->create();
        m_ps = m_ctx->createResource<PixelShader>();
        m_ps->pixelLayout(pixelLayout);
        m_ps->addUniform(constLayout);
        m_ps->addUniform(m_shadowConstLayout);
        m_ps->resourceLayout(m_resourceLayout);
        m_ps->code(R"(
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
            float closestDepth = lightDepthImage.Sample(shadowSampler, projCoords.xy + offset).r;
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
    float shadow = calculateShadow(mul(lightMvp,sIn.srcpos), sIn.normal.xyz, lightDir.xyz);
    float3 finalColor = (sIn.color.xyz * (ambi +  shadow * (spec  + diff) * attenuation));
    sOut.target0 = float4(finalColor, 1.0);
    float3 projCoords = sIn.shadowPos.xyz / sIn.shadowPos.w;

    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;

    sOut.target1 = float4(projCoords.x,projCoords.y,projCoords.z,1);

    float closestDepth = lightDepthImage.Sample(shadowSampler, projCoords.xy).r;
    float currentDepth = projCoords.z;
    sOut.target2 = float4(closestDepth, currentDepth, abs(currentDepth - closestDepth) * 10.0, 1.0);
    return sOut;
}
)");
        m_ps->create();
        //setting up shader vs
        m_vsShadow = m_ctx->createResource<ContextResource::VertexShader>();
        m_vsShadow->addLayout(0,vertexLayout);
        m_vsShadow->pixelLayout(pixelLayout);
        m_vsShadow->addUniform(m_shadowConstLayout);
        m_vsShadow->code(R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = mul(lightMvp,sIn.position);
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.srcpos = sIn.position;
    return sOut;
}
)");
        m_vsShadow->create();
        m_psShadow = m_ctx->createResource<PixelShader>();
        m_psShadow->pixelLayout(pixelLayout);
        m_psShadow->code(R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    return sOut;
}
)");
        m_psShadow->create();
    }

    void Engine::settingUpEnv()
    {
        m_systemManager.init();
        m_wnd = m_rt.createWindow(800,600,"MQ Engine");
        m_ctx = m_rt.createContext();
        m_ctx->create();
        m_wnd->bind(m_ctx);
        m_autoViewport = m_wnd->getModule<WindowModule::AutoViewport>();
    }


    void Engine::settingUpPass()
    {
        auto graph = m_ctx->getModule<RenderGraph>();
        graph->addPass(
            "ShadowMapPass",
            EnablePassClear(ClearType::depth,1.0f),
            DepthStencil("DepthFromLigth0Image",
                2048,2048,
                Format::D32_SFLOAT,
                Samples::sample_1
                ));
        graph->addPass(
            "ObjectPass",
            Texture("DepthFromLigth0Image"),
            EnablePassClear(ClearType::color | ClearType::depthStencil,
                Vec4(0,0,0,1)),
            Target("mainWindowColor",m_wnd),
            Target("PosTarget"),
            Target("RetTarget"),
            DepthStencil("mainWindowDS",m_wnd)
            );
        graph->addPass(
            "ImguiPass",
            Target("mainWindowColor",m_wnd),
            Texture("PosTarget"),
            Texture("RetTarget"),
            DepthStencil("mainWindowDS",m_wnd)
            );
        graph->compile();
        m_lightDepthImage = graph->getImage("DepthFromLigth0Image");
        m_shadowPosTarget = graph->getImage("PosTarget");
        m_shadowRetTarget = graph->getImage("RetTarget");
    }


    void Engine::settingUpPipeline()
    {
        auto graph = m_ctx->getModule<RenderGraph>();
        //RasterizationState* state = m_ctx->create;
        //setting up pipeline

        BlendState* blendState = m_ctx->createResource<BlendState>();
        //blendState->blendEnable(false);
        blendState->create();
        m_pipeline = m_ctx->createTraditionPipeline();
        m_pipeline->vertexLayout(vertexLayout);
        m_pipeline->pixelLayout(pixelLayout);
        m_pipeline->addResources(m_vs);
        m_pipeline->addResources(m_ps);
        //m_pipeline->addResources(blendState);
        //m_pipeline->bindPass(m_objectPass);
        m_pipeline->bindPass(graph->getPass("ObjectPass"));
        m_pipeline->create();
        m_shadowPipeline = m_ctx->createTraditionPipeline();
        m_shadowPipeline->vertexLayout(vertexLayout);
        m_shadowPipeline->pixelLayout(pixelLayout);
        m_shadowPipeline->addResources(m_vsShadow);
        m_shadowPipeline->addResources(m_psShadow);
        //m_shadowPipeline->addResources(blendState);
        //m_shadowPipeline->bindPass(m_lightDepthPass);
        m_shadowPipeline->bindPass(graph->getPass("ShadowMapPass"));
        m_shadowPipeline->create();
        m_shadowSampler = m_ctx->createResource<Sampler>();
        m_shadowSampler->setShadowMap();
        m_shadowSampler->create();
    }

    void Engine::settingUpMesh()
    {
        //setting up mesh
        m_mesh = m_ctx->loadMesh(
            "ball.obj","ball",vertexLayout);
        m_floor = m_ctx->loadMesh(
            "ball.obj","floor",vertexLayout);
    }

    void Engine::settingUpUniforms()
    {
        m_baseUniform =  new Uniform(m_ctx,constLayout);
        m_shadowUniform = new Uniform(m_ctx,m_shadowConstLayout);
    }

    void Engine::settingPassResources()
    {
        m_resource = m_ctx->createResource<PassResource>();
        m_resource->addConstBuffer(*m_baseUniform);
        m_resource->addConstBuffer(*m_shadowUniform);
        m_resource->addTexture(m_lightDepthImage,m_resourceLayout.findTexture("lightDepthImage"));
        m_resource->addTexture(m_shadowPosTarget,m_resourceLayout.findTexture("shadowPos"));
        m_resource->addTexture(m_shadowRetTarget,m_resourceLayout.findTexture("shadowDepth"));
        m_resource->addSampler(m_shadowSampler, m_resourceLayout.findSampler("shadowSampler"));
        m_resource->bind(m_wnd);
        m_resource->create();
        m_shadowResource = m_ctx->createResource<PassResource>();
        m_shadowResource->addConstBuffer(*m_shadowUniform);
        m_shadowResource->bind(m_wnd);
        m_shadowResource->create();
    }

    void Engine::settingUpSync()
    {
        auto &syncGraph = m_ctx->syncTickers();
        syncGraph["syncImgui"] = {
            [this]()
            {
                m_imguiCtx->swapBuffer();
            },
            {},
            {}
        };
        syncGraph.update();
        m_wnd->swapchain()->subscribe<SwapchainEvent::Recreate>([this](SwapchainEvent::Recreate)
        {
            m_resource->markAllDescriptorSetsNeedRecreate();
        });
    }

    void Engine::settingUpSubmitTicker()
    {

        auto graph = m_ctx->getModule<RenderGraph>();
        graph->subscribe("ShadowMapPass",[this](PassSubmitEvent env)
        {
            auto cmdBuf = env.cmdBuf;
            m_shadowPipeline->bind(cmdBuf);
            m_shadowResource->bind(cmdBuf,m_shadowPipeline);
            cmdBuf->viewport(FCT::Vec2(0, 0), FCT::Vec2(2048, 2048));
            cmdBuf->scissor(FCT::Vec2(0, 0), FCT::Vec2(2048, 2048));
            m_mesh->bind(cmdBuf);
            m_mesh->draw(cmdBuf);
            m_floor->bind(cmdBuf);
            m_floor->draw(cmdBuf);
        });
        graph->subscribe("ObjectPass",[this](PassSubmitEvent env)
        {
            auto cmdBuf = env.cmdBuf;
            m_pipeline->bind(cmdBuf);
            m_resource->bind(cmdBuf,m_pipeline);
            m_autoViewport->submit(cmdBuf);
            m_mesh->bind(cmdBuf);
            m_mesh->draw(cmdBuf);
            m_floor->bind(cmdBuf);
            m_floor->draw(cmdBuf);
        });
        graph->subscribe("ImguiPass",[this](PassSubmitEvent env)
        {
            auto cmdBuf = env.cmdBuf;
            m_imguiCtx->submit(cmdBuf);
        });
        auto& tickerGraph = m_ctx->submitTickers();
    }

    void Engine::initUniformValue()
    {
        //init base uniform value
        Mat4 view = Mat4::LookAt(Vec3(40,40,-40), Vec3(0,0,0), Vec3(0,-1,0));
        Mat4 proj = Mat4::Perspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0);;
        Mat4 modelMatrix = Mat4();
        Mat4 mvpMatrix =  view * proj * modelMatrix;
        m_lightPos = Vec4(40,0,0,1);

        m_lightDistance = 40.0f;
        m_ambientColor[0] = m_ambientColor[1] = m_ambientColor[2] = 0.2f;
        m_diffuseColor[0] = m_diffuseColor[1] = m_diffuseColor[2] = 0.5f;
        m_specularColor[0] = m_specularColor[1] = m_specularColor[2] = 1.0f;
        m_shininess = 32.0f;
        m_constant = 1.0f;
        m_linearAttenuation = 0.09f;
        m_quadratic = 0.032f;
        m_cutOffAngle = 45.0f;

        m_baseUniform->setValue("mvp", mvpMatrix);
        m_baseUniform->setValue("viewPos", Vec4(40.0,40.0,-40.0,1.0));
        m_baseUniform->setValue("ambientColor", m_ambientColor);
        m_baseUniform->setValue("diffuseColor", m_diffuseColor);
        m_baseUniform->setValue("specularColor", m_specularColor);
        m_baseUniform->setValue("shininess", m_shininess);
        m_baseUniform->setValue("constant", m_constant);
        m_baseUniform->setValue("linearAttenuation", m_linearAttenuation);
        m_baseUniform->setValue("quadratic", m_quadratic);
        m_baseUniform->setValue("cutOff", cos(m_cutOffAngle * 3.1415926535f / 180.0f));

        m_baseUniform->update();

        //init shadow uniform value
        m_shadowUniform->setValue("lightMvp",
            Mat4::LookAt(m_lightPos.xyz(),
                Vec3(0,0,0),
                m_lightPos.xyz().cross(Vec3(0,0,-1))) *
                Mat4::Ortho(-5.0f,5.0f,
                    -5.0f, 5.0f,
                    1.0f, 7.5f));
        m_shadowUniform->update();
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
        m_baseUniform->setValue("lightPos", m_lightPos);
        m_baseUniform->setValue("lightDirection", (-m_lightPos).normalize());
        m_baseUniform->setValue("lightType",m_lightType);
        m_baseUniform->update();
        m_shadowUniform->setValue("lightMvp",
            Mat4::LookAt(m_lightPos.xyz(),
                Vec3(0,0,0),
                m_lightPos.xyz().cross(Vec3(0,0,-1))) *
                Mat4::Ortho(-5.0f,5.0f,
                    -5.0f, 5.0f,
                    1.0f, 7.5f));
        m_shadowUniform->update();
        imguiLogicTick();
        m_ctx->flush();
    }

    void Engine::init()
    {
        settingUpEnv();
        settingUpShaders();
        settingUpPass();
        settingUpImgui();
        settingUpShaders();
        settingUpPipeline();
        settingUpMesh();
        settingUpUniforms();
        initUniformValue();
        settingPassResources();
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
