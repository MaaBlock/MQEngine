#include "./SkeletalAnimationSystem.h"
#include "../../render/system/MatrixCacheSystem.h"
#include "../../base/MemoryManager.h"
#include "../../scene/manager/RegistriesManager.h"

namespace MQEngine {

    namespace {
        constexpr int kMaxAncestorLookupDepth = 8;
        constexpr size_t kParallelPoseGrainSize = 4;
        constexpr float kRadToDeg = 180.0f / 3.1415926535f;

        FCT::Vec3<> extractEulerDegreesXYZ(const FCT::Mat4& rotMat)
        {
            const float sinY = std::clamp(rotMat.m[2], -1.0f, 1.0f);
            const float pitchY = std::asin(sinY);
            float rollX = 0.0f;
            float yawZ = 0.0f;

            if (std::abs(sinY) < 0.9999f) {
                rollX = std::atan2(-rotMat.m[6], rotMat.m[10]);
                yawZ = std::atan2(-rotMat.m[1], rotMat.m[0]);
            } else {
                rollX = std::atan2(rotMat.m[9], rotMat.m[5]);
                yawZ = 0.0f;
            }
            return FCT::Vec3<>(rollX * kRadToDeg, pitchY * kRadToDeg, yawZ * kRadToDeg);
        }

        void decomposeMatrixToComponents(
            const FCT::Mat4& mat,
            FCT::Vec3<>& outPos,
            FCT::Vec3<>& outEulerDeg,
            FCT::Vec3<>& outScale)
        {
            outPos = FCT::Vec3<>(mat.m[3], mat.m[7], mat.m[11]);

            FCT::Vec3<> col0(mat.m[0], mat.m[4], mat.m[8]);
            FCT::Vec3<> col1(mat.m[1], mat.m[5], mat.m[9]);
            FCT::Vec3<> col2(mat.m[2], mat.m[6], mat.m[10]);

            float sx = col0.length();
            float sy = col1.length();
            float sz = col2.length();
            if (col0.cross(col1).dot(col2) < 0.0f) {
                sx = -sx;
            }
            outScale = FCT::Vec3<>(
                std::abs(sx) > 1e-6f ? sx : 1.0f,
                std::abs(sy) > 1e-6f ? sy : 1.0f,
                std::abs(sz) > 1e-6f ? sz : 1.0f);

            FCT::Mat4 pureRot;
            pureRot.m[0] = mat.m[0] / outScale.x;
            pureRot.m[4] = mat.m[4] / outScale.x;
            pureRot.m[8] = mat.m[8] / outScale.x;

            pureRot.m[1] = mat.m[1] / outScale.y;
            pureRot.m[5] = mat.m[5] / outScale.y;
            pureRot.m[9] = mat.m[9] / outScale.y;

            pureRot.m[2] = mat.m[2] / outScale.z;
            pureRot.m[6] = mat.m[6] / outScale.z;
            pureRot.m[10] = mat.m[10] / outScale.z;

            outEulerDeg = extractEulerDegreesXYZ(pureRot);
        }
    }

    SkeletalAnimationSystem::SkeletalAnimationSystem(DataManager* dataManager)
        : m_ctx(nullptr), m_dataManager(dataManager)
    {
    }

    void SkeletalAnimationSystem::init(FCT::Context* ctx, DataManager* dataManager)
    {
        m_ctx = ctx;
        if (dataManager) {
            m_dataManager = dataManager;
        }
        if (m_ctx) {
            m_defaultBoneUniform = FCT::Uniform(m_ctx, BoneUniformSlot);
            std::vector<FCT::Mat4> defaultBones(MAX_BONES, FCT::Mat4());
            m_defaultBoneUniform.setValue("boneMatrices", defaultBones);
            m_defaultBoneUniform.setValue("hasBones", 0);
            m_defaultBoneUniform.update();
        }
        if (m_memoryTrimHandle == 0 && g_engineGlobal.memoryManager) {
            m_memoryTrimHandle = g_engineGlobal.memoryManager->registerTrimCallback(
                "SkeletalAnimationSystem",
                [this]() -> size_t {
                    return static_cast<size_t>(trimUnusedSkeletons());
                });
        }
    }

    SkeletalAnimationSystem::SkeletalAnimationSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager), m_defaultBoneUniform(ctx, BoneUniformSlot)
    {
        std::vector<FCT::Mat4> defaultBones(MAX_BONES, FCT::Mat4());
        m_defaultBoneUniform.setValue("boneMatrices", defaultBones);
        m_defaultBoneUniform.setValue("hasBones", 0);
        m_defaultBoneUniform.update();

        if (g_engineGlobal.memoryManager) {
            m_memoryTrimHandle = g_engineGlobal.memoryManager->registerTrimCallback(
                "SkeletalAnimationSystem",
                [this]() -> size_t {
                    return static_cast<size_t>(trimUnusedSkeletons());
                });
        }
    }

    SkeletalAnimationSystem::~SkeletalAnimationSystem()
    {
        if (m_memoryTrimHandle != 0 && g_engineGlobal.memoryManager) {
            g_engineGlobal.memoryManager->unregisterTrimCallback(m_memoryTrimHandle);
            m_memoryTrimHandle = 0;
        }
    }

    std::shared_ptr<FCT::ModelSkeletonData> SkeletalAnimationSystem::findCachedSkeleton(const std::string& modelUuid) const
    {
        std::shared_lock<std::shared_mutex> lock(m_cacheMutex);
        auto it = m_skeletonCache.find(modelUuid);
        return it != m_skeletonCache.end() ? it->second : nullptr;
    }

    std::string SkeletalAnimationSystem::resolveModelFullPath(const std::string& modelUuid) const
    {
        if (modelUuid.empty() || !m_dataManager) {
            return "";
        }
        auto located = m_dataManager->locateModelFile(modelUuid);
        if (located.ok()) {
            return located.value();
        }
        m_dataManager->updateModelPathList();
        located = m_dataManager->locateModelFile(modelUuid);
        return located.ok() ? located.value() : "";
    }

    std::shared_ptr<FCT::ModelSkeletonData> SkeletalAnimationSystem::getOrLoadSkeleton(const std::string& modelUuid)
    {
        if (modelUuid.empty() || !m_dataManager) {
            return nullptr;
        }
        if (auto cached = findCachedSkeleton(modelUuid)) {
            return cached;
        }
        if (!g_engineGlobal.rt) {
            return nullptr;
        }

        const std::string fullPath = resolveModelFullPath(modelUuid);
        if (fullPath.empty()) {
            return nullptr;
        }

        FCT::UniquePtr<FCT::ModelLoader> loader(g_engineGlobal.rt->createModelLoader());
        if (!loader) {
            return nullptr;
        }

        auto skeleton = loader->loadSkeletonData(fullPath);
        if (skeleton && !skeleton->runtimeCachePrepared) {
            skeleton->prepareRuntimeCache();
        }

        std::unique_lock<std::shared_mutex> lock(m_cacheMutex);
        m_pendingLoads.erase(modelUuid);
        auto [it, inserted] = m_skeletonCache.emplace(modelUuid, skeleton);
        if (!inserted && it->second) {
            return it->second;
        }
        return skeleton;
    }

    void SkeletalAnimationSystem::requestAsyncSkeletonLoad(const std::string& modelUuid)
    {
        if (modelUuid.empty() || !m_dataManager || !g_engineGlobal.rt) {
            return;
        }
        {
            std::shared_lock<std::shared_mutex> readLock(m_cacheMutex);
            if (m_skeletonCache.find(modelUuid) != m_skeletonCache.end() ||
                m_pendingLoads.find(modelUuid) != m_pendingLoads.end()) {
                return;
            }
        }

        const std::string fullPath = resolveModelFullPath(modelUuid);
        if (fullPath.empty()) {
            return;
        }

        {
            std::unique_lock<std::shared_mutex> writeLock(m_cacheMutex);
            if (m_skeletonCache.find(modelUuid) != m_skeletonCache.end() ||
                !m_pendingLoads.insert(modelUuid).second) {
                return;
            }
        }

        FCT::Runtime* rt = g_engineGlobal.rt;
        ThreadPool::globalInstance().dispatch([this, rt, modelUuid, fullPath]() {
            ZoneScoped();
            std::shared_ptr<FCT::ModelSkeletonData> loaded;
            if (rt) {
                FCT::UniquePtr<FCT::ModelLoader> loader(rt->createModelLoader());
                if (loader) {
                    loaded = loader->loadSkeletonData(fullPath);
                    if (loaded && !loaded->runtimeCachePrepared) {
                        loaded->prepareRuntimeCache();
                    }
                }
            }
            std::unique_lock<std::shared_mutex> writeLock(m_cacheMutex);
            m_pendingLoads.erase(modelUuid);
            if (m_skeletonCache.find(modelUuid) == m_skeletonCache.end()) {
                m_skeletonCache.emplace(modelUuid, std::move(loaded));
            }
        }, JobPriority::Normal);
    }

    void SkeletalAnimationSystem::cleanupCacheComponents(entt::registry* registry)
    {
        auto cacheView = registry->view<CacheSkeletalAnimation>();
        for (auto entity : cacheView) {
            if (!registry->all_of<SkeletalAnimatorComponent>(entity)) {
                g_engineGlobal.registriesManager->requestRemoveComponent<CacheSkeletalAnimation>(registry, entity);
            }
        }
    }

    void SkeletalAnimationSystem::resolveAnimatorModelUuid(
        entt::registry* registry,
        entt::entity entity,
        SkeletalAnimatorComponent& animator)
    {
        if (!animator.modelUuid.empty()) {
            return;
        }
        if (const auto* meshInst = registry->try_get<StaticMeshInstance>(entity)) {
            if (!meshInst->modelUuid.empty()) {
                animator.modelUuid = meshInst->modelUuid;
                return;
            }
        }
        auto hierarchyView = registry->view<HierarchyComponent, StaticMeshInstance>();
        for (auto child : hierarchyView) {
            const auto& h = hierarchyView.get<HierarchyComponent>(child);
            if (static_cast<entt::entity>(h.parentEntity) == entity) {
                const auto& meshInst = hierarchyView.get<StaticMeshInstance>(child);
                if (!meshInst.modelUuid.empty()) {
                    animator.modelUuid = meshInst.modelUuid;
                    return;
                }
            }
        }
    }

    float SkeletalAnimationSystem::advanceClipClock(
        float currentTime,
        float deltaSec,
        const FCT::AnimationClip* clip,
        bool loop,
        bool& inOutPlaying) const
    {
        if (!clip) {
            return 0.0f;
        }
        const float durationSec = clip->getDurationInSeconds();
        if (durationSec <= 0.0f) {
            return 0.0f;
        }
        float nextTime = currentTime + deltaSec;
        if (loop) {
            nextTime = std::fmod(nextTime, durationSec);
            if (nextTime < 0.0f) {
                nextTime += durationSec;
            }
        } else if (nextTime >= durationSec) {
            nextTime = durationSec;
            inOutPlaying = false;
        } else if (nextTime < 0.0f) {
            nextTime = 0.0f;
            inOutPlaying = false;
        }
        return nextTime;
    }

    void SkeletalAnimationSystem::advanceAnimatorState(
        SkeletalAnimatorComponent& animator,
        const FCT::ModelSkeletonData& skeleton) const
    {
        if (animator.clipName.empty() && !skeleton.animations.empty()) {
            animator.clipName = skeleton.animations[0].name;
        }
        if (!animator.playing || m_deltaTime <= 0.0f) {
            return;
        }

        const float scaledDt = m_deltaTime * animator.speed;
        animator.currentTime = advanceClipClock(
            animator.currentTime,
            scaledDt,
            skeleton.findAnimation(animator.clipName),
            animator.loop,
            animator.playing);

        if (!animator.targetClipName.empty()) {
            bool targetPlaying = true;
            animator.targetTime = advanceClipClock(
                animator.targetTime,
                scaledDt,
                skeleton.findAnimationExact(animator.targetClipName),
                animator.loop,
                targetPlaying);

            if (animator.blendDuration > 1e-4f) {
                animator.blendElapsed += m_deltaTime;
                animator.blendWeight = std::clamp(animator.blendElapsed / animator.blendDuration, 0.0f, 1.0f);
                if (animator.blendElapsed >= animator.blendDuration) {
                    animator.clipName = animator.targetClipName;
                    animator.currentTime = animator.targetTime;
                    animator.targetClipName.clear();
                    animator.targetTime = 0.0f;
                    animator.blendElapsed = 0.0f;
                    animator.blendWeight = 0.0f;
                }
            }
        }

        if (!animator.layerClipName.empty() && animator.layerWeight > 1e-4f) {
            bool layerPlaying = true;
            animator.layerTime = advanceClipClock(
                animator.layerTime,
                scaledDt,
                skeleton.findAnimationExact(animator.layerClipName),
                true,
                layerPlaying);
        }
    }

    FCT::SkeletalBlendParams SkeletalAnimationSystem::buildBlendParams(const SkeletalAnimatorComponent& animator) const
    {
        FCT::SkeletalBlendParams params;
        params.primaryClipName = animator.clipName;
        params.primaryTimeSec = animator.currentTime;
        params.primaryLoop = animator.loop;

        params.targetClipName = animator.targetClipName;
        params.targetTimeSec = animator.targetTime;
        params.targetLoop = animator.loop;
        params.blendWeight = animator.blendWeight;

        params.layerClipName = animator.layerClipName;
        params.layerTimeSec = animator.layerTime;
        params.layerLoop = true;
        params.layerWeight = animator.layerWeight;
        params.layerMaskRootBone = animator.layerMaskRootBone;

        params.lockRootPosition = animator.lockRootPosition;
        return params;
    }

    void SkeletalAnimationSystem::prepareRegistryPoseTasks(
        entt::registry* registry,
        std::vector<PoseEvalTask>& outTasks)
    {
        auto view = registry->view<SkeletalAnimatorComponent>();
        for (auto entity : view) {
            auto& animator = view.get<SkeletalAnimatorComponent>(entity);
            resolveAnimatorModelUuid(registry, entity, animator);

            CacheSkeletalAnimation* cache = registry->try_get<CacheSkeletalAnimation>(entity);
            if (!cache) {
                g_engineGlobal.registriesManager->requestEmplaceComponent<CacheSkeletalAnimation>(registry, entity, m_ctx);
                continue;
            }

            if (cache->loadedModelUuid != animator.modelUuid) {
                auto cachedSkeleton = findCachedSkeleton(animator.modelUuid);
                if (cachedSkeleton) {
                    cache->skeleton = std::move(cachedSkeleton);
                    cache->loadedModelUuid = animator.modelUuid;
                } else if (!animator.modelUuid.empty()) {
                    requestAsyncSkeletonLoad(animator.modelUuid);
                }
            }

            if (cache->skeleton && (cache->skeleton->hasSkeleton() || !cache->skeleton->flatNodes.empty())) {
                advanceAnimatorState(animator, *cache->skeleton);
                outTasks.push_back(PoseEvalTask{cache, buildBlendParams(animator)});
            } else {
                cache->hasBones = false;
                if (cache->uniform) {
                    cache->uniform->setValue("hasBones", 0);
                }
                cache->init = true;
            }
        }
    }

    void SkeletalAnimationSystem::evaluatePoseTasksParallel(std::vector<PoseEvalTask>& tasks)
    {
        if (tasks.empty()) {
            return;
        }
        ZoneScoped();
        ThreadPool::globalInstance().parallelFor<size_t>(
            0,
            tasks.size(),
            kParallelPoseGrainSize,
            [&tasks](size_t idx) {
                PoseEvalTask& item = tasks[idx];
                CacheSkeletalAnimation* cache = item.cache;
                if (!cache || !cache->skeleton) {
                    return;
                }
                cache->skeleton->evaluateBlendedPose(
                    item.blendParams,
                    cache->finalBoneMatrices,
                    &cache->globalNodeMatrices,
                    MAX_BONES);

                cache->hasBones = cache->skeleton->hasSkeleton();
                if (cache->uniform) {
                    if (cache->hasBones) {
                        cache->uniform->setValue("boneMatrices", cache->finalBoneMatrices);
                        cache->uniform->setValue("hasBones", 1);
                    } else {
                        cache->uniform->setValue("hasBones", 0);
                    }
                }
                cache->init = true;
            },
            JobPriority::High);
    }

    entt::entity SkeletalAnimationSystem::findSocketTargetEntity(
        entt::registry* registry,
        entt::entity socketEntity,
        const SkeletalBoneSocketComponent& socket,
        const std::unordered_map<std::string, entt::entity>& uuidMap) const
    {
        if (!socket.targetEntityUuid.empty()) {
            auto it = uuidMap.find(socket.targetEntityUuid);
            if (it != uuidMap.end() && registry->valid(it->second) && it->second != socketEntity) {
                return it->second;
            }
        }

        entt::entity cur = socketEntity;
        for (int depth = 0; depth < kMaxAncestorLookupDepth; ++depth) {
            const auto* hierarchy = registry->try_get<HierarchyComponent>(cur);
            if (!hierarchy) {
                break;
            }
            const entt::entity parent = static_cast<entt::entity>(hierarchy->parentEntity);
            if (parent == entt::null || parent == cur || !registry->valid(parent)) {
                break;
            }
            if (registry->all_of<CacheSkeletalAnimation>(parent)) {
                return parent;
            }
            cur = parent;
        }
        return entt::null;
    }

    void SkeletalAnimationSystem::applySocketTransformToEntity(
        entt::registry* registry,
        entt::entity socketEntity,
        entt::entity targetEntity,
        const SkeletalBoneSocketComponent& socket,
        const FCT::Mat4& boneModelMatrix)
    {
        const PositionComponent offsetPos{socket.localPosition};
        const RotationComponent offsetRot{socket.localRotation};
        const ScaleComponent offsetScale{socket.localScale};
        const FCT::Mat4 offsetMat = MatrixCacheSystem::calculateModelMatrix(offsetPos, offsetRot, offsetScale);
        const FCT::Mat4 socketInTargetModel = boneModelMatrix * offsetMat;

        entt::entity directParent = entt::null;
        if (const auto* h = registry->try_get<HierarchyComponent>(socketEntity)) {
            const entt::entity p = static_cast<entt::entity>(h->parentEntity);
            if (p != entt::null && p != socketEntity && registry->valid(p)) {
                directParent = p;
            }
        }

        FCT::Mat4 desiredLocalMatrix = socketInTargetModel;
        if (directParent != targetEntity) {
            const FCT::Mat4 targetWorld = g_engineGlobal.matrixCacheSystem
                ? g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(registry, targetEntity)
                : FCT::Mat4();
            const FCT::Mat4 desiredWorld = targetWorld * socketInTargetModel;
            if (directParent != entt::null && g_engineGlobal.matrixCacheSystem) {
                const FCT::Mat4 parentWorld = g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(registry, directParent);
                desiredLocalMatrix = parentWorld.inverse() * desiredWorld;
            } else {
                desiredLocalMatrix = desiredWorld;
            }
        }

        FCT::Vec3<> newPos;
        FCT::Vec3<> newEuler;
        FCT::Vec3<> newScale;
        decomposeMatrixToComponents(desiredLocalMatrix, newPos, newEuler, newScale);

        registry->get_or_emplace<PositionComponent>(socketEntity).position = newPos;
        if (socket.followRotation) {
            registry->get_or_emplace<RotationComponent>(socketEntity).rotation = newEuler;
        }
        if (socket.followScale) {
            registry->get_or_emplace<ScaleComponent>(socketEntity).scale = newScale;
        }

        if (auto* cacheModel = registry->try_get<CacheModelMatrix>(socketEntity)) {
            if (g_engineGlobal.matrixCacheSystem) {
                const FCT::Mat4 worldMat = g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(registry, socketEntity);
                cacheModel->localMatrix = desiredLocalMatrix;
                cacheModel->worldMatrix = worldMat;
                cacheModel->uniform->setValue(FCT::UniformType::ModelMatrix, worldMat);
                cacheModel->uniform->setValue("modelInverseTransposeMatrix", worldMat.inverse().transpose());
                cacheModel->init = true;
            }
        }
    }

    void SkeletalAnimationSystem::updateRegistryBoneSockets(entt::registry* registry)
    {
        auto socketView = registry->view<SkeletalBoneSocketComponent>();
        if (socketView.begin() == socketView.end()) {
            return;
        }

        std::unordered_map<std::string, entt::entity> uuidMap;
        auto nameView = registry->view<NameTag>();
        for (auto e : nameView) {
            const auto& tag = nameView.get<NameTag>(e);
            if (!tag.uuid.empty()) {
                uuidMap.emplace(tag.uuid, e);
            }
        }

        for (auto socketEntity : socketView) {
            const auto& socket = socketView.get<SkeletalBoneSocketComponent>(socketEntity);
            if (socket.boneName.empty()) {
                continue;
            }
            const entt::entity targetEntity = findSocketTargetEntity(registry, socketEntity, socket, uuidMap);
            if (targetEntity == entt::null) {
                continue;
            }
            const auto* targetCache = registry->try_get<CacheSkeletalAnimation>(targetEntity);
            if (!targetCache || !targetCache->skeleton || targetCache->globalNodeMatrices.empty()) {
                continue;
            }
            const int32_t nodeIdx = targetCache->skeleton->findFlatNodeIndex(socket.boneName);
            if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= targetCache->globalNodeMatrices.size()) {
                continue;
            }
            applySocketTransformToEntity(
                registry,
                socketEntity,
                targetEntity,
                socket,
                targetCache->globalNodeMatrices[static_cast<size_t>(nodeIdx)]);
        }
    }

    void SkeletalAnimationSystem::updateLogic()
    {
        ZoneScoped();
        if (!m_dataManager) {
            return;
        }
        auto registries = m_dataManager->currentRegistries();
        m_frameTasks.clear();

        for (auto* registry : registries) {
            if (!registry) {
                continue;
            }
            cleanupCacheComponents(registry);
            prepareRegistryPoseTasks(registry, m_frameTasks);
        }

        evaluatePoseTasksParallel(m_frameTasks);

        for (auto* registry : registries) {
            if (!registry) {
                continue;
            }
            updateRegistryBoneSockets(registry);
        }
    }

    void SkeletalAnimationSystem::updateRender()
    {
        ZoneScoped();
        if (!m_dataManager) {
            return;
        }
        auto registries = m_dataManager->currentRegistries();
        for (auto* registry : registries) {
            if (!registry) {
                continue;
            }
            registry->view<CacheSkeletalAnimation>().each([](CacheSkeletalAnimation& cache) {
                if (cache.init && cache.uniform) {
                    cache.uniform->update();
                }
            });
        }
    }

    const CacheSkeletalAnimation* SkeletalAnimationSystem::findEntityOrAncestorCache(
        entt::registry* registry,
        entt::entity entity) const
    {
        if (!registry || !registry->valid(entity)) {
            return nullptr;
        }
        if (const auto* cache = registry->try_get<CacheSkeletalAnimation>(entity)) {
            if (cache->init && cache->hasBones) {
                return cache;
            }
        }
        if (const auto* subTag = registry->try_get<RuntimeSubEntityTag>(entity)) {
            entt::entity owner = static_cast<entt::entity>(subTag->ownerEntity);
            if (owner != entt::null && registry->valid(owner)) {
                if (const auto* ownerCache = registry->try_get<CacheSkeletalAnimation>(owner)) {
                    if (ownerCache->init && ownerCache->hasBones) {
                        return ownerCache;
                    }
                }
            }
        }
        entt::entity current = entity;
        for (int depth = 0; depth < kMaxAncestorLookupDepth; ++depth) {
            const auto* hierarchy = registry->try_get<HierarchyComponent>(current);
            if (!hierarchy) {
                break;
            }
            entt::entity parent = static_cast<entt::entity>(hierarchy->parentEntity);
            if (parent == entt::null || parent == current || !registry->valid(parent)) {
                break;
            }
            if (const auto* parentCache = registry->try_get<CacheSkeletalAnimation>(parent)) {
                if (parentCache->init && parentCache->hasBones) {
                    return parentCache;
                }
            }
            current = parent;
        }
        return nullptr;
    }

    void SkeletalAnimationSystem::bindBoneMatrices(
        entt::registry* registry,
        entt::entity entity,
        FCT::Layout* layout)
    {
        if (!layout) {
            return;
        }
        const CacheSkeletalAnimation* cache = findEntityOrAncestorCache(registry, entity);
        if (cache && cache->uniform) {
            layout->bindUniform(cache->uniform);
            return;
        }
        layout->bindUniform(m_defaultBoneUniform);
    }

    bool SkeletalAnimationSystem::findEntityByUuid(
        const std::string& entityUuid,
        entt::registry*& outRegistry,
        entt::entity& outEntity) const
    {
        outRegistry = nullptr;
        outEntity = entt::null;
        if (entityUuid.empty() || !m_dataManager) {
            return false;
        }
        for (auto* registry : m_dataManager->currentRegistries()) {
            if (!registry) {
                continue;
            }
            auto view = registry->view<NameTag>();
            for (auto entity : view) {
                if (view.get<NameTag>(entity).uuid == entityUuid) {
                    outRegistry = registry;
                    outEntity = entity;
                    return true;
                }
            }
        }
        return false;
    }

    bool SkeletalAnimationSystem::playEntityClip(
        const std::string& entityUuid,
        const std::string& clipName,
        bool loop,
        float speed)
    {
        entt::registry* registry = nullptr;
        entt::entity entity = entt::null;
        if (!findEntityByUuid(entityUuid, registry, entity)) {
            return false;
        }
        auto* anim = registry->try_get<SkeletalAnimatorComponent>(entity);
        if (!anim) {
            return false;
        }
        anim->clipName = clipName;
        anim->currentTime = 0.0f;
        anim->loop = loop;
        anim->speed = speed;
        anim->playing = true;
        anim->targetClipName.clear();
        anim->blendElapsed = 0.0f;
        anim->blendWeight = 0.0f;
        return true;
    }

    bool SkeletalAnimationSystem::crossFadeEntityClip(
        const std::string& entityUuid,
        const std::string& targetClipName,
        float durationSec,
        bool loop)
    {
        if (targetClipName.empty()) {
            return false;
        }
        entt::registry* registry = nullptr;
        entt::entity entity = entt::null;
        if (!findEntityByUuid(entityUuid, registry, entity)) {
            return false;
        }
        auto* anim = registry->try_get<SkeletalAnimatorComponent>(entity);
        if (!anim) {
            return false;
        }
        if (durationSec <= 1e-4f || anim->clipName.empty() || anim->clipName == targetClipName) {
            anim->clipName = targetClipName;
            anim->loop = loop;
            anim->playing = true;
            anim->targetClipName.clear();
            anim->blendElapsed = 0.0f;
            anim->blendWeight = 0.0f;
            return true;
        }
        anim->targetClipName = targetClipName;
        anim->targetTime = 0.0f;
        anim->blendDuration = durationSec;
        anim->blendElapsed = 0.0f;
        anim->blendWeight = 0.0f;
        anim->loop = loop;
        anim->playing = true;
        return true;
    }

    bool SkeletalAnimationSystem::setEntityLayerClip(
        const std::string& entityUuid,
        const std::string& layerClipName,
        float weight,
        const std::string& maskRootBone)
    {
        entt::registry* registry = nullptr;
        entt::entity entity = entt::null;
        if (!findEntityByUuid(entityUuid, registry, entity)) {
            return false;
        }
        auto* anim = registry->try_get<SkeletalAnimatorComponent>(entity);
        if (!anim) {
            return false;
        }
        if (anim->layerClipName != layerClipName) {
            anim->layerTime = 0.0f;
        }
        anim->layerClipName = layerClipName;
        anim->layerWeight = std::clamp(weight, 0.0f, 1.0f);
        anim->layerMaskRootBone = maskRootBone;
        return true;
    }

    bool SkeletalAnimationSystem::stopEntityAnimation(const std::string& entityUuid)
    {
        entt::registry* registry = nullptr;
        entt::entity entity = entt::null;
        if (!findEntityByUuid(entityUuid, registry, entity)) {
            return false;
        }
        auto* anim = registry->try_get<SkeletalAnimatorComponent>(entity);
        if (!anim) {
            return false;
        }
        anim->playing = false;
        return true;
    }

    std::vector<std::string> SkeletalAnimationSystem::getModelClipNames(const std::string& modelUuid)
    {
        auto skeleton = getOrLoadSkeleton(modelUuid);
        return skeleton ? skeleton->getAnimationNames() : std::vector<std::string>{};
    }

    std::vector<std::string> SkeletalAnimationSystem::getModelBoneNames(const std::string& modelUuid)
    {
        auto skeleton = getOrLoadSkeleton(modelUuid);
        return skeleton ? skeleton->getNodeNames() : std::vector<std::string>{};
    }

    float SkeletalAnimationSystem::getModelClipDuration(const std::string& modelUuid, const std::string& clipName)
    {
        auto skeleton = getOrLoadSkeleton(modelUuid);
        if (!skeleton) {
            return 0.0f;
        }
        const auto* clip = skeleton->findAnimationExact(clipName);
        return clip ? clip->getDurationInSeconds() : 0.0f;
    }

    uint32_t SkeletalAnimationSystem::getCachedSkeletonCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_cacheMutex);
        return static_cast<uint32_t>(m_skeletonCache.size());
    }

    uint32_t SkeletalAnimationSystem::trimUnusedSkeletons()
    {
        std::unique_lock<std::shared_mutex> lock(m_cacheMutex);
        uint32_t removed = 0;
        for (auto it = m_skeletonCache.begin(); it != m_skeletonCache.end(); ) {
            if (!it->second || it->second.use_count() <= 1) {
                it = m_skeletonCache.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

} // namespace MQEngine
