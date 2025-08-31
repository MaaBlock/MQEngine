/*
 *@file engine.h
 */

#ifndef ENGINE_H
#define ENGINE_H
#include "../thirdparty/thirdparty.h"
#include "application.h"
#include "systemmanager.h"
#include "layout.h"
#include "../data/DataManager.h"
#include "../system/CameraSystem.h"
#include "../system/MeshRenderSystem.h"
#include "./EngineGlobal.h"
#include "Tech.h"

namespace FCT
{
    class Layout;
}




namespace MQEngine {
    class EngineScope;
    class ENGINE_API Engine
    {
    public:
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        void loop();
        static Engine& getInstance();
        friend class EngineScope;
    private:
        void init(Application* application);
        void term();
        void settingUpEnv();
        void settingUpTechs();
        void settingUpPass();
        void settingUpResources();
        void settingUpSync();
        void settingUpSubmitTicker();
        void initUniformValue();
        void logicTick();
        Engine() = default;
        ~Engine() = default;
        static Engine* s_instance;
        Application* m_application;
        FCT::Runtime m_rt;
        SystemManager m_systemManager;
        FCT::Window* m_wnd;
        FCT::Context* m_ctx;
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
        FCT::Sampler* m_shadowSampler;
        FCT::ShaderRef m_vs;
        FCT::ShaderRef m_ps;
        FCT::ShaderRef m_vsShadow;
        FCT::RHI::RasterizationPipeline* m_pipeline;
        FCT::UniformBuffer* m_uniform;
        FCT::RHI::ConstBuffer* m_constBuffer;
        FCT::Vec4 m_lightPos;
        FCT::AutoViewport* m_autoViewport;
        int m_lightType = 1;
        FCT::Uniform m_shadowUniform;
        FCT::Uniform m_baseUniform;
        FCT::Layout* m_layout;
        FCT::Layout* m_shadowLayout;
        float m_lightDistance = 40.0f;
        float m_ambientColor[3] = { 0.2f, 0.2f, 0.2f };
        float m_diffuseColor[3] = { 0.5f, 0.5f, 0.5f };
        float m_specularColor[3] = { 1.0f, 1.0f, 1.0f };
        float m_shininess = 32.0f;
        float m_constant = 1.0f;
        float m_linearAttenuation = 0.09f;
        float m_quadratic = 0.032f;
        float m_cutOffAngle = 45.0f;
        DataManager* m_dataManager;
        FCT::UniquePtr<CameraSystem> m_cameraSystem;
        FCT::UniquePtr<MeshRenderSystem> m_meshRenderSystem;
        FCT::UniquePtr<TechManager> m_techManager;
        FCT::Uniform m_meshModelUniform;
        FCT::Uniform m_floorModelUniform;
        
        // OutputInfo for passes
        FCT::OutputInfo m_shadowPassOutputInfo;
        FCT::OutputInfo m_objectPassOutputInfo;
        FCT::NodeEnvironment* m_nodeEnv;
    };
    /**
     * @return engine version
     */
    ENGINE_API const char* getEngineVersion();
    /**
     * @return Engine instance
     */
    ENGINE_API Engine& getEngine();
    ENGINE_API void OutputDebugObject();
    ENGINE_API std::vector<FCT::_fct_object_t*> GetDebugObject();
}
#endif //ENGINE_H
