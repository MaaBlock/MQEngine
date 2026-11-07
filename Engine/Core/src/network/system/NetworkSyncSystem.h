#ifndef ENGINE_NETWORK_SYNC_SYSTEM_H
#define ENGINE_NETWORK_SYNC_SYSTEM_H

#include "../../scene/manager/DataManager.h"
#include "../../scene/data/Component.h"
#include "../manager/NetworkManager.h"
#include "../../scene/system/ComponentReflection.h"
#include "../../runtime/system/ISystem.h"

namespace MQEngine {

    /**
     * @brief 网络实体状态同步系统，负责全量基线快照、实体生成销毁、变换与骨骼动画增量压缩、反射字段复制及客户端时间缓冲插值
     */
    class ENGINE_API NetworkSyncSystem : public ISystem {
    public:
        NetworkSyncSystem(DataManager* dataManager, NetworkManager* networkManager);
        ~NetworkSyncSystem() override = default;

        void setDeltaTime(float deltaTime) noexcept { m_deltaTime = deltaTime; }

        std::vector<std::string> getSuccessors() override {
            return {"SkeletalAnimationSystem"};
        }

        void updateLogic() override;
        void updateRender() override {}

        bool assignEntityAuthority(const std::string& entityUuid, int ownerClientId, bool serverAuthority);
        int getEntityNetworkId(const std::string& entityUuid) const;
        std::string findEntityUuidByNetworkId(int networkId) const;
        uint32_t getReplicatedEntityCount() const;
        void forceFullSnapshotBroadcast();

    private:
        struct ReplicatedEntityRef {
            entt::registry* registry = nullptr;
            entt::entity entity = entt::null;
            int networkId = 0;
            std::string guid;
        };

        struct ReflectedFieldPatch {
            std::string componentName;
            std::string fieldName;
            ComponentValue value;
        };

        void rebuildEntityIndex();
        int resolveOrAssignNetworkId(
            entt::registry* registry,
            entt::entity entity,
            NetworkIdentityComponent& identity);

        void handleInboundSyncPackets();
        void handleEntitySpawnBatchPacket(const InboundNetworkPacket& packet);
        void handleEntityDespawnBatchPacket(const InboundNetworkPacket& packet);
        void handleStateDeltaBatchPacket(const InboundNetworkPacket& packet);
        void handleClientAuthorityDeltaPacket(const InboundNetworkPacket& packet);
        void handleReflectedFieldsDeltaPacket(const InboundNetworkPacket& packet);

        void sendBaselineToNewClients();
        void syncServerEntityLifecycle();
        void sendSpawnBatchForEntities(
            const std::vector<ReplicatedEntityRef>& entities,
            int targetClientId);
        void writeSingleEntitySpawnRecord(
            NetworkByteWriter& writer,
            entt::registry* registry,
            entt::entity entity);
        bool readAndApplySingleEntitySpawnRecord(NetworkByteReader& reader);

        void replicateServerTransformDeltas();
        void sendClientAuthorityDeltas();
        uint8_t computeEntityTransformDirtyMask(
            entt::registry* registry,
            entt::entity entity,
            const NetworkTransformComponent& netTrans,
            CacheNetworkSyncComponent& cache) const;
        void writeEntityTransformDelta(
            NetworkByteWriter& writer,
            entt::registry* registry,
            entt::entity entity,
            int networkId,
            uint8_t dirtyMask,
            uint32_t sequence,
            CacheNetworkSyncComponent& cache);
        bool readAndApplyEntityTransformDelta(
            NetworkByteReader& reader,
            double serverTimestamp,
            bool isServerValidatingClient,
            int senderClientId);
        void applyRemoteAnimatorState(
            entt::registry* registry,
            entt::entity entity,
            uint8_t dirtyMask,
            const std::string& clipName,
            bool playing,
            bool loop,
            float speed,
            const std::string& targetClipName,
            float blendDuration,
            const std::string& layerClipName,
            float layerWeight,
            const std::string& layerMaskBone);

        void replicateReflectedFieldsDeltas(bool isServerPass);
        void collectEntityDirtyReflectedFields(
            ComponentReflection* refl,
            entt::registry* registry,
            entt::entity entity,
            const NetworkReplicatedComponent& rep,
            CacheNetworkSyncComponent& cache,
            bool forceAll,
            std::vector<ReflectedFieldPatch>& outPatches);
        void applyReflectedFieldPatches(
            ComponentReflection* refl,
            entt::registry* registry,
            entt::entity entity,
            const std::vector<ReflectedFieldPatch>& patches);

        void interpolateRemoteClientEntities();
        void pushTransformSnapshot(
            CacheNetworkSyncComponent& cache,
            const TransformSnapshotSample& sample,
            const entt::registry* registry,
            entt::entity entity);

        entt::registry* getPrimarySceneRegistry() const;
        const ReplicatedEntityRef* findByNetworkId(int networkId) const;
        const ReplicatedEntityRef* findByGuid(const std::string& guid) const;
        ComponentReflection* resolveReflection() const;

        DataManager* m_dataManager = nullptr;
        NetworkManager* m_networkManager = nullptr;
        float m_deltaTime = 0.0f;
        uint32_t m_nextSyncSeq = 1;
        int m_nextDynamicNetworkId = 1000000;
        NetworkRole m_lastActiveRole = NetworkRole::Offline;

        std::vector<ReplicatedEntityRef> m_activeEntities;
        std::unordered_map<int, size_t> m_idToIndex;
        std::unordered_map<std::string, size_t> m_guidToIndex;
        std::unordered_set<int> m_serverKnownIds;
        std::unordered_set<int> m_remoteSpawnedIds;
        std::vector<InboundNetworkPacket> m_inboundBuffer;
        std::vector<int> m_clientIdBuffer;
    };

} // namespace MQEngine

#endif // ENGINE_NETWORK_SYNC_SYSTEM_H
