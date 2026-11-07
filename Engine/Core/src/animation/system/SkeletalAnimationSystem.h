#ifndef SKELETALANIMATIONSYSTEM_H
#define SKELETALANIMATIONSYSTEM_H

#include "../../scene/manager/DataManager.h"
#include "../../scene/data/Component.h"
#include "../../runtime/system/ISystem.h"

namespace MQEngine {

    /**
     * @brief 骨骼动画与骨骼挂点系统，支持异步骨骼加载、跨片段平滑过渡（Crossfade）、分层骨骼遮罩混合以及多线程并行姿态求值
     */
    class ENGINE_API SkeletalAnimationSystem : public ISystem {
    public:
        explicit SkeletalAnimationSystem(DataManager* dataManager = nullptr);
        SkeletalAnimationSystem(FCT::Context* ctx, DataManager* dataManager);
        void init(FCT::Context* ctx, DataManager* dataManager = nullptr);
        ~SkeletalAnimationSystem() override;

        void setDeltaTime(float deltaTime) noexcept { m_deltaTime = deltaTime; }
        void updateLogic() override;
        void updateRender() override;

        void bindBoneMatrices(entt::registry* registry, entt::entity entity, FCT::Layout* layout);
        std::shared_ptr<FCT::ModelSkeletonData> getOrLoadSkeleton(const std::string& modelUuid);
        void requestAsyncSkeletonLoad(const std::string& modelUuid);

        bool playEntityClip(const std::string& entityUuid, const std::string& clipName, bool loop = true, float speed = 1.0f);
        bool crossFadeEntityClip(const std::string& entityUuid, const std::string& targetClipName, float durationSec = 0.25f, bool loop = true);
        bool setEntityLayerClip(const std::string& entityUuid, const std::string& layerClipName, float weight, const std::string& maskRootBone);
        bool stopEntityAnimation(const std::string& entityUuid);

        std::vector<std::string> getModelClipNames(const std::string& modelUuid);
        std::vector<std::string> getModelBoneNames(const std::string& modelUuid);
        float getModelClipDuration(const std::string& modelUuid, const std::string& clipName);
        uint32_t getCachedSkeletonCount() const;
        uint32_t trimUnusedSkeletons();

    private:
        struct PoseEvalTask {
            CacheSkeletalAnimation* cache = nullptr;
            FCT::SkeletalBlendParams blendParams;
        };

        void cleanupCacheComponents(entt::registry* registry);
        void resolveAnimatorModelUuid(entt::registry* registry, entt::entity entity, SkeletalAnimatorComponent& animator);
        float advanceClipClock(float currentTime, float deltaSec, const FCT::AnimationClip* clip, bool loop, bool& inOutPlaying) const;
        void advanceAnimatorState(SkeletalAnimatorComponent& animator, const FCT::ModelSkeletonData& skeleton) const;
        FCT::SkeletalBlendParams buildBlendParams(const SkeletalAnimatorComponent& animator) const;
        void prepareRegistryPoseTasks(entt::registry* registry, std::vector<PoseEvalTask>& outTasks);
        void evaluatePoseTasksParallel(std::vector<PoseEvalTask>& tasks);
        void updateRegistryBoneSockets(entt::registry* registry);
        void applySocketTransformToEntity(
            entt::registry* registry,
            entt::entity socketEntity,
            entt::entity targetEntity,
            const SkeletalBoneSocketComponent& socket,
            const FCT::Mat4& boneModelMatrix);

        std::shared_ptr<FCT::ModelSkeletonData> findCachedSkeleton(const std::string& modelUuid) const;
        std::string resolveModelFullPath(const std::string& modelUuid) const;
        bool findEntityByUuid(const std::string& entityUuid, entt::registry*& outRegistry, entt::entity& outEntity) const;
        const CacheSkeletalAnimation* findEntityOrAncestorCache(entt::registry* registry, entt::entity entity) const;
        entt::entity findSocketTargetEntity(
            entt::registry* registry,
            entt::entity socketEntity,
            const SkeletalBoneSocketComponent& socket,
            const std::unordered_map<std::string, entt::entity>& uuidMap) const;

        FCT::Context* m_ctx = nullptr;
        DataManager* m_dataManager = nullptr;
        FCT::Uniform m_defaultBoneUniform;
        float m_deltaTime = 0.0f;
        uint64_t m_memoryTrimHandle = 0;

        mutable std::shared_mutex m_cacheMutex;
        std::unordered_map<std::string, std::shared_ptr<FCT::ModelSkeletonData>> m_skeletonCache;
        std::unordered_set<std::string> m_pendingLoads;
        std::vector<PoseEvalTask> m_frameTasks;
    };

} // namespace MQEngine

#endif // SKELETALANIMATIONSYSTEM_H
