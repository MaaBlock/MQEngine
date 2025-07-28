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
        lightDir = -lightDirection;
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
