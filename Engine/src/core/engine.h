﻿/*
 *@file engine.h
 */

#ifndef ENGINE_H
#define ENGINE_H
#include "../data/DataManager.h"
#include "../system/CameraSystem.h"
#include "../system/LightingSystem.h"
#include "../system/MatrixCacheSystem.h"
#include "../system/MeshCacheSystem.h"
#include "../system/ScriptSystem.h"
#include "../system/TextureCacheSystem.h"
#include "../system/ShininessSystem.h"
#include "../thirdparty/thirdparty.h"
#include "./EngineGlobal.h"
#include "Tech.h"
#include "VertexLayouts.h"
#include "application.h"
#include "layout.h"
#include "systemmanager.h"

namespace FCT
{
    class Layout;
}

#include "UniformSlots.h"

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
        FCT::VertexLayout vertexLayout = StandardMeshVertexLayout;
        FCT::PixelLayout pixelLayout = {
            FCT::VertexElement{FCT::VtxType::Custom,"srcpos",FCT::Format::R32G32B32A32_SFLOAT},
            FCT::VertexElement{FCT::VtxType::Custom,"shadowPos",FCT::Format::R32G32B32A32_SFLOAT},
            vertexLayout
        };
        FCT::Sampler* m_shadowSampler;
        FCT::Sampler* m_diffuseSampler;
        FCT::ShaderRef m_vs;
        FCT::ShaderRef m_ps;
        FCT::ShaderRef m_vsShadow;
        FCT::RHI::RasterizationPipeline* m_pipeline;
        FCT::UniformBuffer* m_uniform;
        FCT::RHI::ConstBuffer* m_constBuffer;
        FCT::AutoViewport* m_autoViewport;

        FCT::Layout* m_layout;
        FCT::Layout* m_shadowLayout;
        DataManager* m_dataManager;
        FCT::UniquePtr<CameraSystem> m_cameraSystem;
        FCT::UniquePtr<MeshCacheSystem> m_meshRenderSystem;
        FCT::UniquePtr<ScriptSystem> m_scriptSystem;
        FCT::UniquePtr<MatrixCacheSystem> m_matrixCacheSystem;
        FCT::UniquePtr<LightingSystem> m_lightingSystem;
        FCT::UniquePtr<TextureCacheSystem> m_textureRenderSystem;
        FCT::UniquePtr<ShininessSystem> m_shininessSystem;
        FCT::UniquePtr<TechManager> m_techManager;
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
#ifdef FCT_DEBUG
    ENGINE_API std::vector<FCT::_fct_object_t*> GetDebugObject();
#endif // DEBUG
}
#endif //ENGINE_H
