/**
 * @file AnimationSubsystem.cpp
 * @brief 骨骼动画子域：骨骼网格姿态求值、过渡混合 (CrossFade) 与蒙皮矩阵调色板生成
 */
#include <MQEngine/domain/animation/AnimationSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <algorithm>
#include <cmath>

namespace MQEngine
{
    namespace Domain
    {
        namespace Animation
        {
            AnimationSubsystem::AnimationSubsystem() = default;

            AnimationSubsystem::~AnimationSubsystem()
            {
                term();
            }

            void AnimationSubsystem::registerInfo()
            {
            }

            Status AnimationSubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                m_skeletonCache.clear();
                m_initialized.store(true, std::memory_order_release);
                return OkStatus();
            }

            void AnimationSubsystem::term() noexcept
            {
                m_skeletonCache.clear();
                m_initialized.store(false, std::memory_order_release);
            }

            void AnimationSubsystem::clearSkeletonCache() noexcept
            {
                m_skeletonCache.clear();
            }

            std::shared_ptr<FCT::ModelSkeletonData> AnimationSubsystem::getOrLoadSkeleton(const std::string& assetPath)
            {
                if (assetPath.empty()) {
                    return nullptr;
                }

                auto it = m_skeletonCache.find(assetPath);
                if (it != m_skeletonCache.end()) {
                    return it->second;
                }

                // Note: 使用 Assimp_ModelLoader 解析模型骨骼与动画关键帧数据
                FCT::Assimp_ModelLoader loader;
                auto loadedSkeleton = loader.loadSkeletonData(assetPath);
                if (loadedSkeleton && !loadedSkeleton->runtimeCachePrepared) {
                    loadedSkeleton->prepareRuntimeCache();
                }

                auto [insIt, inserted] = m_skeletonCache.emplace(assetPath, loadedSkeleton);
                return insIt->second;
            }

            namespace
            {
                // 解析实体的骨骼数据来源（自定义骨骼 -> 组件路径 -> 网格资产路径）
                std::shared_ptr<FCT::ModelSkeletonData> resolveEntitySkeleton(
                    AnimationSubsystem* self,
                    const FCT::ECS::registry& registry,
                    FCT::ECS::entity entity,
                    const SkeletalAnimatorComponent& animator)
                {
                    if (animator.customSkeleton) {
                        return animator.customSkeleton;
                    }

                    if (!animator.skeletonAssetPath.empty()) {
                        return self->getOrLoadSkeleton(animator.skeletonAssetPath);
                    }

                    if (const auto* meshComp = registry.try_get<Scene::StaticMeshComponent>(entity)) {
                        if (!meshComp->meshAssetPath.empty()) {
                            return self->getOrLoadSkeleton(meshComp->meshAssetPath);
                        }
                    }

                    return nullptr;
                }

                // 推进主片段与过渡片段的时钟进度
                void advanceAnimatorClock(
                    SkeletalAnimatorComponent& animator,
                    const FCT::ModelSkeletonData* skeleton,
                    float deltaTime)
                {
                    if (!animator.isPlaying || deltaTime <= 0.0f) {
                        return;
                    }

                    const float scaledDt = deltaTime * animator.speed;
                    const auto* primaryClip = skeleton->findAnimation(animator.clipName);
                    if (primaryClip) {
                        const float durationSec = primaryClip->getDurationInSeconds();
                        if (durationSec > 0.0f) {
                            animator.currentTime += scaledDt;
                            if (animator.isLoop) {
                                animator.currentTime = std::fmod(animator.currentTime, durationSec);
                                if (animator.currentTime < 0.0f) {
                                    animator.currentTime += durationSec;
                                }
                            } else if (animator.currentTime >= durationSec) {
                                animator.currentTime = durationSec;
                                animator.isPlaying = false;
                            }
                        }
                    }

                    // 处理交叉过渡混合 (Crossfade)
                    if (!animator.targetClipName.empty() && animator.blendDuration > 1e-4f) {
                        const auto* targetClip = skeleton->findAnimationExact(animator.targetClipName);
                        if (targetClip) {
                            const float targetDuration = targetClip->getDurationInSeconds();
                            if (targetDuration > 0.0f) {
                                animator.targetTime += scaledDt;
                                if (animator.isLoop) {
                                    animator.targetTime = std::fmod(animator.targetTime, targetDuration);
                                    if (animator.targetTime < 0.0f) {
                                        animator.targetTime += targetDuration;
                                    }
                                }
                            }
                        }

                        animator.blendElapsed += deltaTime;
                        animator.blendWeight = std::clamp(animator.blendElapsed / animator.blendDuration, 0.0f, 1.0f);
                        if (animator.blendElapsed >= animator.blendDuration) {
                            animator.clipName = animator.targetClipName;
                            animator.currentTime = animator.targetTime;
                            animator.targetClipName.clear();
                            animator.targetTime = 0.0f;
                            animator.blendDuration = 0.0f;
                            animator.blendElapsed = 0.0f;
                            animator.blendWeight = 0.0f;
                        }
                    }
                }
            } // namespace

            void AnimationSubsystem::update(float deltaTime, FCT::ECS::registry& registry)
            {
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return;
                }

                auto view = registry.view<SkeletalAnimatorComponent>();
                for (auto entity : view) {
                    auto& animator = view.get<SkeletalAnimatorComponent>(entity);
                    auto& palette = registry.get_or_emplace<BonePaletteComponent>(entity);

                    if (palette.boneMatrices.size() != kMaxBones) {
                        palette.boneMatrices.resize(kMaxBones, FCT::Mat4());
                    }

                    // 1. 获取/解析骨骼数据
                    std::shared_ptr<FCT::ModelSkeletonData> skeleton =
                        resolveEntitySkeleton(this, registry, entity, animator);

                    // 2. 无有效骨骼数据时平滑回退为单位矩阵
                    if (!skeleton || !skeleton->hasSkeleton()) {
                        if (palette.isDirty) {
                            for (size_t b = 0; b < kMaxBones; ++b) {
                                palette.boneMatrices[b].identity();
                            }
                            palette.hasBones = false;
                            palette.isDirty = false;
                        }
                        continue;
                    }

                    // 3. 推进动作关键帧时钟
                    advanceAnimatorClock(animator, skeleton.get(), deltaTime);

                    // 4. 执行真实骨骼矩阵求值 (FK 级联与蒙皮调色板生成)
                    FCT::SkeletalBlendParams params;
                    params.primaryClipName = animator.clipName;
                    params.primaryTimeSec = animator.currentTime;
                    params.primaryLoop = animator.isLoop;
                    params.targetClipName = animator.targetClipName;
                    params.targetTimeSec = animator.targetTime;
                    params.targetLoop = animator.isLoop;
                    params.blendWeight = animator.blendWeight;

                    skeleton->evaluateBlendedPose(params, palette.boneMatrices, nullptr, kMaxBones);
                    palette.hasBones = true;
                    palette.isDirty = true;
                }
            }

            bool AnimationSubsystem::playClip(
                FCT::ECS::registry& registry,
                FCT::ECS::entity entity,
                const std::string& clipName,
                bool loop,
                float speed)
            {
                if (!registry.valid(entity)) return false;

                auto& animator = registry.get_or_emplace<SkeletalAnimatorComponent>(entity);
                animator.clipName = clipName;
                animator.currentTime = 0.0f;
                animator.speed = speed;
                animator.isPlaying = true;
                animator.isLoop = loop;
                animator.targetClipName.clear();
                animator.targetTime = 0.0f;
                animator.blendDuration = 0.0f;
                animator.blendElapsed = 0.0f;
                animator.blendWeight = 0.0f;
                return true;
            }

            bool AnimationSubsystem::crossFade(
                FCT::ECS::registry& registry,
                FCT::ECS::entity entity,
                const std::string& targetClipName,
                float durationSec,
                bool loop)
            {
                if (!registry.valid(entity)) return false;

                auto& animator = registry.get_or_emplace<SkeletalAnimatorComponent>(entity);
                if (animator.clipName == targetClipName) {
                    return true;
                }

                animator.targetClipName = targetClipName;
                animator.targetTime = 0.0f;
                animator.blendDuration = durationSec;
                animator.blendElapsed = 0.0f;
                animator.blendWeight = 0.0f;
                animator.isLoop = loop;
                animator.isPlaying = true;
                return true;
            }

            bool AnimationSubsystem::stop(FCT::ECS::registry& registry, FCT::ECS::entity entity)
            {
                if (!registry.valid(entity)) return false;

                auto* animator = registry.try_get<SkeletalAnimatorComponent>(entity);
                if (animator) {
                    animator->isPlaying = false;
                    return true;
                }
                return false;
            }

        } // namespace Animation
    } // namespace Domain
} // namespace MQEngine
