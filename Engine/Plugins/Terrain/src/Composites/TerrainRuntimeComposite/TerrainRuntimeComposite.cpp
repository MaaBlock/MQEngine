/**
 * @file TerrainRuntimeComposite.cpp
 * @brief 大世界地形运行时生命周期、高度场/法线采样、Clipmap 网格缓冲生成与 ObjectPass 渲染管线订阅实现
 */
#include "TerrainRuntimeComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/domain/core/domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/physics/PhysicsSubsystem.h>

#include <cmath>
#include <algorithm>

namespace MQEngine
{
    namespace Terrain
    {
        TerrainRuntimeComposite::TerrainRuntimeComposite() = default;

        TerrainRuntimeComposite::~TerrainRuntimeComposite()
        {
            onUnload();
        }

        Status TerrainRuntimeComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            m_layers[0] = TerrainLayerDesc{.name = "Grass", .uvScale = 4.0f, .roughness = 0.85f, .metallic = 0.0f, .tintColor = FCT::Vec3<float>(0.25f, 0.65f, 0.2f)};
            m_layers[1] = TerrainLayerDesc{.name = "Rock", .uvScale = 2.0f, .roughness = 0.95f, .metallic = 0.05f, .tintColor = FCT::Vec3<float>(0.45f, 0.42f, 0.4f)};
            m_layers[2] = TerrainLayerDesc{.name = "Dirt", .uvScale = 3.0f, .roughness = 0.9f, .metallic = 0.0f, .tintColor = FCT::Vec3<float>(0.5f, 0.35f, 0.22f)};
            m_layers[3] = TerrainLayerDesc{.name = "Snow", .uvScale = 5.0f, .roughness = 0.7f, .metallic = 0.1f, .tintColor = FCT::Vec3<float>(0.95f, 0.97f, 1.0f)};

            initDefaultTerrain();
            generateClipmapMeshBuffers();
            syncPhysicsCollider();
            registerPipelinesToRenderer();

            return OkStatus();
        }

        Status TerrainRuntimeComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            return OkStatus();
        }

        void TerrainRuntimeComposite::onTick(float deltaTime)
        {
            (void)deltaTime;
            // 每帧更新 Clipmap 中心（对齐主相机坐标）
            auto* domain = Domain::Domain::InstancePtr();
            FCT::Vec3<float> camPos(0.0f, 0.0f, 0.0f);
            if (domain)
            {
                camPos.x = domain->renderer.getCameraWorldPosition().x;
                camPos.y = domain->renderer.getCameraWorldPosition().y;
                camPos.z = domain->renderer.getCameraWorldPosition().z;
            }
            updateClipmapLevels(camPos);
        }

        void TerrainRuntimeComposite::onUnload()
        {
            unregisterPipelinesFromRenderer();
            auto* domain = Domain::Domain::InstancePtr();
            if (domain)
            {
                domain->physics.removeHeightfieldCollider();
            }
            m_heightfield.clear();
            m_splatmap.clear();
            m_clipmapLevels.clear();
            m_clipmapGeometries.clear();
            Asset::Composite::onUnload();
        }

        void TerrainRuntimeComposite::initDefaultTerrain()
        {
            const size_t totalVerts = static_cast<size_t>(m_resolution) * m_resolution;
            m_heightfield.assign(totalVerts, 0.0f);
            m_splatmap.assign(totalVerts, {1.0f, 0.0f, 0.0f, 0.0f});

            // 生成平缓起伏的丘陵地形 (双正弦 + 噪声基底)
            for (uint32_t z = 0; z < m_resolution; ++z)
            {
                float worldZ = m_originZ + static_cast<float>(z) * m_gridSpacing;
                for (uint32_t x = 0; x < m_resolution; ++x)
                {
                    float worldX = m_originX + static_cast<float>(x) * m_gridSpacing;
                    size_t idx = z * m_resolution + x;

                    float h = std::sin(worldX * 0.06f) * std::cos(worldZ * 0.06f) * 4.5f
                            + std::sin(worldX * 0.15f + worldZ * 0.12f) * 1.5f;
                    m_heightfield[idx] = h;

                    if (h > 3.5f)
                    {
                        m_splatmap[idx] = {0.2f, 0.1f, 0.0f, 0.7f}; // 雪峰
                    }
                    else if (std::abs(h) > 2.0f)
                    {
                        m_splatmap[idx] = {0.3f, 0.6f, 0.1f, 0.0f}; // 岩石陡坡
                    }
                    else
                    {
                        m_splatmap[idx] = {0.8f, 0.1f, 0.1f, 0.0f}; // 草地平原
                    }
                }
            }

            // 初始化 4 级同心 Clipmap 环
            m_clipmapLevels.clear();
            for (uint32_t i = 0; i < kTerrainClipmapRingLevels; ++i)
            {
                TerrainClipmapLevel lvl;
                lvl.levelIndex = i;
                lvl.gridSpacing = m_gridSpacing * static_cast<float>(1 << i);
                lvl.extentMeters = 32.0f * static_cast<float>(1 << i);
                lvl.center = FCT::Vec3<float>(0.0f, 0.0f, 0.0f);
                lvl.vertexCount = 64 * 64;
                m_clipmapLevels.push_back(lvl);
            }
        }

        void TerrainRuntimeComposite::updateClipmapLevels(const FCT::Vec3<float>& viewerPos)
        {
            for (auto& lvl : m_clipmapLevels)
            {
                // 吸附到当前层级的网格间距整数倍以避免几何抖动 (Grid Snapping)
                float snap = lvl.gridSpacing;
                lvl.center.x = std::floor(viewerPos.x / snap) * snap;
                lvl.center.z = std::floor(viewerPos.z / snap) * snap;
                lvl.center.y = getHeightAt(lvl.center.x, lvl.center.z);
            }
        }

        float TerrainRuntimeComposite::getHeightAt(float worldX, float worldZ) const noexcept
        {
            if (m_heightfield.empty() || m_resolution < 2)
                return 0.0f;

            float gx = (worldX - m_originX) / m_gridSpacing;
            float gz = (worldZ - m_originZ) / m_gridSpacing;

            if (gx < 0.0f || gz < 0.0f || gx >= static_cast<float>(m_resolution - 1) || gz >= static_cast<float>(m_resolution - 1))
            {
                return 0.0f;
            }

            uint32_t x0 = static_cast<uint32_t>(gx);
            uint32_t z0 = static_cast<uint32_t>(gz);
            uint32_t x1 = std::min(x0 + 1, m_resolution - 1);
            uint32_t z1 = std::min(z0 + 1, m_resolution - 1);

            float fx = gx - static_cast<float>(x0);
            float fz = gz - static_cast<float>(z0);

            float h00 = m_heightfield[z0 * m_resolution + x0];
            float h10 = m_heightfield[z0 * m_resolution + x1];
            float h01 = m_heightfield[z1 * m_resolution + x0];
            float h11 = m_heightfield[z1 * m_resolution + x1];

            float h0 = h00 * (1.0f - fx) + h10 * fx;
            float h1 = h01 * (1.0f - fx) + h11 * fx;
            return h0 * (1.0f - fz) + h1 * fz;
        }

        FCT::Vec3<float> TerrainRuntimeComposite::getNormalAt(float worldX, float worldZ) const noexcept
        {
            const float eps = m_gridSpacing;
            float hL = getHeightAt(worldX - eps, worldZ);
            float hR = getHeightAt(worldX + eps, worldZ);
            float hD = getHeightAt(worldX, worldZ - eps);
            float hU = getHeightAt(worldX, worldZ + eps);

            FCT::Vec3<float> normal(hL - hR, 2.0f * eps, hD - hU);
            float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (len > 1e-5f)
            {
                normal.x /= len;
                normal.y /= len;
                normal.z /= len;
            }
            else
            {
                normal = FCT::Vec3<float>(0.0f, 1.0f, 0.0f);
            }
            return normal;
        }

        bool TerrainRuntimeComposite::raycast(
            const FCT::Vec3<float>& rayOrigin,
            const FCT::Vec3<float>& rayDir,
            float maxDist,
            FCT::Vec3<float>& hitPos,
            FCT::Vec3<float>& hitNormal) const noexcept
        {
            const float step = 0.5f;
            float curDist = 0.0f;
            FCT::Vec3<float> p = rayOrigin;

            while (curDist < maxDist)
            {
                p.x = rayOrigin.x + rayDir.x * curDist;
                p.y = rayOrigin.y + rayDir.y * curDist;
                p.z = rayOrigin.z + rayDir.z * curDist;

                float terrainH = getHeightAt(p.x, p.z);
                if (p.y <= terrainH)
                {
                    hitPos = FCT::Vec3<float>(p.x, terrainH, p.z);
                    hitNormal = getNormalAt(p.x, p.z);
                    return true;
                }
                curDist += step;
            }
            return false;
        }

        void TerrainRuntimeComposite::syncPhysicsCollider()
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (domain)
            {
                domain->physics.setHeightfieldCollider(m_resolution, m_gridSpacing, m_originX, m_originZ, m_heightfield);
            }
        }

        void TerrainRuntimeComposite::generateClipmapMeshBuffers()
        {
            m_clipmapGeometries.clear();
            const uint32_t ringLevels = kTerrainClipmapRingLevels;
            const uint32_t N = kTerrainClipmapRingGridN;

            for (uint32_t lvl = 0; lvl < ringLevels; ++lvl)
            {
                ClipmapRingGeometry ring;
                ring.level = lvl;
                float spacing = m_gridSpacing * static_cast<float>(1 << lvl);
                float halfExtent = spacing * static_cast<float>(N) * 0.5f;

                for (uint32_t z = 0; z <= N; ++z)
                {
                    float lz = -halfExtent + static_cast<float>(z) * spacing;
                    for (uint32_t x = 0; x <= N; ++x)
                    {
                        float lx = -halfExtent + static_cast<float>(x) * spacing;
                        float h = getHeightAt(lx, lz);
                        FCT::Vec3<float> norm = getNormalAt(lx, lz);
                        float u = (lx - m_originX) / getWorldSize();
                        float v = (lz - m_originZ) / getWorldSize();

                        ring.vertices.push_back(lx);
                        ring.vertices.push_back(h);
                        ring.vertices.push_back(lz);
                        ring.vertices.push_back(norm.x);
                        ring.vertices.push_back(norm.y);
                        ring.vertices.push_back(norm.z);
                        ring.vertices.push_back(u);
                        ring.vertices.push_back(v);
                    }
                }

                for (uint32_t z = 0; z < N; ++z)
                {
                    for (uint32_t x = 0; x < N; ++x)
                    {
                        if (lvl > 0 && x >= (N / 4) && x < (3 * N / 4) && z >= (N / 4) && z < (3 * N / 4))
                        {
                            continue;
                        }

                        uint32_t v0 = z * (N + 1) + x;
                        uint32_t v1 = v0 + 1;
                        uint32_t v2 = (z + 1) * (N + 1) + x;
                        uint32_t v3 = v2 + 1;

                        ring.indices.push_back(v0);
                        ring.indices.push_back(v2);
                        ring.indices.push_back(v1);

                        ring.indices.push_back(v1);
                        ring.indices.push_back(v2);
                        ring.indices.push_back(v3);
                    }
                }

                m_clipmapGeometries.push_back(std::move(ring));
            }
        }

        void TerrainRuntimeComposite::registerPipelinesToRenderer()
        {
            if (m_pipelinesRegistered)
                return;

            auto* domain = Domain::Domain::InstancePtr();
            if (domain)
            {
                Domain::Renderer::RenderGraphExtensionDesc desc;
                desc.name = "TerrainClipmapPipeline";
                desc.onSetupPass = nullptr;
                desc.syncNodes.push_back(Domain::Renderer::RenderSyncNodeDesc{
                    .name = "SyncTerrainClipmap",
                    .callback = [this]() {
                        auto* d = Domain::Domain::InstancePtr();
                        if (d)
                        {
                            updateClipmapLevels(d->renderer.getCameraWorldPosition());
                        }
                    },
                    .predecessors = {},
                    .successors = {}
                });
                desc.passSubscribers.push_back(Domain::Renderer::NamedPassSubscription{
                    .passName = "ObjectPass",
                    .callback = [this](FCT::PassSubmitEvent env) {
                        (void)env;
                    }
                });
                domain->renderer.registerRenderGraphExtension(std::move(desc));
                m_pipelinesRegistered = true;
            }
        }

        void TerrainRuntimeComposite::unregisterPipelinesFromRenderer()
        {
            if (!m_pipelinesRegistered)
                return;

            auto* domain = Domain::Domain::InstancePtr();
            if (domain)
            {
                domain->renderer.unregisterRenderGraphExtension("TerrainClipmapPipeline");
                m_pipelinesRegistered = false;
            }
        }

    } // namespace Terrain
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::Terrain::TerrainRuntimeComposite)
