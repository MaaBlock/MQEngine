#ifndef MQENGINE_EFFECT_EFFECTOR_H
#define MQENGINE_EFFECT_EFFECTOR_H

#include "../../base/CoreEngineModule.h"
#include "../../render/core/VertexLayouts.h"
#include "../../render/manager/ShaderFileWatcher.h"
#include "../../render/manager/ShaderGraphManager.h"
#include "../../render/manager/ShaderSnippetManager.h"
#include "../../render/manager/TechManager.h"
#include "../../thirdparty/thirdparty.h"

#ifndef ENGINE_API
#ifdef _WIN32
    #ifdef BUILD_ENGINE
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
    #endif
#else
    #define ENGINE_API
#endif
#endif

namespace MQEngine {
    namespace Effect {

        /**
         * @brief 材质与着色效果核心模块 (Effector)
         * @details 归属 Effect 层，处于 Script 与 Render 之间：
         *          统一托管着色器片段库（ShaderSnippetManager）、材质着色器图（ShaderGraphManager）、
         *          材质渲染技术管理（TechManager）以及着色器热重载监听（ShaderFileWatcher）。
         */
        class ENGINE_API Effector : public CoreEngineModule<Effector> {
        public:
            Effector();
            ~Effector();

            Effector(const Effector&) = delete;
            Effector& operator=(const Effector&) = delete;
            Effector(Effector&&) = delete;
            Effector& operator=(Effector&&) = delete;

            void registerInfo();
            Status init();
            void updateLogic();
            void shutdownWatcher();
            void term();

            void registerShaderSnippets();

            TechManager* getTechManager() noexcept { return &techManager; }
            const TechManager* getTechManager() const noexcept { return &techManager; }
            ShaderSnippetManager* getShaderSnippetManager() const noexcept { return const_cast<ShaderSnippetManager*>(&shaderSnippetManager); }
            ShaderGraphManager* getShaderGraphManager() const noexcept { return const_cast<ShaderGraphManager*>(&shaderGraphManager); }
            ShaderFileWatcher* getShaderFileWatcher() const noexcept { return m_shaderFileWatcher.get(); }

            const FCT::VertexLayout& getVertexLayout() const noexcept { return m_vertexLayout; }
            const FCT::PixelLayout& getPixelLayout() const noexcept { return m_pixelLayout; }

            TechManager techManager;
            ShaderSnippetManager shaderSnippetManager;
            ShaderGraphManager shaderGraphManager;

        private:
            UniquePtr<ShaderFileWatcher> m_shaderFileWatcher;

            FCT::VertexLayout m_vertexLayout = StandardMeshVertexLayout;
            FCT::PixelLayout m_pixelLayout = {
                FCT::VertexElement{FCT::VtxType::Custom, "worldPos", FCT::Format::R32G32B32A32_SFLOAT},
                FCT::VertexElement{FCT::VtxType::Custom, "shadowPos", FCT::Format::R32G32B32A32_SFLOAT},
                m_vertexLayout
            };
        };

    } // namespace Effect
} // namespace MQEngine

#endif // MQENGINE_EFFECT_EFFECTOR_H
