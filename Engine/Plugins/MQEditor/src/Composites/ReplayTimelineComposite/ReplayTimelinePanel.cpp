/**
 * @file ReplayTimelinePanel.cpp
 * @brief ECS 回放时间轴控制栏、滑块标尺、关键帧书签标记与实体状态差异检视面板实现
 */
#include "ReplayTimelineComposite.h"

#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cmath>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kReplayWindowTitle = "ECS 回放时间轴 (ECS Replay Timeline)";
        }

        void ReplayTimelineComposite::renderPlaybackControls()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const bool isRecording = (m_state == ReplayPlaybackState::Recording);
            const bool isPlaying = (m_state == ReplayPlaybackState::Playing);

            if (isRecording)
            {
                if (ImGui::Button(L("暂停录制", "Pause Record")))
                {
                    pause();
                }
            }
            else
            {
                if (ImGui::Button(L("● 录制快照 (Record)", "● Record")))
                {
                    startRecording();
                }
            }

            ImGui::SameLine();
            if (isPlaying)
            {
                if (ImGui::Button(L("暂停 (Pause)", "Pause")))
                {
                    pause();
                }
            }
            else
            {
                if (ImGui::Button(L("▶ 播放 (Play)", "Play")))
                {
                    play();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(L("■ 停止并重置 (Stop)", "Stop")))
            {
                stopAndRewindToStart();
            }

            ImGui::SameLine();
            if (ImGui::Button(L("⏮ 逐帧退", "|< Step Prev")))
            {
                stepBackward();
            }

            ImGui::SameLine();
            if (ImGui::Button(L("⏭ 逐帧进", "Step Next >|")))
            {
                stepForward();
            }

            ImGui::SameLine();
            if (ImGui::Button(L("★ 标记关键帧", "Mark Keyframe")))
            {
                addKeyframeMarker(m_currentFrame);
            }

            ImGui::SameLine();
            ImGui::Checkbox(L("循环", "Loop"), &m_loopPlayback);

            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::SliderFloat(L("倍速", "Speed"), &m_playbackSpeed, 0.25f, 4.0f, "%.2fx");

            ImGui::SameLine();
            if (ImGui::SmallButton(L("导出 .mqreplay", "Save .mqreplay")))
            {
                (void)saveReplayToFile(m_replayFilePath);
            }
        }

        void ReplayTimelineComposite::renderTimelineScrubberAndBookmarks()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            uint32_t totalFrames = static_cast<uint32_t>(m_history.size());
            int maxFrameIdx = std::max(0, static_cast<int>(totalFrames) - 1);
            int currentFrameInt = static_cast<int>(m_currentFrame);

            ImGui::Text(L("当前帧: %u / %d  (已录制 %u 帧, %zu 个关键帧书签)",
                          "Current Frame: %u / %d  (Recorded %u Frames, %zu Bookmarks)"),
                        m_currentFrame, maxFrameIdx, totalFrames, m_keyframeMarkers.size());

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("##ScrubberSlider", &currentFrameInt, 0, maxFrameIdx, "Frame %d"))
            {
                scrubToFrame(static_cast<uint32_t>(currentFrameInt));
            }

            ImVec2 rulerP0 = ImGui::GetCursorScreenPos();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float rulerW = std::max(80.0f, avail.x);
            float rulerH = 26.0f;
            ImVec2 rulerP1(rulerP0.x + rulerW, rulerP0.y + rulerH);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(rulerP0, rulerP1, IM_COL32(28, 32, 40, 255), 3.0f);
            drawList->AddRect(rulerP0, rulerP1, IM_COL32(55, 65, 80, 255), 3.0f);

            if (maxFrameIdx > 0)
            {
                float headX = rulerP0.x + (static_cast<float>(m_currentFrame) / static_cast<float>(maxFrameIdx)) * rulerW;
                drawList->AddLine(ImVec2(headX, rulerP0.y), ImVec2(headX, rulerP1.y), IM_COL32(255, 70, 70, 255), 2.5f);

                for (uint32_t kf : m_keyframeMarkers)
                {
                    if (kf <= static_cast<uint32_t>(maxFrameIdx))
                    {
                        float kfX = rulerP0.x + (static_cast<float>(kf) / static_cast<float>(maxFrameIdx)) * rulerW;
                        float kfY = rulerP0.y + rulerH * 0.5f;
                        drawList->AddQuadFilled(
                            ImVec2(kfX, kfY - 6.0f),
                            ImVec2(kfX + 6.0f, kfY),
                            ImVec2(kfX, kfY + 6.0f),
                            ImVec2(kfX - 6.0f, kfY),
                            IM_COL32(255, 215, 0, 255));
                    }
                }
            }

            ImGui::Dummy(ImVec2(rulerW, rulerH));
        }

        void ReplayTimelineComposite::renderEntityStateDiffInspector()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (m_history.empty() || m_currentFrame >= m_history.size() || !m_hasInitialSnapshot)
            {
                return;
            }

            if (!ImGui::CollapsingHeader(L("实体状态差异检视 (Entity State Diff vs Frame 0)", "Entity State Diff vs Frame 0")))
            {
                return;
            }

            const ReplayFrameRecord& curSnap = m_history[m_currentFrame];
            if (ImGui::BeginTable("ReplayEntityDiffTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn(L("实体 ID", "Entity ID"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(L("当前位置 (Pos)", "Current Position"));
                ImGui::TableSetupColumn(L("位移增量 (Delta Pos)", "Delta vs Frame 0"));
                ImGui::TableSetupColumn(L("可见性", "Visible"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                for (const auto& curEnt : curSnap.entities)
                {
                    FCT::Vec3<float> deltaPos{0.0f, 0.0f, 0.0f};
                    for (const auto& baseEnt : m_initialSnapshot.entities)
                    {
                        if (baseEnt.entityId == curEnt.entityId)
                        {
                            deltaPos.x = curEnt.position.x - baseEnt.position.x;
                            deltaPos.y = curEnt.position.y - baseEnt.position.y;
                            deltaPos.z = curEnt.position.z - baseEnt.position.z;
                            break;
                        }
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("#%u", curEnt.entityId);

                    ImGui::TableNextColumn();
                    ImGui::Text("(%.2f, %.2f, %.2f)", curEnt.position.x, curEnt.position.y, curEnt.position.z);

                    ImGui::TableNextColumn();
                    ImGui::Text("(%.2f, %.2f, %.2f)", deltaPos.x, deltaPos.y, deltaPos.z);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(curEnt.isVisible ? "Yes" : "No");
                }
                ImGui::EndTable();
            }
        }

        void ReplayTimelineComposite::renderTimelineWindow()
        {
            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kReplayWindowTitle)
                : "ECS 回放时间轴###ECS 回放时间轴 (ECS Replay Timeline)";

            bool* openPtr = m_editorUiComposite ? m_editorUiComposite->getDockWindowOpenPtr(kReplayWindowTitle) : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            renderPlaybackControls();
            ImGui::Separator();
            renderTimelineScrubberAndBookmarks();
            renderEntityStateDiffInspector();

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine
