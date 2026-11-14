/**
 * @file TerrainEditorComposite.cpp
 * @brief 大世界地形编辑器生命周期、笔刷空间投射雕刻与程序化植被 ECS 散布实现
 */
#include "TerrainEditorComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/domain/core/domain.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <cmath>
#include <algorithm>
#include <random>

namespace MQEngine
{
    namespace Terrain
    {
        namespace
        {
            constexpr const char* kWindowTitle = "大世界地形工坊 (Terrain Studio)";
        }

        TerrainEditorComposite::TerrainEditorComposite() = default;

        TerrainEditorComposite::~TerrainEditorComposite()
        {
            onUnload();
        }

        Status TerrainEditorComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
                m_runtimeComposite = dynamic_cast<TerrainRuntimeComposite*>(
                    assetModule->compositeManager.getComposite("TerrainRuntimeComposite"));
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kWindowTitle,
                    EditorBase::EditorDockSlot::Right,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderTerrainEditorPanel();
                    },
                    false);
            }

            return OkStatus();
        }

        Status TerrainEditorComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            return OkStatus();
        }

        void TerrainEditorComposite::onTick(float deltaTime)
        {
            (void)deltaTime;
            if (m_isApplyingBrush)
            {
                applyBrushAt(m_brushCursorX, m_brushCursorZ);
            }
        }

        void TerrainEditorComposite::onUnload()
        {
            clearAllFoliage();
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kWindowTitle);
            }
            m_editorUiComposite = nullptr;
            m_runtimeComposite = nullptr;
            Asset::Composite::onUnload();
        }

        void TerrainEditorComposite::applyBrushAt(float worldX, float worldZ)
        {
            if (!m_runtimeComposite)
                return;

            switch (m_brushMode)
            {
            case TerrainBrushMode::SculptRaise:
                m_runtimeComposite->modifyHeight(worldX, worldZ, m_brushRadius, m_brushStrength * 0.1f, m_brushFalloff);
                break;
            case TerrainBrushMode::SculptLower:
                m_runtimeComposite->modifyHeight(worldX, worldZ, m_brushRadius, -m_brushStrength * 0.1f, m_brushFalloff);
                break;
            case TerrainBrushMode::SculptSmooth:
                m_runtimeComposite->smoothHeight(worldX, worldZ, m_brushRadius, m_brushStrength * 0.2f);
                break;
            case TerrainBrushMode::SculptFlatten:
                m_runtimeComposite->flattenHeight(worldX, worldZ, m_brushRadius, m_flattenTargetHeight, m_brushStrength * 0.2f);
                break;
            case TerrainBrushMode::PaintSplatmap:
                m_runtimeComposite->paintSplatmap(worldX, worldZ, m_brushRadius, static_cast<uint8_t>(m_selectedLayerIndex), m_brushStrength * 0.1f);
                break;
            case TerrainBrushMode::FoliageScatter:
                scatterFoliage(worldX, worldZ, m_brushRadius, m_foliageDensity);
                break;
            }
        }

        void TerrainEditorComposite::scatterFoliage(float centerX, float centerZ, float radius, float density)
        {
            if (!m_runtimeComposite)
                return;

            auto* domain = Domain::Domain::InstancePtr();
            if (!domain)
                return;

            auto& scene = domain->scene;
            auto& reg = scene.getRegistry();

            const float r2 = radius * radius;
            const int sampleCount = std::max(1, static_cast<int>(3.14159265f * r2 * density * 0.006f));

            static std::mt19937 rng(777);
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
            std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);

            for (int i = 0; i < sampleCount; ++i)
            {
                float r = std::sqrt(dist01(rng)) * radius;
                float theta = distAngle(rng);
                float px = centerX + r * std::cos(theta);
                float pz = centerZ + r * std::sin(theta);

                float h = m_runtimeComposite->getHeightAt(px, pz);
                if (h < m_foliageMinElevation || h > m_foliageMaxElevation)
                {
                    continue;
                }

                FCT::Vec3<float> normal = m_runtimeComposite->getNormalAt(px, pz);
                float cosSlope = std::clamp(normal.y, 0.0f, 1.0f);
                float slopeDeg = std::acos(cosSlope) * 180.0f / 3.14159265f;
                if (slopeDeg > m_foliageMaxSlope)
                {
                    continue;
                }

                // 实例化真实 ECS 实体
                auto entity = scene.createEntity();
                std::string prefix = (m_foliageType == 0) ? "Conifer" : (m_foliageType == 1) ? "Shrub" : "Flower";
                std::string entityName = prefix + "_" + std::to_string(m_foliageEntities.size() + 1);

                if (scene.shouldAttachEditorMetadata())
                {
                    reg.emplace_or_replace<Domain::Scene::EditorNameComponent>(
                        entity, Domain::Scene::EditorNameComponent{entityName});
                }
                reg.emplace_or_replace<Domain::Scene::UuidComponent>(
                    entity,
                    Domain::Scene::UuidComponent{"foliage-" + std::to_string(m_foliageEntities.size() + 1)});

                float yawDeg = dist01(rng) * 360.0f;
                float scale = 0.8f + dist01(rng) * 0.5f;
                reg.emplace<Domain::Scene::LocalTransform>(entity, Domain::Scene::LocalTransform{
                    .position = FCT::Vec3<>(px, h, pz),
                    .rotation = FCT::Vec3<>(0.0f, yawDeg, 0.0f),
                    .scale = FCT::Vec3<>(scale, scale, scale),
                    .dirtyVersion = 1,
                    .isDirty = true
                });

                int prim = (m_foliageType == 0) ? 0 : (m_foliageType == 1) ? 2 : 1;
                std::string meshPath = (m_foliageType == 0) ? "Assets/Models/ConiferTree.obj" : (m_foliageType == 1) ? "Assets/Models/Shrub.obj" : "Assets/Models/Flower.obj";
                reg.emplace<Domain::Scene::StaticMeshComponent>(entity, Domain::Scene::StaticMeshComponent{
                    .primitiveType = prim,
                    .meshAssetPath = meshPath,
                    .castShadows = true,
                    .visible = true
                });

                reg.emplace<Domain::Scene::HierarchyRelation>(entity);
                reg.emplace<Domain::Scene::WorldTransform>(entity);

                scene.getTransformGraph().markDirty(reg, entity);
                m_foliageEntities.push_back(entity);
            }
        }

        void TerrainEditorComposite::scatterAllTerrain()
        {
            if (!m_runtimeComposite)
                return;

            float extent = m_runtimeComposite->getWorldSize();
            float half = extent * 0.5f;
            const float step = 16.0f;

            for (float z = -half + 8.0f; z < half; z += step)
            {
                for (float x = -half + 8.0f; x < half; x += step)
                {
                    scatterFoliage(x, z, step * 0.7f, m_foliageDensity);
                }
            }
        }

        void TerrainEditorComposite::clearAllFoliage()
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (domain)
            {
                for (auto entity : m_foliageEntities)
                {
                    if (domain->scene.valid(entity))
                    {
                        domain->scene.destroyEntity(entity);
                    }
                }
            }
            m_foliageEntities.clear();
        }

    } // namespace Terrain
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::Terrain::TerrainEditorComposite)
