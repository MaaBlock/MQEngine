/**
 * @file EffectSubsystem.h
 * @brief 特效子域核心门面：
 *        统一管理全管线后处理特效链 (DLSS / 硬件光追 RTReflection/RTGI/RTShadow / 自动曝光 / 景深 / 运动模糊 / 泛光 / 色调映射)
 *        与 GPU Compute / CPU 粒子仿真系统 (Docs 02 & Docs 06)，严格无锁设计
 */
#ifndef MQENGINE_DOMAIN_EFFECT_EFFECTSUBSYSTEM_H
#define MQENGINE_DOMAIN_EFFECT_EFFECTSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/domain/effect/NtcSubsystem.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Effect
        {
            /**
             * @brief DLSS / 超分辨率重构组件配置
             */
            struct ENGINE_API DlssComponent
            {
                bool enabled = true;
                float sharpness = 0.5f;
                int qualityMode = 1; // 0: UltraPerformance, 1: Performance, 2: Balanced, 3: Quality
                FCT::Vec2 currentJitter{0.0f, 0.0f};
                bool resetAccumulation = false;
            };

            /**
             * @brief 自动曝光与眼适应组件配置
             */
            struct ENGINE_API AutoExposureComponent
            {
                bool enabled = true;
                float minLogLuminance = -8.0f;
                float maxLogLuminance = 4.0f;
                float speedUp = 3.0f;
                float speedDown = 1.0f;
                float targetLuminance = 0.18f;
                float currentExposure = 1.0f;
            };

            /**
             * @brief 景深 (Depth Of Field) 组件配置
             */
            struct ENGINE_API DepthOfFieldComponent
            {
                bool enabled = false;
                float focusDistance = 10.0f;
                float focalLength = 50.0f;
                float aperture = 2.8f;
                float maxBokehRadius = 8.0f;
            };

            /**
             * @brief 运动模糊 (Motion Blur) 组件配置
             */
            struct ENGINE_API MotionBlurComponent
            {
                bool enabled = false;
                float intensity = 0.5f;
                int maxSamples = 16;
            };

            /**
             * @brief 泛光 (Bloom) 组件配置
             */
            struct ENGINE_API BloomComponent
            {
                bool enabled = true;
                float threshold = 1.0f;
                float intensity = 0.8f;
                float softKnee = 0.5f;
                float scatter = 0.7f;
            };

            /**
             * @brief 色调映射 (Tone Mapping) 配置
             */
            struct ENGINE_API ToneMappingSettings
            {
                int operatorType = 0; // 0: ACES Filmic, 1: AgX, 2: Reinhard, 3: Uncharted2
                float gamma = 2.2f;
                float whitePoint = 1.0f;
            };

            /**
             * @brief 硬件光线追踪 (Hardware Ray Tracing) 配置
             */
            struct ENGINE_API RayTracingSettings
            {
                bool enableRTReflections = false;
                bool enableRTGI = false;
                bool enableRTShadows = false;
                uint32_t maxBounces = 2;
                float rayLength = 100.0f;
                bool isHardwareSupported = false;
            };

            /**
             * @brief 单个活跃粒子状态
             */
            struct ENGINE_API Particle
            {
                FCT::Vec3<> position{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> velocity{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> color{1.0f, 1.0f, 1.0f};
                float alpha = 1.0f;
                float size = 0.1f;
                float age = 0.0f;
                float lifetime = 1.0f;
                bool isAlive = false;
            };

            /**
             * @brief 粒子发射器组件
             */
            struct ENGINE_API ParticleEmitterComponent
            {
                float spawnRate = 20.0f;       // 每秒生成粒子数
                float particleLifetime = 2.0f;  // 粒子存活周期 (秒)
                FCT::Vec3<> initialVelocity{0.0f, 2.0f, 0.0f};
                FCT::Vec3<> velocityVariance{0.5f, 0.5f, 0.5f};
                FCT::Vec3<> gravity{0.0f, -9.8f, 0.0f};
                FCT::Vec3<> startColor{1.0f, 0.8f, 0.2f};
                FCT::Vec3<> endColor{0.2f, 0.2f, 0.2f};
                float startSize = 0.2f;
                float endSize = 0.05f;
                bool isEmitting = true;

                // 预分配粒子池
                std::vector<Particle> particles;
                float spawnAccumulator = 0.0f;
                size_t maxParticles = 256;
            };

            /**
             * @brief 特效子系统核心类 (严格无锁设计，由 LogicHost 驱动)
             */
            class ENGINE_API EffectSubsystem
            {
            public:
                EffectSubsystem();
                ~EffectSubsystem();

                EffectSubsystem(const EffectSubsystem&) = delete;
                EffectSubsystem& operator=(const EffectSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                /**
                 * @brief 每帧推进粒子系统与后处理特效状态
                 */
                void update(float deltaTime, FCT::ECS::registry& registry);

                /**
                 * @brief 重新初始化指定实体的粒子发射器
                 */
                void resetEmitter(FCT::ECS::registry& registry, FCT::ECS::entity entity);

                /**
                 * @brief 将后处理特效 Pass 链注册挂载至 RenderGraph (Docs 06)
                 */
                void setupRenderGraphPasses(FCT::RenderGraph* rg, FCT::Context* ctx);

                // 特效参数访问器
                DlssComponent& getDlssSettings() noexcept { return m_dlssSettings; }
                const DlssComponent& getDlssSettings() const noexcept { return m_dlssSettings; }

                AutoExposureComponent& getAutoExposureSettings() noexcept { return m_autoExposureSettings; }
                const AutoExposureComponent& getAutoExposureSettings() const noexcept { return m_autoExposureSettings; }

                BloomComponent& getBloomSettings() noexcept { return m_bloomSettings; }
                const BloomComponent& getBloomSettings() const noexcept { return m_bloomSettings; }

                DepthOfFieldComponent& getDepthOfFieldSettings() noexcept { return m_dofSettings; }
                const DepthOfFieldComponent& getDepthOfFieldSettings() const noexcept { return m_dofSettings; }

                MotionBlurComponent& getMotionBlurSettings() noexcept { return m_motionBlurSettings; }
                const MotionBlurComponent& getMotionBlurSettings() const noexcept { return m_motionBlurSettings; }

                ToneMappingSettings& getToneMappingSettings() noexcept { return m_toneMappingSettings; }
                const ToneMappingSettings& getToneMappingSettings() const noexcept { return m_toneMappingSettings; }

                RayTracingSettings& getRayTracingSettings() noexcept { return m_rtSettings; }
                const RayTracingSettings& getRayTracingSettings() const noexcept { return m_rtSettings; }

                NtcSubsystem& getNtcSubsystem() noexcept { return m_ntcSubsystem; }
                const NtcSubsystem& getNtcSubsystem() const noexcept { return m_ntcSubsystem; }

                /**
                 * @brief 计算指定帧的 Halton 低差异子序列抖动偏移
                 */
                static FCT::Vec2 computeHaltonJitter(uint32_t frameIndex, uint32_t width, uint32_t height) noexcept;

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

            private:
                void simulateParticles(float dt, FCT::ECS::registry& registry);
                void updateAutoExposure(float dt);

            private:
                std::atomic<bool> m_initialized{false};
                DlssComponent m_dlssSettings;
                AutoExposureComponent m_autoExposureSettings;
                BloomComponent m_bloomSettings;
                DepthOfFieldComponent m_dofSettings;
                MotionBlurComponent m_motionBlurSettings;
                ToneMappingSettings m_toneMappingSettings;
                RayTracingSettings m_rtSettings;
                NtcSubsystem m_ntcSubsystem;

                uint32_t m_currentFrameIndex = 0;
            };

        } // namespace Effect
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_EFFECT_EFFECTSUBSYSTEM_H
