#ifndef ENGINEGLOBAL_H
#define ENGINEGLOBAL_H
#include "../thirdparty/thirdparty.h"
#include "../../EnginePCH.h"
#include "../../scene/data/sceneEvent.h"
namespace MQEngine
{
    class DataManager;
    class ScriptSystem;
    class CameraSystem;
    class LightingSystem;
    class MatrixCacheSystem;
    class TextureCacheSystem;
    class ShininessSystem;
    class RegistriesManager;
    class ScriptCacheSystem;
    class SystemManager;
    class InputSystem;
    class ResourceActiveSystem;
    class ShaderSnippetManager;
    class ShaderGraphManager;
    class ResourceLoader;
    class TechManager;
    class SkeletalAnimationSystem;
    class MemoryManager;
    class JobSystem;
    class NetworkManager;
    class NetworkSyncSystem;
    class PhysicsSystem;
    class GpuDrivenSystem;
    class ComponentReflection;
    namespace Base { class Foundation; }
    namespace Platform { class Platform; }
    namespace Render { class Renderer; }

    struct ENGINE_API EngineGlobal
    {
        Context* ctx;
        PlatformRuntime* rt;
        DataManager* dataManager;
        ScriptSystem* scriptSystem;
        InputSystem* inputSystem;
        CameraSystem* cameraSystem;
        LightingSystem* lightingSystem;
        MatrixCacheSystem* matrixCacheSystem;
        TextureCacheSystem* textureRenderSystem;
        ShininessSystem* shininessSystem;
        RegistriesManager* registriesManager;
        ScriptCacheSystem* scriptCacheSystem;
        SystemManager* systemManager;
        ResourceActiveSystem* resourceActiveSystem;
        ShaderSnippetManager* shaderSnippetManager;
        ShaderGraphManager* shaderGraphManager;
        ResourceLoader* resourceLoader;
        TechManager* techManager = nullptr;
        SkeletalAnimationSystem* skeletalAnimationSystem = nullptr;
        MemoryManager* memoryManager = nullptr;
        JobSystem* jobSystem = nullptr;
        NetworkManager* networkManager = nullptr;
        NetworkSyncSystem* networkSyncSystem = nullptr;
        PhysicsSystem* physicsSystem = nullptr;
        GpuDrivenSystem* gpuDrivenSystem = nullptr;
        ComponentReflection* componentReflection = nullptr;
        Base::Foundation* foundation = nullptr;
        Platform::Platform* platform = nullptr;
        Render::Renderer* renderer = nullptr;
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly> sceneEventPipe;
        bool isRunning = false;
        EngineGlobal() = default;
        EngineGlobal(const EngineGlobal&) = delete;
        EngineGlobal& operator=(const EngineGlobal&) = delete;
        EngineGlobal(EngineGlobal&&) = default;
        EngineGlobal& operator=(EngineGlobal&&) = default;
    };

    extern ENGINE_API EngineGlobal g_engineGlobal;
}
#endif //ENGINEGLOBAL_H
