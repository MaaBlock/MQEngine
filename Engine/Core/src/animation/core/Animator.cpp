#include "Animator.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"

namespace MQEngine
{
    namespace Animation
    {
        Animator::Animator() = default;

        Animator::~Animator()
        {
            term();
        }

        void Animator::registerInfo()
        {
            skeletalAnimationSystem = makeUnique<SkeletalAnimationSystem>(g_engineGlobal.dataManager);
            g_engineGlobal.skeletalAnimationSystem = skeletalAnimationSystem.get();
        }

        Status Animator::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            assert(skeletalAnimationSystem != nullptr && "SkeletalAnimationSystem 不能为空");

            FCT::Context* ctx = engine->platform->getContext();
            DataManager* dm = engine->scene->getDataManager();
            assert(ctx != nullptr && dm != nullptr && "Context 与 DataManager 不能为空");

            skeletalAnimationSystem->init(ctx, dm);
            g_engineGlobal.skeletalAnimationSystem = skeletalAnimationSystem.get();
            engine->host->getSystemManager()->requestAddSystem("SkeletalAnimationSystem", skeletalAnimationSystem.get());

            m_initialized = true;
            return OkStatus();
        }

        void Animator::term()
        {
            if (!m_initialized && !skeletalAnimationSystem) {
                return;
            }
            m_initialized = false;

            if (g_engineGlobal.skeletalAnimationSystem == skeletalAnimationSystem.get()) {
                g_engineGlobal.skeletalAnimationSystem = nullptr;
            }
            skeletalAnimationSystem.reset();
        }

    } // namespace Animation
} // namespace MQEngine
