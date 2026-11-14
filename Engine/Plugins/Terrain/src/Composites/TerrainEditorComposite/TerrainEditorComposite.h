/**
 * @file TerrainEditorComposite.h
 * @brief 大世界地形雕刻与材质绘制编辑器 Composite（tags: [Editor, ImGui]，独立编译为 TerrainEditorComposite.dll）
 */
#ifndef MQENGINE_TERRAIN_EDITOR_COMPOSITE_H
#define MQENGINE_TERRAIN_EDITOR_COMPOSITE_H

#include "../../TerrainApi.h"
#include "../TerrainRuntimeComposite/TerrainTypes.h"
#include "../TerrainRuntimeComposite/TerrainRuntimeComposite.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace Terrain
    {
        /**
         * @brief 地形编辑器 Composite：
         *        提供地形雕刻（抬升/下压/平滑/压平）、多层 Splatmap 权重涂抹与植被散布工具面板
         */
        class TERRAIN_EDITOR_COMPOSITE_API TerrainEditorComposite : public Asset::Composite
        {
        public:
            TerrainEditorComposite();
            ~TerrainEditorComposite() override;

            TerrainEditorComposite(const TerrainEditorComposite&) = delete;
            TerrainEditorComposite& operator=(const TerrainEditorComposite&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "TerrainEditorComposite"; }
            std::string getPluginName() const override { return "Terrain"; }
            std::vector<std::string> getDependencies() const override { return {"TerrainRuntimeComposite", "EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

        private:
            void renderTerrainEditorPanel();
            void renderSculptingControls();
            void renderSplatmapControls();
            void renderFoliageControls();
            void renderTerrainStatistics();

            void applyBrushAt(float worldX, float worldZ);
            void scatterFoliage(float centerX, float centerZ, float radius, float density);
            void scatterAllTerrain();
            void clearAllFoliage();

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;
            TerrainRuntimeComposite* m_runtimeComposite = nullptr;

            TerrainBrushMode m_brushMode = TerrainBrushMode::SculptRaise;
            float m_brushRadius = 8.0f;
            float m_brushStrength = 0.5f;
            float m_brushFalloff = 0.5f;
            float m_flattenTargetHeight = 0.0f;
            int m_selectedLayerIndex = 0;

            // 程序化植被散布参数与已生成 ECS 实体追踪
            float m_foliageDensity = 1.0f;
            float m_foliageMinElevation = -2.0f;
            float m_foliageMaxElevation = 25.0f;
            float m_foliageMaxSlope = 35.0f;
            int m_foliageType = 0; // 0=Conifer, 1=Shrub, 2=Flora
            std::vector<FCT::ECS::entity> m_foliageEntities;

            float m_brushCursorX = 0.0f;
            float m_brushCursorZ = 0.0f;
            bool m_isApplyingBrush = false;
        };

    } // namespace Terrain
} // namespace MQEngine

#endif // MQENGINE_TERRAIN_EDITOR_COMPOSITE_H
