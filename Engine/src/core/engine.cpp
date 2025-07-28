//
// Created by Administrator on 2025/7/21.
//
#include "../engineapi.h"
#define TEXT(str) (const char*)u8##str

namespace MQEngine
{
    Engine* Engine::s_instance = nullptr;
    const char* getEngineVersion()
    {
        return "0.0.1";
    }
    Engine& getEngine() {
        return Engine::getInstance();
    }

    void Engine::init()
    {
        m_systemManager.init();

        m_wnd = m_rt.createWindow(800,600,"MQ Engine");
        m_ctx = m_rt.createContext();
        m_ctx->create();
        m_wnd->enableDepthBuffer(Format::D32_SFLOAT_S8_UINT);
        m_wnd->bind(m_ctx);
        m_ctx->maxFrameInFlight(5);

        m_autoViewport = AutoViewport({800,600},{800,600});
        m_autoViewport.ctx(m_ctx);
        m_wnd->getCallBack()->addResizeCallback([this](Window* w,int width,int height)
        {
            m_autoViewport.resize(width,height);
        });

        //setting up pass
        m_imguiPass = m_ctx->createResource<RHI::Pass>();
        m_imguiPass->enableClear(ClearType::color | ClearType::depthStencil,
            Vec4(0,0,0,1));
        m_imguiPass->bindTarget(0,m_wnd->getCurrentTarget()->targetImage());
        m_imguiPass->depthStencil(m_wnd->getCurrentTarget()->depthStencilBuffer());
        m_defaultPassGroup = m_ctx->createResource<RHI::PassGroup>();
        m_defaultPassGroup->addPass(m_imguiPass);
        m_defaultPassGroup->create();

        m_imguiCtx = m_imguiModule.createContext(m_wnd,m_ctx);
        m_imguiCtx->create(m_imguiPass);
        m_imguiCtx->enableChinese();

        //settting up shader
        m_vs = m_ctx->createResource<ContextResource::VertexShader>();
        m_vs->addLayout(0,vertexLayout);
        m_vs->pixelLayout(pixelLayout);
        m_vs->addUniform(constLayout);
        m_vs->code(R"(
ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = mul(mvp,sIn.position);
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.srcpos = sIn.position;
    return sOut;
}
)");
        m_vs->create();
        m_ps = m_ctx->createResource<PixelShader>();
        m_ps->pixelLayout(pixelLayout);
        m_ps->addUniform(constLayout);
        m_ps->code(R"(
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
    float3 diff = max(dot(sIn.normal, lightDir), 0.0) * diffuseColor;
    float3 spec = pow(max(dot(sIn.normal, halfDir), 0.0), shininess) * specularColor;
    float3 ambi = ambientColor;
    float3 finalColor = (sIn.color.xyz * (ambi + (spec  + diff) * attenuation));
    sOut.target0 = float4(finalColor, 1.0);
    return sOut;
}
)");
        m_ps->create();

        //RasterizationState* state = m_ctx->create;
        //setting up pipeline
        m_pipeline = m_ctx->createTraditionPipeline();
        m_pipeline->vertexLayout(vertexLayout);
        m_pipeline->pixelLayout(pixelLayout);
        m_pipeline->addResources(m_vs);
        m_pipeline->addResources(m_ps);
        m_pipeline->bindPass(m_imguiPass);
        m_pipeline->create();

        //setting up mesh
        m_mesh = m_ctx->loadMesh(
            "ball.obj","ball",vertexLayout);
        m_floor = m_ctx->loadMesh(
            "ball.obj","floor",vertexLayout);
        m_uniform = new UniformBuffer(constLayout);

        Mat4 view = Mat4::LookAt(Vec3(40,40,-40), Vec3(0,0,0), Vec3(0,-1,0));
        Mat4 proj = Mat4::Perspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0);;

        Mat4 modelMatrix = Mat4();
        Mat4 mvpMatrix =  view * proj * modelMatrix;
        m_uniform->setValue("mvp", mvpMatrix);
        m_lightPos = Vec4(40,0,0,1);
        m_uniform->setValue("viewPos", Vec4(40.0,40.0,-40.0,1.0));
        m_uniform->setValue("ambientColor",Vec3(0.2f, 0.2f, 0.2f));
        m_uniform->setValue("diffuseColor",Vec3(0.5f, 0.5f, 0.5f));
        m_uniform->setValue("specularColor",Vec3(1.0f, 1.0f, 1.0f));
        m_uniform->setValue("shininess", 32.0f);
        m_uniform->setValue("constant",  1.0f);
        m_uniform->setValue("linearAttenuation",    0.09f);
        m_uniform->setValue("quadratic", 0.032f);
        m_uniform->setValue("cutOff",cos(3.1415926535 / 4));
        const void* data = m_uniform->getData();
        for (int i = 0; i < m_uniform->getSize() / 4;i++)
        {
            if (i % 4 == 0)
                fout << "[" << i * 4 << "]" << ": ";
            fout << std::setw(8) << *((const float*)((const char*)data + i * 4)) << " ";
            if (i % 4 == 3)
                fout << std::endl;
        }
        m_constBuffer = m_ctx->createResource<RHI::ConstBuffer>();
        m_constBuffer->buffer(m_uniform);
        m_constBuffer->layout(constLayout);
        m_constBuffer->create();
        m_constBuffer->mapData();

        m_resource = m_ctx->createResource<PassResource>();
        m_resource->addConstBuffer(m_constBuffer);
        m_resource->bind(m_wnd);
        m_resource->create();

        //setting up stencil buffer

        //setting up ticker graph
        auto& tickerGraph = m_ctx->submitTickers();
        tickerGraph.removeNode(RenderGraphSubmitTickerName);
        tickerGraph.removeNode(RenderGraphExcutePassSubmitTickerName);
        tickerGraph["executeCmd"] = {
            [this]()
            {
                auto cmdBuf = m_ctx->getCmdBuf(m_wnd, 0);
                cmdBuf->reset();
                cmdBuf->begin();

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
            },
            {},
            {SwapBufferSubmitTicker}
        };
        tickerGraph.update();

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
    }

    void Engine::loop()
    {
        while (m_wnd->isRunning())
        {
            static auto lastFrameTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime).count() / 1000000.0f;
            lastFrameTime = currentTime;
            Mat4 mat;
            mat.rotateY(deltaTime * 90);
            m_lightPos = mat * m_lightPos;
            m_uniform->setValue("lightPos", m_lightPos);
            m_uniform->setValue("lightDirection", (-m_lightPos).normalize());
            m_uniform->setValue("lightType",m_lightType);
            m_constBuffer->updataData();
            m_imguiCtx->push([this]()
            {
                ImGui::Begin("MQ Engine");
                ImGui::Text("Version: %s", getEngineVersion());
                ImGui::Text(TEXT("灯光类型:"));
                const char* lightTypes[] = {
                    TEXT("点"),
                    TEXT("方向"),
                    TEXT("聚光灯")
                };
                ImGui::Combo("##LightType", &m_lightType, lightTypes, 3);
                ImGui::Separator();
                if (m_lightType == 0 || m_lightType == 2) {
                    ImGui::Text(TEXT("光源距离:"));
                    static float lightDistance = 40.0f;
                    if (ImGui::SliderFloat("##LightDistance", &lightDistance, 10.0f, 100.0f)) {
                        Vec3 currentDir = Vec3(m_lightPos.x, m_lightPos.y, m_lightPos.z).normalize();
                        m_lightPos = Vec4(currentDir * lightDistance, 1.0f);
                        m_uniform->setValue("lightPos", m_lightPos);
                        m_uniform->setValue("lightDirection", (-m_lightPos).normalize());
                    }

                    ImGui::Text(TEXT("当前位置: (%.1f, %.1f, %.1f)"), m_lightPos.x, m_lightPos.y, m_lightPos.z);
                }
                ImGui::Text(TEXT("环境光颜色:"));
                static float ambientColor[3] = { 0.2f, 0.2f, 0.2f };
                if (ImGui::ColorEdit3("##AmbientColor", ambientColor)) {
                    m_uniform->setValue("ambientColor", Vec3(ambientColor[0], ambientColor[1], ambientColor[2]));
                }

                ImGui::Text(TEXT("漫反射颜色:"));
                static float diffuseColor[3] = { 0.5f, 0.5f, 0.5f };
                if (ImGui::ColorEdit3("##DiffuseColor", diffuseColor)) {
                    m_uniform->setValue("diffuseColor", Vec3(diffuseColor[0], diffuseColor[1], diffuseColor[2]));
                }

                ImGui::Text(TEXT("镜面反射颜色:"));
                static float specularColor[3] = { 1.0f, 1.0f, 1.0f };
                if (ImGui::ColorEdit3("##SpecularColor", specularColor)) {
                    m_uniform->setValue("specularColor", Vec3(specularColor[0], specularColor[1], specularColor[2]));
                }

                ImGui::Text(TEXT("光泽度:"));
                static float shininess = 32.0f;
                if (ImGui::SliderFloat("##Shininess", &shininess, 1.0f, 256.0f)) {
                    m_uniform->setValue("shininess", shininess);
                }
                if (m_lightType == 0) {
                    ImGui::Separator();
                    ImGui::Text(TEXT("点光源衰减参数:"));

                    static float constant = 1.0f;
                    if (ImGui::SliderFloat(TEXT("常数项"), &constant, 0.1f, 2.0f)) {
                        m_uniform->setValue("constant", constant);
                    }

                    static float linearAttenuation = 0.09f;
                    if (ImGui::SliderFloat(TEXT("线性项"), &linearAttenuation, 0.01f, 0.5f)) {
                        m_uniform->setValue("linearAttenuation", linearAttenuation);
                    }

                    static float quadratic = 0.032f;
                    if (ImGui::SliderFloat(TEXT("二次项"), &quadratic, 0.001f, 0.1f)) {
                        m_uniform->setValue("quadratic", quadratic);
                    }
                }

                if (m_lightType == 2)
                {
                    ImGui::Separator();
                    ImGui::Text(TEXT("聚光灯参数:"));

                    static float cutOffAngle = 45.0f;
                    if (ImGui::SliderFloat(TEXT("切光角度"), &cutOffAngle, 10.0f, 90.0f)) {
                        float cutOffRad = cutOffAngle * 3.1415926535f / 180.0f; // 转换为弧度
                        m_uniform->setValue("cutOff", cos(cutOffRad));
                    }
                }
                ImGui::Separator();
                if (ImGui::Button(TEXT("重置为默认值"))) {
                    ambientColor[0] = ambientColor[1] = ambientColor[2] = 0.2f;
                    diffuseColor[0] = diffuseColor[1] = diffuseColor[2] = 0.5f;
                    specularColor[0] = specularColor[1] = specularColor[2] = 1.0f;
                    shininess = 32.0f;

                    m_uniform->setValue("ambientColor", Vec3(0.2f, 0.2f, 0.2f));
                    m_uniform->setValue("diffuseColor", Vec3(0.5f, 0.5f, 0.5f));
                    m_uniform->setValue("specularColor", Vec3(1.0f, 1.0f, 1.0f));
                    m_uniform->setValue("shininess", 32.0f);
                    m_uniform->setValue("constant", 1.0f);
                    m_uniform->setValue("linearAttenuation", 0.09f);
                    m_uniform->setValue("quadratic", 0.032f);
                    m_uniform->setValue("cutOff", cos(3.1415926535f / 4));
                }

                ImGui::End();
            });
            m_ctx->flush();
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
