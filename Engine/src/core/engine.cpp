//
// Created by Administrator on 2025/7/21.
//
#include "../engineapi.h"
#define TEXT(str) (const char*)u8##str

namespace MQEngine
{
     struct Uniform
     {
     public:
         Uniform(Context* ctx,ConstLayout layout)
         {
             m_uniformBuffer = new FCT::UniformBuffer(layout);
             m_constBuffer = ctx->createResource<FCT::RHI::ConstBuffer>();
             m_constBuffer->layout(layout);
             m_constBuffer->buffer(m_uniformBuffer);
             m_constBuffer->create();
         }
         ~Uniform()
         {
             delete m_uniformBuffer;
         }
         template<typename T>
         void setValue(const char* name, const T& value)
         {
             m_uniformBuffer->setValue(name, value);
         }
         void update()
         {
             m_constBuffer->updataData();
         }
         operator FCT::RHI::ConstBuffer*() const
         {
             return m_constBuffer;
         }
     private:
         FCT::UniformBuffer* m_uniformBuffer;
         FCT::RHI::ConstBuffer* m_constBuffer;
     };
    Engine* Engine::s_instance = nullptr;
    const char* getEngineVersion()
    {
        return "0.0.1";
    }
    Engine& getEngine() {
        return Engine::getInstance();
    }
    Vec3 calculateUpVector(const Vec3& lightPos) {
        Vec3 forward = (Vec3(0,0,0) - lightPos).normalize();

        Vec3 worldUp = Vec3(0, 0, 1);

        if (abs(forward.dot(worldUp)) > 0.99f) {
            worldUp = Vec3(0, 1, 0);
        }

        Vec3 right = forward.cross(worldUp).normalize();
        Vec3 up = right.cross(forward).normalize();

        return up;
    }

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
        m_ps->resourceLayout(m_resourceLayout);
        m_ps->code(R"(

float calculateShadow(float4 shadowPos, float3 normal, float3 lightDir) {
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    projCoords = projCoords * 0.5 + 0.5;
    //projCoords.y = 1.0 - projCoords.y;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    float closestDepth = lightDepthImage.Sample(shadowSampler, projCoords.xy).r;
    float currentDepth = projCoords.z;

//    float cosTheta = dot(normal, lightDir);
//    float bias = 0.01 * tan(acos(cosTheta));
//    bias = clamp(bias, 0.0, 0.02);

    float shadow = (currentDepth) <= closestDepth ? 1.0 : 0.0;
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
    float shadow = calculateShadow(sIn.shadowPos, sIn.normal.xyz, lightDir.xyz);
    float3 finalColor = (sIn.color.xyz * (ambi +  shadow * (spec  + diff) * attenuation));
    sOut.target0 = float4(finalColor, 1.0);
    //float3 projCoords = sIn.shadowPos.xyz / sIn.shadowPos.w;
    //projCoords = projCoords * 0.5 + 0.5;
    //sOut.target0 = float4(projCoords.x,projCoords.y,projCoords.z,shadow);
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
    }

    void Engine::settingUpEnv()
    {
        m_systemManager.init();

        m_wnd = m_rt.createWindow(800,600,"MQ Engine");
        m_ctx = m_rt.createContext();
        m_ctx->create();
        m_ctx->addModule<ResourceManager>();
        m_wnd->enableDepthBuffer(Format::D32_SFLOAT_S8_UINT);
        m_wnd->bind(m_ctx);
        m_ctx->maxFrameInFlight(5);

        m_autoViewport = AutoViewport({800,600},{800,600});
        m_autoViewport.ctx(m_ctx);
        m_wnd->getCallBack()->addResizeCallback([this](Window* w,int width,int height)
        {
            m_autoViewport.resize(width,height);
        });
    }

    void Engine::settingUpImage()
    {
        //setting up depth buffer
        auto resourceManager = m_ctx->getModule<ResourceManager>();
        m_lightDepthImage = resourceManager->allocateTarget("lightDepthBuffer",{
            2048,
            2048,
            Samples::sample_1,
            Format::D32_SFLOAT,
            Flags(ImageUsage::Texture) | ImageUsage::DepthStencil
        });
    }

    void Engine::settingUpPass()
    {
        //setting up depth pass
        m_lightDepthPass = m_ctx->createResource<RHI::Pass>();
        m_lightDepthPass->enableClear(ClearType::depth,
          Vec4(0,0,0,1),1.0);
        m_lightDepthPass->depthStencil(m_lightDepthImage);

        //setting up pass
        m_imguiPass = m_ctx->createResource<RHI::Pass>();
        m_imguiPass->enableClear(ClearType::color | ClearType::depthStencil,
            Vec4(0,0,0,1));
        m_imguiPass->bindTarget(0,m_wnd->getCurrentTarget()->targetImage());
        m_imguiPass->depthStencil(m_wnd->getCurrentTarget()->depthStencilBuffer());
        m_imguiPass->bindTexture(0,m_lightDepthImage);
        //setting up pass group
        m_defaultPassGroup = m_ctx->createResource<RHI::PassGroup>();
        m_defaultPassGroup->addPass(
            {
                m_imguiPass,
                {
                    {
                        RHI::Pass::external,
                        PipelineStage::lateFragmentTests,
                        AccessFlag::depthStencilAttachmentWrite,
                        PipelineStage::fragmentShader,
                        AccessFlag::shaderRead
                    }
                },
                {
                    {
                        RHI::Pass::present
                    }
                }
            }
        );
        m_defaultPassGroup->create();
        m_shadowPassGroup =  m_ctx->createResource<RHI::PassGroup>();
        m_shadowPassGroup->addPass(
            {
                m_lightDepthPass,
                {
                    RHI::Pass::begin
                },
                {
                    {
                        RHI::Pass::external,
                        PipelineStage::fragmentShader,
                        AccessFlag::shaderRead,
                        PipelineStage::lateFragmentTests,
                        AccessFlag::depthStencilAttachmentWrite
                    }
                }
            }
        );
        m_shadowPassGroup->create();
    }

    void Engine::settingUpImgui()
    {
        m_imguiCtx = m_imguiModule.createContext(m_wnd,m_ctx);
        m_imguiCtx->create(m_imguiPass);
        m_imguiCtx->enableChinese();
    }

    void Engine::settingUpPipeline()
    {
        //RasterizationState* state = m_ctx->create;
        //setting up pipeline
        BlendState* blendState = m_ctx->createResource<BlendState>();
        blendState->blendEnable(false);
        blendState->create();
        m_pipeline = m_ctx->createTraditionPipeline();
        m_pipeline->vertexLayout(vertexLayout);
        m_pipeline->pixelLayout(pixelLayout);
        m_pipeline->addResources(m_vs);
        m_pipeline->addResources(m_ps);
        //m_pipeline->addResources(blendState);
        m_pipeline->bindPass(m_imguiPass);
        m_pipeline->create();
        m_shadowPipeline = m_ctx->createTraditionPipeline();
        m_shadowPipeline->vertexLayout(vertexLayout);
        m_shadowPipeline->pixelLayout(pixelLayout);
        m_shadowPipeline->addResources(m_vsShadow);
        //m_shadowPipeline->addResources(blendState);
        m_shadowPipeline->bindPass(m_lightDepthPass);
        m_shadowPipeline->create();
        m_shadowSampler = m_ctx->createResource<Sampler>();
        m_shadowSampler->setLinear();
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
        syncGraph.removeNode(RenderGraphSyncTicker_SwapJobQueueName);
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
            //m_resource->markAllDescriptorSetsNeedRecreate();
        });
    }

    void Engine::settingUpSubmitTicker()
    {
        //setting up ticker graph
        auto& tickerGraph = m_ctx->submitTickers();
        tickerGraph.removeNode(RenderGraphSubmitTickerName);
        tickerGraph.removeNode(RenderGraphExcutePassSubmitTickerName);
        tickerGraph["executeCmd"] = {
            [this]()
            {
                submitTick();
            },
            {},
            {SwapBufferSubmitTicker}
        };
        tickerGraph.update();
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
        m_shadowUniform->setValue("lightMvp",Mat4::LookAt(m_lightPos.xyz(),
            Vec3(0,0,0),Vec3(0,-1,0)) * Mat4::Ortho(-40.0f, 40.0f, -40.0f, 40.0f, 0.1f, 100.0f));
        m_shadowUniform->update();
    }

    void Engine::imguiLogicTick()
    {

        m_imguiCtx->push([this]()
        {
            ImGui::Begin("MQ Engine");
            ImGui::Text("Version: %s", getEngineVersion());
            ImGui::Text(TEXT("灯光类型:"));
            const char* lightTypes[] = {
                TEXT("点光源"),
                TEXT("方向光"),
                TEXT("聚光灯")
            };
            ImGui::Combo("##LightType", &m_lightType, lightTypes, 3);
            ImGui::Separator();

            if (m_lightType == 0 || m_lightType == 2) {
                ImGui::Text(TEXT("光源距离:"));
                if (ImGui::SliderFloat("##LightDistance", &m_lightDistance, 10.0f, 100.0f)) {
                    Vec3 currentDir = Vec3(m_lightPos.x, m_lightPos.y, m_lightPos.z).normalize();
                    m_lightPos = Vec4(currentDir * m_lightDistance, 1.0f);
                    m_baseUniform->setValue("lightPos", m_lightPos);
                    m_baseUniform->setValue("lightDirection", (-m_lightPos).normalize());
                }
                ImGui::Text(TEXT("当前位置: (%.1f, %.1f, %.1f)"), m_lightPos.x, m_lightPos.y, m_lightPos.z);
            }

            ImGui::Text(TEXT("环境光颜色:"));
            if (ImGui::ColorEdit3("##AmbientColor", m_ambientColor)) {
                m_baseUniform->setValue("ambientColor", Vec3(m_ambientColor[0], m_ambientColor[1], m_ambientColor[2]));
            }

            ImGui::Text(TEXT("漫反射颜色:"));
            if (ImGui::ColorEdit3("##DiffuseColor", m_diffuseColor)) {
                m_baseUniform->setValue("diffuseColor", Vec3(m_diffuseColor[0], m_diffuseColor[1], m_diffuseColor[2]));
            }

            ImGui::Text(TEXT("镜面反射颜色:"));
            if (ImGui::ColorEdit3("##SpecularColor", m_specularColor)) {
                m_baseUniform->setValue("specularColor", Vec3(m_specularColor[0], m_specularColor[1], m_specularColor[2]));
            }

            ImGui::Text(TEXT("光泽度:"));
            if (ImGui::SliderFloat("##Shininess", &m_shininess, 1.0f, 256.0f)) {
                m_baseUniform->setValue("shininess", m_shininess);
            }

            if (m_lightType == 0) {
                ImGui::Separator();
                ImGui::Text(TEXT("点光源衰减参数:"));

                if (ImGui::SliderFloat(TEXT("常数项"), &m_constant, 0.1f, 2.0f)) {
                    m_baseUniform->setValue("constant", m_constant);
                }

                if (ImGui::SliderFloat(TEXT("线性项"), &m_linearAttenuation, 0.01f, 0.5f)) {
                    m_baseUniform->setValue("linearAttenuation", m_linearAttenuation);
                }

                if (ImGui::SliderFloat(TEXT("二次项"), &m_quadratic, 0.001f, 0.1f)) {
                    m_baseUniform->setValue("quadratic", m_quadratic);
                }
            }

            if (m_lightType == 2)
            {
                ImGui::Separator();
                ImGui::Text(TEXT("聚光灯参数:"));

                if (ImGui::SliderFloat(TEXT("切光角度"), &m_cutOffAngle, 10.0f, 90.0f)) {
                    float cutOffRad = m_cutOffAngle * 3.1415926535f / 180.0f; // 转换为弧度
                    m_baseUniform->setValue("cutOff", cos(cutOffRad));
                }
            }

            ImGui::Separator();
            if (ImGui::Button(TEXT("重置为默认值"))) {
                initUniformValue();
            }

            ImGui::End();
        });
    }

    void Engine::submitTick()
    {
        m_shadowUniform->setValue("lightMvp", Mat4::LookAt(m_lightPos.xyz(), Vec3(0,0,0), Vec3(0,1,0)) * Mat4::Ortho(-50.0f, 50.0f, -50.0f, 50.0f, 1.0f, 80.0f));
        m_shadowUniform->update();
        auto cmdBuf = m_ctx->getCmdBuf(m_wnd, 0);
        cmdBuf->reset();
        cmdBuf->begin();
        m_shadowPassGroup->beginSubmit(cmdBuf);
        m_lightDepthPass->beginSubmit(cmdBuf);
        m_shadowPipeline->bind(cmdBuf);
        m_shadowResource->bind(cmdBuf,m_shadowPipeline);
        m_autoViewport.submit(cmdBuf);
        m_mesh->bind(cmdBuf);
        m_mesh->draw(cmdBuf);
        m_floor->bind(cmdBuf);
        m_floor->draw(cmdBuf);
        m_lightDepthPass->endSubmit();
        m_shadowPassGroup->endSubmit(cmdBuf);

        cmdBuf->barrier(
            m_lightDepthImage,
            ImageLayout::depthAttachmentOptimal,
            ImageLayout::shaderReadOnlyOptimal,
            PipelineStage::lateFragmentTests,
            PipelineStage::fragmentShader,
            AccessFlag::depthStencilAttachmentWrite,
            AccessFlag::shaderRead,
            ImageAspect::depth
        );

        m_defaultPassGroup->beginSubmit(cmdBuf);
        m_imguiPass->beginSubmit(cmdBuf);
        m_pipeline->bind(cmdBuf);
        m_resource->bind(cmdBuf,m_pipeline);
        m_autoViewport.submit(cmdBuf);
        m_mesh->bind(cmdBuf);
        m_mesh->draw(cmdBuf);
        m_floor->bind(cmdBuf);
        m_floor->draw(cmdBuf);
        m_imguiCtx->submit(cmdBuf);
        m_imguiPass->endSubmit();
        m_defaultPassGroup->endSubmit(cmdBuf);
        cmdBuf->end();
        cmdBuf->submit();
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
        imguiLogicTick();
        m_ctx->flush();
    }

    void Engine::init()
    {
        settingUpEnv();
        settingUpImage();
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
