/*
 *@file engine.h
 */

#ifndef ENGINE_H
#define ENGINE_H
#include "../data/DataManager.h"
#include "../manager/RegistriesManager.h"
#include "../manager/TechManager.h"
#include "../system/CameraSystem.h"
#include "../system/LightingSystem.h"
#include "../system/MatrixCacheSystem.h"
#include "../system/MeshCacheSystem.h"
#include "../system/ScriptSystem.h"
#include "../system/ScriptCacheSystem.h"
#include "../system/SkyboxCacheSystem.h"
#include "../system/ShininessSystem.h"
#include "../system/TextureCacheSystem.h"
#include "../system/TextureSamplerSystem.h"
#include "../system/InputSystem.h"
#include "../system/ResourceActiveSystem.h"
#include "../thirdparty/thirdparty.h"
#include "./EngineGlobal.h"
#include "CreateApplication.h"
#include "Tech.h"
#include "VertexLayouts.h"
#include "application.h"
#include "layout.h"
#include "../manager/SystemManager.h"
#include "../manager/ShaderSnippetManager.h"
#include "../manager/ShaderGraphManager.h"
#include "../manager/ShaderFileWatcher.h"
#include "../data/ResourceLoader.h"

namespace FCT
{
    class Layout;
}

#include "UniformSlots.h"

namespace MQEngine {
    //class TextureSamplerSystem;
    class ENGINE_API Engine
    {
    public:
        Engine();
        ~Engine();
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        void init(Application* application);
        void term();
        void loop();
        static Engine& getInstance();
        static void RegisterApplicationFactory(CreateApplicationFn fn);
        
        SystemManager& getSystemManager() { return m_systemManager; }

        static Engine* s_instance;
    private:
        /**
         * @brief 提前让entt的hash表里拥有这些组件，防止后续再添加导致线程冲突
         */
        void registerEnttComponents();
        void registerShaderSnippets();
        void settingUpEnv();
        void settingUpTechs();
        void settingUpPass();
        void settingUpResources();
        void settingUpSync();
        void settingUpSubmitTicker();
        void initUniformValue();
        void logicTick();
        Application* m_application;
        FCT::Runtime m_rt;
        SystemManager m_systemManager;
        FCT::Window* m_wnd;
        FCT::Context* m_ctx;
        FCT::VertexLayout vertexLayout = StandardMeshVertexLayout;
        FCT::PixelLayout pixelLayout = {
            FCT::VertexElement{FCT::VtxType::Custom,"worldPos",FCT::Format::R32G32B32A32_SFLOAT},
            FCT::VertexElement{FCT::VtxType::Custom,"shadowPos",FCT::Format::R32G32B32A32_SFLOAT},
            vertexLayout
        };
        FCT::Sampler* m_shadowSampler;
        FCT::Sampler* m_diffuseSampler;
        FCT::StaticMesh<uint32_t>* m_skyboxMesh = nullptr;
        FCT::StaticMesh<uint32_t>* m_fullScreenMesh = nullptr;
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
        UniquePtr<CameraSystem> m_cameraSystem;
        UniquePtr<MeshCacheSystem> m_meshRenderSystem;
        UniquePtr<ScriptSystem> m_scriptSystem;
        UniquePtr<ScriptCacheSystem> m_scriptCacheSystem;
        UniquePtr<SkyboxCacheSystem> m_skyboxCacheSystem;
        UniquePtr<MatrixCacheSystem> m_matrixCacheSystem;
        UniquePtr<LightingSystem> m_lightingSystem;
        UniquePtr<TextureCacheSystem> m_textureRenderSystem;
        UniquePtr<ShininessSystem> m_shininessSystem;
        UniquePtr<TechManager> m_techManager;
        UniquePtr<RegistriesManager> m_registriesManager;
        UniquePtr<ShaderSnippetManager> m_shaderSnippetManager;
        UniquePtr<ShaderGraphManager> m_shaderGraphManager;
        UniquePtr<ShaderFileWatcher> m_shaderFileWatcher;
        UniquePtr<TextureSamplerSystem> m_textureSamplerSystem;
        UniquePtr<ResourceActiveSystem> m_resourceActiveSystem;
        UniquePtr<ResourceLoader> m_resourceLoader;
        InputSystem* m_inputSystem = nullptr;
        Uniform m_floorModelUniform;
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
