/**
 * @file ReplayTimelineComposite.cpp
 * @brief ECS 回放时间轴生命周期、环形缓冲区录制、快照场景还原与 .mqreplay 序列化实现
 */
#include "ReplayTimelineComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/domain/core/domain.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <sstream>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kReplayWindowTitle = "ECS 回放时间轴 (ECS Replay Timeline)";
        }

        ReplayTimelineComposite::ReplayTimelineComposite() = default;

        ReplayTimelineComposite::~ReplayTimelineComposite()
        {
            onUnload();
        }

        Status ReplayTimelineComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kReplayWindowTitle,
                    EditorBase::EditorDockSlot::Bottom,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderTimelineWindow();
                    },
                    false);
            }

            return OkStatus();
        }

        Status ReplayTimelineComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            return OkStatus();
        }

        void ReplayTimelineComposite::onTick(float deltaTime)
        {
            if (m_state == ReplayPlaybackState::Recording)
            {
                m_accumulatedTime += deltaTime;
                recordCurrentSceneSnapshot();
                if (!m_history.empty())
                {
                    m_currentFrame = static_cast<uint32_t>(m_history.size() - 1);
                }
            }
            else if (m_state == ReplayPlaybackState::Playing && !m_history.empty())
            {
                m_accumulatedTime += deltaTime * m_playbackSpeed;
                if (m_currentFrame + 1 < m_history.size())
                {
                    ++m_currentFrame;
                    restoreSnapshotToScene(m_history[m_currentFrame]);
                }
                else
                {
                    if (m_loopPlayback)
                    {
                        m_currentFrame = 0;
                        restoreSnapshotToScene(m_history[0]);
                    }
                    else
                    {
                        m_state = ReplayPlaybackState::Paused;
                    }
                }
            }
        }

        void ReplayTimelineComposite::onUnload()
        {
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kReplayWindowTitle);
            }
            m_history.clear();
            m_keyframeMarkers.clear();
            m_bookmarks.clear();
            m_editorUiComposite = nullptr;
            Asset::Composite::onUnload();
        }

        void ReplayTimelineComposite::recordCurrentSceneSnapshot()
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain)
                return;

            auto& registry = domain->scene.getRegistry();
            ReplayFrameRecord snap;
            snap.frameIndex = static_cast<uint32_t>(m_history.size());
            snap.timestampSec = m_accumulatedTime;

            auto view = registry.view<Domain::Scene::LocalTransform>();
            for (auto entity : view)
            {
                const auto& local = view.get<Domain::Scene::LocalTransform>(entity);
                const auto* world = registry.try_get<Domain::Scene::WorldTransform>(entity);

                EntityTransformSnapshot es;
                es.entityId = static_cast<uint32_t>(entity);
                es.position = local.position;
                es.rotation = local.rotation;
                es.scale = local.scale;
                es.isVisible = world ? world->isVisible : true;
                snap.entities.push_back(es);
            }

            if (!m_hasInitialSnapshot)
            {
                m_initialSnapshot = snap;
                m_hasInitialSnapshot = true;
            }

            m_history.push_back(std::move(snap));

            // 最多保留 3600 帧（60 FPS 下约 1 分钟连续回放历史）
            if (m_history.size() > 3600)
            {
                m_history.erase(m_history.begin());
            }
        }

        void ReplayTimelineComposite::restoreSnapshotToScene(const ReplayFrameRecord& snapshot)
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain)
                return;

            auto& registry = domain->scene.getRegistry();
            for (const auto& es : snapshot.entities)
            {
                auto entity = static_cast<entt::entity>(es.entityId);
                if (registry.valid(entity))
                {
                    if (auto* local = registry.try_get<Domain::Scene::LocalTransform>(entity))
                    {
                        local->position = es.position;
                        local->rotation = es.rotation;
                        local->scale = es.scale;
                        local->isDirty = true;
                        local->dirtyVersion++;
                    }
                    if (auto* world = registry.try_get<Domain::Scene::WorldTransform>(entity))
                    {
                        world->isVisible = es.isVisible;
                    }
                }
            }
        }

        void ReplayTimelineComposite::startRecording()
        {
            m_state = ReplayPlaybackState::Recording;
            if (!m_hasInitialSnapshot)
            {
                recordCurrentSceneSnapshot();
            }
            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "回放系统",
                    "开始录制 ECS 实体内存快照序列",
                    EditorBase::LogEntry::Level::Info,
                    2.0f);
            }
        }

        void ReplayTimelineComposite::stopAndRewindToStart()
        {
            m_state = ReplayPlaybackState::Stopped;
            m_currentFrame = 0;
            if (m_hasInitialSnapshot)
            {
                restoreSnapshotToScene(m_initialSnapshot);
            }
            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "回放系统",
                    "已停止回放并回滚至第 0 帧初始状态",
                    EditorBase::LogEntry::Level::Info,
                    2.0f);
            }
        }

        void ReplayTimelineComposite::play()
        {
            if (!m_history.empty())
            {
                m_state = ReplayPlaybackState::Playing;
            }
        }

        void ReplayTimelineComposite::pause()
        {
            m_state = ReplayPlaybackState::Paused;
        }

        void ReplayTimelineComposite::scrubToFrame(uint32_t targetFrame)
        {
            if (m_history.empty())
                return;

            m_currentFrame = std::min(targetFrame, static_cast<uint32_t>(m_history.size() - 1));
            restoreSnapshotToScene(m_history[m_currentFrame]);
        }

        void ReplayTimelineComposite::stepForward()
        {
            if (m_currentFrame + 1 < m_history.size())
            {
                scrubToFrame(m_currentFrame + 1);
            }
        }

        void ReplayTimelineComposite::stepBackward()
        {
            if (m_currentFrame > 0)
            {
                scrubToFrame(m_currentFrame - 1);
            }
        }

        void ReplayTimelineComposite::addKeyframeMarker(uint32_t frameIdx)
        {
            if (std::find(m_keyframeMarkers.begin(), m_keyframeMarkers.end(), frameIdx) == m_keyframeMarkers.end())
            {
                m_keyframeMarkers.push_back(frameIdx);
                std::sort(m_keyframeMarkers.begin(), m_keyframeMarkers.end());
                m_bookmarks.push_back(ReplayBookmark{frameIdx, "Bookmark_" + std::to_string(frameIdx)});
            }
        }

        void ReplayTimelineComposite::removeKeyframeMarker(uint32_t frameIdx)
        {
            m_keyframeMarkers.erase(
                std::remove(m_keyframeMarkers.begin(), m_keyframeMarkers.end(), frameIdx),
                m_keyframeMarkers.end());
            m_bookmarks.erase(
                std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
                               [frameIdx](const ReplayBookmark& b) { return b.frameIndex == frameIdx; }),
                m_bookmarks.end());
        }

        Status ReplayTimelineComposite::saveReplayToFile(const std::string& filePath)
        {
            auto* res = Resource::Resource::InstancePtr();
            auto* loader = res ? res->getResourceLoader() : nullptr;
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader 未就绪");
            }

            (void)loader->ensureDir("Saved/Replays");
            std::ostringstream oss;
            oss << "# MQEngine ECS Replay Archive (.mqreplay)\n";
            oss << "frameCount=" << m_history.size() << "\n";
            oss << "bookmarkCount=" << m_keyframeMarkers.size() << "\n";
            for (uint32_t kf : m_keyframeMarkers)
            {
                oss << "bookmark=" << kf << "\n";
            }
            for (const auto& frame : m_history)
            {
                oss << "[frame " << frame.frameIndex << " " << frame.timestampSec << "]\n";
                for (const auto& ent : frame.entities)
                {
                    oss << ent.entityId << " "
                        << ent.position.x << " " << ent.position.y << " " << ent.position.z << " "
                        << ent.rotation.x << " " << ent.rotation.y << " " << ent.rotation.z << " "
                        << ent.scale.x << " " << ent.scale.y << " " << ent.scale.z << " "
                        << (ent.isVisible ? 1 : 0) << "\n";
                }
            }

            CHECK_STATUS(loader->saveFile(filePath, oss.str()));
            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "回放系统",
                    "回放序列已保存至 " + filePath,
                    EditorBase::LogEntry::Level::Info,
                    2.0f);
            }
            return OkStatus();
        }

        Status ReplayTimelineComposite::loadReplayFromFile(const std::string& filePath)
        {
            auto* res = Resource::Resource::InstancePtr();
            auto* loader = res ? res->getResourceLoader() : nullptr;
            if (!loader || !loader->exists(filePath))
            {
                return NotFoundError("未找到 .mqreplay 文件: " + filePath);
            }

            auto contentOr = loader->loadFile(filePath);
            CHECK_STATUS(contentOr.status());
            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "回放系统",
                    "已读取回放序列: " + filePath,
                    EditorBase::LogEntry::Level::Info,
                    2.0f);
            }
            return OkStatus();
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::ReplayTimelineComposite)
