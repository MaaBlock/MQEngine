/**
 * @file SceneSubsystem.h
 * @brief 场景子域核心门面：
 *        统一协调 TransformGraph、SpatialGraph、SceneTrunk 与 SceneReplay，
 *        对接 Resource::DataBase 共享内存数据库与严格无锁帧调度
 */
#ifndef MQENGINE_DOMAIN_SCENE_SCENESUBSYSTEM_H
#define MQENGINE_DOMAIN_SCENE_SCENESUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/domain/scene/SpatialGraph.h>
#include <MQEngine/domain/scene/SceneTrunk.h>
#include <MQEngine/domain/scene/SceneReplay.h>
#include <MQEngine/domain/scene/system/CullingSystem.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/table/DataBase.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            /**
             * @brief ECS Registry 分区语义类别（按职责隔离主场景运行时、编辑器辅助实体与流式分块子场景）
             */
            enum class ERegistryKind : uint8_t
            {
                Main = 0,      // 主场景持久化运行时实体
                Editor = 1,    // 编辑器专属实体（漫游相机、辅助参考体等，与运行时隔离）
                Streaming = 2, // 流式加载子场景分块 (SceneTrunk)
                Custom = 3     // 用户自定义扩展 ECS 分区
            };

            /**
             * @brief ECS Registry 分区描述快照
             */
            struct RegistryPartitionInfo
            {
                std::string name;
                ERegistryKind kind = ERegistryKind::Main;
                bool visible = true;
                uint32_t entityCount = 0;
            };

            /**
             * @brief 场景子系统核心类
             */
            class ENGINE_API SceneSubsystem
            {
            public:
                SceneSubsystem();
                ~SceneSubsystem();

                SceneSubsystem(const SceneSubsystem&) = delete;
                SceneSubsystem& operator=(const SceneSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                /**
                 * @brief 清空场景实体（默认保留 Editor 专属分区与编辑器自由相机）
                 */
                void clearScene(bool includeEditorRegistry = false);

                /**
                 * @brief 从 .mqscene TOML 文件加载场景（含 entities 键时先清空 Main 再重建；无 entities 键时保留程序化实体）
                 */
                Status loadSceneFromFile(const std::string& filePath, ResourceLoader* loader = nullptr);

                /**
                 * @brief 将 Main 分区实体序列化保存为 .mqscene TOML 文件
                 */
                Status saveSceneToFile(const std::string& filePath, ResourceLoader* loader = nullptr) const;

                /**
                 * @brief 每逻辑帧推进场景变换图求值与空间图同步 (单逻辑线程所有权，零锁并发)
                 */
                void update();

                // 实体与层级操作
                FCT::ECS::entity createEntity(std::string_view debugName = {});
                void destroyEntity(FCT::ECS::entity entity);
                bool valid(FCT::ECS::entity entity) const noexcept;

                void attachChild(FCT::ECS::entity parent, FCT::ECS::entity child);
                void detachChild(FCT::ECS::entity child);

                // 分块子场景流式管理
                Status loadTrunk(const std::string& trunkName, const std::string& directoryPath);
                void unloadTrunk(const std::string& trunkName);
                bool isTrunkLoaded(const std::string& trunkName) const noexcept;
                SceneTrunk* getTrunk(const std::string& trunkName) noexcept;
                const SceneTrunk* getTrunk(const std::string& trunkName) const noexcept;

                // 多 Registry 分区管理与可见性控制（区分主场景 Main、编辑器 Editor、流式分块 Streaming 与自定义分区）
                std::vector<std::string> getRegistryNames() const;
                std::vector<RegistryPartitionInfo> getRegistryPartitions() const;
                ERegistryKind getRegistryKind(const std::string& registryName) const noexcept;
                FCT::ECS::registry* getRegistryByName(const std::string& registryName) noexcept;
                const FCT::ECS::registry* getRegistryByName(const std::string& registryName) const noexcept;
                FCT::ECS::registry& getOrCreateRegistry(const std::string& registryName);
                FCT::ECS::entity createEntityInRegistry(const std::string& registryName, std::string_view debugName = {});
                void destroyEntityInRegistry(const std::string& registryName, FCT::ECS::entity entity);

                void setRegistryVisible(const std::string& registryName, bool visible);
                bool isRegistryVisible(const std::string& registryName) const noexcept;

                // 子图与组件访问器
                TransformGraph& getTransformGraph() noexcept { return m_transformGraph; }
                const TransformGraph& getTransformGraph() const noexcept { return m_transformGraph; }

                SpatialGraph& getSpatialGraph() noexcept { return m_spatialGraph; }
                const SpatialGraph& getSpatialGraph() const noexcept { return m_spatialGraph; }

                SceneReplay& getReplay() noexcept { return m_replay; }
                const SceneReplay& getReplay() const noexcept { return m_replay; }

                CullingSystem& getCullingSystem() noexcept { return m_cullingSystem; }
                const CullingSystem& getCullingSystem() const noexcept { return m_cullingSystem; }

                FCT::ECS::registry& getRegistry() noexcept;
                const FCT::ECS::registry& getRegistry() const noexcept;

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

                /**
                 * @brief 同步当前是否允许挂载编辑器专属元数据组件（EditorNameComponent）
                 */
                void setEditorMetadataEnabled(bool enabled) noexcept
                {
                    m_editorMetadataEnabled.store(enabled, std::memory_order_release);
                }

                /**
                 * @brief 查询当前宿主是否处于 Editor / CliEditor 标签态（纯 Game 模式返回 false）
                 */
                bool shouldAttachEditorMetadata() const noexcept;

                float getLastUpdateMs() const noexcept { return m_lastUpdateMs.load(std::memory_order_relaxed); }
                float getLastTransformEvalMs() const noexcept { return m_lastTransformEvalMs.load(std::memory_order_relaxed); }
                float getLastSpatialRebuildMs() const noexcept { return m_lastSpatialRebuildMs.load(std::memory_order_relaxed); }
                uint32_t getActiveEntityCount() const noexcept { return m_activeEntityCount.load(std::memory_order_relaxed); }

            private:
                void refreshActiveEntityCount() noexcept;

                std::atomic<bool> m_initialized{false};
                std::atomic<bool> m_editorMetadataEnabled{false};
                mutable FCT::ECS::registry* m_registry = nullptr;
                mutable UniquePtr<FCT::ECS::registry> m_fallbackRegistry;

                TransformGraph m_transformGraph;
                SpatialGraph m_spatialGraph;
                SceneReplay m_replay;
                CullingSystem m_cullingSystem;

                std::unordered_map<std::string, UniquePtr<SceneTrunk>> m_trunks;
                std::unordered_map<std::string, UniquePtr<FCT::ECS::registry>> m_namedRegistries;
                std::unordered_set<std::string> m_hiddenRegistries;
                uint64_t m_currentFrameIndex = 0;
                std::atomic<float> m_lastUpdateMs{0.0f};
                std::atomic<float> m_lastTransformEvalMs{0.0f};
                std::atomic<float> m_lastSpatialRebuildMs{0.0f};
                std::atomic<uint32_t> m_activeEntityCount{0};
            };

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCENE_SCENESUBSYSTEM_H
