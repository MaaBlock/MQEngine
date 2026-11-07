#include "CesiumPrepareRendererResources.h"
#include "CesiumComponents.h"

namespace MQEngine {

CesiumPrepareRendererResources::CesiumPrepareRendererResources(
    FCT::Context* rhiContext,
    DataManager* dataManager,
    entt::entity tilesetEntity,
    entt::registry* targetRegistry)
    : m_rhiContext(rhiContext)
    , m_dataManager(dataManager)
    , m_tilesetEntity(tilesetEntity)
    , m_targetRegistry(targetRegistry) {}

CesiumPrepareRendererResources::~CesiumPrepareRendererResources() {
    flushAllDeferredDeletions();
}

void CesiumPrepareRendererResources::pumpDeferredDeletions() {
    if (m_deferredDeletions.empty()) {
        return;
    }

    size_t writeIdx = 0;
    for (size_t i = 0; i < m_deferredDeletions.size(); ++i) {
        auto& item = m_deferredDeletions[i];
        if (item.framesRemaining > 1) {
            --item.framesRemaining;
            if (writeIdx != i) {
                m_deferredDeletions[writeIdx] = item;
            }
            ++writeIdx;
        } else {
            if (item.mesh) {
                delete item.mesh;
                item.mesh = nullptr;
            }
            if (item.texture) {
                item.texture->release();
                item.texture = nullptr;
            }
        }
    }
    m_deferredDeletions.resize(writeIdx);
}

void CesiumPrepareRendererResources::flushAllDeferredDeletions() {
    for (auto& item : m_deferredDeletions) {
        if (item.mesh) {
            delete item.mesh;
            item.mesh = nullptr;
        }
        if (item.texture) {
            item.texture->release();
            item.texture = nullptr;
        }
    }
    m_deferredDeletions.clear();
}

entt::registry* CesiumPrepareRendererResources::resolveTargetRegistry() const {
    if (!m_dataManager) {
        return m_targetRegistry;
    }
    auto registries = m_dataManager->currentRegistries();
    if (Scene::Scene* currentScene = m_dataManager->getCurrentScene()) {
        for (const auto& trunkName : currentScene->getTrunkList()) {
            if (SceneTrunk* trunk = currentScene->getLoadedTrunk(trunkName)) {
                if (entt::registry* trunkReg = trunk->getRegistry()) {
                    if (std::find(registries.begin(), registries.end(), trunkReg) == registries.end()) {
                        registries.push_back(trunkReg);
                    }
                }
            }
        }
        if (entt::registry* mainReg = currentScene->getRegistry()) {
            if (std::find(registries.begin(), registries.end(), mainReg) == registries.end()) {
                registries.push_back(mainReg);
            }
        }
    }
    if (m_targetRegistry) {
        for (auto* reg : registries) {
            if (reg == m_targetRegistry) {
                return m_targetRegistry;
            }
        }
        return nullptr;
    }
    for (auto* reg : registries) {
        if (!reg || m_tilesetEntity == entt::null || !reg->valid(m_tilesetEntity)) {
            continue;
        }
        const auto* tilesetPool = std::as_const(*reg).storage<Cesium3DTileset>();
        if (tilesetPool && tilesetPool->contains(m_tilesetEntity)) {
            return reg;
        }
    }
    return nullptr;
}

glm::dmat4 CesiumPrepareRendererResources::computeNodeLocalMatrix(const CesiumGltf::Node& node) {
    if (node.matrix.size() == 16) {
        return glm::dmat4(
            node.matrix[0], node.matrix[1], node.matrix[2], node.matrix[3],
            node.matrix[4], node.matrix[5], node.matrix[6], node.matrix[7],
            node.matrix[8], node.matrix[9], node.matrix[10], node.matrix[11],
            node.matrix[12], node.matrix[13], node.matrix[14], node.matrix[15]);
    }

    glm::dmat4 t(1.0);
    if (node.translation.size() == 3) {
        t = glm::translate(t, glm::dvec3(node.translation[0], node.translation[1], node.translation[2]));
    }

    glm::dmat4 r(1.0);
    if (node.rotation.size() == 4) {
        double x = node.rotation[0];
        double y = node.rotation[1];
        double z = node.rotation[2];
        double w = node.rotation[3];
        r[0][0] = 1.0 - 2.0 * (y * y + z * z);
        r[0][1] = 2.0 * (x * y + z * w);
        r[0][2] = 2.0 * (x * z - y * w);
        r[1][0] = 2.0 * (x * y - z * w);
        r[1][1] = 1.0 - 2.0 * (x * x + z * z);
        r[1][2] = 2.0 * (y * z + x * w);
        r[2][0] = 2.0 * (x * z + y * w);
        r[2][1] = 2.0 * (y * z - x * w);
        r[2][2] = 1.0 - 2.0 * (x * x + y * y);
    }

    glm::dmat4 s(1.0);
    if (node.scale.size() == 3) {
        s = glm::scale(s, glm::dvec3(node.scale[0], node.scale[1], node.scale[2]));
    }

    return t * r * s;
}

bool CesiumPrepareRendererResources::extractPositions(
    const CesiumGltf::Model& model,
    const CesiumGltf::MeshPrimitive& primitive,
    LoadThreadPrimitiveData& outPrim) {
    auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) {
        return false;
    }

    CesiumGltf::AccessorView<glm::vec3> posView(model, posIt->second);
    if (posView.status() != CesiumGltf::AccessorViewStatus::Valid || posView.size() == 0) {
        return false;
    }

    size_t vCount = static_cast<size_t>(posView.size());
    outPrim.positions.resize(vCount);
    for (int64_t i = 0; i < posView.size(); ++i) {
        glm::vec3 rawPos = posView[i];
        outPrim.positions[static_cast<size_t>(i)] = FCT::Vec3<>(rawPos.x, rawPos.y, rawPos.z);
    }
    return true;
}

void CesiumPrepareRendererResources::extractUVs(
    const CesiumGltf::Model& model,
    const CesiumGltf::MeshPrimitive& primitive,
    size_t vertexCount,
    int32_t texCoordIndex,
    LoadThreadPrimitiveData& outPrim) {
    outPrim.uvs.assign(vertexCount, FCT::Vec2(0.0f, 0.0f));
    std::string uvAttrName = "TEXCOORD_" + std::to_string(texCoordIndex);
    auto uvIt = primitive.attributes.find(uvAttrName);
    if (uvIt == primitive.attributes.end() && texCoordIndex != 0) {
        uvIt = primitive.attributes.find("TEXCOORD_0");
    }
    if (uvIt == primitive.attributes.end()) {
        return;
    }

    CesiumGltf::AccessorView<glm::vec2> uvFloatView(model, uvIt->second);
    if (uvFloatView.status() == CesiumGltf::AccessorViewStatus::Valid &&
        static_cast<size_t>(uvFloatView.size()) == vertexCount) {
        for (size_t i = 0; i < vertexCount; ++i) {
            glm::vec2 uv = uvFloatView[static_cast<int64_t>(i)];
            outPrim.uvs[i] = FCT::Vec2(uv.x, uv.y);
        }
        return;
    }

    CesiumGltf::AccessorView<glm::u16vec2> uvU16View(model, uvIt->second);
    if (uvU16View.status() == CesiumGltf::AccessorViewStatus::Valid &&
        static_cast<size_t>(uvU16View.size()) == vertexCount) {
        for (size_t i = 0; i < vertexCount; ++i) {
            glm::u16vec2 uv = uvU16View[static_cast<int64_t>(i)];
            outPrim.uvs[i] = FCT::Vec2(
                static_cast<float>(uv.x) / 65535.0f,
                static_cast<float>(uv.y) / 65535.0f);
        }
        return;
    }

    CesiumGltf::AccessorView<glm::u8vec2> uvU8View(model, uvIt->second);
    if (uvU8View.status() == CesiumGltf::AccessorViewStatus::Valid &&
        static_cast<size_t>(uvU8View.size()) == vertexCount) {
        for (size_t i = 0; i < vertexCount; ++i) {
            glm::u8vec2 uv = uvU8View[static_cast<int64_t>(i)];
            outPrim.uvs[i] = FCT::Vec2(
                static_cast<float>(uv.x) / 255.0f,
                static_cast<float>(uv.y) / 255.0f);
        }
    }
}

bool CesiumPrepareRendererResources::extractNormals(
    const CesiumGltf::Model& model,
    const CesiumGltf::MeshPrimitive& primitive,
    size_t vertexCount,
    LoadThreadPrimitiveData& outPrim) {
    auto normIt = primitive.attributes.find("NORMAL");
    if (normIt == primitive.attributes.end()) {
        return false;
    }

    CesiumGltf::AccessorView<glm::vec3> normView(model, normIt->second);
    if (normView.status() != CesiumGltf::AccessorViewStatus::Valid ||
        static_cast<size_t>(normView.size()) != vertexCount) {
        return false;
    }

    outPrim.normals.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        glm::vec3 rawN = normView[static_cast<int64_t>(i)];
        outPrim.normals[i] = FCT::Vec3<>(rawN.x, rawN.y, rawN.z);
    }
    outPrim.hasNormals = true;
    return true;
}

void CesiumPrepareRendererResources::extractIndices(
    const CesiumGltf::Model& model,
    const CesiumGltf::MeshPrimitive& primitive,
    size_t vertexCount,
    LoadThreadPrimitiveData& outPrim) {
    if (primitive.indices >= 0 && static_cast<size_t>(primitive.indices) < model.accessors.size()) {
        const auto& idxAccessor = model.accessors[static_cast<size_t>(primitive.indices)];
        if (idxAccessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE) {
            CesiumGltf::AccessorView<uint8_t> view(model, primitive.indices);
            if (view.status() == CesiumGltf::AccessorViewStatus::Valid) {
                outPrim.indices.resize(static_cast<size_t>(view.size()));
                for (int64_t i = 0; i < view.size(); ++i) {
                    outPrim.indices[static_cast<size_t>(i)] = static_cast<uint32_t>(view[i]);
                }
            }
        } else if (idxAccessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT) {
            CesiumGltf::AccessorView<uint16_t> view(model, primitive.indices);
            if (view.status() == CesiumGltf::AccessorViewStatus::Valid) {
                outPrim.indices.resize(static_cast<size_t>(view.size()));
                for (int64_t i = 0; i < view.size(); ++i) {
                    outPrim.indices[static_cast<size_t>(i)] = static_cast<uint32_t>(view[i]);
                }
            }
        } else if (idxAccessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_INT) {
            CesiumGltf::AccessorView<uint32_t> view(model, primitive.indices);
            if (view.status() == CesiumGltf::AccessorViewStatus::Valid) {
                outPrim.indices.resize(static_cast<size_t>(view.size()));
                for (int64_t i = 0; i < view.size(); ++i) {
                    outPrim.indices[static_cast<size_t>(i)] = view[i];
                }
            }
        }
    }

    if (outPrim.indices.empty()) {
        outPrim.indices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            outPrim.indices[i] = static_cast<uint32_t>(i);
        }
    }
}

bool CesiumPrepareRendererResources::convertTopologyToTriangleList(
    int32_t mode,
    size_t vertexCount,
    std::vector<uint32_t>& indices) {
    if (mode == CesiumGltf::MeshPrimitive::Mode::TRIANGLES) {
        return indices.size() >= 3;
    }

    if (mode == CesiumGltf::MeshPrimitive::Mode::TRIANGLE_STRIP) {
        if (indices.size() < 3) {
            return false;
        }
        std::vector<uint32_t> unstripped;
        unstripped.reserve((indices.size() - 2) * 3);
        const bool check16BitRestart = (vertexCount <= 65535u);
        size_t wind = 0;
        for (size_t i = 0; i + 2 < indices.size(); ++i) {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            // 处理图元重启标记，重置条带绕序计数器以防翻转法线与裂缝
            if (i0 == 0xFFFFFFFFu || i1 == 0xFFFFFFFFu || i2 == 0xFFFFFFFFu ||
                (check16BitRestart && (i0 == 65535u || i1 == 65535u || i2 == 65535u))) {
                wind = 0;
                continue;
            }

            if (i0 == i1 || i1 == i2 || i0 == i2) {
                ++wind;
                continue;
            }

            if (wind % 2 == 0) {
                unstripped.push_back(i0);
                unstripped.push_back(i1);
                unstripped.push_back(i2);
            } else {
                unstripped.push_back(i0);
                unstripped.push_back(i2);
                unstripped.push_back(i1);
            }
            ++wind;
        }
        indices = std::move(unstripped);
        return indices.size() >= 3;
    }

    if (mode == CesiumGltf::MeshPrimitive::Mode::TRIANGLE_FAN) {
        if (indices.size() < 3) {
            return false;
        }
        std::vector<uint32_t> unfanned;
        unfanned.reserve((indices.size() - 2) * 3);
        for (size_t i = 1; i + 1 < indices.size(); ++i) {
            unfanned.push_back(indices[0]);
            unfanned.push_back(indices[i]);
            unfanned.push_back(indices[i + 1]);
        }
        indices = std::move(unfanned);
        return indices.size() >= 3;
    }

    return false;
}

void CesiumPrepareRendererResources::computeSmoothNormals(LoadThreadPrimitiveData& prim) {
    size_t vertexCount = prim.positions.size();
    prim.normals.assign(vertexCount, FCT::Vec3<>(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i + 2 < prim.indices.size(); i += 3) {
        uint32_t i0 = prim.indices[i];
        uint32_t i1 = prim.indices[i + 1];
        uint32_t i2 = prim.indices[i + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue;
        }

        const FCT::Vec3<>& p0 = prim.positions[i0];
        const FCT::Vec3<>& p1 = prim.positions[i1];
        const FCT::Vec3<>& p2 = prim.positions[i2];

        FCT::Vec3<> e1 = p1 - p0;
        FCT::Vec3<> e2 = p2 - p0;
        FCT::Vec3<> fn = FCT::cross(e1, e2);

        prim.normals[i0] += fn;
        prim.normals[i1] += fn;
        prim.normals[i2] += fn;
    }

    for (size_t i = 0; i < vertexCount; ++i) {
        float len = prim.normals[i].length();
        if (len > 1e-6f) {
            prim.normals[i] = prim.normals[i] * (1.0f / len);
        } else {
            prim.normals[i] = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
        }
    }
    prim.hasNormals = true;
}

void CesiumPrepareRendererResources::extractMaterialAndTexture(
    const CesiumGltf::Model& model,
    const CesiumGltf::MeshPrimitive& primitive,
    int32_t& outTexCoordIndex,
    LoadThreadPrimitiveData& outPrim) {
    outTexCoordIndex = 0;
    if (primitive.material < 0 || static_cast<size_t>(primitive.material) >= model.materials.size()) {
        return;
    }

    const auto& mat = model.materials[static_cast<size_t>(primitive.material)];
    if (mat.hasExtension<CesiumGltf::ExtensionKhrMaterialsUnlit>()) {
        outPrim.isUnlit = true;
    }

    if (!mat.pbrMetallicRoughness.has_value()) {
        return;
    }

    const auto& pbr = mat.pbrMetallicRoughness.value();
    if (pbr.baseColorFactor.size() >= 4) {
        outPrim.baseColorFactor = FCT::Vec4(
            static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]),
            static_cast<float>(pbr.baseColorFactor[3]));
    }

    if (!pbr.baseColorTexture.has_value()) {
        return;
    }

    outTexCoordIndex = static_cast<int32_t>(pbr.baseColorTexture->texCoord);
    int32_t texIdx = pbr.baseColorTexture->index;
    if (texIdx < 0 || static_cast<size_t>(texIdx) >= model.textures.size()) {
        return;
    }

    int32_t imgIdx = model.textures[static_cast<size_t>(texIdx)].source;
    if (imgIdx < 0 || static_cast<size_t>(imgIdx) >= model.images.size()) {
        return;
    }

    const CesiumGltf::ImageCesium& image = model.images[static_cast<size_t>(imgIdx)].cesium;
    if (image.width <= 0 || image.height <= 0 || image.pixelData.empty()) {
        return;
    }

    outPrim.hasTexture = true;
    outPrim.texWidth = image.width;
    outPrim.texHeight = image.height;
    size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    outPrim.texPixels.resize(pixelCount * 4);

    const uint8_t* src = reinterpret_cast<const uint8_t*>(image.pixelData.data());
    int32_t channels = image.channels;
    if (channels == 4 && image.pixelData.size() >= pixelCount * 4) {
        std::memcpy(outPrim.texPixels.data(), src, pixelCount * 4);
    } else if (channels == 3 && image.pixelData.size() >= pixelCount * 3) {
        for (size_t p = 0; p < pixelCount; ++p) {
            outPrim.texPixels[p * 4 + 0] = src[p * 3 + 0];
            outPrim.texPixels[p * 4 + 1] = src[p * 3 + 1];
            outPrim.texPixels[p * 4 + 2] = src[p * 3 + 2];
            outPrim.texPixels[p * 4 + 3] = 255;
        }
    } else if (channels == 1 && image.pixelData.size() >= pixelCount) {
        for (size_t p = 0; p < pixelCount; ++p) {
            uint8_t v = src[p];
            outPrim.texPixels[p * 4 + 0] = v;
            outPrim.texPixels[p * 4 + 1] = v;
            outPrim.texPixels[p * 4 + 2] = v;
            outPrim.texPixels[p * 4 + 3] = 255;
        }
    } else {
        outPrim.hasTexture = false;
        outPrim.texPixels.clear();
    }
}

void CesiumPrepareRendererResources::buildPrimitiveOnLoadThread(
    const CesiumGltf::Model& model,
    const CesiumGltf::MeshPrimitive& primitive,
    const glm::dmat4& nodeTransform,
    LoadThreadTileResult& outResult) {
    LoadThreadPrimitiveData primData;
    primData.nodeTransform = nodeTransform;
    if (!extractPositions(model, primitive, primData)) {
        return;
    }

    size_t vertexCount = primData.positions.size();
    extractIndices(model, primitive, vertexCount, primData);
    if (!convertTopologyToTriangleList(primitive.mode, vertexCount, primData.indices)) {
        return;
    }

    int32_t texCoordIndex = 0;
    extractMaterialAndTexture(model, primitive, texCoordIndex, primData);
    extractUVs(model, primitive, vertexCount, texCoordIndex, primData);

    if (!extractNormals(model, primitive, vertexCount, primData)) {
        if (primData.hasTexture || primData.isUnlit) {
            // 倾斜摄影瓦片自带烘焙光照且无共享边界顶点，禁用按瓦片法线重算以消除跨瓦片光照亮暗接缝
            primData.isUnlit = true;
            primData.normals.assign(vertexCount, FCT::Vec3<>(0.0f, 1.0f, 0.0f));
        } else {
            computeSmoothNormals(primData);
        }
    }

    outResult.primitives.push_back(std::move(primData));
}

void CesiumPrepareRendererResources::processNodeRecursive(
    const CesiumGltf::Model& model,
    int32_t nodeIdx,
    const glm::dmat4& parentTransform,
    LoadThreadTileResult& outResult) const {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= model.nodes.size()) {
        return;
    }

    const auto& node = model.nodes[static_cast<size_t>(nodeIdx)];
    glm::dmat4 nodeTransform = parentTransform * computeNodeLocalMatrix(node);

    if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < model.meshes.size()) {
        const auto& mesh = model.meshes[static_cast<size_t>(node.mesh)];
        for (const auto& primitive : mesh.primitives) {
            buildPrimitiveOnLoadThread(model, primitive, nodeTransform, outResult);
        }
    }

    for (int32_t childIdx : node.children) {
        processNodeRecursive(model, childIdx, nodeTransform, outResult);
    }
}

CesiumAsync::Future<Cesium3DTilesSelection::TileLoadResultAndRenderResources>
CesiumPrepareRendererResources::prepareInLoadThread(
    const CesiumAsync::AsyncSystem& asyncSystem,
    Cesium3DTilesSelection::TileLoadResult&& tileLoadResult,
    const glm::dmat4& transform,
    const std::any& /*rendererOptions*/) {
    CesiumGltf::Model* pModel = std::get_if<CesiumGltf::Model>(&tileLoadResult.contentKind);
    if (!pModel) {
        return asyncSystem.createResolvedFuture(
            Cesium3DTilesSelection::TileLoadResultAndRenderResources{
                std::move(tileLoadResult),
                nullptr});
    }

    auto* result = new LoadThreadTileResult();
    const CesiumGltf::Model& model = *pModel;

    glm::dmat4 rootTransform = transform;
    rootTransform = CesiumGltfContent::GltfUtilities::applyRtcCenter(model, rootTransform);
    rootTransform = CesiumGltfContent::GltfUtilities::applyGltfUpAxisTransform(model, rootTransform);
    result->tileTransform = rootTransform;

    if (!model.scenes.empty()) {
        int32_t sceneIdx = (model.scene >= 0 && static_cast<size_t>(model.scene) < model.scenes.size()) ? model.scene : 0;
        for (int32_t nodeIdx : model.scenes[static_cast<size_t>(sceneIdx)].nodes) {
            processNodeRecursive(model, nodeIdx, glm::dmat4(1.0), *result);
        }
    } else if (!model.nodes.empty()) {
        std::vector<bool> isChild(model.nodes.size(), false);
        for (const auto& n : model.nodes) {
            for (int32_t c : n.children) {
                if (c >= 0 && static_cast<size_t>(c) < model.nodes.size()) {
                    isChild[static_cast<size_t>(c)] = true;
                }
            }
        }
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (!isChild[i]) {
                processNodeRecursive(model, static_cast<int32_t>(i), glm::dmat4(1.0), *result);
            }
        }
    } else {
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            const auto& mesh = model.meshes[i];
            for (const auto& primitive : mesh.primitives) {
                buildPrimitiveOnLoadThread(model, primitive, glm::dmat4(1.0), *result);
            }
        }
    }

    return asyncSystem.createResolvedFuture(
        Cesium3DTilesSelection::TileLoadResultAndRenderResources{
            std::move(tileLoadResult),
            result});
}

FCT::StaticMesh<uint32_t>* CesiumPrepareRendererResources::createGpuMesh(
    const LoadThreadPrimitiveData& primData,
    const glm::dmat4& tileTransform) const {
    if (!m_rhiContext || primData.positions.empty() || primData.indices.empty()) {
        return nullptr;
    }

    // 在主线程同步屏障中统一使用当前地理参考矩阵进行双精度变换，保证所有瓦片边缘坐标严格对齐
    glm::dmat4 fullTransform = getEcefToLocalTransform() * tileTransform * primData.nodeTransform;
    glm::dmat3 normalMat(1.0);
    if (primData.hasNormals) {
        normalMat = glm::transpose(glm::inverse(glm::dmat3(fullTransform)));
    }

    FCT::Vec4 vertexColor = primData.baseColorFactor;
    if (primData.isUnlit) {
        vertexColor.w = -1.0f;
    }

    auto* mesh = new FCT::StaticMesh<uint32_t>(m_rhiContext, StandardMeshVertexLayout);
    size_t vertexCount = primData.positions.size();
    for (size_t i = 0; i < vertexCount; ++i) {
        const FCT::Vec3<>& rawPos = primData.positions[i];
        glm::dvec4 worldPos4 = fullTransform * glm::dvec4(rawPos.x, rawPos.y, rawPos.z, 1.0);
        FCT::Vec3<> pos(
            static_cast<float>(worldPos4.x),
            static_cast<float>(worldPos4.y),
            static_cast<float>(worldPos4.z));

        const FCT::Vec2& uv = i < primData.uvs.size() ? primData.uvs[i] : FCT::Vec2(0.0f, 0.0f);

        FCT::Vec3<> normal(0.0f, 1.0f, 0.0f);
        if (primData.hasNormals && i < primData.normals.size()) {
            const FCT::Vec3<>& rawN = primData.normals[i];
            glm::dvec3 worldN = normalMat * glm::dvec3(rawN.x, rawN.y, rawN.z);
            double len = glm::length(worldN);
            if (len > 1e-6) {
                worldN /= len;
                normal = FCT::Vec3<>(
                    static_cast<float>(worldN.x),
                    static_cast<float>(worldN.y),
                    static_cast<float>(worldN.z));
            }
        }

        FCT::Vec3<> up = std::abs(normal.y) < 0.99f ? FCT::Vec3<>(0.0f, 1.0f, 0.0f) : FCT::Vec3<>(1.0f, 0.0f, 0.0f);
        FCT::Vec3<> tangent = FCT::normalize(FCT::cross(up, normal));
        FCT::Vec3<> bitangent = FCT::cross(normal, tangent);

        mesh->addVertex(
            vertexColor,
            FCT::Vec4(pos.x, pos.y, pos.z, 1.0f),
            uv,
            normal,
            tangent,
            bitangent);
    }

    mesh->setIndices(primData.indices);
    mesh->create();
    return mesh;
}

FCT::SingleBufferImage* CesiumPrepareRendererResources::createGpuTexture(
    const LoadThreadPrimitiveData& primData) const {
    if (!m_rhiContext || !primData.hasTexture || primData.texPixels.empty() ||
        primData.texWidth <= 0 || primData.texHeight <= 0) {
        return nullptr;
    }

    auto* image = new FCT::SingleBufferImage(m_rhiContext);
    image->width(static_cast<uint32_t>(primData.texWidth));
    image->height(static_cast<uint32_t>(primData.texHeight));
    image->format(FCT::Format::R8G8B8A8_SRGB);
    image->as(FCT::ImageUsage::Texture);
    image->initData(
        const_cast<uint8_t*>(primData.texPixels.data()),
        static_cast<int>(primData.texPixels.size()));
    image->create(true);
    return image;
}

entt::entity CesiumPrepareRendererResources::createPrimitiveEntity(
    entt::registry& registry,
    const LoadThreadPrimitiveData& /*primData*/,
    FCT::SingleBufferImage* gpuTexture) const {
    entt::entity entity = registry.create();
    registry.emplace<CesiumTilePrimitiveComponent>(entity, CesiumTilePrimitiveComponent{m_tilesetEntity});
    registry.emplace<RuntimeSubEntityTag>(entity, RuntimeSubEntityTag{static_cast<uint32_t>(m_tilesetEntity), true});

    auto& meshInstance = registry.emplace<StaticMeshInstance>(entity);
    meshInstance.mesh = nullptr;

    auto& diffuseComp = registry.emplace<DiffuseTextureComponent>(entity);
    if (gpuTexture) {
        diffuseComp.texture = gpuTexture;
    } else if (g_engineGlobal.textureRenderSystem) {
        diffuseComp.texture = g_engineGlobal.textureRenderSystem->getDefaultWhiteTexture();
    }
    diffuseComp.visible = (diffuseComp.texture != nullptr);

    return entity;
}

void* CesiumPrepareRendererResources::prepareInMainThread(
    Cesium3DTilesSelection::Tile& /*tile*/,
    void* pLoadThreadResult) {
    auto* loadResult = static_cast<LoadThreadTileResult*>(pLoadThreadResult);
    if (!loadResult) {
        return nullptr;
    }

    auto* renderResources = new CesiumTileRenderResources();
    entt::registry* registry = resolveTargetRegistry();

    if (registry) {
        renderResources->primitives.reserve(loadResult->primitives.size());
        for (const auto& primData : loadResult->primitives) {
            FCT::StaticMesh<uint32_t>* gpuMesh = createGpuMesh(primData, loadResult->tileTransform);
            if (!gpuMesh) {
                continue;
            }
            ++m_createdMeshCount;

            FCT::SingleBufferImage* gpuTexture = createGpuTexture(primData);
            entt::entity entity = createPrimitiveEntity(*registry, primData, gpuTexture);
            registry->emplace_or_replace<CacheGpuDrivenBounds>(
                entity,
                GpuDrivenSystem::computeMeshLocalBounds(gpuMesh));

            CesiumPrimitiveRenderData primRender;
            primRender.entity = entity;
            primRender.mesh = gpuMesh;
            primRender.texture = gpuTexture;
            primRender.ownsTexture = (gpuTexture != nullptr);
            renderResources->primitives.push_back(primRender);
        }
    }

    delete loadResult;
    return renderResources;
}

void CesiumPrepareRendererResources::free(
    Cesium3DTilesSelection::Tile& /*tile*/,
    void* pLoadThreadResult,
    void* pMainThreadResult) noexcept {
    if (pLoadThreadResult) {
        delete static_cast<LoadThreadTileResult*>(pLoadThreadResult);
    }

    if (pMainThreadResult) {
        auto* renderResources = static_cast<CesiumTileRenderResources*>(pMainThreadResult);
        entt::registry* registry = resolveTargetRegistry();

        for (auto& prim : renderResources->primitives) {
            if (registry && prim.entity != entt::null && registry->valid(prim.entity)) {
                if (registry->all_of<StaticMeshInstance>(prim.entity)) {
                    registry->get<StaticMeshInstance>(prim.entity).mesh = nullptr;
                }
                if (registry->all_of<DiffuseTextureComponent>(prim.entity)) {
                    registry->get<DiffuseTextureComponent>(prim.entity).visible = false;
                }
                registry->destroy(prim.entity);
            }

            if (prim.mesh || (prim.ownsTexture && prim.texture)) {
                DeferredGpuGarbage garbage;
                garbage.mesh = prim.mesh;
                garbage.texture = prim.ownsTexture ? prim.texture : nullptr;
                garbage.framesRemaining = kDeferredFreeFrames;
                m_deferredDeletions.push_back(garbage);
                prim.mesh = nullptr;
                prim.texture = nullptr;
            }
        }

        delete renderResources;
    }
}

} // namespace MQEngine
