#ifndef MQENGINE_ANIMATION_CORE_ANIMATOR_H
#define MQENGINE_ANIMATION_CORE_ANIMATOR_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../system/SkeletalAnimationSystem.h"

namespace MQEngine
{
    namespace Animation
    {
        class ENGINE_API Animator : public CoreEngineModule<Animator>
        {
        public:
            Animator();
            ~Animator();

            Animator(const Animator&) = delete;
            Animator& operator=(const Animator&) = delete;

            void registerInfo();
            Status init();
            void term();

            SkeletalAnimationSystem* getSkeletalAnimationSystem() const noexcept { return skeletalAnimationSystem.get(); }

        public:
            UniquePtr<SkeletalAnimationSystem> skeletalAnimationSystem;

        private:
            bool m_initialized = false;
        };

    } // namespace Animation
} // namespace MQEngine

#endif // MQENGINE_ANIMATION_CORE_ANIMATOR_H
