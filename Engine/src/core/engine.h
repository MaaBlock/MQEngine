/*
 *@file engine.h
 */

#ifndef ENGINE_H
#define ENGINE_H
#include "systemmanager.h"

namespace MQEngine {
    struct Uniform;
    class EngineScope;
    ENGINE_API class
    Engine
    {
    public:
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        void loop();
        static Engine& getInstance();
        friend class EngineScope;
    private:
        void init();
        void term();
        void settingUpShaders();
        void settingUpEnv();
        void settingUpImage();
        void settingUpPass();
        void settingUpImgui();
        void settingUpPipeline();
        void settingUpMesh();
        void settingUpUniforms();
        void settingPassResources();
        void settingUpSync();
        void settingUpSubmitTicker();
        void initUniformValue();
        void imguiLogicTick();
        void submitTick();
        void logicTick();
        Engine() = default;
        ~Engine() = default;
        static Engine* s_instance;
        FCT::Runtime m_rt;
        SystemManager m_systemManager;
        FCT::Window* m_wnd;
        FCT::Context* m_ctx;
        FCT::ImguiContext* m_imguiCtx;
        FCT::ImguiModule m_imguiModule;
        FCT::RHI::PassGroup* m_defaultPassGroup;
        FCT::RHI::PassGroup* m_shadowPassGroup;
        FCT::RHI::Pass* m_imguiPass;
        FCT::StaticMesh<uint32_t>* m_mesh;
        FCT::StaticMesh<uint32_t>* m_floor;
        FCT::VertexLayout vertexLayout = {
            FCT::VertexElement{FCT::VtxType::Color4f,"color"},
            FCT::VertexElement{FCT::VtxType::Position4f,"position"},
            FCT::VertexElement{FCT::VtxType::TexCoord2f,"texCoord"},
            FCT::VertexElement{FCT::VtxType::Normal3f,"normal"},
        };
        FCT::PixelLayout pixelLayout = {
            FCT::VertexElement{FCT::VtxType::Custom,"srcpos",FCT::Format::R32G32B32A32_SFLOAT},
            FCT::VertexElement{FCT::VtxType::Custom,"shadowPos",FCT::Format::R32G32B32A32_SFLOAT},
            vertexLayout
        };
        FCT::ConstLayout constLayout = {
            "base",
            FCT::ConstElement{FCT::ConstType::MVPMatrix,"mvp"},
            FCT::ConstElement{FCT::ConstType::Vec4,"lightPos"},
            FCT::ConstElement{FCT::ConstType::Vec4,"viewPos"},
            FCT::ConstElement{FCT::ConstType::Vec4,"lightDirection"},
            FCT::ConstElement{FCT::ConstType::Int,"lightType"},
            FCT::ConstElement{FCT::ConstType::Vec3,"ambientColor"},
            FCT::ConstElement{FCT::ConstType::Vec3,"diffuseColor"},
            FCT::ConstElement{FCT::ConstType::Vec3,"specularColor"},
            FCT::ConstElement{FCT::ConstType::Float,"shininess"},
            FCT::ConstElement{FCT::ConstType::Float,"constant"},
            FCT::ConstElement{FCT::ConstType::Float,"linearAttenuation"},
            FCT::ConstElement{FCT::ConstType::Float,"quadratic"},
            FCT::ConstElement{FCT::ConstType::Float,"cutOff"}
        };
        FCT::ResourceLayout m_resourceLayout = {
            FCT::TextureElement{"lightDepthImage"},
            FCT::SamplerElement{"shadowSampler"}
        };
        FCT::Sampler* m_shadowSampler;
        FCT::VertexShader* m_vs;
        FCT::PixelShader* m_ps;
        FCT::VertexShader* m_vsShadow;
        FCT::PixelShader* m_psShadow;
        FCT::ConstLayout m_shadowConstLayout = {
            "shadow",
            FCT::ConstElement{FCT::ConstType::MVPMatrix,"lightMvp"},
        };
        FCT::RHI::RasterizationPipeline* m_pipeline;
        FCT::UniformBuffer* m_uniform;
        FCT::RHI::ConstBuffer* m_constBuffer;
        FCT::PassResource* m_resource;
        FCT::PassResource* m_shadowResource;
        FCT::Vec4 m_lightPos;
        FCT::AutoViewport m_autoViewport;
        FCT::MutilBufferImage* m_mutilBufferImage;
        FCT::Image* m_lightDepthImage;
        FCT::RHI::Pass* m_lightDepthPass;
        FCT::RHI::RasterizationPipeline* m_shadowPipeline;
        int m_lightType = 0;
        Uniform* m_shadowUniform;
        Uniform* m_baseUniform;

        float m_lightDistance = 40.0f;
        float m_ambientColor[3] = { 0.2f, 0.2f, 0.2f };
        float m_diffuseColor[3] = { 0.5f, 0.5f, 0.5f };
        float m_specularColor[3] = { 1.0f, 1.0f, 1.0f };
        float m_shininess = 32.0f;
        float m_constant = 1.0f;
        float m_linearAttenuation = 0.09f;
        float m_quadratic = 0.032f;
        float m_cutOffAngle = 45.0f;
    };
    /**
     * @return engine version
     */
    ENGINE_API const char* getEngineVersion();
    /**
     * @return Engine instance
     */
    ENGINE_API Engine& getEngine();
}
#endif //ENGINE_H
