/**
 * @file EffectSubsystem.cpp
 * @brief 特效子系统实现：粒子仿真、后处理管线管理与无锁生命周期
 */
#include <MQEngine/domain/effect/EffectSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/resource/table/SchemaTable.h>
#include <algorithm>
#include <cmath>
#include <random>

namespace MQEngine
{
    namespace Domain
    {
        namespace Effect
        {
            namespace
            {
                // 计算基底为 base 的 Halton 逆序数
                inline float haltonSequence(uint32_t index, uint32_t base) noexcept
                {
                    float f = 1.0f;
                    float r = 0.0f;
                    while (index > 0) {
                        f /= static_cast<float>(base);
                        r += f * static_cast<float>(index % base);
                        index /= base;
                    }
                    return r;
                }
            } // namespace

            EffectSubsystem::EffectSubsystem() = default;

            EffectSubsystem::~EffectSubsystem()
            {
                term();
            }

            void EffectSubsystem::registerInfo()
            {
            }

            Status EffectSubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                if (auto* schemaTable = Resource::SchemaTable::InstancePtr()) {
                    (void)schemaTable->registerSchema<ParticleEmitterComponent>("ParticleEmitterComponent", 1);
                    (void)schemaTable->registerSchema<DlssComponent>("DlssComponent", 1);
                    (void)schemaTable->registerSchema<BloomComponent>("BloomComponent", 1);
                    (void)schemaTable->registerSchema<DepthOfFieldComponent>("DepthOfFieldComponent", 1);
                    (void)schemaTable->registerSchema<MotionBlurComponent>("MotionBlurComponent", 1);
                    (void)schemaTable->registerSchema<AutoExposureComponent>("AutoExposureComponent", 1);
                }

                m_initialized.store(true, std::memory_order_release);
                return OkStatus();
            }

            void EffectSubsystem::term() noexcept
            {
                if (!m_initialized.load(std::memory_order_relaxed)) {
                    return;
                }
                m_initialized.store(false, std::memory_order_release);
            }

            FCT::Vec2 EffectSubsystem::computeHaltonJitter(uint32_t frameIndex, uint32_t width, uint32_t height) noexcept
            {
                // 16 帧循环 Halton(2, 3) 低差异序列
                uint32_t idx = (frameIndex % 16) + 1;
                float jx = haltonSequence(idx, 2) - 0.5f;
                float jy = haltonSequence(idx, 3) - 0.5f;

                float invW = 1.0f / static_cast<float>(width > 0 ? width : 1920);
                float invH = 1.0f / static_cast<float>(height > 0 ? height : 1080);
                return FCT::Vec2(jx * invW * 2.0f, jy * invH * 2.0f);
            }

            void EffectSubsystem::update(float deltaTime, FCT::ECS::registry& registry)
            {
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return;
                }

                m_currentFrameIndex++;
                float dt = std::clamp(deltaTime, 0.0001f, 0.25f);

                // 1. 更新 DLSS 相机次像素抖动
                if (m_dlssSettings.enabled) {
                    m_dlssSettings.currentJitter = computeHaltonJitter(m_currentFrameIndex, 1920, 1080);
                } else {
                    m_dlssSettings.currentJitter = FCT::Vec2(0.0f, 0.0f);
                }

                // 2. 推进自动曝光眼适应积分
                if (m_autoExposureSettings.enabled) {
                    updateAutoExposure(dt);
                }

                // 3. 推进全部粒子发射器仿真
                simulateParticles(dt, registry);
            }

            void EffectSubsystem::updateAutoExposure(float dt)
            {
                // 指数移动加权平滑眼适应
                float targetExp = m_autoExposureSettings.targetLuminance;
                float curExp = m_autoExposureSettings.currentExposure;
                float speed = (targetExp > curExp) ? m_autoExposureSettings.speedUp : m_autoExposureSettings.speedDown;
                float factor = 1.0f - std::exp(-speed * dt);
                m_autoExposureSettings.currentExposure = curExp + (targetExp - curExp) * factor;
            }

            void EffectSubsystem::simulateParticles(float dt, FCT::ECS::registry& registry)
            {
                auto view = registry.view<ParticleEmitterComponent>();
                thread_local std::mt19937 rng(1337);
                std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

                for (auto entity : view) {
                    auto& emitter = view.get<ParticleEmitterComponent>(entity);

                    if (emitter.particles.empty() && emitter.maxParticles > 0) {
                        emitter.particles.resize(emitter.maxParticles);
                    }

                    // 检索发射器世界空间基准坐标
                    FCT::Vec3<> spawnOrigin(0.0f, 0.0f, 0.0f);
                    if (const auto* worldTf = registry.try_get<Scene::WorldTransform>(entity)) {
                        spawnOrigin = FCT::Vec3<>(
                            worldTf->worldMatrix.m[3],
                            worldTf->worldMatrix.m[7],
                            worldTf->worldMatrix.m[11]
                        );
                    } else if (const auto* localTf = registry.try_get<Scene::LocalTransform>(entity)) {
                        spawnOrigin = localTf->position;
                    }

                    // 1. 发射新粒子
                    if (emitter.isEmitting && emitter.spawnRate > 0.0f) {
                        emitter.spawnAccumulator += dt * emitter.spawnRate;
                        while (emitter.spawnAccumulator >= 1.0f) {
                            emitter.spawnAccumulator -= 1.0f;

                            // 寻找空闲或死亡粒子插槽
                            for (auto& p : emitter.particles) {
                                if (!p.isAlive) {
                                    p.isAlive = true;
                                    p.age = 0.0f;
                                    p.lifetime = std::max(emitter.particleLifetime, 0.1f);
                                    p.position = spawnOrigin;
                                    p.velocity = emitter.initialVelocity + FCT::Vec3<>(
                                        dist(rng) * emitter.velocityVariance.x,
                                        dist(rng) * emitter.velocityVariance.y,
                                        dist(rng) * emitter.velocityVariance.z
                                    );
                                    p.color = emitter.startColor;
                                    p.size = emitter.startSize;
                                    p.alpha = 1.0f;
                                    break;
                                }
                            }
                        }
                    }

                    // 2. 积分推进活跃粒子状态
                    for (auto& p : emitter.particles) {
                        if (!p.isAlive) continue;

                        p.age += dt;
                        if (p.age >= p.lifetime) {
                            p.isAlive = false;
                            continue;
                        }

                        float progress = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);

                        // 速度与位置欧拉积分
                        p.velocity = p.velocity + emitter.gravity * dt;
                        p.position = p.position + p.velocity * dt;

                        // 颜色、大小与不透明度插值
                        p.color = FCT::Vec3<>(
                            std::lerp(emitter.startColor.x, emitter.endColor.x, progress),
                            std::lerp(emitter.startColor.y, emitter.endColor.y, progress),
                            std::lerp(emitter.startColor.z, emitter.endColor.z, progress)
                        );
                        p.size = std::lerp(emitter.startSize, emitter.endSize, progress);
                        p.alpha = 1.0f - progress;
                    }
                }
            }

            void EffectSubsystem::resetEmitter(FCT::ECS::registry& registry, FCT::ECS::entity entity)
            {
                auto* emitter = registry.try_get<ParticleEmitterComponent>(entity);
                if (emitter) {
                    for (auto& p : emitter->particles) {
                        p.isAlive = false;
                        p.age = 0.0f;
                    }
                    emitter->spawnAccumulator = 0.0f;
                }
            }

            void EffectSubsystem::setupRenderGraphPasses(FCT::RenderGraph* rg, FCT::Context* ctx)
            {
                if (!rg || !ctx) {
                    return;
                }
                // 后处理 Pass 节点配置与 ResourceGroup 拓扑连接已在 RendererSubsystem 统一调度管控
            }

        } // namespace Effect
    } // namespace Domain
} // namespace MQEngine
