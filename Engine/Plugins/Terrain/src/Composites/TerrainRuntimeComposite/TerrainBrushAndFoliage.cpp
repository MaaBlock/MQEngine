/**
 * @file TerrainBrushAndFoliage.cpp
 * @brief 大世界地形高度场雕刻笔刷算子、4 层 Splatmap 权重涂抹与磁盘持久化序列化实现
 */
#include "TerrainRuntimeComposite.h"

#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>

#include <cmath>
#include <algorithm>
#include <sstream>

namespace MQEngine
{
    namespace Terrain
    {
        void TerrainRuntimeComposite::modifyHeight(float worldX, float worldZ, float radius, float delta, float falloff)
        {
            (void)falloff;
            if (radius <= 0.0f || m_heightfield.empty())
                return;

            const float r2 = radius * radius;
            int minX = std::max(0, static_cast<int>((worldX - radius - m_originX) / m_gridSpacing));
            int maxX = std::min(static_cast<int>(m_resolution - 1), static_cast<int>((worldX + radius - m_originX) / m_gridSpacing));
            int minZ = std::max(0, static_cast<int>((worldZ - radius - m_originZ) / m_gridSpacing));
            int maxZ = std::min(static_cast<int>(m_resolution - 1), static_cast<int>((worldZ + radius - m_originZ) / m_gridSpacing));

            for (int z = minZ; z <= maxZ; ++z)
            {
                float pz = m_originZ + static_cast<float>(z) * m_gridSpacing;
                for (int x = minX; x <= maxX; ++x)
                {
                    float px = m_originX + static_cast<float>(x) * m_gridSpacing;
                    float d2 = (px - worldX) * (px - worldX) + (pz - worldZ) * (pz - worldZ);
                    if (d2 <= r2)
                    {
                        float dist = std::sqrt(d2);
                        float factor = (std::cos(dist / radius * 3.14159265f) + 1.0f) * 0.5f;
                        size_t idx = static_cast<size_t>(z) * m_resolution + static_cast<size_t>(x);
                        m_heightfield[idx] += delta * factor;
                    }
                }
            }
            syncPhysicsCollider();
        }

        void TerrainRuntimeComposite::smoothHeight(float worldX, float worldZ, float radius, float strength)
        {
            if (radius <= 0.0f || m_heightfield.empty())
                return;

            const float r2 = radius * radius;
            int minX = std::max(1, static_cast<int>((worldX - radius - m_originX) / m_gridSpacing));
            int maxX = std::min(static_cast<int>(m_resolution - 2), static_cast<int>((worldX + radius - m_originX) / m_gridSpacing));
            int minZ = std::max(1, static_cast<int>((worldZ - radius - m_originZ) / m_gridSpacing));
            int maxZ = std::min(static_cast<int>(m_resolution - 2), static_cast<int>((worldZ + radius - m_originZ) / m_gridSpacing));

            for (int z = minZ; z <= maxZ; ++z)
            {
                float pz = m_originZ + static_cast<float>(z) * m_gridSpacing;
                for (int x = minX; x <= maxX; ++x)
                {
                    float px = m_originX + static_cast<float>(x) * m_gridSpacing;
                    float d2 = (px - worldX) * (px - worldX) + (pz - worldZ) * (pz - worldZ);
                    if (d2 <= r2)
                    {
                        size_t idx = static_cast<size_t>(z) * m_resolution + static_cast<size_t>(x);
                        // 3x3 邻域高斯均值滤波
                        float avg = (m_heightfield[idx - 1] + m_heightfield[idx + 1] +
                                     m_heightfield[idx - m_resolution] + m_heightfield[idx + m_resolution] +
                                     m_heightfield[idx] * 4.0f) / 8.0f;
                        m_heightfield[idx] = m_heightfield[idx] * (1.0f - strength) + avg * strength;
                    }
                }
            }
            syncPhysicsCollider();
        }

        void TerrainRuntimeComposite::flattenHeight(float worldX, float worldZ, float radius, float targetHeight, float strength)
        {
            if (radius <= 0.0f || m_heightfield.empty())
                return;

            const float r2 = radius * radius;
            int minX = std::max(0, static_cast<int>((worldX - radius - m_originX) / m_gridSpacing));
            int maxX = std::min(static_cast<int>(m_resolution - 1), static_cast<int>((worldX + radius - m_originX) / m_gridSpacing));
            int minZ = std::max(0, static_cast<int>((worldZ - radius - m_originZ) / m_gridSpacing));
            int maxZ = std::min(static_cast<int>(m_resolution - 1), static_cast<int>((worldZ + radius - m_originZ) / m_gridSpacing));

            for (int z = minZ; z <= maxZ; ++z)
            {
                float pz = m_originZ + static_cast<float>(z) * m_gridSpacing;
                for (int x = minX; x <= maxX; ++x)
                {
                    float px = m_originX + static_cast<float>(x) * m_gridSpacing;
                    float d2 = (px - worldX) * (px - worldX) + (pz - worldZ) * (pz - worldZ);
                    if (d2 <= r2)
                    {
                        float dist = std::sqrt(d2);
                        float factor = (std::cos(dist / radius * 3.14159265f) + 1.0f) * 0.5f;
                        size_t idx = static_cast<size_t>(z) * m_resolution + static_cast<size_t>(x);
                        m_heightfield[idx] = m_heightfield[idx] * (1.0f - factor * strength) + targetHeight * (factor * strength);
                    }
                }
            }
            syncPhysicsCollider();
        }

        void TerrainRuntimeComposite::paintSplatmap(float worldX, float worldZ, float radius, uint8_t layerIndex, float weight)
        {
            if (layerIndex >= kTerrainSplatLayerCount || radius <= 0.0f || m_splatmap.empty())
                return;

            const float r2 = radius * radius;
            int minX = std::max(0, static_cast<int>((worldX - radius - m_originX) / m_gridSpacing));
            int maxX = std::min(static_cast<int>(m_resolution - 1), static_cast<int>((worldX + radius - m_originX) / m_gridSpacing));
            int minZ = std::max(0, static_cast<int>((worldZ - radius - m_originZ) / m_gridSpacing));
            int maxZ = std::min(static_cast<int>(m_resolution - 1), static_cast<int>((worldZ + radius - m_originZ) / m_gridSpacing));

            for (int z = minZ; z <= maxZ; ++z)
            {
                float pz = m_originZ + static_cast<float>(z) * m_gridSpacing;
                for (int x = minX; x <= maxX; ++x)
                {
                    float px = m_originX + static_cast<float>(x) * m_gridSpacing;
                    float d2 = (px - worldX) * (px - worldX) + (pz - worldZ) * (pz - worldZ);
                    if (d2 <= r2)
                    {
                        float dist = std::sqrt(d2);
                        float factor = (std::cos(dist / radius * 3.14159265f) + 1.0f) * 0.5f * weight;
                        size_t idx = static_cast<size_t>(z) * m_resolution + static_cast<size_t>(x);

                        m_splatmap[idx][layerIndex] = std::clamp(m_splatmap[idx][layerIndex] + factor, 0.0f, 1.0f);

                        // 归一化权重使四层权重和始终为 1.0
                        float sum = m_splatmap[idx][0] + m_splatmap[idx][1] + m_splatmap[idx][2] + m_splatmap[idx][3];
                        if (sum > 1e-4f)
                        {
                            for (int k = 0; k < 4; ++k)
                            {
                                m_splatmap[idx][k] /= sum;
                            }
                        }
                    }
                }
            }
        }

        Status TerrainRuntimeComposite::saveTerrainToDisk(const std::string& filePath)
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader unavailable");
            }

            std::ostringstream oss;
            oss << "TERRAIN_V1\n";
            oss << m_resolution << " " << m_gridSpacing << " " << m_originX << " " << m_originZ << "\n";
            for (float h : m_heightfield)
            {
                oss << h << " ";
            }
            oss << "\n";

            return loader->saveFile(filePath, oss.str());
        }

        Status TerrainRuntimeComposite::loadTerrainFromDisk(const std::string& filePath)
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader unavailable");
            }

            auto res = loader->loadFile(filePath);
            if (!res.ok())
            {
                return res.status();
            }

            std::istringstream iss(*res);
            std::string magic;
            iss >> magic;
            if (magic != "TERRAIN_V1")
            {
                return InvalidArgumentError("Invalid terrain file format: " + filePath);
            }

            iss >> m_resolution >> m_gridSpacing >> m_originX >> m_originZ;
            const size_t total = static_cast<size_t>(m_resolution) * m_resolution;
            m_heightfield.resize(total);
            for (size_t i = 0; i < total; ++i)
            {
                iss >> m_heightfield[i];
            }

            syncPhysicsCollider();
            generateClipmapMeshBuffers();

            return OkStatus();
        }

    } // namespace Terrain
} // namespace MQEngine
