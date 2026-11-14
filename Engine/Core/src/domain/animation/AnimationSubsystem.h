/**
 * @file AnimationSubsystem.h
 * @brief 骨骼动画子域：骨骼网格姿态求值、过渡混合 (CrossFade) 与蒙皮矩阵调色板生成
 */
#ifndef MQENGINE_DOMAIN_ANIMATION_ANIMATIONSUBSYSTEM_H
#define MQENGINE_DOMAIN_ANIMATION_ANIMATIONSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <memory>

namespace MQEngine
{
    namespace Domain
    {
        namespace Animation
        {
            /**
             * @brief 最大支持骨骼数量
             */
            constexpr size_t kMaxBones = 128;

            /**
             * @brief 骨骼动画控制器组件
             */
            struct ENGINE_API SkeletalAnimatorComponent
            {
                // 资产定位：模型或骨骼文件路径（如 "Assets/Models/Hero.fbx"）
                // Note: 若为空，AnimationSubsystem 会自动回退至实体的 StaticMeshComponent::meshAssetPath
                std::string skeletonAssetPath;

                // 主播放动作
                std::string clipName;
                float currentTime = 0.0f;
                float speed = 1.0f;
                bool isPlaying = true;
                bool isLoop = true;

                // 交叉过渡混合 (Crossfade) 参数
                std::string targetClipName;
                float targetTime = 0.0f;
                float blendDuration = 0.0f;
                float blendElapsed = 0.0f;
                float blendWeight = 0.0f;

                // 可选：直接持有骨骼数据共享指针（用于纯代码生成或动态绑定）
                std::shared_ptr<FCT::ModelSkeletonData> customSkeleton;
            };

            /**
             * @brief 骨骼最终蒙皮矩阵调色板组件 (GPU Skinning)
             */
            struct ENGINE_API BonePaletteComponent
            {
                std::vector<FCT::Mat4> boneMatrices;
                bool isDirty = true;
                bool hasBones = false;
            };

            /**
             * @brief 动画子系统核心类
             */
            class ENGINE_API AnimationSubsystem
            {
            public:
                AnimationSubsystem();
                ~AnimationSubsystem();

                AnimationSubsystem(const AnimationSubsystem&) = delete;
                AnimationSubsystem& operator=(const AnimationSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                /**
                 * @brief 推进骨骼动画状态并计算蒙皮矩阵调色板
                 * @param deltaTime 逻辑帧增量时间 (秒)
                 * @param registry ECS 关系表
                 */
                void update(float deltaTime, FCT::ECS::registry& registry);

                /**
                 * @brief 获取或加载指定资产路径的骨骼数据（带缓存）
                 */
                std::shared_ptr<FCT::ModelSkeletonData> getOrLoadSkeleton(const std::string& assetPath);

                /**
                 * @brief 清理已缓存的骨骼资产
                 */
                void clearSkeletonCache() noexcept;

                /**
                 * @brief 播放指定实体的动画片段
                 */
                bool playClip(FCT::ECS::registry& registry, FCT::ECS::entity entity, const std::string& clipName, bool loop = true, float speed = 1.0f);

                /**
                 * @brief 交叉平滑过渡到目标片段
                 */
                bool crossFade(FCT::ECS::registry& registry, FCT::ECS::entity entity, const std::string& targetClipName, float durationSec = 0.25f, bool loop = true);

                /**
                 * @brief 停止动画播放
                 */
                bool stop(FCT::ECS::registry& registry, FCT::ECS::entity entity);

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

            private:
                std::atomic<bool> m_initialized{false};
                std::unordered_map<std::string, std::shared_ptr<FCT::ModelSkeletonData>> m_skeletonCache;
            };

        } // namespace Animation
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_ANIMATION_ANIMATIONSUBSYSTEM_H
