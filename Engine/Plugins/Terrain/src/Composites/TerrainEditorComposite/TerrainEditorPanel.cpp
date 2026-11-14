/**
 * @file TerrainEditorPanel.cpp
 * @brief 大世界地形工坊面板 UI：工具调色板、图层材质设置、高度场 Min/Max 俯视预览与导入导出实现
 */
#include "TerrainEditorComposite.h"

#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>

namespace MQEngine
{
    namespace Terrain
    {
        namespace
        {
            constexpr const char* kWindowTitle = "大世界地形工坊 (Terrain Studio)";
        }

        void TerrainEditorComposite::renderSculptingControls()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("雕刻笔刷模式", "Sculpting Modes"));
            const char* modes[] = {
                L("抬升", "Raise"),
                L("下压", "Lower"),
                L("平滑", "Smooth"),
                L("压平", "Flatten")
            };

            int currentModeIdx = static_cast<int>(m_brushMode);
            if (currentModeIdx > 3) currentModeIdx = 0;
            if (ImGui::Combo(L("笔刷类型", "Brush Type"), &currentModeIdx, modes, 4))
            {
                m_brushMode = static_cast<TerrainBrushMode>(currentModeIdx);
            }

            ImGui::SliderFloat(L("笔刷半径", "Brush Radius"), &m_brushRadius, 1.0f, 40.0f, "%.1f m");
            ImGui::SliderFloat(L("笔刷强度", "Brush Strength"), &m_brushStrength, 0.05f, 2.0f, "%.2f");
            ImGui::SliderFloat(L("衰减系数", "Falloff"), &m_brushFalloff, 0.1f, 1.0f, "%.2f");

            if (m_brushMode == TerrainBrushMode::SculptFlatten)
            {
                ImGui::SliderFloat(L("目标高度", "Target Height"), &m_flattenTargetHeight, -20.0f, 50.0f, "%.1f m");
            }
        }

        void TerrainEditorComposite::renderSplatmapControls()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("Splatmap 多层材质权重绘制", "Splatmap Layer Painting"));
            const char* layers[] = {
                L("图层 0: 草地", "Layer 0: Grass"),
                L("图层 1: 岩石", "Layer 1: Rock"),
                L("图层 2: 泥土", "Layer 2: Dirt"),
                L("图层 3: 雪地", "Layer 3: Snow")
            };

            ImGui::Combo(L("活动图层", "Active Layer"), &m_selectedLayerIndex, layers, 4);
            ImGui::SliderFloat(L("涂抹半径", "Paint Radius"), &m_brushRadius, 1.0f, 30.0f, "%.1f m");
            ImGui::SliderFloat(L("涂抹强度", "Paint Opacity"), &m_brushStrength, 0.05f, 1.0f, "%.2f");
        }

        void TerrainEditorComposite::renderFoliageControls()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("植被与程序化散布", "Procedural Foliage & Scatter"));

            const char* types[] = {
                L("针叶树 (Conifer)", "Conifer"),
                L("灌木丛 (Shrub)", "Shrub"),
                L("地表野花 (Flora)", "Flora")
            };
            ImGui::Combo(L("植被实例类型", "Foliage Type"), &m_foliageType, types, 3);

            ImGui::SliderFloat(L("散布密度", "Scatter Density"), &m_foliageDensity, 0.1f, 5.0f, "%.2f");
            ImGui::SliderFloat(L("最低散布海拔", "Min Elevation"), &m_foliageMinElevation, -20.0f, 40.0f, "%.1f m");
            ImGui::SliderFloat(L("最高散布海拔", "Max Elevation"), &m_foliageMaxElevation, -10.0f, 60.0f, "%.1f m");
            ImGui::SliderFloat(L("最大坡度限制", "Max Slope"), &m_foliageMaxSlope, 5.0f, 60.0f, "%.1f deg");

            ImGui::Text(L("当前场景植被实例数: %zu 个", "Active Foliage Count: %zu"), m_foliageEntities.size());

            if (ImGui::Button(L("全地形一键程序化散布 (Scatter All)", "Scatter All Terrain")))
            {
                scatterAllTerrain();
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "地形植被",
                        "全地图植被程序化散布完成，当前实例数: " + std::to_string(m_foliageEntities.size()),
                        EditorBase::LogEntry::Level::Info,
                        2.5f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(L("清除当前所有植被实例 (Clear All)", "Clear All Foliage Instances")))
            {
                clearAllFoliage();
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "地形植被",
                        "已清空场景中所有程序化植被散布实例",
                        EditorBase::LogEntry::Level::Info,
                        2.0f);
                }
            }
        }

        void TerrainEditorComposite::renderTerrainStatistics()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (!m_runtimeComposite)
                return;

            ImGui::TextDisabled("%s", L("地形规格与显存指标", "Terrain Metrics & Telemetry"));
            ImGui::Text(L("网格分辨率: %u × %u (间距 %.1f m)", "Grid Resolution: %u × %u (Spacing %.1f m)"),
                        m_runtimeComposite->getGridResolution(), m_runtimeComposite->getGridResolution(),
                        m_runtimeComposite->getGridSpacing());
            ImGui::Text(L("覆盖范围: %.1f m × %.1f m", "World Extent: %.1f m × %.1f m"),
                        m_runtimeComposite->getWorldSize(), m_runtimeComposite->getWorldSize());
            ImGui::Text(L("Clipmap 环数量: %zu 级同心 LOD", "Clipmap Rings: %zu Levels Concentric LOD"),
                        m_runtimeComposite->getClipmapLevels().size());

            const auto& heights = m_runtimeComposite->getHeightData();
            if (!heights.empty())
            {
                float minH = heights[0];
                float maxH = heights[0];
                for (float h : heights)
                {
                    if (h < minH) minH = h;
                    if (h > maxH) maxH = h;
                }
                ImGui::Text(L("高度范围: [%.2f m, %.2f m]", "Elevation Bounds: [%.2f m, %.2f m]"), minH, maxH);
            }
        }

        void TerrainEditorComposite::renderTerrainEditorPanel()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kWindowTitle)
                : "大世界地形工坊###大世界地形工坊 (Terrain Studio)";

            bool* openPtr = m_editorUiComposite ? m_editorUiComposite->getDockWindowOpenPtr(kWindowTitle) : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            ImGui::Text(L("GPU Clipmap 大世界地形雕刻与材质工坊", "GPU Clipmap Terrain Sculpting & Material Studio"));
            ImGui::Separator();

            if (ImGui::Button(L("雕刻模式", "Sculpt Mode")))
            {
                m_brushMode = TerrainBrushMode::SculptRaise;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("材质绘制", "Paint Mode")))
            {
                m_brushMode = TerrainBrushMode::PaintSplatmap;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("植被散布", "Foliage Mode")))
            {
                m_brushMode = TerrainBrushMode::FoliageScatter;
            }
            ImGui::Separator();

            if (m_brushMode == TerrainBrushMode::PaintSplatmap)
            {
                renderSplatmapControls();
            }
            else if (m_brushMode == TerrainBrushMode::FoliageScatter)
            {
                renderFoliageControls();
            }
            else
            {
                renderSculptingControls();
            }

            ImGui::Separator();
            renderTerrainStatistics();

            ImGui::Separator();
            ImGui::TextDisabled("%s", L("俯视高度场交互预览", "Top-down Interactive Heightfield"));

            ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x < 120.0f) avail.x = 120.0f;
            float previewSize = std::min(avail.x, 240.0f);
            ImVec2 canvasP1(canvasP0.x + previewSize, canvasP0.y + previewSize);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(20, 24, 30, 255), 4.0f);
            drawList->AddRect(canvasP0, canvasP1, IM_COL32(60, 70, 90, 255), 4.0f);

            // 绘制高度场等高线微缩图
            if (m_runtimeComposite && !m_runtimeComposite->getHeightData().empty())
            {
                uint32_t res = m_runtimeComposite->getGridResolution();
                const auto& hData = m_runtimeComposite->getHeightData();
                const uint32_t step = std::max(1u, res / 32);

                for (uint32_t z = 0; z < res; z += step)
                {
                    float yNorm = static_cast<float>(z) / static_cast<float>(res);
                    float sy = canvasP0.y + yNorm * previewSize;
                    for (uint32_t x = 0; x < res; x += step)
                    {
                        float xNorm = static_cast<float>(x) / static_cast<float>(res);
                        float sx = canvasP0.x + xNorm * previewSize;

                        float h = hData[z * res + x];
                        int c = std::clamp(static_cast<int>((h + 5.0f) * 20.0f), 30, 240);
                        drawList->AddRectFilled(
                            ImVec2(sx, sy),
                            ImVec2(sx + previewSize / 32.0f, sy + previewSize / 32.0f),
                            IM_COL32(c, c + 20, c, 220));
                    }
                }
            }

            // 笔刷交互
            ImGuiIO& io = ImGui::GetIO();
            const ImVec2 mousePos = io.MousePos;
            const bool inCanvas = (mousePos.x >= canvasP0.x && mousePos.x <= canvasP1.x &&
                                  mousePos.y >= canvasP0.y && mousePos.y <= canvasP1.y);

            if (inCanvas)
            {
                float normX = (mousePos.x - canvasP0.x) / previewSize;
                float normZ = (mousePos.y - canvasP0.y) / previewSize;
                float halfSize = m_runtimeComposite ? m_runtimeComposite->getWorldSize() * 0.5f : 64.0f;
                m_brushCursorX = (normX - 0.5f) * 2.0f * halfSize;
                m_brushCursorZ = (normZ - 0.5f) * 2.0f * halfSize;

                float brushScreenRadius = (m_brushRadius / (halfSize * 2.0f)) * previewSize;
                drawList->AddCircle(mousePos, brushScreenRadius, IM_COL32(255, 215, 0, 220), 24, 1.5f);

                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    applyBrushAt(m_brushCursorX, m_brushCursorZ);
                }
            }

            ImGui::InvisibleButton("TerrainMiniPreviewCanvasBtn", ImVec2(previewSize, previewSize));

            ImGui::Separator();
            if (ImGui::Button(L("保存高度图资产", "Save Heightmap Asset")))
            {
                if (m_runtimeComposite)
                {
                    auto* resMod = Resource::Resource::InstancePtr();
                    auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
                    if (loader) (void)loader->makeDir("Assets/Terrain");
                    (void)m_runtimeComposite->saveTerrainToDisk("Assets/Terrain/Default.terrain");
                    if (m_editorUiComposite)
                    {
                        m_editorUiComposite->showToast(
                            "地形保存",
                            "已将当前高度场保存至 Assets/Terrain/Default.terrain",
                            EditorBase::LogEntry::Level::Info,
                            2.5f);
                    }
                }
            }

            ImGui::End();
        }

    } // namespace Terrain
} // namespace MQEngine
