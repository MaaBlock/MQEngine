#include "Effector.h"
#include "../../platform/core/Platform.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../scene/core/World.h"
#include "../../script/core/Interpreter.h"
#include <cassert>

namespace MQEngine {
    namespace Effect {

        Effector::Effector() = default;

        Effector::~Effector() {
            term();
        }

        void Effector::registerInfo() {
            shaderSnippetManager.registerInfo();
            shaderGraphManager.registerInfo();
            techManager.registerInfo();

            g_engineGlobal.shaderSnippetManager = &shaderSnippetManager;
            g_engineGlobal.shaderGraphManager = &shaderGraphManager;
            g_engineGlobal.techManager = &techManager;
        }

        Status Effector::init() {
            g_engineGlobal.shaderSnippetManager = &shaderSnippetManager;
            g_engineGlobal.shaderGraphManager = &shaderGraphManager;
            g_engineGlobal.techManager = &techManager;

            shaderGraphManager.registerVertexLayout("StandardMeshVertexLayout", StandardMeshVertexLayout);
            shaderGraphManager.registerPixelLayout("StandardPixelLayout", m_pixelLayout);

            ShaderGraphConfig sgConfig = shaderGraphManager.getConfig();
            sgConfig.bindedSystemNames = {"CameraSystem", "LightingSystem", "TextureSamplerSystem"};
            shaderGraphManager.setConfig(sgConfig);

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            FCT::Context* ctx = engine->platform->getContext();
            DataManager* dataManager = engine->scene->getDataManager();
            assert(ctx != nullptr && dataManager != nullptr && "Context 与 DataManager 不能为空");

            techManager.init(ctx, dataManager);

            ScriptSystem* scriptSys = engine->script->getScriptSystem();
            assert(scriptSys != nullptr && "ScriptSystem 不能为空");
            FCT::JS::NodeEnvironment* nodeEnv = scriptSys->getNodeEnvironment();
            m_shaderFileWatcher = makeUnique<ShaderFileWatcher>(nodeEnv, &shaderSnippetManager);
            auto snippetStatus = shaderSnippetManager.loadSnippetFromResource();
            if (!snippetStatus.ok()) {
                spdlog::warn("加载着色器片段失败: {}", snippetStatus.message());
            }
            m_shaderFileWatcher->startWatching("./Assets/Snippets/");
            registerShaderSnippets();

            return OkStatus();
        }

        void Effector::updateLogic() {
            assert(m_shaderFileWatcher != nullptr && "ShaderFileWatcher 不能为空");
            m_shaderFileWatcher->update();
        }

        void Effector::shutdownWatcher() {
            m_shaderFileWatcher.reset();
        }

        void Effector::term() {
            shutdownWatcher();
            techManager.term();
            shaderGraphManager.clear();
        }

    } // namespace Effect
} // namespace MQEngine
