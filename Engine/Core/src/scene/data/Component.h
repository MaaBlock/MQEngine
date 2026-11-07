//
// Created by Administrator on 2025/8/25.
//

#ifndef COMPONENT_H
#define COMPONENT_H
#include "../../thirdparty/thirdparty.h"
#include "NameTag.h"

#include "../../render/core/UniformSlots.h"
#include "../../render/data/Camera.h"
namespace MQEngine {
    struct StaticMeshInstance
    {
        std::string modelUuid;           // 模型UUID，用于定位模型文件
        std::string meshName;            // 网格体名称，用于从模型中选择特定网格
        FCT::StaticMesh<uint32_t>* mesh = nullptr; // 实际存储的mesh，默认为nullptr表示未加载

        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & modelUuid;
            ar & meshName;
        }
    };

    struct ENGINE_API TickerScriptComponent
    {
        std::string functionName;

        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & functionName;
        }
    };

    struct ENGINE_API OnStartScriptComponent
    {
        std::string functionName;

        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & functionName;
        }
    };

    struct ENGINE_API DirectionalLightComponent
    {
        FCT::Vec3<> direction = FCT::Vec3(0.0f, -1.0f, 0.0f);  // 光照方向
        FCT::Vec3<> color = FCT::Vec3(1.0f, 1.0f, 1.0f);       // 光照颜色
        float intensity = 1.0f;                              // 光照强度
        bool enabled = true;                                  // 是否启用

        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & direction;
            ar & color;
            ar & intensity;
            ar & enabled;
        }
    };

    struct ENGINE_API PointLightComponent
    {
        FCT::Vec3<> color = FCT::Vec3(1.0f, 1.0f, 1.0f);
        float intensity = 2.0f;
        float constant = 1.0f;
        float linear = 0.09f;
        float quadratic = 0.032f;
        float range = 25.0f;
        bool enabled = true;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & color;
            ar & intensity;
            ar & constant;
            ar & linear;
            ar & quadratic;
            ar & range;
            ar & enabled;
        }
    };

    struct ENGINE_API PBRMaterialComponent
    {
        FCT::Vec3<> baseColor = FCT::Vec3(1.0f, 1.0f, 1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        FCT::Vec3<> emissive = FCT::Vec3(0.0f, 0.0f, 0.0f);
        std::string shaderGraphName;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & baseColor;
            ar & metallic;
            ar & roughness;
            ar & ao;
            ar & emissive;
            ar & shaderGraphName;
        }
    };

    struct ENGINE_API ShininessComponent
    {
        float shininess = 32.0f;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & shininess;
        }
    };

    struct ENGINE_API CacheShininess
    {
        FCT::UniquePtr<FCT::Uniform> uniform;
        bool init = false;
        CacheShininess()
        {

        }
        CacheShininess(FCT::Context* ctx)
        {
            uniform = FCT::makeUnique<FCT::Uniform>(ctx, ShininessUniformSlot);
        }
    };

    struct CacheResource
    {
        /*
         * @brief 在render线程中是否可见
         */
        bool visible = false;
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {

        }
    };

    struct TextureComponent
    {
        /*
         * @brief 用于定位模型
         */
        std::string modelUuid;
        /*
         * @brief 相对模型的相对路径
         */
        std::string texturePath;
        /*
         * @brief 缓存的图片对象
         */
        FCT::Image* texture = nullptr;
        /*
         * @brief 在render线程中是否可见
         */
        bool visible = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            CacheResource base{visible};
            ar & base;
            ar & modelUuid;
            ar & texturePath;
        }
    };

    struct DiffuseTextureComponent
    {
        std::string modelUuid;
        std::string texturePath;
        FCT::Image* texture = nullptr;
        bool visible = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            TextureComponent base{modelUuid, texturePath, nullptr, visible};
            ar & base;
            modelUuid = base.modelUuid;
            texturePath = base.texturePath;
        }
    };

    struct NormalTextureComponent
    {
        std::string modelUuid;
        std::string texturePath;
        FCT::Image* texture = nullptr;
        bool visible = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            TextureComponent base{modelUuid, texturePath, nullptr, visible};
            ar & base;
            modelUuid = base.modelUuid;
            texturePath = base.texturePath;
        }
    };

    struct AlbedoTextureComponent
    {
        std::string modelUuid;
        std::string texturePath;
        FCT::Image* texture = nullptr;
        bool visible = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            TextureComponent base{modelUuid, texturePath, nullptr, visible};
            ar & base;
            modelUuid = base.modelUuid;
            texturePath = base.texturePath;
        }
    };

    struct EmissiveTextureComponent
    {
        std::string modelUuid;
        std::string texturePath;
        FCT::Image* texture = nullptr;
        bool visible = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            TextureComponent base{modelUuid, texturePath, nullptr, visible};
            ar & base;
            modelUuid = base.modelUuid;
            texturePath = base.texturePath;
        }
    };

    struct OrmTextureComponent
    {
        std::string modelUuid;
        std::string texturePath;
        FCT::Image* texture = nullptr;
        bool visible = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            TextureComponent base{modelUuid, texturePath, nullptr, visible};
            ar & base;
            modelUuid = base.modelUuid;
            texturePath = base.texturePath;
        }
    };

    struct ENGINE_API CacheScriptComponent
    {
        FCT::JS::Object* m_jsObject = nullptr;
        FCT::PyBri::Object* m_pyObject = nullptr;
    };

    struct ENGINE_API ScriptFunctionTableComponent
    {
        std::vector<std::string> onTicker;
        std::string className;

        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & onTicker;
            if (version > 0) {
                ar & className;
            }
        }
    };

    struct ENGINE_API SkyboxComponent
    {
        std::string texturePath;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & texturePath;
        }
    };

    struct CacheSkyboxComponent
    {
        FCT::Image* texture = nullptr;
        bool visible = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            CacheResource base{visible};
            ar & base;
        }
    };

    struct ENGINE_API SkeletalAnimatorComponent
    {
        std::string modelUuid;
        std::string clipName;
        float currentTime = 0.0f;
        float speed = 1.0f;
        bool playing = true;
        bool loop = true;

        std::string targetClipName;
        float targetTime = 0.0f;
        float blendDuration = 0.25f;
        float blendElapsed = 0.0f;
        float blendWeight = 0.0f;

        std::string layerClipName;
        float layerTime = 0.0f;
        float layerWeight = 0.0f;
        std::string layerMaskRootBone;
        bool lockRootPosition = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & modelUuid;
            ar & clipName;
            ar & currentTime;
            ar & speed;
            ar & playing;
            ar & loop;
            if (version >= 1) {
                ar & targetClipName;
                ar & targetTime;
                ar & blendDuration;
                ar & blendElapsed;
                ar & blendWeight;
                ar & layerClipName;
                ar & layerTime;
                ar & layerWeight;
                ar & layerMaskRootBone;
                ar & lockRootPosition;
            }
        }
    };

    struct ENGINE_API SkeletalBoneSocketComponent
    {
        std::string targetEntityUuid;
        std::string boneName;
        FCT::Vec3<> localPosition = FCT::Vec3(0.0f, 0.0f, 0.0f);
        FCT::Vec3<> localRotation = FCT::Vec3(0.0f, 0.0f, 0.0f);
        FCT::Vec3<> localScale = FCT::Vec3(1.0f, 1.0f, 1.0f);
        bool followRotation = true;
        bool followScale = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            (void)version;
            ar & targetEntityUuid;
            ar & boneName;
            ar & localPosition;
            ar & localRotation;
            ar & localScale;
            ar & followRotation;
            ar & followScale;
        }
    };

    struct ENGINE_API NetworkIdentityComponent
    {
        std::string networkGuid;
        int networkId = 0;
        int ownerClientId = 0;
        bool serverAuthority = true;
        bool autoReplicate = true;
        std::string prefabTag;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            (void)version;
            ar & networkGuid;
            ar & networkId;
            ar & ownerClientId;
            ar & serverAuthority;
            ar & autoReplicate;
            ar & prefabTag;
        }
    };

    struct ENGINE_API NetworkTransformComponent
    {
        bool syncPosition = true;
        bool syncRotation = true;
        bool syncScale = false;
        bool syncAnimation = true;
        float sendRateHz = 20.0f;
        float positionThreshold = 0.005f;
        float rotationThreshold = 0.15f;
        float interpolationBackTime = 0.10f;
        float interpolationSpeed = 15.0f;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            (void)version;
            ar & syncPosition;
            ar & syncRotation;
            ar & syncScale;
            ar & syncAnimation;
            ar & sendRateHz;
            ar & positionThreshold;
            ar & rotationThreshold;
            ar & interpolationBackTime;
            ar & interpolationSpeed;
        }
    };

    struct ENGINE_API NetworkReplicatedComponent
    {
        std::vector<std::string> componentNames;
        float syncIntervalSec = 0.10f;
        bool reliableSync = false;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            (void)version;
            ar & componentNames;
            ar & syncIntervalSec;
            ar & reliableSync;
        }
    };

    struct TransformSnapshotSample
    {
        double serverTimestamp = 0.0;
        uint32_t sequence = 0;
        FCT::Vec3<> position = FCT::Vec3(0.0f, 0.0f, 0.0f);
        FCT::Vec3<> rotation = FCT::Vec3(0.0f, 0.0f, 0.0f);
        FCT::Vec3<> scale = FCT::Vec3(1.0f, 1.0f, 1.0f);
        bool hasPosition = false;
        bool hasRotation = false;
        bool hasScale = false;
    };

    struct ENGINE_API CacheNetworkSyncComponent
    {
        std::deque<TransformSnapshotSample> snapshotBuffer;
        FCT::Vec3<> lastSentPosition = FCT::Vec3(0.0f, 0.0f, 0.0f);
        FCT::Vec3<> lastSentRotation = FCT::Vec3(0.0f, 0.0f, 0.0f);
        FCT::Vec3<> lastSentScale = FCT::Vec3(1.0f, 1.0f, 1.0f);
        std::string lastSentClipName;
        std::string lastSentTargetClipName;
        std::string lastSentLayerClipName;
        float lastSentAnimSpeed = 1.0f;
        float lastSentLayerWeight = 0.0f;
        bool lastSentAnimPlaying = false;
        bool lastSentAnimLoop = true;
        bool hasBaselineSent = false;
        float transformSendAccumulator = 0.0f;
        float fieldSyncAccumulator = 0.0f;
        uint32_t lastAppliedTransformSeq = 0;
        uint32_t lastAppliedFieldSeq = 0;
        std::unordered_map<std::string, uint64_t> lastReflectedFieldHashes;
    };

    struct ENGINE_API CacheSkeletalAnimation
    {
        FCT::Uniform* uniform = nullptr;
        std::shared_ptr<FCT::ModelSkeletonData> skeleton;
        std::string loadedModelUuid;
        std::vector<FCT::Mat4> finalBoneMatrices;
        std::vector<FCT::Mat4> globalNodeMatrices;
        bool hasBones = false;
        bool init = false;
        bool ownsUniform = true;

        CacheSkeletalAnimation() = default;

        explicit CacheSkeletalAnimation(FCT::Context* ctx)
            : finalBoneMatrices(MAX_BONES, FCT::Mat4()), hasBones(false), init(false), ownsUniform(true)
        {
            uniform = new FCT::Uniform(ctx, BoneUniformSlot);
        }

        ~CacheSkeletalAnimation()
        {
            if (ownsUniform && uniform) {
                delete uniform;
                uniform = nullptr;
            }
        }

        CacheSkeletalAnimation(const CacheSkeletalAnimation&) = delete;
        CacheSkeletalAnimation& operator=(const CacheSkeletalAnimation&) = delete;

        CacheSkeletalAnimation(CacheSkeletalAnimation&& other) noexcept
            : uniform(other.uniform),
              skeleton(std::move(other.skeleton)),
              loadedModelUuid(std::move(other.loadedModelUuid)),
              finalBoneMatrices(std::move(other.finalBoneMatrices)),
              globalNodeMatrices(std::move(other.globalNodeMatrices)),
              hasBones(other.hasBones),
              init(other.init),
              ownsUniform(other.ownsUniform)
        {
            other.uniform = nullptr;
            other.ownsUniform = false;
        }

        CacheSkeletalAnimation& operator=(CacheSkeletalAnimation&& other) noexcept
        {
            if (this != &other) {
                if (ownsUniform && uniform) {
                    delete uniform;
                }
                uniform = other.uniform;
                skeleton = std::move(other.skeleton);
                loadedModelUuid = std::move(other.loadedModelUuid);
                finalBoneMatrices = std::move(other.finalBoneMatrices);
                globalNodeMatrices = std::move(other.globalNodeMatrices);
                hasBones = other.hasBones;
                init = other.init;
                ownsUniform = other.ownsUniform;
                other.uniform = nullptr;
                other.ownsUniform = false;
            }
            return *this;
        }
    };

    enum class RigidBodyType : uint8_t {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2
    };

    struct ENGINE_API RigidBodyComponent {
        RigidBodyType bodyType = RigidBodyType::Dynamic;
        float mass = 1.0f;
        bool useGravity = true;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        FCT::Vec3<> linearVelocity{0.0f, 0.0f, 0.0f};
        FCT::Vec3<> angularVelocity{0.0f, 0.0f, 0.0f};
        bool freezePositionX = false;
        bool freezePositionY = false;
        bool freezePositionZ = false;
        bool freezeRotationX = false;
        bool freezeRotationY = false;
        bool freezeRotationZ = false;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & bodyType;
            ar & mass;
            ar & useGravity;
            ar & linearDamping;
            ar & angularDamping;
            ar & linearVelocity;
            ar & angularVelocity;
            ar & freezePositionX;
            ar & freezePositionY;
            ar & freezePositionZ;
            ar & freezeRotationX;
            ar & freezeRotationY;
            ar & freezeRotationZ;
        }
    };

    struct ENGINE_API PhysicsMaterialComponent {
        float staticFriction = 0.5f;
        float dynamicFriction = 0.5f;
        float restitution = 0.1f;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & staticFriction;
            ar & dynamicFriction;
            ar & restitution;
        }
    };

    struct ENGINE_API BoxColliderComponent {
        FCT::Vec3<> halfExtents{0.5f, 0.5f, 0.5f};
        FCT::Vec3<> center{0.0f, 0.0f, 0.0f};
        bool isTrigger = false;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & halfExtents;
            ar & center;
            ar & isTrigger;
        }
    };

    struct ENGINE_API SphereColliderComponent {
        float radius = 0.5f;
        FCT::Vec3<> center{0.0f, 0.0f, 0.0f};
        bool isTrigger = false;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & radius;
            ar & center;
            ar & isTrigger;
        }
    };

    struct ENGINE_API CapsuleColliderComponent {
        float radius = 0.5f;
        float halfHeight = 0.5f;
        FCT::Vec3<> center{0.0f, 0.0f, 0.0f};
        bool isTrigger = false;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & radius;
            ar & halfHeight;
            ar & center;
            ar & isTrigger;
        }
    };

    struct ENGINE_API CachePhysicsActor {
        physx::PxRigidActor* actor = nullptr;
        physx::PxShape* shape = nullptr;
        RigidBodyType bodyType = RigidBodyType::Dynamic;
        bool isKinematic = false;
        uint32_t lastTransformVersion = 0;
    };

    struct ENGINE_API CacheGpuDrivenBounds {
        FCT::Vec3<> localCenter = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
        float localRadius = 0.0f;
        bool enableGpuCulling = true;
        bool valid = false;
    };

    using AllComponentsList = std::tuple<
        NameTag,
        StaticMeshInstance,
        OrmTextureComponent,
        AlbedoTextureComponent,
        NormalTextureComponent,
        DirectionalLightComponent,
        DiffuseTextureComponent,
        PositionComponent,
        RotationComponent,
        ScaleComponent,
        CameraComponent,
        SkyboxComponent,
        ScriptFunctionTableComponent,
        PointLightComponent,
        PBRMaterialComponent,
        TickerScriptComponent,
        OnStartScriptComponent,
        ShininessComponent,
        EmissiveTextureComponent,
        SkeletalAnimatorComponent,
        HierarchyComponent,
        SkeletalBoneSocketComponent,
        NetworkIdentityComponent,
        NetworkTransformComponent,
        NetworkReplicatedComponent,
        RuntimeSubEntityTag,
        RigidBodyComponent,
        PhysicsMaterialComponent,
        BoxColliderComponent,
        SphereColliderComponent,
        CapsuleColliderComponent,
        CacheShininess,
        CacheRotationMatrix,
        CacheModelMatrix,
        CacheScriptComponent,
        CacheSkyboxComponent,
        CacheSkeletalAnimation,
        CacheNetworkSyncComponent,
        CachePhysicsActor
    >;

    template<typename T>
    inline constexpr bool is_runtime_only_component_v =
        std::is_same_v<T, CacheResource> ||
        std::is_same_v<T, CacheShininess> ||
        std::is_same_v<T, CacheRotationMatrix> ||
        std::is_same_v<T, CacheModelMatrix> ||
        std::is_same_v<T, CacheScriptComponent> ||
        std::is_same_v<T, CacheSkyboxComponent> ||
        std::is_same_v<T, CacheSkeletalAnimation> ||
        std::is_same_v<T, CacheNetworkSyncComponent> ||
        std::is_same_v<T, CachePhysicsActor> ||
        std::is_same_v<T, RuntimeSubEntityTag>;

    template<typename T>
    inline constexpr bool is_persistent_component_v = !is_runtime_only_component_v<T>;

    namespace detail {
        template<typename SnapshotType, typename ArchiveWrapper, typename Tuple, size_t... Is>
        void serializePersistentComponentsImpl(SnapshotType&& snapshot, ArchiveWrapper& wrapper, std::index_sequence<Is...>)
        {
            snapshot.template get<entt::entity>(wrapper);
            (([&]() {
                using CompType = std::tuple_element_t<Is, Tuple>;
                if constexpr (is_persistent_component_v<CompType>) {
                    snapshot.template get<CompType>(wrapper);
                }
            }()), ...);
        }

        template<typename Tuple, size_t... Is>
        void copyPersistentComponentsForEntityImpl(
            const entt::registry& src,
            entt::registry& dst,
            entt::entity srcEntity,
            entt::entity dstEntity,
            std::index_sequence<Is...>)
        {
            (([&]() {
                using CompType = std::tuple_element_t<Is, Tuple>;
                if constexpr (is_persistent_component_v<CompType>) {
                    if (src.all_of<CompType>(srcEntity)) {
                        dst.emplace_or_replace<CompType>(dstEntity, src.get<CompType>(srcEntity));
                    }
                }
            }()), ...);
        }
    }

    template<typename SnapshotType, typename ArchiveWrapper>
    void SerializeComponents(SnapshotType&& snapshot, ArchiveWrapper& wrapper)
    {
        detail::serializePersistentComponentsImpl<SnapshotType, ArchiveWrapper, AllComponentsList>(
            std::forward<SnapshotType>(snapshot),
            wrapper,
            std::make_index_sequence<std::tuple_size_v<AllComponentsList>>{});
    }

    inline void CopyPersistentRegistryForSnapshot(const entt::registry& src, entt::registry& dst)
    {
        dst.clear();
        const auto* entityStorage = src.storage<entt::entity>();
        if (!entityStorage) {
            return;
        }
        for (auto entity : *entityStorage) {
            if (!src.valid(entity) || src.all_of<RuntimeSubEntityTag>(entity)) {
                continue;
            }
            entt::entity dstEntity = dst.create(entity);
            detail::copyPersistentComponentsForEntityImpl<AllComponentsList>(
                src,
                dst,
                entity,
                dstEntity,
                std::make_index_sequence<std::tuple_size_v<AllComponentsList>>{});
        }
    }
}

BOOST_CLASS_VERSION(MQEngine::ScriptFunctionTableComponent, 1)
BOOST_CLASS_VERSION(MQEngine::CameraComponent, 1)
BOOST_CLASS_VERSION(MQEngine::SkeletalAnimatorComponent, 1)
#endif //COMPONENT_H
