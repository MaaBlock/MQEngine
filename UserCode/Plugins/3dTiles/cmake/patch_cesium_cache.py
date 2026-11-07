import os
import sys

def patch_file(filepath, tag):
    if not os.path.exists(filepath):
        return
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Avoid patching twice
    if 'X-Cesium-Cache-Key' in content:
        return

    # 1. Add fmt::format include if not present
    if '#include <fmt/format.h>' not in content:
        content = '#include <fmt/format.h>\n' + content

    # 2. Add the hash function at the top
    hash_func = """
#include <CesiumGeometry/BoundingSphere.h>
#include <CesiumGeometry/OrientedBoundingBox.h>
#include <CesiumGeospatial/BoundingRegion.h>
#include <CesiumGeospatial/BoundingRegionWithLooseFittingHeights.h>
#include <CesiumGeospatial/S2CellBoundingVolume.h>

namespace {
std::string computeBoundingVolumeHash(const Cesium3DTilesSelection::BoundingVolume& boundingVolume) {
    std::string key;
    struct Operation {
        std::string& key;
        void operator()(const CesiumGeometry::BoundingSphere& sphere) {
            key = fmt::format("sphere_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
                              sphere.getCenter().x, sphere.getCenter().y, sphere.getCenter().z,
                              sphere.getRadius());
        }
        void operator()(const CesiumGeometry::OrientedBoundingBox& box) {
            const auto& center = box.getCenter();
            const auto& halfAxes = box.getHalfAxes();
            key = fmt::format("box_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
                              center.x, center.y, center.z,
                              halfAxes[0].x, halfAxes[0].y, halfAxes[0].z,
                              halfAxes[1].x, halfAxes[1].y, halfAxes[1].z,
                              halfAxes[2].x, halfAxes[2].y, halfAxes[2].z);
        }
        void operator()(const CesiumGeospatial::BoundingRegion& region) {
            const auto& rectangle = region.getRectangle();
            key = fmt::format("region_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
                              rectangle.getWest(), rectangle.getSouth(),
                              rectangle.getEast(), rectangle.getNorth(),
                              region.getMinimumHeight(),
                              region.getMaximumHeight());
        }
        void operator()(const CesiumGeospatial::BoundingRegionWithLooseFittingHeights& region) {
            const auto& rectangle = region.getBoundingRegion().getRectangle();
            key = fmt::format("region_loose_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
                              rectangle.getWest(), rectangle.getSouth(),
                              rectangle.getEast(), rectangle.getNorth(),
                              region.getBoundingRegion().getMinimumHeight(),
                              region.getBoundingRegion().getMaximumHeight());
        }
        void operator()(const CesiumGeospatial::S2CellBoundingVolume& s2) {
            key = fmt::format("s2_{}_{:.8f}_{:.8f}",
                              s2.getCellID().getID(),
                              s2.getMinimumHeight(), s2.getMaximumHeight());
        }
    };
    std::visit(Operation{key}, boundingVolume);
    return key;
}
} // namespace
"""
    insert_pos = content.find("namespace Cesium3DTilesSelection {")
    if insert_pos == -1:
        return

    content = content[:insert_pos] + hash_func + content[insert_pos:]

    # 3. Patch the request call
    find_str = "return pAssetAccessor->get(asyncSystem, "
    if find_str not in content:
        return

    if tag == 'TilesetJsonLoader':
        replace_str = """
    std::string bboxHash = computeBoundingVolumeHash(tile.getBoundingVolume());
    std::vector<CesiumAsync::IAssetAccessor::THeader> newHeaders = requestHeaders;
    newHeaders.emplace_back("X-Cesium-Cache-Key", bboxHash);
    return pAssetAccessor->get(asyncSystem, resolvedUrl, newHeaders)
"""
        content = content.replace("return pAssetAccessor->get(asyncSystem, resolvedUrl, requestHeaders)", replace_str.strip())
    else:
        content = content.replace(
            "const std::vector<CesiumAsync::IAssetAccessor::THeader>& requestHeaders,\n    CesiumGltf::Ktx2TranscodeTargets",
            "const std::vector<CesiumAsync::IAssetAccessor::THeader>& requestHeaders,\n    const Tile& tile,\n    CesiumGltf::Ktx2TranscodeTargets"
        )
        content = content.replace(
            "tileUrl,\n      requestHeaders,\n      contentOptions.",
            "tileUrl,\n      requestHeaders,\n      tile,\n      contentOptions."
        )

        replace_str = """
    std::string bboxHash = computeBoundingVolumeHash(tile.getBoundingVolume());
    std::vector<CesiumAsync::IAssetAccessor::THeader> newHeaders = requestHeaders;
    newHeaders.emplace_back("X-Cesium-Cache-Key", bboxHash);
    return pAssetAccessor->get(asyncSystem, tileUrl, newHeaders)
"""
        content = content.replace("return pAssetAccessor->get(asyncSystem, tileUrl, requestHeaders)", replace_str.strip())

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(1)
    base_dir = os.path.join(sys.argv[1], "Cesium3DTilesSelection", "src")
    patch_file(os.path.join(base_dir, "TilesetJsonLoader.cpp"), "TilesetJsonLoader")
    patch_file(os.path.join(base_dir, "ImplicitQuadtreeLoader.cpp"), "ImplicitQuadtreeLoader")
    patch_file(os.path.join(base_dir, "ImplicitOctreeLoader.cpp"), "ImplicitOctreeLoader")
