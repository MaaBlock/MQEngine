#include "NetworkSyncSystem.h"
#include "../../runtime/core/Engine.h"

namespace MQEngine {

    namespace {
        constexpr size_t kMaxSnapshotHistory = 32;
        constexpr size_t kUdpDeltaBatchSoftLimitBytes = 1200;

        int computeDeterministicNetworkId(std::string_view guid) noexcept {
            uint32_t hash = 2166136261u;
            for (unsigned char ch : guid) {
                hash ^= static_cast<uint32_t>(ch);
                hash *= 16777619u;
            }
            const int positiveId = static_cast<int>(hash & 0x7FFFFFFFu);
            return positiveId > 0 ? positiveId : 1;
        }

        uint64_t fnv1aMixBytes(uint64_t seed, const void* data, size_t len) noexcept {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < len; ++i) {
                seed ^= static_cast<uint64_t>(bytes[i]);
                seed *= 1099511628211ull;
            }
            return seed;
        }

        uint64_t hashComponentValue(const ComponentValue& val) noexcept {
            uint64_t h = 14695981039346656037ull;
            const uint8_t idx = static_cast<uint8_t>(val.index());
            h = fnv1aMixBytes(h, &idx, sizeof(idx));

            std::visit([&h](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    const uint8_t b = v ? 1u : 0u;
                    h = fnv1aMixBytes(h, &b, sizeof(b));
                } else if constexpr (std::is_same_v<T, int> ||
                                     std::is_same_v<T, float> ||
                                     std::is_same_v<T, double>) {
                    h = fnv1aMixBytes(h, &v, sizeof(v));
                } else if constexpr (std::is_same_v<T, std::string>) {
                    h = fnv1aMixBytes(h, v.data(), v.size());
                } else if constexpr (std::is_same_v<T, FCT::Vec2>) {
                    h = fnv1aMixBytes(h, &v.x, sizeof(float));
                    h = fnv1aMixBytes(h, &v.y, sizeof(float));
                } else if constexpr (std::is_same_v<T, FCT::Vec3<>>) {
                    h = fnv1aMixBytes(h, &v.x, sizeof(float));
                    h = fnv1aMixBytes(h, &v.y, sizeof(float));
                    h = fnv1aMixBytes(h, &v.z, sizeof(float));
                } else if constexpr (std::is_same_v<T, FCT::Vec4>) {
                    h = fnv1aMixBytes(h, &v.x, sizeof(float));
                    h = fnv1aMixBytes(h, &v.y, sizeof(float));
                    h = fnv1aMixBytes(h, &v.z, sizeof(float));
                    h = fnv1aMixBytes(h, &v.w, sizeof(float));
                } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                    for (const auto& item : v) {
                        const uint32_t len = static_cast<uint32_t>(item.size());
                        h = fnv1aMixBytes(h, &len, sizeof(len));
                        h = fnv1aMixBytes(h, item.data(), item.size());
                    }
                }
            }, val);
            return h;
        }

        float shortestAngleDeltaDeg(float fromDeg, float toDeg) noexcept {
            float diff = std::fmod(toDeg - fromDeg, 360.0f);
            if (diff > 180.0f) {
                diff -= 360.0f;
            } else if (diff < -180.0f) {
                diff += 360.0f;
            }
            return diff;
        }

        float lerpAngleDeg(float a, float b, float t) noexcept {
            return a + shortestAngleDeltaDeg(a, b) * t;
        }

        FCT::Vec3<> lerpVec3(const FCT::Vec3<>& a, const FCT::Vec3<>& b, float t) noexcept {
            return FCT::Vec3<>(
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t);
        }

        FCT::Vec3<> lerpEulerAnglesDeg(const FCT::Vec3<>& a, const FCT::Vec3<>& b, float t) noexcept {
            return FCT::Vec3<>(
                lerpAngleDeg(a.x, b.x, t),
                lerpAngleDeg(a.y, b.y, t),
                lerpAngleDeg(a.z, b.z, t));
        }

        float maxEulerDeltaDeg(const FCT::Vec3<>& a, const FCT::Vec3<>& b) noexcept {
            const float dx = std::abs(shortestAngleDeltaDeg(a.x, b.x));
            const float dy = std::abs(shortestAngleDeltaDeg(a.y, b.y));
            const float dz = std::abs(shortestAngleDeltaDeg(a.z, b.z));
            return std::max({dx, dy, dz});
        }

        float distanceSqVec3(const FCT::Vec3<>& a, const FCT::Vec3<>& b) noexcept {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        }

        bool isNewerSequence(uint32_t incoming, uint32_t lastApplied) noexcept {
            if (lastApplied == 0) {
                return true;
            }
            return static_cast<int32_t>(incoming - lastApplied) > 0;
        }

        bool isProtectedCoreComponent(const std::string& compName) noexcept {
            return compName == "NameTag" ||
                   compName == "NetworkIdentityComponent" ||
                   compName == "HierarchyComponent";
        }

        std::string generateRandomUuidString() {
            boost::uuids::random_generator gen;
            return boost::uuids::to_string(gen());
        }
    }

    NetworkSyncSystem::NetworkSyncSystem(DataManager* dataManager, NetworkManager* networkManager)
        : m_dataManager(dataManager), m_networkManager(networkManager) {}

    void NetworkSyncSystem::updateLogic() {
        ZoneScopedN("NetworkSyncSystem::updateLogic");
        if (!m_dataManager || !m_networkManager) {
            return;
        }

        const NetworkRole role = m_networkManager->getRole();
        if (role != m_lastActiveRole) {
            m_serverKnownIds.clear();
            m_remoteSpawnedIds.clear();
            m_lastActiveRole = role;
        }
        if (role == NetworkRole::Offline) {
            return;
        }

        rebuildEntityIndex();
        handleInboundSyncPackets();

        if (m_networkManager->isServerAuthorityHost()) {
            sendBaselineToNewClients();
            syncServerEntityLifecycle();
            replicateServerTransformDeltas();
            replicateReflectedFieldsDeltas(true);
        } else if (m_networkManager->isPureRemoteClient() && m_networkManager->isConnected()) {
            sendClientAuthorityDeltas();
            replicateReflectedFieldsDeltas(false);
            interpolateRemoteClientEntities();
        }
    }

    bool NetworkSyncSystem::assignEntityAuthority(
        const std::string& entityUuid,
        int ownerClientId,
        bool serverAuthority) {
        const auto* ref = findByGuid(entityUuid);
        if (!ref || !ref->registry || !ref->registry->valid(ref->entity)) {
            return false;
        }
        auto* identity = ref->registry->try_get<NetworkIdentityComponent>(ref->entity);
        if (!identity) {
            return false;
        }
        identity->ownerClientId = ownerClientId;
        identity->serverAuthority = serverAuthority;

        if (m_networkManager && m_networkManager->isServerAuthorityHost()) {
            std::vector<ReplicatedEntityRef> single{*ref};
            sendSpawnBatchForEntities(single, -1);
        }
        return true;
    }

    int NetworkSyncSystem::getEntityNetworkId(const std::string& entityUuid) const {
        const auto* ref = findByGuid(entityUuid);
        return ref ? ref->networkId : 0;
    }

    std::string NetworkSyncSystem::findEntityUuidByNetworkId(int networkId) const {
        const auto* ref = findByNetworkId(networkId);
        return ref ? ref->guid : "";
    }

    uint32_t NetworkSyncSystem::getReplicatedEntityCount() const {
        return static_cast<uint32_t>(m_activeEntities.size());
    }

    void NetworkSyncSystem::forceFullSnapshotBroadcast() {
        if (!m_networkManager || !m_networkManager->isServerAuthorityHost()) {
            return;
        }
        rebuildEntityIndex();
        if (!m_activeEntities.empty()) {
            sendSpawnBatchForEntities(m_activeEntities, -1);
        }
    }

    void NetworkSyncSystem::rebuildEntityIndex() {
        m_activeEntities.clear();
        m_idToIndex.clear();
        m_guidToIndex.clear();

        if (!m_dataManager) {
            return;
        }

        const auto registries = m_dataManager->currentRegistries();
        for (auto* registry : registries) {
            if (!registry) {
                continue;
            }
            auto view = registry->view<NetworkIdentityComponent>();
            for (auto entity : view) {
                if (registry->all_of<RuntimeSubEntityTag>(entity)) {
                    continue;
                }
                auto& identity = view.get<NetworkIdentityComponent>(entity);
                if (!identity.autoReplicate) {
                    continue;
                }

                const int netId = resolveOrAssignNetworkId(registry, entity, identity);
                const size_t idx = m_activeEntities.size();
                m_activeEntities.push_back(ReplicatedEntityRef{
                    registry,
                    entity,
                    netId,
                    identity.networkGuid
                });
                m_idToIndex[netId] = idx;
                if (!identity.networkGuid.empty()) {
                    m_guidToIndex[identity.networkGuid] = idx;
                }
            }
        }
    }

    int NetworkSyncSystem::resolveOrAssignNetworkId(
        entt::registry* registry,
        entt::entity entity,
        NetworkIdentityComponent& identity) {
        if (identity.networkGuid.empty()) {
            if (auto* tag = registry->try_get<NameTag>(entity)) {
                if (tag->uuid.empty()) {
                    tag->uuid = generateRandomUuidString();
                }
                identity.networkGuid = tag->uuid;
            } else {
                identity.networkGuid = generateRandomUuidString();
            }
        }

        if (identity.networkId <= 0) {
            identity.networkId = !identity.networkGuid.empty()
                ? computeDeterministicNetworkId(identity.networkGuid)
                : m_nextDynamicNetworkId++;
        }

        while (m_idToIndex.find(identity.networkId) != m_idToIndex.end()) {
            identity.networkId = (identity.networkId < 0x7FFFFFF0) ? (identity.networkId + 1) : 1;
        }
        return identity.networkId;
    }

    void NetworkSyncSystem::handleInboundSyncPackets() {
        m_inboundBuffer.clear();
        m_networkManager->drainSyncPackets(m_inboundBuffer);
        if (m_inboundBuffer.empty()) {
            return;
        }

        for (const auto& pkt : m_inboundBuffer) {
            switch (pkt.header.type) {
                case NetworkPacketType::EntitySpawnBatch:
                    handleEntitySpawnBatchPacket(pkt);
                    break;
                case NetworkPacketType::EntityDespawnBatch:
                    handleEntityDespawnBatchPacket(pkt);
                    break;
                case NetworkPacketType::StateDeltaBatch:
                    handleStateDeltaBatchPacket(pkt);
                    break;
                case NetworkPacketType::ClientAuthorityDelta:
                    handleClientAuthorityDeltaPacket(pkt);
                    break;
                case NetworkPacketType::ReflectedFieldsDelta:
                    handleReflectedFieldsDeltaPacket(pkt);
                    break;
                default:
                    break;
            }
        }
    }

    void NetworkSyncSystem::handleEntitySpawnBatchPacket(const InboundNetworkPacket& packet) {
        if (!m_networkManager->isPureRemoteClient()) {
            return;
        }
        NetworkByteReader reader(packet.payload);
        uint32_t entityCount = 0;
        if (!reader.readU32(entityCount) || entityCount > 65536) {
            return;
        }
        for (uint32_t i = 0; i < entityCount; ++i) {
            if (!readAndApplySingleEntitySpawnRecord(reader)) {
                break;
            }
        }
        rebuildEntityIndex();
    }

    void NetworkSyncSystem::handleEntityDespawnBatchPacket(const InboundNetworkPacket& packet) {
        if (!m_networkManager->isPureRemoteClient()) {
            return;
        }
        NetworkByteReader reader(packet.payload);
        uint32_t count = 0;
        if (!reader.readU32(count) || count > 65536) {
            return;
        }
        for (uint32_t i = 0; i < count; ++i) {
            int32_t netId = 0;
            if (!reader.readI32(netId)) {
                break;
            }
            if (m_remoteSpawnedIds.count(netId) == 0) {
                continue;
            }
            if (const auto* ref = findByNetworkId(netId)) {
                if (ref->registry && ref->registry->valid(ref->entity)) {
                    ref->registry->destroy(ref->entity);
                }
            }
            m_remoteSpawnedIds.erase(netId);
        }
        rebuildEntityIndex();
    }

    void NetworkSyncSystem::handleStateDeltaBatchPacket(const InboundNetworkPacket& packet) {
        if (!m_networkManager->isPureRemoteClient()) {
            return;
        }
        NetworkByteReader reader(packet.payload);
        double serverTimestamp = 0.0;
        uint32_t count = 0;
        if (!reader.readF64(serverTimestamp) || !reader.readU32(count) || count > 8192) {
            return;
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (!readAndApplyEntityTransformDelta(reader, serverTimestamp, false, packet.sourceClientId)) {
                break;
            }
        }
    }

    void NetworkSyncSystem::handleClientAuthorityDeltaPacket(const InboundNetworkPacket& packet) {
        if (!m_networkManager->isServerAuthorityHost()) {
            return;
        }
        NetworkByteReader reader(packet.payload);
        double clientTimestamp = 0.0;
        uint32_t count = 0;
        if (!reader.readF64(clientTimestamp) || !reader.readU32(count) || count > 1024) {
            return;
        }
        (void)clientTimestamp;
        const double serverNow = m_networkManager->getLocalTimeSec();
        for (uint32_t i = 0; i < count; ++i) {
            if (!readAndApplyEntityTransformDelta(reader, serverNow, true, packet.sourceClientId)) {
                break;
            }
        }
    }

    void NetworkSyncSystem::handleReflectedFieldsDeltaPacket(const InboundNetworkPacket& packet) {
        ComponentReflection* refl = resolveReflection();
        if (!refl) {
            return;
        }

        NetworkByteReader reader(packet.payload);
        int32_t netId = 0;
        uint32_t seq = 0;
        uint32_t patchCount = 0;
        if (!reader.readI32(netId) || !reader.readU32(seq) || !reader.readU32(patchCount) || patchCount > 2048) {
            return;
        }

        const auto* ref = findByNetworkId(netId);
        if (!ref || !ref->registry || !ref->registry->valid(ref->entity)) {
            return;
        }
        const auto* identity = ref->registry->try_get<NetworkIdentityComponent>(ref->entity);
        if (!identity) {
            return;
        }

        if (m_networkManager->isServerAuthorityHost()) {
            if (identity->serverAuthority || identity->ownerClientId != packet.sourceClientId) {
                return;
            }
        } else if (m_networkManager->isPureRemoteClient()) {
            if (!identity->serverAuthority && identity->ownerClientId == m_networkManager->getLocalClientId()) {
                return;
            }
        }

        auto& cache = ref->registry->get_or_emplace<CacheNetworkSyncComponent>(ref->entity);
        if (!isNewerSequence(seq, cache.lastAppliedFieldSeq)) {
            return;
        }
        cache.lastAppliedFieldSeq = seq;

        std::vector<ReflectedFieldPatch> patches;
        patches.reserve(patchCount);
        for (uint32_t i = 0; i < patchCount; ++i) {
            ReflectedFieldPatch patch;
            if (!reader.readString(patch.componentName, 256) ||
                !reader.readString(patch.fieldName, 256) ||
                !reader.readComponentValue(patch.value)) {
                return;
            }
            patches.push_back(std::move(patch));
        }

        applyReflectedFieldPatches(refl, ref->registry, ref->entity, patches);
    }

    void NetworkSyncSystem::sendBaselineToNewClients() {
        m_clientIdBuffer.clear();
        m_networkManager->drainNewClientIds(m_clientIdBuffer);
        if (m_clientIdBuffer.empty() || m_activeEntities.empty()) {
            return;
        }
        for (int clientId : m_clientIdBuffer) {
            sendSpawnBatchForEntities(m_activeEntities, clientId);
        }
    }

    void NetworkSyncSystem::syncServerEntityLifecycle() {
        m_clientIdBuffer.clear();
        m_networkManager->drainDisconnectedClientIds(m_clientIdBuffer);

        std::unordered_set<int> currentIds;
        currentIds.reserve(m_activeEntities.size());
        std::vector<ReplicatedEntityRef> newlySpawned;

        for (const auto& ref : m_activeEntities) {
            currentIds.insert(ref.networkId);
            if (m_serverKnownIds.find(ref.networkId) == m_serverKnownIds.end()) {
                newlySpawned.push_back(ref);
            }
        }

        std::vector<int> despawnedIds;
        for (int oldId : m_serverKnownIds) {
            if (currentIds.find(oldId) == currentIds.end()) {
                despawnedIds.push_back(oldId);
            }
        }

        m_serverKnownIds = std::move(currentIds);

        if (!newlySpawned.empty() && m_networkManager->getConnectedClientCount() > 0) {
            sendSpawnBatchForEntities(newlySpawned, -1);
        }

        if (!despawnedIds.empty() && m_networkManager->getConnectedClientCount() > 0) {
            NetworkByteWriter writer(16 + despawnedIds.size() * 4);
            writer.writeU32(static_cast<uint32_t>(despawnedIds.size()));
            for (int id : despawnedIds) {
                writer.writeI32(id);
            }
            (void)m_networkManager->broadcastSyncPacket(
                NetworkPacketType::EntityDespawnBatch,
                NetworkChannel::ReliableTcp,
                writer.buffer());
        }
    }

    void NetworkSyncSystem::sendSpawnBatchForEntities(
        const std::vector<ReplicatedEntityRef>& entities,
        int targetClientId) {
        if (entities.empty()) {
            return;
        }

        NetworkByteWriter writer(entities.size() * 256);
        writer.writeU32(static_cast<uint32_t>(entities.size()));
        for (const auto& ref : entities) {
            writeSingleEntitySpawnRecord(writer, ref.registry, ref.entity);
        }

        if (targetClientId >= 0) {
            (void)m_networkManager->sendSyncPacketToClient(
                targetClientId,
                NetworkPacketType::EntitySpawnBatch,
                NetworkChannel::ReliableTcp,
                writer.buffer());
        } else {
            (void)m_networkManager->broadcastSyncPacket(
                NetworkPacketType::EntitySpawnBatch,
                NetworkChannel::ReliableTcp,
                writer.buffer());
        }
    }

    void NetworkSyncSystem::writeSingleEntitySpawnRecord(
        NetworkByteWriter& writer,
        entt::registry* registry,
        entt::entity entity) {
        const auto& identity = registry->get<NetworkIdentityComponent>(entity);
        std::string entityName = "NetEntity";
        if (const auto* tag = registry->try_get<NameTag>(entity)) {
            entityName = tag->name;
        }

        writer.writeI32(identity.networkId);
        writer.writeString(identity.networkGuid);
        writer.writeString(entityName);
        writer.writeI32(identity.ownerClientId);
        writer.writeBool(identity.serverAuthority);
        writer.writeString(identity.prefabTag);

        const FCT::Vec3<> pos = registry->all_of<PositionComponent>(entity)
            ? registry->get<PositionComponent>(entity).position
            : FCT::Vec3<>(0.0f, 0.0f, 0.0f);
        const FCT::Vec3<> rot = registry->all_of<RotationComponent>(entity)
            ? registry->get<RotationComponent>(entity).rotation
            : FCT::Vec3<>(0.0f, 0.0f, 0.0f);
        const FCT::Vec3<> scl = registry->all_of<ScaleComponent>(entity)
            ? registry->get<ScaleComponent>(entity).scale
            : FCT::Vec3<>(1.0f, 1.0f, 1.0f);

        writer.writeVec3(pos);
        writer.writeVec3(rot);
        writer.writeVec3(scl);

        const auto* netTrans = registry->try_get<NetworkTransformComponent>(entity);
        writer.writeBool(netTrans != nullptr);
        if (netTrans) {
            writer.writeBool(netTrans->syncPosition);
            writer.writeBool(netTrans->syncRotation);
            writer.writeBool(netTrans->syncScale);
            writer.writeBool(netTrans->syncAnimation);
            writer.writeF32(netTrans->sendRateHz);
            writer.writeF32(netTrans->interpolationBackTime);
            writer.writeF32(netTrans->interpolationSpeed);
        }

        const auto* meshInst = registry->try_get<StaticMeshInstance>(entity);
        writer.writeBool(meshInst != nullptr);
        if (meshInst) {
            writer.writeString(meshInst->modelUuid);
        }

        const auto* animator = registry->try_get<SkeletalAnimatorComponent>(entity);
        writer.writeBool(animator != nullptr);
        if (animator) {
            writer.writeString(animator->modelUuid);
            writer.writeString(animator->clipName);
            writer.writeF32(animator->speed);
            writer.writeBool(animator->playing);
            writer.writeBool(animator->loop);
            writer.writeString(animator->layerClipName);
            writer.writeF32(animator->layerWeight);
            writer.writeString(animator->layerMaskRootBone);
            writer.writeBool(animator->lockRootPosition);
        }

        std::vector<ReflectedFieldPatch> patches;
        if (const auto* rep = registry->try_get<NetworkReplicatedComponent>(entity)) {
            if (ComponentReflection* refl = resolveReflection()) {
                auto& cache = registry->get_or_emplace<CacheNetworkSyncComponent>(entity);
                collectEntityDirtyReflectedFields(refl, registry, entity, *rep, cache, true, patches);
            }
        }
        writer.writeU32(static_cast<uint32_t>(patches.size()));
        for (const auto& p : patches) {
            writer.writeString(p.componentName);
            writer.writeString(p.fieldName);
            writer.writeComponentValue(p.value);
        }
    }

    bool NetworkSyncSystem::readAndApplySingleEntitySpawnRecord(NetworkByteReader& reader) {
        int32_t netId = 0;
        std::string guid;
        std::string entityName;
        int32_t ownerClientId = 0;
        bool serverAuthority = true;
        std::string prefabTag;
        FCT::Vec3<> pos;
        FCT::Vec3<> rot;
        FCT::Vec3<> scl;

        if (!reader.readI32(netId) ||
            !reader.readString(guid, 256) ||
            !reader.readString(entityName, 256) ||
            !reader.readI32(ownerClientId) ||
            !reader.readBool(serverAuthority) ||
            !reader.readString(prefabTag, 256) ||
            !reader.readVec3(pos) ||
            !reader.readVec3(rot) ||
            !reader.readVec3(scl)) {
            return false;
        }

        bool hasNetTrans = false;
        NetworkTransformComponent parsedTrans;
        if (!reader.readBool(hasNetTrans)) {
            return false;
        }
        if (hasNetTrans) {
            if (!reader.readBool(parsedTrans.syncPosition) ||
                !reader.readBool(parsedTrans.syncRotation) ||
                !reader.readBool(parsedTrans.syncScale) ||
                !reader.readBool(parsedTrans.syncAnimation) ||
                !reader.readF32(parsedTrans.sendRateHz) ||
                !reader.readF32(parsedTrans.interpolationBackTime) ||
                !reader.readF32(parsedTrans.interpolationSpeed)) {
                return false;
            }
        }

        bool hasMesh = false;
        std::string meshUuid;
        if (!reader.readBool(hasMesh)) {
            return false;
        }
        if (hasMesh && !reader.readString(meshUuid, 256)) {
            return false;
        }

        bool hasAnim = false;
        SkeletalAnimatorComponent parsedAnim;
        if (!reader.readBool(hasAnim)) {
            return false;
        }
        if (hasAnim) {
            if (!reader.readString(parsedAnim.modelUuid, 256) ||
                !reader.readString(parsedAnim.clipName, 256) ||
                !reader.readF32(parsedAnim.speed) ||
                !reader.readBool(parsedAnim.playing) ||
                !reader.readBool(parsedAnim.loop) ||
                !reader.readString(parsedAnim.layerClipName, 256) ||
                !reader.readF32(parsedAnim.layerWeight) ||
                !reader.readString(parsedAnim.layerMaskRootBone, 256) ||
                !reader.readBool(parsedAnim.lockRootPosition)) {
                return false;
            }
        }

        uint32_t patchCount = 0;
        if (!reader.readU32(patchCount) || patchCount > 4096) {
            return false;
        }
        std::vector<ReflectedFieldPatch> patches;
        patches.reserve(patchCount);
        for (uint32_t i = 0; i < patchCount; ++i) {
            ReflectedFieldPatch p;
            if (!reader.readString(p.componentName, 256) ||
                !reader.readString(p.fieldName, 256) ||
                !reader.readComponentValue(p.value)) {
                return false;
            }
            patches.push_back(std::move(p));
        }

        entt::registry* targetReg = nullptr;
        entt::entity targetEntity = entt::null;

        if (const auto* existingById = findByNetworkId(netId)) {
            targetReg = existingById->registry;
            targetEntity = existingById->entity;
        } else if (!guid.empty()) {
            if (const auto* existingByGuid = findByGuid(guid)) {
                targetReg = existingByGuid->registry;
                targetEntity = existingByGuid->entity;
            } else {
                for (auto* reg : m_dataManager->currentRegistries()) {
                    if (!reg) continue;
                    auto tagView = reg->view<NameTag>();
                    for (auto e : tagView) {
                        if (tagView.get<NameTag>(e).uuid == guid) {
                            targetReg = reg;
                            targetEntity = e;
                            break;
                        }
                    }
                    if (targetEntity != entt::null) break;
                }
            }
        }

        if (!targetReg || targetEntity == entt::null || !targetReg->valid(targetEntity)) {
            targetReg = getPrimarySceneRegistry();
            if (!targetReg) {
                return true;
            }
            targetEntity = targetReg->create();
            auto& tag = targetReg->emplace<NameTag>(targetEntity);
            tag.name = entityName;
            tag.uuid = guid.empty() ? generateRandomUuidString() : guid;
            m_remoteSpawnedIds.insert(netId);
        }

        auto& identity = targetReg->get_or_emplace<NetworkIdentityComponent>(targetEntity);
        identity.networkId = netId;
        identity.networkGuid = guid;
        identity.ownerClientId = ownerClientId;
        identity.serverAuthority = serverAuthority;
        identity.autoReplicate = true;
        identity.prefabTag = prefabTag;

        targetReg->get_or_emplace<PositionComponent>(targetEntity).position = pos;
        targetReg->get_or_emplace<RotationComponent>(targetEntity).rotation = rot;
        targetReg->get_or_emplace<ScaleComponent>(targetEntity).scale = scl;

        if (hasNetTrans) {
            targetReg->emplace_or_replace<NetworkTransformComponent>(targetEntity, parsedTrans);
        }
        if (hasMesh && !targetReg->all_of<StaticMeshInstance>(targetEntity)) {
            targetReg->emplace<StaticMeshInstance>(targetEntity, StaticMeshInstance{meshUuid});
        }
        if (hasAnim) {
            auto& anim = targetReg->get_or_emplace<SkeletalAnimatorComponent>(targetEntity);
            anim.modelUuid = parsedAnim.modelUuid;
            anim.clipName = parsedAnim.clipName;
            anim.speed = parsedAnim.speed;
            anim.playing = parsedAnim.playing;
            anim.loop = parsedAnim.loop;
            anim.layerClipName = parsedAnim.layerClipName;
            anim.layerWeight = parsedAnim.layerWeight;
            anim.layerMaskRootBone = parsedAnim.layerMaskRootBone;
            anim.lockRootPosition = parsedAnim.lockRootPosition;
        }

        auto& cache = targetReg->get_or_emplace<CacheNetworkSyncComponent>(targetEntity);
        cache.snapshotBuffer.clear();
        TransformSnapshotSample baselineSample;
        baselineSample.serverTimestamp = m_networkManager->getEstimatedServerTimeSec();
        baselineSample.position = pos;
        baselineSample.rotation = rot;
        baselineSample.scale = scl;
        baselineSample.hasPosition = true;
        baselineSample.hasRotation = true;
        baselineSample.hasScale = true;
        cache.snapshotBuffer.push_back(baselineSample);

        if (!patches.empty()) {
            if (ComponentReflection* refl = resolveReflection()) {
                applyReflectedFieldPatches(refl, targetReg, targetEntity, patches);
            }
        }
        return true;
    }

    void NetworkSyncSystem::replicateServerTransformDeltas() {
        if (m_networkManager->getConnectedClientCount() <= 0 || m_activeEntities.empty()) {
            return;
        }

        const double serverNow = m_networkManager->getLocalTimeSec();
        NetworkByteWriter batchWriter(kUdpDeltaBatchSoftLimitBytes + 256);
        uint32_t batchCount = 0;
        size_t countPatchOffset = 0;

        auto beginNewBatch = [&]() {
            batchWriter.clear();
            batchWriter.writeF64(serverNow);
            countPatchOffset = batchWriter.size();
            batchWriter.writeU32(0);
            batchCount = 0;
        };

        auto flushBatch = [&]() {
            if (batchCount == 0) {
                return;
            }
            batchWriter.patchU32(countPatchOffset, batchCount);
            (void)m_networkManager->broadcastSyncPacket(
                NetworkPacketType::StateDeltaBatch,
                NetworkChannel::UnreliableUdp,
                batchWriter.buffer());
            beginNewBatch();
        };

        beginNewBatch();

        for (const auto& ref : m_activeEntities) {
            const auto* netTrans = ref.registry->try_get<NetworkTransformComponent>(ref.entity);
            if (!netTrans) {
                continue;
            }

            auto& cache = ref.registry->get_or_emplace<CacheNetworkSyncComponent>(ref.entity);
            cache.transformSendAccumulator += m_deltaTime;
            const float interval = 1.0f / std::max(1.0f, netTrans->sendRateHz);
            if (cache.hasBaselineSent && cache.transformSendAccumulator < interval) {
                continue;
            }
            cache.transformSendAccumulator = 0.0f;

            const uint8_t dirtyMask = computeEntityTransformDirtyMask(ref.registry, ref.entity, *netTrans, cache);
            if (dirtyMask == 0) {
                continue;
            }

            const uint32_t seq = m_nextSyncSeq++;
            writeEntityTransformDelta(batchWriter, ref.registry, ref.entity, ref.networkId, dirtyMask, seq, cache);
            cache.hasBaselineSent = true;
            ++batchCount;

            if (batchWriter.size() >= kUdpDeltaBatchSoftLimitBytes) {
                flushBatch();
            }
        }

        flushBatch();
    }

    void NetworkSyncSystem::sendClientAuthorityDeltas() {
        const int localCid = m_networkManager->getLocalClientId();
        if (localCid <= 0 || m_activeEntities.empty()) {
            return;
        }

        const double clientNow = m_networkManager->getEstimatedServerTimeSec();
        NetworkByteWriter batchWriter(512);
        batchWriter.writeF64(clientNow);
        const size_t countPatchOffset = batchWriter.size();
        batchWriter.writeU32(0);
        uint32_t batchCount = 0;

        for (const auto& ref : m_activeEntities) {
            const auto& identity = ref.registry->get<NetworkIdentityComponent>(ref.entity);
            if (identity.serverAuthority || identity.ownerClientId != localCid) {
                continue;
            }
            const auto* netTrans = ref.registry->try_get<NetworkTransformComponent>(ref.entity);
            if (!netTrans) {
                continue;
            }

            auto& cache = ref.registry->get_or_emplace<CacheNetworkSyncComponent>(ref.entity);
            cache.transformSendAccumulator += m_deltaTime;
            const float interval = 1.0f / std::max(1.0f, netTrans->sendRateHz);
            if (cache.hasBaselineSent && cache.transformSendAccumulator < interval) {
                continue;
            }
            cache.transformSendAccumulator = 0.0f;

            const uint8_t dirtyMask = computeEntityTransformDirtyMask(ref.registry, ref.entity, *netTrans, cache);
            if (dirtyMask == 0) {
                continue;
            }

            const uint32_t seq = m_nextSyncSeq++;
            writeEntityTransformDelta(batchWriter, ref.registry, ref.entity, ref.networkId, dirtyMask, seq, cache);
            cache.hasBaselineSent = true;
            ++batchCount;

            if (batchWriter.size() >= kUdpDeltaBatchSoftLimitBytes) {
                batchWriter.patchU32(countPatchOffset, batchCount);
                (void)m_networkManager->sendSyncPacketToServer(
                    NetworkPacketType::ClientAuthorityDelta,
                    NetworkChannel::UnreliableUdp,
                    batchWriter.buffer());

                batchWriter.clear();
                batchWriter.writeF64(clientNow);
                batchWriter.writeU32(0);
                batchCount = 0;
            }
        }

        if (batchCount > 0) {
            batchWriter.patchU32(countPatchOffset, batchCount);
            (void)m_networkManager->sendSyncPacketToServer(
                NetworkPacketType::ClientAuthorityDelta,
                NetworkChannel::UnreliableUdp,
                batchWriter.buffer());
        }
    }

    uint8_t NetworkSyncSystem::computeEntityTransformDirtyMask(
        entt::registry* registry,
        entt::entity entity,
        const NetworkTransformComponent& netTrans,
        CacheNetworkSyncComponent& cache) const {
        uint8_t mask = 0;
        const float posThreshSq = netTrans.positionThreshold * netTrans.positionThreshold;

        if (netTrans.syncPosition && registry->all_of<PositionComponent>(entity)) {
            const auto& pos = registry->get<PositionComponent>(entity).position;
            if (!cache.hasBaselineSent || distanceSqVec3(pos, cache.lastSentPosition) > posThreshSq) {
                mask |= NetworkTransformDirtyFlags::kPosition;
            }
        }

        if (netTrans.syncRotation && registry->all_of<RotationComponent>(entity)) {
            const auto& rot = registry->get<RotationComponent>(entity).rotation;
            if (!cache.hasBaselineSent || maxEulerDeltaDeg(rot, cache.lastSentRotation) > netTrans.rotationThreshold) {
                mask |= NetworkTransformDirtyFlags::kRotation;
            }
        }

        if (netTrans.syncScale && registry->all_of<ScaleComponent>(entity)) {
            const auto& scl = registry->get<ScaleComponent>(entity).scale;
            if (!cache.hasBaselineSent || distanceSqVec3(scl, cache.lastSentScale) > posThreshSq) {
                mask |= NetworkTransformDirtyFlags::kScale;
            }
        }

        if (netTrans.syncAnimation) {
            if (const auto* anim = registry->try_get<SkeletalAnimatorComponent>(entity)) {
                if (!cache.hasBaselineSent ||
                    anim->clipName != cache.lastSentClipName ||
                    anim->playing != cache.lastSentAnimPlaying ||
                    anim->loop != cache.lastSentAnimLoop ||
                    std::abs(anim->speed - cache.lastSentAnimSpeed) > 0.01f) {
                    mask |= NetworkTransformDirtyFlags::kAnimation;
                }
                if (anim->targetClipName != cache.lastSentTargetClipName && !anim->targetClipName.empty()) {
                    mask |= NetworkTransformDirtyFlags::kCrossfade;
                }
                if (anim->layerClipName != cache.lastSentLayerClipName ||
                    std::abs(anim->layerWeight - cache.lastSentLayerWeight) > 0.01f) {
                    mask |= NetworkTransformDirtyFlags::kLayerAnim;
                }
            }
        }

        return mask;
    }

    void NetworkSyncSystem::writeEntityTransformDelta(
        NetworkByteWriter& writer,
        entt::registry* registry,
        entt::entity entity,
        int networkId,
        uint8_t dirtyMask,
        uint32_t sequence,
        CacheNetworkSyncComponent& cache) {
        writer.writeI32(networkId);
        writer.writeU32(sequence);
        writer.writeU8(dirtyMask);

        if (dirtyMask & NetworkTransformDirtyFlags::kPosition) {
            const auto& pos = registry->get<PositionComponent>(entity).position;
            writer.writeVec3(pos);
            cache.lastSentPosition = pos;
        }
        if (dirtyMask & NetworkTransformDirtyFlags::kRotation) {
            const auto& rot = registry->get<RotationComponent>(entity).rotation;
            writer.writeVec3(rot);
            cache.lastSentRotation = rot;
        }
        if (dirtyMask & NetworkTransformDirtyFlags::kScale) {
            const auto& scl = registry->get<ScaleComponent>(entity).scale;
            writer.writeVec3(scl);
            cache.lastSentScale = scl;
        }

        const auto* anim = registry->try_get<SkeletalAnimatorComponent>(entity);
        if (dirtyMask & NetworkTransformDirtyFlags::kAnimation) {
            const std::string clip = anim ? anim->clipName : "";
            const bool playing = anim ? anim->playing : false;
            const bool loop = anim ? anim->loop : true;
            const float speed = anim ? anim->speed : 1.0f;
            writer.writeString(clip);
            writer.writeBool(playing);
            writer.writeBool(loop);
            writer.writeF32(speed);
            cache.lastSentClipName = clip;
            cache.lastSentAnimPlaying = playing;
            cache.lastSentAnimLoop = loop;
            cache.lastSentAnimSpeed = speed;
        }
        if (dirtyMask & NetworkTransformDirtyFlags::kCrossfade) {
            const std::string targetClip = anim ? anim->targetClipName : "";
            const float blendDur = anim ? anim->blendDuration : 0.25f;
            writer.writeString(targetClip);
            writer.writeF32(blendDur);
            cache.lastSentTargetClipName = targetClip;
        }
        if (dirtyMask & NetworkTransformDirtyFlags::kLayerAnim) {
            const std::string layerClip = anim ? anim->layerClipName : "";
            const float layerWeight = anim ? anim->layerWeight : 0.0f;
            const std::string maskBone = anim ? anim->layerMaskRootBone : "";
            writer.writeString(layerClip);
            writer.writeF32(layerWeight);
            writer.writeString(maskBone);
            cache.lastSentLayerClipName = layerClip;
            cache.lastSentLayerWeight = layerWeight;
        }
    }

    bool NetworkSyncSystem::readAndApplyEntityTransformDelta(
        NetworkByteReader& reader,
        double serverTimestamp,
        bool isServerValidatingClient,
        int senderClientId) {
        int32_t netId = 0;
        uint32_t sequence = 0;
        uint8_t dirtyMask = 0;
        if (!reader.readI32(netId) || !reader.readU32(sequence) || !reader.readU8(dirtyMask)) {
            return false;
        }

        FCT::Vec3<> pos;
        FCT::Vec3<> rot;
        FCT::Vec3<> scl;
        if ((dirtyMask & NetworkTransformDirtyFlags::kPosition) && !reader.readVec3(pos)) {
            return false;
        }
        if ((dirtyMask & NetworkTransformDirtyFlags::kRotation) && !reader.readVec3(rot)) {
            return false;
        }
        if ((dirtyMask & NetworkTransformDirtyFlags::kScale) && !reader.readVec3(scl)) {
            return false;
        }

        std::string clipName;
        bool playing = true;
        bool loop = true;
        float speed = 1.0f;
        if (dirtyMask & NetworkTransformDirtyFlags::kAnimation) {
            if (!reader.readString(clipName, 256) ||
                !reader.readBool(playing) ||
                !reader.readBool(loop) ||
                !reader.readF32(speed)) {
                return false;
            }
        }

        std::string targetClipName;
        float blendDuration = 0.25f;
        if (dirtyMask & NetworkTransformDirtyFlags::kCrossfade) {
            if (!reader.readString(targetClipName, 256) || !reader.readF32(blendDuration)) {
                return false;
            }
        }

        std::string layerClipName;
        float layerWeight = 0.0f;
        std::string layerMaskBone;
        if (dirtyMask & NetworkTransformDirtyFlags::kLayerAnim) {
            if (!reader.readString(layerClipName, 256) ||
                !reader.readF32(layerWeight) ||
                !reader.readString(layerMaskBone, 256)) {
                return false;
            }
        }

        const auto* ref = findByNetworkId(netId);
        if (!ref || !ref->registry || !ref->registry->valid(ref->entity)) {
            return true;
        }

        const auto& identity = ref->registry->get<NetworkIdentityComponent>(ref->entity);
        if (isServerValidatingClient) {
            if (identity.serverAuthority || identity.ownerClientId != senderClientId) {
                return true;
            }
        } else {
            if (!identity.serverAuthority && identity.ownerClientId == m_networkManager->getLocalClientId()) {
                return true;
            }
        }

        auto& cache = ref->registry->get_or_emplace<CacheNetworkSyncComponent>(ref->entity);
        if (!isNewerSequence(sequence, cache.lastAppliedTransformSeq)) {
            return true;
        }
        cache.lastAppliedTransformSeq = sequence;

        if (isServerValidatingClient) {
            if (dirtyMask & NetworkTransformDirtyFlags::kPosition) {
                ref->registry->get_or_emplace<PositionComponent>(ref->entity).position = pos;
            }
            if (dirtyMask & NetworkTransformDirtyFlags::kRotation) {
                ref->registry->get_or_emplace<RotationComponent>(ref->entity).rotation = rot;
            }
            if (dirtyMask & NetworkTransformDirtyFlags::kScale) {
                ref->registry->get_or_emplace<ScaleComponent>(ref->entity).scale = scl;
            }
        } else {
            TransformSnapshotSample sample;
            sample.serverTimestamp = serverTimestamp;
            sample.sequence = sequence;
            sample.position = pos;
            sample.rotation = rot;
            sample.scale = scl;
            sample.hasPosition = (dirtyMask & NetworkTransformDirtyFlags::kPosition) != 0;
            sample.hasRotation = (dirtyMask & NetworkTransformDirtyFlags::kRotation) != 0;
            sample.hasScale = (dirtyMask & NetworkTransformDirtyFlags::kScale) != 0;
            pushTransformSnapshot(cache, sample, ref->registry, ref->entity);
        }

        applyRemoteAnimatorState(
            ref->registry,
            ref->entity,
            dirtyMask,
            clipName,
            playing,
            loop,
            speed,
            targetClipName,
            blendDuration,
            layerClipName,
            layerWeight,
            layerMaskBone);

        return true;
    }

    void NetworkSyncSystem::applyRemoteAnimatorState(
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
        const std::string& layerMaskBone) {
        constexpr uint8_t kAnyAnimMask =
            NetworkTransformDirtyFlags::kAnimation |
            NetworkTransformDirtyFlags::kCrossfade |
            NetworkTransformDirtyFlags::kLayerAnim;
        if ((dirtyMask & kAnyAnimMask) == 0) {
            return;
        }
        auto* anim = registry->try_get<SkeletalAnimatorComponent>(entity);
        if (!anim) {
            return;
        }

        if (dirtyMask & NetworkTransformDirtyFlags::kAnimation) {
            anim->playing = playing;
            anim->loop = loop;
            anim->speed = speed;
            if (!clipName.empty() && anim->clipName != clipName) {
                if (anim->clipName.empty()) {
                    anim->clipName = clipName;
                    anim->currentTime = 0.0f;
                } else {
                    anim->targetClipName = clipName;
                    anim->targetTime = 0.0f;
                    anim->blendDuration = 0.2f;
                    anim->blendElapsed = 0.0f;
                    anim->blendWeight = 0.0f;
                }
            }
        }
        if ((dirtyMask & NetworkTransformDirtyFlags::kCrossfade) && !targetClipName.empty()) {
            anim->targetClipName = targetClipName;
            anim->targetTime = 0.0f;
            anim->blendDuration = std::max(0.01f, blendDuration);
            anim->blendElapsed = 0.0f;
            anim->blendWeight = 0.0f;
        }
        if (dirtyMask & NetworkTransformDirtyFlags::kLayerAnim) {
            anim->layerClipName = layerClipName;
            anim->layerWeight = layerWeight;
            anim->layerMaskRootBone = layerMaskBone;
        }
    }

    void NetworkSyncSystem::replicateReflectedFieldsDeltas(bool isServerPass) {
        ComponentReflection* refl = resolveReflection();
        if (!refl || m_activeEntities.empty()) {
            return;
        }
        if (isServerPass && m_networkManager->getConnectedClientCount() <= 0) {
            return;
        }

        const int localCid = m_networkManager->getLocalClientId();
        std::vector<ReflectedFieldPatch> dirtyPatches;

        for (const auto& ref : m_activeEntities) {
            const auto* rep = ref.registry->try_get<NetworkReplicatedComponent>(ref.entity);
            if (!rep || rep->componentNames.empty()) {
                continue;
            }

            const auto& identity = ref.registry->get<NetworkIdentityComponent>(ref.entity);
            if (!isServerPass) {
                if (identity.serverAuthority || identity.ownerClientId != localCid) {
                    continue;
                }
            }

            auto& cache = ref.registry->get_or_emplace<CacheNetworkSyncComponent>(ref.entity);
            cache.fieldSyncAccumulator += m_deltaTime;
            if (cache.fieldSyncAccumulator < std::max(0.01f, rep->syncIntervalSec)) {
                continue;
            }
            cache.fieldSyncAccumulator = 0.0f;

            dirtyPatches.clear();
            collectEntityDirtyReflectedFields(refl, ref.registry, ref.entity, *rep, cache, false, dirtyPatches);
            if (dirtyPatches.empty()) {
                continue;
            }

            const uint32_t seq = m_nextSyncSeq++;
            NetworkByteWriter writer(64 + dirtyPatches.size() * 48);
            writer.writeI32(ref.networkId);
            writer.writeU32(seq);
            writer.writeU32(static_cast<uint32_t>(dirtyPatches.size()));
            for (const auto& p : dirtyPatches) {
                writer.writeString(p.componentName);
                writer.writeString(p.fieldName);
                writer.writeComponentValue(p.value);
            }

            const NetworkChannel channel = rep->reliableSync
                ? NetworkChannel::ReliableTcp
                : NetworkChannel::UnreliableUdp;

            if (isServerPass) {
                (void)m_networkManager->broadcastSyncPacket(
                    NetworkPacketType::ReflectedFieldsDelta,
                    channel,
                    writer.buffer());
            } else {
                (void)m_networkManager->sendSyncPacketToServer(
                    NetworkPacketType::ReflectedFieldsDelta,
                    channel,
                    writer.buffer());
            }
        }
    }

    void NetworkSyncSystem::collectEntityDirtyReflectedFields(
        ComponentReflection* refl,
        entt::registry* registry,
        entt::entity entity,
        const NetworkReplicatedComponent& rep,
        CacheNetworkSyncComponent& cache,
        bool forceAll,
        std::vector<ReflectedFieldPatch>& outPatches) {
        for (const auto& compName : rep.componentNames) {
            if (compName.empty() || isProtectedCoreComponent(compName)) {
                continue;
            }
            const ComponentInfo* compInfo = refl->findComponent(compName);
            if (!compInfo || !compInfo->hasComponent(*registry, entity)) {
                continue;
            }
            void* compPtr = compInfo->getComponent(*registry, entity);
            if (!compPtr) {
                continue;
            }

            for (const auto& field : compInfo->fields) {
                if (!field.getter) {
                    continue;
                }
                ComponentValue val = field.getter(compPtr);
                const uint64_t valHash = hashComponentValue(val);
                const std::string hashKey = compName + "." + field.name;

                auto it = cache.lastReflectedFieldHashes.find(hashKey);
                const bool changed = forceAll ||
                                     (it == cache.lastReflectedFieldHashes.end()) ||
                                     (it->second != valHash);
                cache.lastReflectedFieldHashes[hashKey] = valHash;

                if (changed) {
                    outPatches.push_back(ReflectedFieldPatch{
                        compName,
                        field.name,
                        std::move(val)
                    });
                }
            }
        }
    }

    void NetworkSyncSystem::applyReflectedFieldPatches(
        ComponentReflection* refl,
        entt::registry* registry,
        entt::entity entity,
        const std::vector<ReflectedFieldPatch>& patches) {
        auto& cache = registry->get_or_emplace<CacheNetworkSyncComponent>(entity);
        for (const auto& patch : patches) {
            if (isProtectedCoreComponent(patch.componentName)) {
                continue;
            }
            const ComponentInfo* compInfo = refl->findComponent(patch.componentName);
            if (!compInfo) {
                continue;
            }
            if (!compInfo->hasComponent(*registry, entity) && compInfo->addComponent) {
                compInfo->addComponent(*registry, entity);
            }
            try {
                refl->setComponentField(*registry, entity, patch.componentName, patch.fieldName, patch.value);
                cache.lastReflectedFieldHashes[patch.componentName + "." + patch.fieldName] =
                    hashComponentValue(patch.value);
            } catch (const std::exception&) {
            }
        }
    }

    void NetworkSyncSystem::pushTransformSnapshot(
        CacheNetworkSyncComponent& cache,
        const TransformSnapshotSample& incomingSample,
        const entt::registry* registry,
        entt::entity entity) {
        TransformSnapshotSample completed = incomingSample;
        if (!cache.snapshotBuffer.empty()) {
            const auto& prev = cache.snapshotBuffer.back();
            if (!completed.hasPosition) completed.position = prev.position;
            if (!completed.hasRotation) completed.rotation = prev.rotation;
            if (!completed.hasScale) completed.scale = prev.scale;
        } else {
            if (!completed.hasPosition && registry->all_of<PositionComponent>(entity)) {
                completed.position = registry->get<PositionComponent>(entity).position;
            }
            if (!completed.hasRotation && registry->all_of<RotationComponent>(entity)) {
                completed.rotation = registry->get<RotationComponent>(entity).rotation;
            }
            if (!completed.hasScale && registry->all_of<ScaleComponent>(entity)) {
                completed.scale = registry->get<ScaleComponent>(entity).scale;
            }
        }
        completed.hasPosition = true;
        completed.hasRotation = true;
        completed.hasScale = true;

        auto insertPos = std::upper_bound(
            cache.snapshotBuffer.begin(),
            cache.snapshotBuffer.end(),
            completed.serverTimestamp,
            [](double ts, const TransformSnapshotSample& s) { return ts < s.serverTimestamp; });
        cache.snapshotBuffer.insert(insertPos, completed);

        while (cache.snapshotBuffer.size() > kMaxSnapshotHistory) {
            cache.snapshotBuffer.pop_front();
        }
    }

    void NetworkSyncSystem::interpolateRemoteClientEntities() {
        const double serverNow = m_networkManager->getEstimatedServerTimeSec();
        const int localCid = m_networkManager->getLocalClientId();

        for (const auto& ref : m_activeEntities) {
            const auto& identity = ref.registry->get<NetworkIdentityComponent>(ref.entity);
            if (!identity.serverAuthority && identity.ownerClientId == localCid) {
                continue;
            }
            const auto* netTrans = ref.registry->try_get<NetworkTransformComponent>(ref.entity);
            auto* cache = ref.registry->try_get<CacheNetworkSyncComponent>(ref.entity);
            if (!netTrans || !cache || cache->snapshotBuffer.empty()) {
                continue;
            }

            const double renderTime = serverNow - std::max(0.0f, netTrans->interpolationBackTime);
            const auto& buf = cache->snapshotBuffer;

            FCT::Vec3<> targetPos;
            FCT::Vec3<> targetRot;
            FCT::Vec3<> targetScale;
            bool useExponentialSmoothing = false;

            if (buf.size() == 1 || renderTime <= buf.front().serverTimestamp) {
                targetPos = buf.front().position;
                targetRot = buf.front().rotation;
                targetScale = buf.front().scale;
                useExponentialSmoothing = true;
            } else if (renderTime >= buf.back().serverTimestamp) {
                targetPos = buf.back().position;
                targetRot = buf.back().rotation;
                targetScale = buf.back().scale;
                useExponentialSmoothing = true;
            } else {
                for (size_t i = 0; i + 1 < buf.size(); ++i) {
                    const auto& s0 = buf[i];
                    const auto& s1 = buf[i + 1];
                    if (renderTime >= s0.serverTimestamp && renderTime <= s1.serverTimestamp) {
                        const double span = s1.serverTimestamp - s0.serverTimestamp;
                        const float alpha = (span > 1e-6)
                            ? static_cast<float>(std::clamp((renderTime - s0.serverTimestamp) / span, 0.0, 1.0))
                            : 1.0f;
                        targetPos = lerpVec3(s0.position, s1.position, alpha);
                        targetRot = lerpEulerAnglesDeg(s0.rotation, s1.rotation, alpha);
                        targetScale = lerpVec3(s0.scale, s1.scale, alpha);
                        break;
                    }
                }
            }

            const float smoothAlpha = useExponentialSmoothing
                ? std::clamp(1.0f - std::exp(-std::max(1.0f, netTrans->interpolationSpeed) * m_deltaTime), 0.0f, 1.0f)
                : 1.0f;

            if (netTrans->syncPosition) {
                auto& posComp = ref.registry->get_or_emplace<PositionComponent>(ref.entity);
                posComp.position = useExponentialSmoothing
                    ? lerpVec3(posComp.position, targetPos, smoothAlpha)
                    : targetPos;
            }
            if (netTrans->syncRotation) {
                auto& rotComp = ref.registry->get_or_emplace<RotationComponent>(ref.entity);
                rotComp.rotation = useExponentialSmoothing
                    ? lerpEulerAnglesDeg(rotComp.rotation, targetRot, smoothAlpha)
                    : targetRot;
            }
            if (netTrans->syncScale) {
                auto& scaleComp = ref.registry->get_or_emplace<ScaleComponent>(ref.entity);
                scaleComp.scale = useExponentialSmoothing
                    ? lerpVec3(scaleComp.scale, targetScale, smoothAlpha)
                    : targetScale;
            }

            while (cache->snapshotBuffer.size() > 2 &&
                   cache->snapshotBuffer[1].serverTimestamp < (renderTime - 0.5)) {
                cache->snapshotBuffer.pop_front();
            }
        }
    }

    entt::registry* NetworkSyncSystem::getPrimarySceneRegistry() const {
        if (!m_dataManager) {
            return nullptr;
        }
        if (Scene::Scene* scene = m_dataManager->getCurrentScene()) {
            if (SceneTrunk* defaultTrunk = scene->getLoadedTrunk("default")) {
                if (defaultTrunk->getRegistry()) {
                    return defaultTrunk->getRegistry();
                }
            }
            const auto regs = m_dataManager->currentRegistries();
            for (auto* reg : regs) {
                if (reg) {
                    return reg;
                }
            }
            return scene->getRegistry();
        }
        const auto regs = m_dataManager->currentRegistries();
        return regs.empty() ? nullptr : regs.front();
    }

    const NetworkSyncSystem::ReplicatedEntityRef* NetworkSyncSystem::findByNetworkId(int networkId) const {
        auto it = m_idToIndex.find(networkId);
        return (it != m_idToIndex.end() && it->second < m_activeEntities.size())
            ? &m_activeEntities[it->second]
            : nullptr;
    }

    const NetworkSyncSystem::ReplicatedEntityRef* NetworkSyncSystem::findByGuid(const std::string& guid) const {
        if (guid.empty()) {
            return nullptr;
        }
        auto it = m_guidToIndex.find(guid);
        return (it != m_guidToIndex.end() && it->second < m_activeEntities.size())
            ? &m_activeEntities[it->second]
            : nullptr;
    }

    ComponentReflection* NetworkSyncSystem::resolveReflection() const {
        assert(Runtime::Engine::getInstance() != nullptr && "Engine 实例不能为空"); return Runtime::Engine::getInstance()->foundation->getComponentReflection();
    }

} // namespace MQEngine
