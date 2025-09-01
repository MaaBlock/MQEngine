#ifndef ENGINEGLOBAL_H
#define ENGINEGLOBAL_H
#include "../Thirdparty/thirdparty.h"
#include "../EnginePCH.h"
#include "../event/sceneEvent.h"
namespace MQEngine
{
    class DataManager;
    class ScriptSystem;
    class CameraSystem;
    class LightingSystem;
    class MatrixCacheSystem;
    class TextureRenderSystem;
    using Context = FCT::Context;
    struct ENGINE_API EngineGlobal
    {
        Context* ctx;
        FCT::Runtime* rt;
        DataManager* dataManager;
        ScriptSystem* scriptSystem;
        CameraSystem* cameraSystem;
        LightingSystem* lightingSystem;
        MatrixCacheSystem* matrixCacheSystem;
        TextureRenderSystem* textureRenderSystem;
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly> sceneEventPipe;
        EngineGlobal() = default;
        EngineGlobal(const EngineGlobal&) = delete;
        EngineGlobal& operator=(const EngineGlobal&) = delete;
        EngineGlobal(EngineGlobal&&) = default;
        EngineGlobal& operator=(EngineGlobal&&) = default;
    };

    extern ENGINE_API EngineGlobal g_engineGlobal;
}
#endif //ENGINEGLOBAL_H
