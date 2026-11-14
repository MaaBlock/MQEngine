/**
 * @file SceneViewerComposite.cpp
 * @brief SceneViewerComposite 生命周期、Dock 窗口注册、相机投影矩阵与 Domain ECS 实体同步实现
 */
#include "SceneViewerComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/resource/table/SchemaTable.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>
#include <Composites/EditorUiComposite/UiSignals.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>
#include <unordered_set>

namespace MQEngine
{
    namespace MQEditor
    {
        SceneViewerComposite::SceneViewerComposite() = default;

        SceneViewerComposite::~SceneViewerComposite()
        {
            onUnload();
        }

        Status SceneViewerComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            // 空场景启动：不创建任何硬编码默认实体，由 onInit 按优先级加载工程场景

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    "3D 视口 (Scene Viewport)",
                    EditorBase::EditorDockSlot::Center,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderViewport();
                    });

                m_editorUiComposite->registerDockWindow(
                    "层级大纲 (Scene Hierarchy)",
                    EditorBase::EditorDockSlot::Left,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderHierarchy();
                    });

                m_editorUiComposite->registerDockWindow(
                    "实体检视器 (Inspector)",
                    EditorBase::EditorDockSlot::Right,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderInspector();
                    });

                m_editorUiComposite->registerDockWindow(
                    "相机控制 (Camera Control)",
                    EditorBase::EditorDockSlot::Right,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderCameraControl();
                    });
            }

            return OkStatus();
        }

        Status SceneViewerComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            registerSceneSignalHandlers();
            syncProjectStartupStateIfReady();
            return OkStatus();
        }

        void SceneViewerComposite::onTick(float deltaTime)
        {
            ZoneScoped();
            m_lastDeltaTime = (deltaTime > 0.0001f) ? deltaTime : 0.016f;
            if (!m_sceneSignalsRegistered)
            {
                registerSceneSignalHandlers();
            }
            syncProjectStartupStateIfReady();

            for (auto& entity : m_entities)
            {
                if (entity.ecsEntity == FCT::ECS::null)
                {
                    syncEntityToDomain(entity);
                }
            }

            updatePieSimulation(m_lastDeltaTime);

            // 若外部游戏/编辑器插件在运行时向 Domain::Scene 增删实体，自动同步至层级缓存
            if (auto* domain = Domain::Domain::InstancePtr();
                domain && domain->getScene() && domain->getScene()->isInitialized())
            {
                auto* scene = domain->getScene();
                size_t domainEntityCount = 0;
                for (const auto& regName : scene->getRegistryNames())
                {
                    if (regName == "Editor")
                    {
                        continue;
                    }
                    if (const auto* reg = scene->getRegistryByName(regName))
                    {
                        auto view = reg->view<EditorNameComponent>();
                        domainEntityCount += view.size();
                    }
                }
                if (isPiePlaying() || domainEntityCount != m_entities.size())
                {
                    syncEntitiesFromDomain();
                }
            }

            syncCameraToRenderer();
        }

        void SceneViewerComposite::onUnload()
        {
            m_wasPiePlaying = false;
            m_initialStartupResolved = false;
            m_boundProjectDir.clear();
            m_entityClipboard.clear();

            if (m_editorUiComposite)
            {
                m_editorUiComposite->getCommandStack().clear();
                m_editorUiComposite->unregisterDockWindow("3D 视口 (Scene Viewport)");
                m_editorUiComposite->unregisterDockWindow("层级大纲 (Scene Hierarchy)");
                m_editorUiComposite->unregisterDockWindow("实体检视器 (Inspector)");
                m_editorUiComposite->unregisterDockWindow("相机控制 (Camera Control)");
            }

            if (auto* domain = Domain::Domain::InstancePtr())
            {
                if (auto* scene = domain->getScene(); scene && m_editorCameraEntity != FCT::ECS::null)
                {
                    if (auto* editorReg = scene->getRegistryByName("Editor");
                        editorReg && editorReg->valid(m_editorCameraEntity))
                    {
                        editorReg->destroy(m_editorCameraEntity);
                    }
                }
                if (auto* renderer = domain->getRenderer(); renderer && renderer->isInitialized())
                {
                    renderer->unbindRenderTarget("EditorViewport");
                    renderer->unbindRenderTarget("EditorCameraPreview");
                }
            }

            m_editorCameraEntity = FCT::ECS::null;
            m_entities.clear();
            m_editorUiComposite = nullptr;

            Asset::Composite::onUnload();
        }

        FCT::Mat4 SceneViewerComposite::computeEditorViewMatrix() const
        {
            const FCT::Vec3<> eye(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]);
            const FCT::Vec3<> target(m_cameraTarget[0], m_cameraTarget[1], m_cameraTarget[2]);
            return Domain::Renderer::RendererSubsystem::buildViewMatrix(eye, target);
        }

        FCT::Mat4 SceneViewerComposite::computeEditorProjMatrix(float aspect) const
        {
            const float safeAspect = (aspect > 0.05f) ? aspect : (16.0f / 9.0f);
            if (m_editorCameraOrthographic)
            {
                const float halfH = std::max(m_editorCameraOrthoSize, 0.1f);
                const float orthoNear = -std::max(m_cameraFar * 0.5f, 250.0f);
                return Domain::Renderer::RendererSubsystem::buildOrthoProjection(
                    halfH * safeAspect,
                    halfH,
                    orthoNear,
                    m_cameraFar,
                    false);
            }
            return Domain::Renderer::RendererSubsystem::buildPerspectiveProjection(
                m_cameraFov,
                safeAspect,
                m_cameraNear,
                m_cameraFar,
                false);
        }

        FCT::Mat4 SceneViewerComposite::computeViewMatrix() const
        {
            return getActiveViewMatrix();
        }

        FCT::Mat4 SceneViewerComposite::computeProjMatrix(float aspect) const
        {
            if (auto* domain = Domain::Domain::InstancePtr();
                domain && domain->getRenderer() && domain->getRenderer()->isInitialized())
            {
                if (const auto activeCam = domain->getRenderer()->findActiveCameraForTarget(getActiveViewportTargetName()))
                {
                    return activeCam->projMatrix;
                }
            }
            return computeEditorProjMatrix(aspect);
        }

        bool SceneViewerComposite::projectWorldToScreen(
            const FCT::Vec3<>& worldPos,
            const FCT::Mat4& viewProj,
            const ImVec2& canvasP0,
            const ImVec2& avail,
            ImVec2& outScreen) const
        {
            FCT::Vec4 clip = viewProj * FCT::Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
            if (clip.w <= 0.05f)
            {
                return false;
            }
            if (std::abs(clip.w - 1.0f) < 1e-4f && (clip.z < -0.05f || clip.z > 1.05f))
            {
                return false;
            }
            const float invW = 1.0f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;

            outScreen.x = canvasP0.x + (ndcX * 0.5f + 0.5f) * avail.x;
            outScreen.y = canvasP0.y + (ndcY * 0.5f + 0.5f) * avail.y;
            return true;
        }

        void SceneViewerComposite::buildCameraRay(
            const ImVec2& mouseScreen,
            const ImVec2& canvasP0,
            const ImVec2& avail,
            FCT::Vec3<>& outOrigin,
            FCT::Vec3<>& outDir) const
        {
            ZoneScoped();
            const float ndcX = ((mouseScreen.x - canvasP0.x) / std::max(avail.x, 1.0f)) * 2.0f - 1.0f;
            const float ndcY = ((mouseScreen.y - canvasP0.y) / std::max(avail.y, 1.0f)) * 2.0f - 1.0f;
            const float aspect = avail.x / std::max(avail.y, 1.0f);

            const FCT::Mat4 view = computeViewMatrix();
            const FCT::Mat4 proj = computeProjMatrix(aspect);
            if (std::abs(proj.m[15] - 1.0f) < 1e-4f)
            {
                const FCT::Mat4 invView = view.inverse();
                const FCT::Vec3<> eye(invView.m[3], invView.m[7], invView.m[11]);
                FCT::Vec3<> right(view.m[0], view.m[1], view.m[2]);
                FCT::Vec3<> up(-view.m[4], -view.m[5], -view.m[6]);
                FCT::Vec3<> forward(view.m[8], view.m[9], view.m[10]);
                right = (right.lengthSquared() > 1e-8f) ? right.normalize() : FCT::Vec3<>(1.0f, 0.0f, 0.0f);
                up = (up.lengthSquared() > 1e-8f) ? up.normalize() : FCT::Vec3<>(0.0f, 1.0f, 0.0f);
                outDir = (forward.lengthSquared() > 1e-8f) ? forward.normalize() : FCT::Vec3<>(0.0f, 0.0f, 1.0f);

                const float halfW = 1.0f / std::max(std::abs(proj.m[0]), 1e-5f);
                const float halfH = 1.0f / std::max(std::abs(proj.m[5]), 1e-5f);
                const FCT::Vec3<> planePt = eye + right * (ndcX * halfW) - up * (ndcY * halfH);
                outOrigin = planePt - outDir * 500.0f;
                return;
            }

            const FCT::Mat4 invVP = (proj * view).inverse();

            // Vulkan ZO [0,1] 下标准深度 m[10] > 0（近 0 远 1），Reverse-Z 下 m[10] < 0（近 1 远 0）
            const bool isReverseZ = (proj.m[10] < 0.0f);
            const float zNearNdc = isReverseZ ? 1.0f : 0.0f;
            const float zFarNdc = 0.5f;

            const FCT::Vec4 nearWorldH = invVP * FCT::Vec4(ndcX, ndcY, zNearNdc, 1.0f);
            const FCT::Vec4 farWorldH = invVP * FCT::Vec4(ndcX, ndcY, zFarNdc, 1.0f);
            const float nearInvW = (std::abs(nearWorldH.w) > 1e-6f) ? (1.0f / nearWorldH.w) : 1.0f;
            const float farInvW = (std::abs(farWorldH.w) > 1e-6f) ? (1.0f / farWorldH.w) : 1.0f;

            outOrigin = FCT::Vec3<>(nearWorldH.x * nearInvW, nearWorldH.y * nearInvW, nearWorldH.z * nearInvW);
            const FCT::Vec3<> farPt(farWorldH.x * farInvW, farWorldH.y * farInvW, farWorldH.z * farInvW);
            const FCT::Vec3<> dir = farPt - outOrigin;
            outDir = (dir.lengthSquared() > 1e-8f) ? dir.normalize() : FCT::Vec3<>(0.0f, 0.0f, 1.0f);
        }

        FCT::ECS::registry* SceneViewerComposite::resolveEntityRegistry(const SceneEntityDesc& entity) const
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain || !domain->getScene() || !domain->getScene()->isInitialized())
            {
                return nullptr;
            }
            const std::string regName = entity.registryName.empty() ? "Main" : entity.registryName;
            if (auto* regPtr = domain->getScene()->getRegistryByName(regName))
            {
                return regPtr;
            }
            return &domain->getScene()->getRegistry();
        }

        bool SceneViewerComposite::isEntityPassingFilter(const SceneEntityDesc& entity) const
        {
            const std::string filterStr = m_hierarchyFilter;
            if (!filterStr.empty() &&
                entity.name.find(filterStr) == std::string::npos &&
                entity.registryName.find(filterStr) == std::string::npos)
            {
                return false;
            }
            if (m_selectedRegistryFilter != "All" && entity.registryName != m_selectedRegistryFilter)
            {
                return false;
            }
            return true;
        }

        void SceneViewerComposite::syncFilterVisibilityToScene() const
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain || !domain->getScene() || !domain->getScene()->isInitialized())
            {
                return;
            }
            auto* scene = domain->getScene();
            for (const std::string& regName : scene->getRegistryNames())
            {
                const bool visible = !m_syncFilterToViewport ||
                                     (m_selectedRegistryFilter == "All" || m_selectedRegistryFilter == regName);
                scene->setRegistryVisible(regName, visible);
            }
        }

        void SceneViewerComposite::syncEntityToDomain(SceneEntityDesc& entity)
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain || !domain->getScene() || !domain->getScene()->isInitialized())
            {
                return;
            }

            auto* scene = domain->getScene();
            if (entity.registryName.empty())
            {
                entity.registryName = "Main";
            }

            FCT::ECS::registry& reg = scene->getOrCreateRegistry(entity.registryName);

            if (entity.ecsEntity == FCT::ECS::null || !reg.valid(entity.ecsEntity))
            {
                entity.ecsEntity = scene->createEntityInRegistry(entity.registryName);
            }

            const auto* engine = Runtime::Engine::getInstance();
            const bool hasEditorTag = !engine || engine->hasApplicationTag("Editor") || engine->hasApplicationTag("CliEditor");
            if (hasEditorTag)
            {
                reg.get_or_emplace<EditorNameComponent>(entity.ecsEntity).name = entity.name;
            }

            if (entity.active)
            {
                reg.remove<DisabledTag>(entity.ecsEntity);
            }
            else
            {
                reg.emplace_or_replace<DisabledTag>(entity.ecsEntity);
            }

            auto isEmptyOrZeroUuid = [](const std::string& idStr) -> bool {
                return idStr.empty() || idStr == "00000000-0000-0000-0000-000000000000";
            };
            const auto* existingUuid = reg.try_get<UuidComponent>(entity.ecsEntity);
            if (isEmptyOrZeroUuid(entity.uuid))
            {
                entity.uuid = (existingUuid && !isEmptyOrZeroUuid(existingUuid->uuid))
                                  ? existingUuid->uuid
                                  : generateEntityUuid();
            }
            if (!isEmptyOrZeroUuid(entity.uuid))
            {
                reg.get_or_emplace<UuidComponent>(entity.ecsEntity).uuid = entity.uuid;
            }

            if (entity.hasTransform)
            {
                auto& localTf = reg.get_or_emplace<Domain::Scene::LocalTransform>(entity.ecsEntity);
                localTf.position = FCT::Vec3<>(entity.position[0], entity.position[1], entity.position[2]);
                localTf.rotation = FCT::Vec3<>(entity.rotation[0], entity.rotation[1], entity.rotation[2]);
                localTf.scale = FCT::Vec3<>(entity.scale[0], entity.scale[1], entity.scale[2]);
                localTf.isDirty = true;

                auto& worldTf = reg.get_or_emplace<Domain::Scene::WorldTransform>(entity.ecsEntity);
                const FCT::Mat4 rotMat = FCT::Mat4::RotateZ(localTf.rotation.z) *
                                         FCT::Mat4::RotateY(localTf.rotation.y) *
                                         FCT::Mat4::RotateX(localTf.rotation.x);
                worldTf.worldMatrix = FCT::Mat4::Translate(localTf.position.x, localTf.position.y, localTf.position.z) *
                                      rotMat *
                                      FCT::Mat4::Scale(localTf.scale.x, localTf.scale.y, localTf.scale.z);
                if (entity.customMesh)
                {
                    worldTf.mesh = entity.customMesh;
                }
                worldTf.isVisible = entity.active;
                scene->getTransformGraph().markDirty(reg, entity.ecsEntity);
            }
            else
            {
                reg.remove<Domain::Scene::LocalTransform>(entity.ecsEntity);
                reg.remove<Domain::Scene::WorldTransform>(entity.ecsEntity);
            }

            if (entity.hasCamera)
            {
                auto& camComp = reg.get_or_emplace<CameraComponent>(entity.ecsEntity);
                camComp.fov = entity.cameraFov;
                camComp.nearPlane = entity.cameraNear;
                camComp.farPlane = entity.cameraFar;
                camComp.orthographic = entity.cameraOrthographic;
                camComp.orthoSize = entity.cameraOrthoSize;
                camComp.active = entity.cameraActive;
                camComp.priority = entity.cameraPriority;
                camComp.targetName = entity.cameraTargetName.empty() ? "MainWindow" : entity.cameraTargetName;
            }

            if (entity.hasDirectionalLight)
            {
                auto& dlComp = reg.get_or_emplace<DirectionalLightComponent>(entity.ecsEntity);
                FCT::Vec3<> dir(entity.lightDirection[0], entity.lightDirection[1], entity.lightDirection[2]);
                if (dir.lengthSquared() > 1e-6f)
                {
                    dir = dir.normalize();
                }
                else
                {
                    dir = FCT::Vec3<>(0.0f, -1.0f, 0.0f);
                }
                dlComp.direction = dir;
                dlComp.color = FCT::Vec3<>(entity.lightColor[0], entity.lightColor[1], entity.lightColor[2]);
                dlComp.intensity = entity.lightIntensity;
            }

            if (entity.hasPointLight)
            {
                auto& plComp = reg.get_or_emplace<PointLightComponent>(entity.ecsEntity);
                plComp.color = FCT::Vec3<>(entity.pointLightColor[0], entity.pointLightColor[1], entity.pointLightColor[2]);
                plComp.intensity = entity.pointLightIntensity;
                plComp.range = entity.pointLightRange;
            }

            if (entity.hasMeshRenderer)
            {
                auto& meshComp = reg.get_or_emplace<StaticMeshComponent>(entity.ecsEntity);
                meshComp.primitiveType = entity.primitiveType;
                meshComp.meshAssetPath = entity.meshName;
                meshComp.customMesh = entity.customMesh;
                meshComp.visible = entity.active;
                meshComp.castShadows = entity.castShadows;

                auto& matComp = reg.get_or_emplace<PbrMaterialComponent>(entity.ecsEntity);
                matComp.baseColor = FCT::Vec3<>(entity.baseColor[0], entity.baseColor[1], entity.baseColor[2]);
                matComp.metallic = entity.metallic;
                matComp.roughness = entity.roughness;
            }

            scene->getTransformGraph().markDirty(reg, entity.ecsEntity);
        }

        void SceneViewerComposite::syncEntitiesFromDomain()
        {
            ZoneScoped();
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain || !domain->getScene() || !domain->getScene()->isInitialized())
            {
                return;
            }

            auto* scene = domain->getScene();
            const std::vector<std::string> regNames = scene->getRegistryNames();

            // 0. 清理所属 Registry 已被卸载或实体已被销毁的无效缓存条目
            m_entities.erase(
                std::remove_if(m_entities.begin(), m_entities.end(), [&](const SceneEntityDesc& ent) {
                    const std::string rName = ent.registryName.empty() ? "Main" : ent.registryName;
                    FCT::ECS::registry* rPtr = scene->getRegistryByName(rName);
                    if (!rPtr)
                    {
                        return true;
                    }
                    return (ent.ecsEntity != FCT::ECS::null && !rPtr->valid(ent.ecsEntity));
                }),
                m_entities.end());

            // 1. 吸收所有 Registry 中新增的外部实体（使用哈希索引消除 O(N^2) 线性扫描）
            std::unordered_map<std::string, FCT::ECS::registry*> regCache;
            regCache.reserve(regNames.size() + 1);
            for (const std::string& rName : regNames)
            {
                regCache[rName] = scene->getRegistryByName(rName);
            }

            auto makeRegEntKey = [](const FCT::ECS::registry* regPtr, FCT::ECS::entity ent) noexcept -> uint64_t {
                const uint64_t regHash = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(regPtr)) * 11400714819323198485ull;
                return regHash ^ (static_cast<uint64_t>(FCT::ECS::to_integral(ent)) + 0x9e3779b97f4a7c15ull);
            };

            std::unordered_map<uint64_t, uint32_t> existingEntityIdMap;
            existingEntityIdMap.reserve(m_entities.size() * 2 + 16);
            for (const auto& existing : m_entities)
            {
                const std::string rName = existing.registryName.empty() ? "Main" : existing.registryName;
                auto regIt = regCache.find(rName);
                const FCT::ECS::registry* rPtr = (regIt != regCache.end()) ? regIt->second : scene->getRegistryByName(rName);
                if (rPtr && existing.ecsEntity != FCT::ECS::null)
                {
                    existingEntityIdMap.emplace(makeRegEntKey(rPtr, existing.ecsEntity), existing.id);
                }
            }

            for (const std::string& regName : regNames)
            {
                if (regName == "Editor")
                {
                    continue;
                }
                FCT::ECS::registry* curReg = regCache[regName];
                if (!curReg)
                {
                    continue;
                }
                auto nameView = curReg->view<EditorNameComponent>();
                for (auto ecsEnt : nameView)
                {
                    const uint64_t key = makeRegEntKey(curReg, ecsEnt);
                    if (existingEntityIdMap.find(key) == existingEntityIdMap.end())
                    {
                        const auto& nc = nameView.get<EditorNameComponent>(ecsEnt);
                        SceneEntityDesc desc{};
                        desc.id = m_nextEntityId++;
                        desc.ecsEntity = ecsEnt;
                        desc.name = nc.name;
                        if (const auto* uc = curReg->try_get<UuidComponent>(ecsEnt))
                        {
                            desc.uuid = uc->uuid;
                        }
                        desc.active = !curReg->all_of<DisabledTag>(ecsEnt);
                        desc.registryName = regName;
                        desc.hasTransform = curReg->all_of<Domain::Scene::LocalTransform>(ecsEnt);
                        existingEntityIdMap.emplace(key, desc.id);
                        m_entities.push_back(std::move(desc));
                    }
                }
            }

            // 2. 将各 Registry 的 ECS 组件状态同步至编辑器描述结构
            for (auto& entity : m_entities)
            {
                const std::string rName = entity.registryName.empty() ? "Main" : entity.registryName;
                auto regIt = regCache.find(rName);
                FCT::ECS::registry* curReg = (regIt != regCache.end() && regIt->second)
                    ? regIt->second
                    : resolveEntityRegistry(entity);
                if (curReg && entity.ecsEntity != FCT::ECS::null && curReg->valid(entity.ecsEntity))
                {
                    if (const auto* nameComp = curReg->try_get<EditorNameComponent>(entity.ecsEntity))
                    {
                        if (!nameComp->name.empty())
                        {
                            entity.name = nameComp->name;
                        }
                    }
                    if (const auto* uuidComp = curReg->try_get<UuidComponent>(entity.ecsEntity))
                    {
                        if (!uuidComp->uuid.empty())
                        {
                            entity.uuid = uuidComp->uuid;
                        }
                    }
                    entity.active = !curReg->all_of<DisabledTag>(entity.ecsEntity);
                    entity.hasTransform = curReg->all_of<Domain::Scene::LocalTransform>(entity.ecsEntity);
                    if (auto* localTf = curReg->try_get<Domain::Scene::LocalTransform>(entity.ecsEntity))
                    {
                        const bool tfChanged =
                            std::abs(entity.position[0] - localTf->position.x) > 1e-5f ||
                            std::abs(entity.position[1] - localTf->position.y) > 1e-5f ||
                            std::abs(entity.position[2] - localTf->position.z) > 1e-5f ||
                            std::abs(entity.rotation[0] - localTf->rotation.x) > 1e-5f ||
                            std::abs(entity.rotation[1] - localTf->rotation.y) > 1e-5f ||
                            std::abs(entity.rotation[2] - localTf->rotation.z) > 1e-5f ||
                            std::abs(entity.scale[0] - localTf->scale.x) > 1e-5f ||
                            std::abs(entity.scale[1] - localTf->scale.y) > 1e-5f ||
                            std::abs(entity.scale[2] - localTf->scale.z) > 1e-5f;

                        entity.position[0] = localTf->position.x;
                        entity.position[1] = localTf->position.y;
                        entity.position[2] = localTf->position.z;
                        entity.rotation[0] = localTf->rotation.x;
                        entity.rotation[1] = localTf->rotation.y;
                        entity.rotation[2] = localTf->rotation.z;
                        entity.scale[0] = localTf->scale.x;
                        entity.scale[1] = localTf->scale.y;
                        entity.scale[2] = localTf->scale.z;
                        if (tfChanged || localTf->isDirty)
                        {
                            localTf->isDirty = true;
                            scene->getTransformGraph().markDirty(*curReg, entity.ecsEntity);
                        }
                    }
                    entity.hasMeshRenderer = curReg->all_of<StaticMeshComponent>(entity.ecsEntity);
                    if (auto* meshComp = curReg->try_get<StaticMeshComponent>(entity.ecsEntity))
                    {
                        if (meshComp->primitiveType >= 0 && meshComp->primitiveType <= 2 &&
                            meshComp->primitiveType != entity.primitiveType)
                        {
                            meshComp->customMesh = nullptr;
                            entity.customMesh = nullptr;
                            if (meshComp->primitiveType == 0) meshComp->meshAssetPath = "Builtin_Cube";
                            else if (meshComp->primitiveType == 1) meshComp->meshAssetPath = "Builtin_Plane";
                            else if (meshComp->primitiveType == 2) meshComp->meshAssetPath = "Builtin_Sphere";
                        }
                        entity.primitiveType = meshComp->primitiveType;
                        entity.castShadows = meshComp->castShadows;
                        if (!meshComp->meshAssetPath.empty())
                        {
                            entity.meshName = meshComp->meshAssetPath;
                        }
                        entity.customMesh = meshComp->customMesh;
                    }
                    if (const auto* matComp = curReg->try_get<PbrMaterialComponent>(entity.ecsEntity))
                    {
                        entity.baseColor[0] = matComp->baseColor.x;
                        entity.baseColor[1] = matComp->baseColor.y;
                        entity.baseColor[2] = matComp->baseColor.z;
                        entity.metallic = matComp->metallic;
                        entity.roughness = matComp->roughness;
                    }
                    entity.hasCamera = curReg->all_of<CameraComponent>(entity.ecsEntity);
                    if (const auto* camComp = curReg->try_get<CameraComponent>(entity.ecsEntity))
                    {
                        entity.cameraActive = camComp->active;
                        entity.cameraPriority = camComp->priority;
                        entity.cameraTargetName = camComp->targetName;
                        entity.cameraFov = camComp->fov;
                        entity.cameraNear = camComp->nearPlane;
                        entity.cameraFar = camComp->farPlane;
                        entity.cameraOrthographic = camComp->orthographic;
                        entity.cameraOrthoSize = camComp->orthoSize;
                    }
                    entity.hasDirectionalLight = curReg->all_of<DirectionalLightComponent>(entity.ecsEntity);
                    if (const auto* dlComp = curReg->try_get<DirectionalLightComponent>(entity.ecsEntity))
                    {
                        entity.lightDirection[0] = dlComp->direction.x;
                        entity.lightDirection[1] = dlComp->direction.y;
                        entity.lightDirection[2] = dlComp->direction.z;
                        entity.lightColor[0] = dlComp->color.x;
                        entity.lightColor[1] = dlComp->color.y;
                        entity.lightColor[2] = dlComp->color.z;
                        entity.lightIntensity = dlComp->intensity;
                    }
                    entity.hasPointLight = curReg->all_of<PointLightComponent>(entity.ecsEntity);
                    if (const auto* plComp = curReg->try_get<PointLightComponent>(entity.ecsEntity))
                    {
                        entity.pointLightColor[0] = plComp->color.x;
                        entity.pointLightColor[1] = plComp->color.y;
                        entity.pointLightColor[2] = plComp->color.z;
                        entity.pointLightIntensity = plComp->intensity;
                        entity.pointLightRange = plComp->range;
                    }
                }
            }

            // 3. 同步各 Registry 内实体的父子层级关系 (HierarchyRelation::parent -> parentId)
            for (auto& entity : m_entities)
            {
                const std::string rName = entity.registryName.empty() ? "Main" : entity.registryName;
                auto regIt = regCache.find(rName);
                FCT::ECS::registry* curReg = (regIt != regCache.end() && regIt->second)
                    ? regIt->second
                    : resolveEntityRegistry(entity);
                if (!curReg || entity.ecsEntity == FCT::ECS::null || !curReg->valid(entity.ecsEntity))
                {
                    continue;
                }
                const auto* rel = curReg->try_get<Domain::Scene::HierarchyRelation>(entity.ecsEntity);
                if (!rel || rel->parent == FCT::ECS::null || !curReg->valid(rel->parent))
                {
                    entity.parentId = 0;
                    continue;
                }
                auto parentIt = existingEntityIdMap.find(makeRegEntKey(curReg, rel->parent));
                const uint32_t resolvedParentId = (parentIt != existingEntityIdMap.end()) ? parentIt->second : 0u;
                entity.parentId = (resolvedParentId != entity.id) ? resolvedParentId : 0u;
            }
        }

        std::string SceneViewerComposite::getActiveViewportTargetName() const noexcept
        {
            switch (m_cameraSource)
            {
            case ViewportCameraSource::ActiveGameMain:
                return "MainWindow";
            case ViewportCameraSource::SpecificEntity:
                return "EditorCameraPreview";
            case ViewportCameraSource::EditorFree:
            default:
                return "EditorViewport";
            }
        }

        void SceneViewerComposite::ensureEditorCameraEntity()
        {
            ZoneScoped();
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain || !domain->getScene() || !domain->getScene()->isInitialized())
            {
                return;
            }

            auto* scene = domain->getScene();
            FCT::ECS::registry& reg = scene->getOrCreateRegistry("Editor");

            if (m_editorCameraEntity == FCT::ECS::null || !reg.valid(m_editorCameraEntity))
            {
                auto nameView = reg.view<EditorNameComponent>();
                for (auto existing : nameView)
                {
                    if (nameView.get<EditorNameComponent>(existing).name == "EditorCamera")
                    {
                        m_editorCameraEntity = existing;
                        break;
                    }
                }
                if (m_editorCameraEntity == FCT::ECS::null || !reg.valid(m_editorCameraEntity))
                {
                    m_editorCameraEntity = scene->createEntityInRegistry("Editor", "EditorCamera");
                }
            }

            reg.get_or_emplace<EditorNameComponent>(m_editorCameraEntity).name = "EditorCamera";
            reg.remove<DisabledTag>(m_editorCameraEntity);

            if (m_cameraSource == ViewportCameraSource::SpecificEntity)
            {
                FCT::ECS::registry* srcReg = scene->getRegistryByName(m_specificCameraRegistry);
                const CameraComponent* srcCam =
                    (srcReg && m_specificCameraEcsEntity != FCT::ECS::null && srcReg->valid(m_specificCameraEcsEntity))
                        ? srcReg->try_get<CameraComponent>(m_specificCameraEcsEntity)
                        : nullptr;
                if (!srcCam)
                {
                    m_cameraSource = isPiePlaying()
                        ? ViewportCameraSource::ActiveGameMain
                        : ViewportCameraSource::EditorFree;
                    m_useGameCameraPreview = (m_cameraSource != ViewportCameraSource::EditorFree);
                }
                else
                {
                    auto& localTf = reg.get_or_emplace<Domain::Scene::LocalTransform>(m_editorCameraEntity);
                    auto& worldTf = reg.get_or_emplace<Domain::Scene::WorldTransform>(m_editorCameraEntity);
                    if (const auto* srcLocal = srcReg->try_get<Domain::Scene::LocalTransform>(m_specificCameraEcsEntity))
                    {
                        localTf = *srcLocal;
                        localTf.isDirty = true;
                    }
                    if (const auto* srcWorld = srcReg->try_get<Domain::Scene::WorldTransform>(m_specificCameraEcsEntity))
                    {
                        worldTf = *srcWorld;
                        worldTf.isVisible = true;
                    }
                    else
                    {
                        const FCT::Mat4 rotMat = FCT::Mat4::RotateZ(localTf.rotation.z) *
                                                 FCT::Mat4::RotateY(localTf.rotation.y) *
                                                 FCT::Mat4::RotateX(localTf.rotation.x);
                        worldTf.worldMatrix =
                            FCT::Mat4::Translate(localTf.position.x, localTf.position.y, localTf.position.z) * rotMat;
                        worldTf.isVisible = true;
                    }
                    scene->getTransformGraph().markDirty(reg, m_editorCameraEntity);

                    auto& camComp = reg.get_or_emplace<CameraComponent>(m_editorCameraEntity);
                    camComp = *srcCam;
                    camComp.active = true;
                    camComp.priority = 100;
                    camComp.targetName = "EditorCameraPreview";
                    return;
                }
            }

            const FCT::Vec3<> eye(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]);
            const FCT::Vec3<> target(m_cameraTarget[0], m_cameraTarget[1], m_cameraTarget[2]);
            FCT::Vec3<> forward = target - eye;
            if (forward.lengthSquared() < 1e-6f)
            {
                forward = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
            }
            else
            {
                forward = forward.normalize();
            }

            constexpr float radToDeg = 180.0f / 3.14159265358979323846f;
            const float pitchDeg = std::asin(std::clamp(-forward.y, -1.0f, 1.0f)) * radToDeg;
            const float yawDeg = std::atan2(forward.x, forward.z) * radToDeg;

            auto& localTf = reg.get_or_emplace<Domain::Scene::LocalTransform>(m_editorCameraEntity);
            localTf.position = eye;
            localTf.rotation = FCT::Vec3<>(pitchDeg, yawDeg, 0.0f);
            localTf.scale = FCT::Vec3<>(1.0f, 1.0f, 1.0f);
            localTf.isDirty = true;

            auto& worldTf = reg.get_or_emplace<Domain::Scene::WorldTransform>(m_editorCameraEntity);
            const FCT::Mat4 rotMat = FCT::Mat4::RotateZ(localTf.rotation.z) *
                                     FCT::Mat4::RotateY(localTf.rotation.y) *
                                     FCT::Mat4::RotateX(localTf.rotation.x);
            worldTf.worldMatrix = FCT::Mat4::Translate(localTf.position.x, localTf.position.y, localTf.position.z) * rotMat;
            worldTf.isVisible = true;
            scene->getTransformGraph().markDirty(reg, m_editorCameraEntity);

            auto& camComp = reg.get_or_emplace<CameraComponent>(m_editorCameraEntity);
            camComp.fov = m_cameraFov;
            camComp.nearPlane = m_editorCameraOrthographic
                ? -std::max(m_cameraFar * 0.5f, 250.0f)
                : m_cameraNear;
            camComp.farPlane = m_cameraFar;
            camComp.active = isEditorFreeCameraActive();
            camComp.orthographic = m_editorCameraOrthographic;
            camComp.orthoSize = m_editorCameraOrthoSize;
            camComp.priority = 100;
            camComp.targetName = "EditorViewport";
        }

        void SceneViewerComposite::syncCameraToRenderer()
        {
            ZoneScoped();
            ensureEditorCameraEntity();

            auto* domain = Domain::Domain::InstancePtr();
            if (!domain || !domain->getRenderer() || !domain->getRenderer()->isInitialized())
            {
                return;
            }

            auto* renderer = domain->getRenderer();
            const std::string targetName = getActiveViewportTargetName();
            renderer->clearCameraOverride();
            renderer->bindRenderTarget(
                targetName,
                nullptr,
                nullptr,
                std::max(m_lastViewportWidth, 1u),
                std::max(m_lastViewportHeight, 1u),
                m_lastViewportOffsetX,
                m_lastViewportOffsetY);
            renderer->setPrimaryPresentTargetName(targetName);
            renderer->setAntiAliasingEnabled(m_enableAntiAliasing);
            renderer->refreshActiveCameras();
        }

        bool SceneViewerComposite::isPiePlaying() const noexcept
        {
            if (m_wasPiePlaying)
            {
                return true;
            }
            if (auto* engine = Runtime::Engine::getInstance())
            {
                return engine->hasApplicationTag("Game");
            }
            return false;
        }

        void SceneViewerComposite::updatePieSimulation(float deltaTime)
        {
            if (!isPiePlaying())
            {
                return;
            }

            m_pieTimeAccum += deltaTime;
        }

        bool SceneViewerComposite::isEditorFreeCameraActive() const noexcept
        {
            return m_cameraSource == ViewportCameraSource::EditorFree;
        }

        bool SceneViewerComposite::isGameCameraPreviewActive() const noexcept
        {
            return m_cameraSource != ViewportCameraSource::EditorFree;
        }

        void SceneViewerComposite::setViewportCameraSource(ViewportCameraSource source) noexcept
        {
            if (m_cameraSource == source)
            {
                return;
            }
            m_cameraSource = source;
            if (!isPiePlaying())
            {
                m_useGameCameraPreview = (source != ViewportCameraSource::EditorFree);
                saveEditorSession();
            }
            syncCameraToRenderer();
        }

        void SceneViewerComposite::setSpecificCameraEntity(
            const std::string& registryName,
            FCT::ECS::entity entity) noexcept
        {
            m_specificCameraRegistry = registryName.empty() ? "Main" : registryName;
            m_specificCameraEcsEntity = entity;
            m_cameraSource = ViewportCameraSource::SpecificEntity;
            if (!isPiePlaying())
            {
                m_useGameCameraPreview = true;
            }
            syncCameraToRenderer();
        }

        void SceneViewerComposite::toggleEditorAndGameCamera() noexcept
        {
            if (m_cameraSource == ViewportCameraSource::EditorFree)
            {
                setViewportCameraSource(ViewportCameraSource::ActiveGameMain);
            }
            else
            {
                setViewportCameraSource(ViewportCameraSource::EditorFree);
            }
        }

        void SceneViewerComposite::alignEditorCameraToCurrentView() noexcept
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (domain && domain->getRenderer() && domain->getRenderer()->isInitialized())
            {
                if (const auto activeCam = domain->getRenderer()->findActiveCameraForTarget(getActiveViewportTargetName()))
                {
                    const FCT::Mat4 invView = activeCam->viewMatrix.inverse();
                    const FCT::Vec4 fwd4 = invView * FCT::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
                    FCT::Vec3<> fwd(fwd4.x, fwd4.y, fwd4.z);
                    if (fwd.lengthSquared() < 1e-6f)
                    {
                        fwd = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
                    }
                    else
                    {
                        fwd = fwd.normalize();
                    }

                    const float focusDist = 18.0f;
                    m_cameraPos[0] = activeCam->worldPosition.x;
                    m_cameraPos[1] = activeCam->worldPosition.y;
                    m_cameraPos[2] = activeCam->worldPosition.z;
                    m_cameraTarget[0] = activeCam->worldPosition.x + fwd.x * focusDist;
                    m_cameraTarget[1] = activeCam->worldPosition.y + fwd.y * focusDist;
                    m_cameraTarget[2] = activeCam->worldPosition.z + fwd.z * focusDist;

                    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
                    m_cameraRot[0] = std::asin(std::clamp(-fwd.y, -1.0f, 1.0f)) * kRadToDeg;
                    m_cameraRot[1] = std::atan2(fwd.x, fwd.z) * kRadToDeg;
                    m_cameraRot[2] = 0.0f;

                    m_editorCameraOrthographic = activeCam->orthographic;
                    if (activeCam->orthographic)
                    {
                        m_editorCameraOrthoSize = std::max(activeCam->orthoSize, 0.5f);
                    }
                    else
                    {
                        m_cameraFov = std::clamp(activeCam->fov, 15.0f, 140.0f);
                    }
                    m_cameraNear = std::max(activeCam->nearPlane, 0.01f);
                    m_cameraFar = std::max(activeCam->farPlane, 10.0f);
                }
            }
            setViewportCameraSource(ViewportCameraSource::EditorFree);
        }

        void SceneViewerComposite::setGameCameraPreviewActive(bool active) noexcept
        {
            setViewportCameraSource(active ? ViewportCameraSource::ActiveGameMain : ViewportCameraSource::EditorFree);
        }

        FCT::Mat4 SceneViewerComposite::getActiveViewMatrix() const
        {
            if (auto* domain = Domain::Domain::InstancePtr();
                domain && domain->getRenderer() && domain->getRenderer()->isInitialized())
            {
                if (const auto activeCam = domain->getRenderer()->findActiveCameraForTarget(getActiveViewportTargetName()))
                {
                    return activeCam->viewMatrix;
                }
            }
            return computeEditorViewMatrix();
        }

        FCT::Mat4 SceneViewerComposite::getActiveProjectionMatrix() const
        {
            return computeProjMatrix(m_lastViewportAspect);
        }

        SceneEntityDesc* SceneViewerComposite::getSelectedEntity()
        {
            return findEntityById(m_selectedEntityId);
        }

        SceneEntityDesc* SceneViewerComposite::findEntityById(uint32_t id)
        {
            for (auto& entity : m_entities)
            {
                if (entity.id == id)
                {
                    return &entity;
                }
            }
            return nullptr;
        }

        void SceneViewerComposite::createNewEntity(const std::string& name, uint32_t parentId)
        {
            ZoneScoped();
            SceneEntityDesc entity;
            entity.id = m_nextEntityId++;
            entity.parentId = parentId;
            entity.registryName = m_activeTargetRegistry.empty() ? "Main" : m_activeTargetRegistry;
            if (parentId != 0)
            {
                if (const auto* parentEnt = findEntityById(parentId))
                {
                    entity.registryName = parentEnt->registryName;
                }
            }
            entity.name = makeUniqueEntityName(name, entity.registryName);
            entity.uuid = generateEntityUuid();
            entity.hasTransform = true;
            syncEntityToDomain(entity);
            m_entities.push_back(entity);
            if (parentId != 0)
            {
                setEntityParent(entity.id, parentId);
            }
            m_selectedEntityId = entity.id;

            if (m_editorUiComposite)
            {
                EditorBase::SelectEntitySignal sig;
                sig.entityId = entity.id;
                sig.entityName = entity.name;
                m_editorUiComposite->getUiContext().emit(std::move(sig));
            }
        }

        void SceneViewerComposite::deleteEntity(uint32_t id)
        {
            auto* domain = Domain::Domain::InstancePtr();
            for (auto& e : m_entities)
            {
                if (e.id == id && e.ecsEntity != FCT::ECS::null && domain && domain->getScene() && domain->getScene()->isInitialized())
                {
                    domain->getScene()->destroyEntityInRegistry(
                        e.registryName.empty() ? "Main" : e.registryName,
                        e.ecsEntity);
                    break;
                }
            }

            // 解除所有直接子节点的父引用
            for (auto& e : m_entities)
            {
                if (e.parentId == id)
                {
                    e.parentId = 0;
                }
            }

            auto it = std::remove_if(m_entities.begin(), m_entities.end(),
                                     [id](const SceneEntityDesc& e) { return e.id == id; });
            m_entities.erase(it, m_entities.end());

            if (m_selectedEntityId == id)
            {
                m_selectedEntityId = m_entities.empty() ? 0 : m_entities.front().id;
            }
        }

        void SceneViewerComposite::setEntityParent(uint32_t childId, uint32_t newParentId)
        {
            if (childId == newParentId) return;
            auto* child = findEntityById(childId);
            if (!child) return;

            child->parentId = newParentId;

            auto* domain = Domain::Domain::InstancePtr();
            if (!domain || !domain->getScene() || !domain->getScene()->isInitialized())
            {
                return;
            }

            auto* scene = domain->getScene();
            if (child->ecsEntity == FCT::ECS::null) return;

            FCT::ECS::registry* childReg = resolveEntityRegistry(*child);
            if (!childReg) return;

            if (newParentId == 0)
            {
                child->parentId = 0;
                scene->getTransformGraph().detachChild(*childReg, child->ecsEntity);
            }
            else if (auto* parent = findEntityById(newParentId); parent && parent->ecsEntity != FCT::ECS::null)
            {
                if (parent->registryName != child->registryName)
                {
                    child->parentId = 0;
                    return;
                }
                scene->getTransformGraph().attachChild(*childReg, parent->ecsEntity, child->ecsEntity);
            }
        }

        namespace
        {
            struct EntityPresetDef
            {
                EntityPresetKind kind;
                const char* defaultName;
                bool hasTransform = true;
                float position[3] = {0.0f, 0.0f, 0.0f};
                float rotation[3] = {0.0f, 0.0f, 0.0f};
                float scale[3] = {1.0f, 1.0f, 1.0f};
                bool hasMeshRenderer = false;
                int primitiveType = 0;
                bool castShadows = true;
                bool hasDirectionalLight = false;
                bool hasPointLight = false;
                bool hasCamera = false;
                bool cameraOrthographic = false;
                float cameraOrthoSize = 10.0f;
                bool hasRigidBody = false;
                int bodyType = 0;
                int colliderType = 0;
                bool isTrigger = false;
                bool hasNativeScript = false;
            };

            const EntityPresetDef* findEntityPresetDef(EntityPresetKind kind)
            {
                static constexpr EntityPresetDef kPresets[] = {
                    {EntityPresetKind::EmptyNode, "EmptyNode", true},
                    {EntityPresetKind::LogicEntity, "LogicEntity", false},
                    {EntityPresetKind::NativeScriptEntity, "NativeScriptActor", true, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false, 0, false, false, false, false, false, 10.0f, false, 0, 0, false, true},
                    {EntityPresetKind::Cube, "Cube", true, {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, true, 0, true},
                    {EntityPresetKind::Plane, "Plane", true, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {10.0f, 1.0f, 10.0f}, true, 1, false},
                    {EntityPresetKind::Sphere, "Sphere", true, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, true, 2, true},
                    {EntityPresetKind::StaticMesh, "StaticMesh", true, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, true, 0, true},
                    {EntityPresetKind::DirectionalLight, "DirectionalLight", true, {0.0f, 5.0f, 0.0f}, {50.0f, -30.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false, 0, true, true},
                    {EntityPresetKind::PointLight, "PointLight", true, {0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false, 0, true, false, true},
                    {EntityPresetKind::PerspectiveCamera, "Camera", true, {0.0f, 2.0f, -6.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false, 0, true, false, false, true, false, 10.0f},
                    {EntityPresetKind::OrthographicCamera, "OrthoCamera", true, {0.0f, 10.0f, -10.0f}, {35.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false, 0, true, false, false, true, true, 10.0f},
                    {EntityPresetKind::StaticBoxCollider, "StaticBoxCollider", true, {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false, 0, true, false, false, false, false, 10.0f, true, 0, 0, false},
                    {EntityPresetKind::DynamicBox, "DynamicRigidBox", true, {0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, true, 0, true, false, false, false, false, 10.0f, true, 1, 0, false},
                    {EntityPresetKind::DynamicSphere, "DynamicRigidSphere", true, {0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, true, 2, true, false, false, false, false, 10.0f, true, 1, 2, false},
                    {EntityPresetKind::TriggerVolume, "TriggerVolume", true, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false, 0, false, false, false, false, false, 10.0f, true, 0, 0, true}
                };
                for (const auto& item : kPresets)
                {
                    if (item.kind == kind)
                    {
                        return &item;
                    }
                }
                return nullptr;
            }

            std::string stripDuplicateSuffix(const std::string& rawName, bool* outStrippedCopySuffix = nullptr)
            {
                if (outStrippedCopySuffix)
                {
                    *outStrippedCopySuffix = false;
                }
                std::string stem = rawName.empty() ? std::string("Entity") : rawName;
                for (;;)
                {
                    constexpr const char* kCopySuffix = "_Copy";
                    constexpr size_t kCopyLen = 5;
                    if (stem.size() > kCopyLen &&
                        stem.compare(stem.size() - kCopyLen, kCopyLen, kCopySuffix) == 0)
                    {
                        stem.resize(stem.size() - kCopyLen);
                        if (outStrippedCopySuffix)
                        {
                            *outStrippedCopySuffix = true;
                        }
                        continue;
                    }
                    const size_t underscorePos = stem.rfind('_');
                    if (underscorePos != std::string::npos &&
                        underscorePos > 0 &&
                        underscorePos + 1 < stem.size())
                    {
                        bool allDigits = true;
                        for (size_t i = underscorePos + 1; i < stem.size(); ++i)
                        {
                            if (!std::isdigit(static_cast<unsigned char>(stem[i])))
                            {
                                allDigits = false;
                                break;
                            }
                        }
                        if (allDigits)
                        {
                            stem.resize(underscorePos);
                            continue;
                        }
                    }
                    break;
                }
                return stem.empty() ? std::string("Entity") : stem;
            }

            std::string resolveUniqueNameAgainstSet(
                const std::string& baseName,
                std::unordered_set<std::string>& usedNames)
            {
                const std::string normalizedInput = baseName.empty() ? std::string("Entity") : baseName;
                bool hadCopySuffix = false;
                const std::string stem = stripDuplicateSuffix(normalizedInput, &hadCopySuffix);

                // 旧数据残留 _Copy 后缀或输入名已被占用时，统一从干净词根递增查找可用编号
                const std::string firstCandidate = hadCopySuffix ? stem : normalizedInput;
                if (usedNames.find(firstCandidate) == usedNames.end())
                {
                    usedNames.insert(firstCandidate);
                    return firstCandidate;
                }

                uint32_t suffixIndex = 1;
                for (;;)
                {
                    std::string candidate = stem + "_" + std::to_string(suffixIndex);
                    if (usedNames.find(candidate) == usedNames.end())
                    {
                        usedNames.insert(candidate);
                        return candidate;
                    }
                    ++suffixIndex;
                }
            }
        } // namespace

        /**
         * @brief 实体子树实例化可撤销命令（接入 CommandStack 支持 Ctrl+Z / Ctrl+Y）
         */
        class SubtreeInstantiateCommand : public EditorBase::IUndoableCommand
        {
        public:
            SubtreeInstantiateCommand(
                SceneViewerComposite* composite,
                std::vector<EntityClipboardNode> resolvedNodes,
                std::string targetRegistry,
                std::string commandName)
                : m_composite(composite)
                , m_resolvedNodes(std::move(resolvedNodes))
                , m_targetRegistry(std::move(targetRegistry))
                , m_commandName(std::move(commandName))
            {
            }

            void execute() override
            {
                if (!m_composite)
                {
                    return;
                }
                m_createdIds = m_composite->commitSubtreeInstantiationDirect(m_resolvedNodes, m_targetRegistry);
                for (size_t i = 0; i < m_createdIds.size() && i < m_resolvedNodes.size(); ++i)
                {
                    m_resolvedNodes[i].desc.id = m_createdIds[i];
                }
            }

            void undo() override
            {
                if (!m_composite || m_createdIds.empty())
                {
                    return;
                }
                m_composite->removeSubtreeEntitiesDirect(m_createdIds);
                m_createdIds.clear();
            }

            std::string getName() const override
            {
                return m_commandName;
            }

        private:
            SceneViewerComposite* m_composite = nullptr;
            std::vector<EntityClipboardNode> m_resolvedNodes;
            std::string m_targetRegistry;
            std::string m_commandName;
            std::vector<uint32_t> m_createdIds;
        };

        std::string SceneViewerComposite::generateEntityUuid() const
        {
            static std::atomic<uint64_t> s_uuidCounter{1};
            thread_local std::mt19937_64 s_rng([]() {
                std::random_device rd;
                return (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
            }());
            const uint64_t seq = s_uuidCounter.fetch_add(1, std::memory_order_relaxed);
            const uint64_t hi = s_rng() ^ (seq * 0x9e3779b97f4a7c15ULL);
            const uint64_t lo = s_rng() ^ (seq << 17);
            const uint32_t p1 = static_cast<uint32_t>(hi >> 32);
            const uint16_t p2 = static_cast<uint16_t>((hi >> 16) & 0xFFFFU);
            const uint16_t p3 = static_cast<uint16_t>((hi & 0x0FFFU) | 0x4000U);
            const uint16_t p4 = static_cast<uint16_t>(((lo >> 48) & 0x3FFFU) | 0x8000U);
            const uint64_t p5 = lo & 0x0000FFFFFFFFFFFFULL;
            char buf[40] = {};
            std::snprintf(
                buf,
                sizeof(buf),
                "%08x-%04x-%04x-%04x-%012llx",
                p1,
                p2,
                p3,
                p4,
                static_cast<unsigned long long>(p5));
            return std::string(buf);
        }

        std::string SceneViewerComposite::makeUniqueEntityName(
            const std::string& baseName,
            const std::string& registryName) const
        {
            ZoneScoped();
            const std::string targetReg = registryName.empty() ? "Main" : registryName;
            std::unordered_set<std::string> usedNames;
            usedNames.reserve(m_entities.size() + 8);
            for (const auto& entity : m_entities)
            {
                const std::string entReg = entity.registryName.empty() ? "Main" : entity.registryName;
                if (entReg == targetReg && !entity.name.empty())
                {
                    usedNames.insert(entity.name);
                }
            }

            if (auto* domain = Domain::Domain::InstancePtr();
                domain && domain->getScene() && domain->getScene()->isInitialized())
            {
                if (FCT::ECS::registry* reg = domain->getScene()->getRegistryByName(targetReg))
                {
                    auto nameView = reg->view<EditorNameComponent>();
                    for (auto ecsEnt : nameView)
                    {
                        const auto& nc = nameView.get<EditorNameComponent>(ecsEnt);
                        if (!nc.name.empty())
                        {
                            usedNames.insert(nc.name);
                        }
                    }
                }
            }
            return resolveUniqueNameAgainstSet(baseName, usedNames);
        }

        SceneEntityDesc* SceneViewerComposite::createEntityFromPreset(EntityPresetKind preset, uint32_t parentId)
        {
            ZoneScoped();
            const EntityPresetDef* def = findEntityPresetDef(preset);
            if (!def)
            {
                return nullptr;
            }

            std::string targetRegistry = m_activeTargetRegistry.empty() ? "Main" : m_activeTargetRegistry;
            if (parentId != 0)
            {
                if (const auto* parentEnt = findEntityById(parentId))
                {
                    targetRegistry = parentEnt->registryName.empty() ? "Main" : parentEnt->registryName;
                }
                else
                {
                    parentId = 0;
                }
            }

            EntityClipboardNode node{};
            node.parentIndex = -1;
            node.desc.name = def->defaultName;
            node.desc.registryName = targetRegistry;
            node.desc.parentId = parentId;
            node.desc.hasTransform = def->hasTransform;
            for (int i = 0; i < 3; ++i)
            {
                node.desc.position[i] = def->position[i];
                node.desc.rotation[i] = def->rotation[i];
                node.desc.scale[i] = def->scale[i];
            }
            node.desc.hasMeshRenderer = def->hasMeshRenderer;
            node.desc.primitiveType = def->primitiveType;
            node.desc.castShadows = def->castShadows;
            if (def->hasMeshRenderer)
            {
                if (def->primitiveType == 0)
                {
                    node.desc.meshName = (preset == EntityPresetKind::StaticMesh) ? "" : "Builtin_Cube";
                }
                else if (def->primitiveType == 1)
                {
                    node.desc.meshName = "Builtin_Plane";
                }
                else if (def->primitiveType == 2)
                {
                    node.desc.meshName = "Builtin_Sphere";
                }
            }
            node.desc.hasDirectionalLight = def->hasDirectionalLight;
            if (def->hasDirectionalLight)
            {
                node.desc.lightDirection[0] = 0.4f;
                node.desc.lightDirection[1] = -0.8f;
                node.desc.lightDirection[2] = 0.4f;
                node.desc.lightIntensity = 3.0f;
            }
            node.desc.hasPointLight = def->hasPointLight;
            if (def->hasPointLight)
            {
                node.desc.pointLightIntensity = 5.0f;
                node.desc.pointLightRange = 10.0f;
            }
            node.desc.hasCamera = def->hasCamera;
            if (def->hasCamera)
            {
                node.desc.cameraActive = true;
                node.desc.cameraPriority = 10;
                node.desc.cameraTargetName = "MainWindow";
                node.desc.cameraOrthographic = def->cameraOrthographic;
                node.desc.cameraOrthoSize = def->cameraOrthoSize;
            }

            if (def->hasRigidBody)
            {
                RigidBodyComponent rb{};
                rb.bodyType = def->bodyType;
                rb.colliderType = def->colliderType;
                rb.colliderHalfExtents = FCT::Vec3<>(0.5f, 0.5f, 0.5f);
                rb.mass = (def->bodyType == 1) ? 1.0f : 0.0f;
                rb.useGravity = (def->bodyType == 1);
                rb.isTrigger = def->isTrigger;

                if (auto* schemaTable = Resource::SchemaTable::InstancePtr())
                {
                    if (const Resource::TypeDescriptor* rbDesc = schemaTable->getSchema("RigidBodyComponent"))
                    {
                        node.dynamicComponents.emplace_back(rbDesc, &rb);
                    }
                }
            }

            if (def->hasNativeScript)
            {
                NativeScriptComponent ns{};
                NativeScriptSlot slot{};
                slot.enabled = true;
                if (auto* dom = Domain::Domain::InstancePtr())
                {
                    const auto regNames = dom->gameplay.getRegisteredNativeScriptNames();
                    if (!regNames.empty())
                    {
                        slot.className = regNames.front();
                        if (const auto* sDesc = dom->gameplay.getNativeScriptDescriptor(slot.className))
                        {
                            for (const auto& propMeta : sDesc->properties)
                            {
                                Domain::Scene::UpsertNativeScriptProperty(slot, propMeta.name, propMeta.defaultValue);
                            }
                        }
                    }
                }
                ns.scripts.push_back(std::move(slot));
                Domain::Scene::NormalizeNativeScriptSlots(ns);

                if (auto* schemaTable = Resource::SchemaTable::InstancePtr())
                {
                    if (const Resource::TypeDescriptor* nsDesc = schemaTable->getSchema("NativeScriptComponent"))
                    {
                        node.dynamicComponents.emplace_back(nsDesc, &ns);
                    }
                }
            }

            std::vector<EntityClipboardNode> nodes;
            nodes.push_back(std::move(node));
            const std::vector<uint32_t> createdIds = instantiateSubtreeSnapshot(
                nodes,
                parentId,
                targetRegistry,
                true,
                std::string("Create ") + def->defaultName);
            if (createdIds.empty())
            {
                return nullptr;
            }

            SceneEntityDesc* createdEnt = findEntityById(createdIds.front());
            if (createdEnt && def->hasRigidBody && nodes[0].dynamicComponents.empty())
            {
                if (FCT::ECS::registry* reg = resolveEntityRegistry(*createdEnt);
                    reg && reg->valid(createdEnt->ecsEntity))
                {
                    RigidBodyComponent rb{};
                    rb.bodyType = def->bodyType;
                    rb.colliderType = def->colliderType;
                    rb.colliderHalfExtents = FCT::Vec3<>(0.5f, 0.5f, 0.5f);
                    rb.mass = (def->bodyType == 1) ? 1.0f : 0.0f;
                    rb.useGravity = (def->bodyType == 1);
                    rb.isTrigger = def->isTrigger;
                    reg->emplace_or_replace<RigidBodyComponent>(createdEnt->ecsEntity, rb);
                }
            }
            return createdEnt;
        }

        void SceneViewerComposite::collectSubtreeSnapshot(
            uint32_t rootEntityId,
            std::vector<EntityClipboardNode>& outNodes) const
        {
            ZoneScoped();
            outNodes.clear();
            if (rootEntityId == 0)
            {
                return;
            }

            auto collectRecursive = [&](auto& self, uint32_t entId, int32_t parentSnapIdx) -> void {
                const SceneEntityDesc* src = nullptr;
                for (const auto& candidate : m_entities)
                {
                    if (candidate.id == entId)
                    {
                        src = &candidate;
                        break;
                    }
                }
                if (!src)
                {
                    return;
                }

                EntityClipboardNode node{};
                node.desc = *src;
                node.desc.id = 0;
                node.desc.ecsEntity = FCT::ECS::null;
                node.desc.uuid.clear();
                node.parentIndex = parentSnapIdx;

                if (FCT::ECS::registry* reg = resolveEntityRegistry(*src);
                    reg && src->ecsEntity != FCT::ECS::null && reg->valid(src->ecsEntity))
                {
                    if (const auto* nameComp = reg->try_get<EditorNameComponent>(src->ecsEntity))
                    {
                        if (!nameComp->name.empty()) node.desc.name = nameComp->name;
                    }
                    node.desc.active = !reg->all_of<DisabledTag>(src->ecsEntity);
                    if (const auto* localTf = reg->try_get<Domain::Scene::LocalTransform>(src->ecsEntity))
                    {
                        node.desc.hasTransform = true;
                        node.desc.position[0] = localTf->position.x;
                        node.desc.position[1] = localTf->position.y;
                        node.desc.position[2] = localTf->position.z;
                        node.desc.rotation[0] = localTf->rotation.x;
                        node.desc.rotation[1] = localTf->rotation.y;
                        node.desc.rotation[2] = localTf->rotation.z;
                        node.desc.scale[0] = localTf->scale.x;
                        node.desc.scale[1] = localTf->scale.y;
                        node.desc.scale[2] = localTf->scale.z;
                    }
                    else
                    {
                        node.desc.hasTransform = false;
                    }

                    if (auto* schemaTable = Resource::SchemaTable::InstancePtr())
                    {
                        for (const Resource::TypeDescriptor* desc : schemaTable->getAllLatestSchemas())
                        {
                            if (!desc || !desc->ecsHas || !desc->ecsGetConst || !desc->ecsEmplaceOrReplace)
                            {
                                continue;
                            }
                            if (desc->typeName == "EditorNameComponent" ||
                                desc->typeName == "UuidComponent" ||
                                desc->typeName == "DisabledTag" ||
                                desc->typeName == "LocalTransform" ||
                                desc->typeName == "WorldTransform" ||
                                desc->typeName == "HierarchyRelation")
                            {
                                continue;
                            }
                            if (desc->ecsHas(*reg, src->ecsEntity))
                            {
                                if (const void* compPtr = desc->ecsGetConst(*reg, src->ecsEntity))
                                {
                                    node.dynamicComponents.emplace_back(desc, compPtr);
                                }
                            }
                        }
                    }
                }

                const int32_t currentIdx = static_cast<int32_t>(outNodes.size());
                outNodes.push_back(std::move(node));

                for (const auto& child : m_entities)
                {
                    if (child.parentId == entId)
                    {
                        self(self, child.id, currentIdx);
                    }
                }
            };

            collectRecursive(collectRecursive, rootEntityId, -1);
        }

        std::vector<uint32_t> SceneViewerComposite::instantiateSubtreeSnapshot(
            const std::vector<EntityClipboardNode>& nodes,
            uint32_t targetParentId,
            const std::string& targetRegistry,
            bool pushUndoCommand,
            const std::string& commandLabel)
        {
            ZoneScoped();
            if (nodes.empty())
            {
                return {};
            }

            const std::string resolvedReg = targetRegistry.empty() ? "Main" : targetRegistry;
            std::unordered_set<std::string> usedNames;
            usedNames.reserve(m_entities.size() + nodes.size() + 8);
            for (const auto& existing : m_entities)
            {
                const std::string entReg = existing.registryName.empty() ? "Main" : existing.registryName;
                if (entReg == resolvedReg && !existing.name.empty())
                {
                    usedNames.insert(existing.name);
                }
            }
            if (auto* domain = Domain::Domain::InstancePtr();
                domain && domain->getScene() && domain->getScene()->isInitialized())
            {
                if (FCT::ECS::registry* reg = domain->getScene()->getRegistryByName(resolvedReg))
                {
                    auto nameView = reg->view<EditorNameComponent>();
                    for (auto ecsEnt : nameView)
                    {
                        const auto& nc = nameView.get<EditorNameComponent>(ecsEnt);
                        if (!nc.name.empty())
                        {
                            usedNames.insert(nc.name);
                        }
                    }
                }
            }

            std::vector<EntityClipboardNode> resolvedNodes = nodes;
            for (size_t i = 0; i < resolvedNodes.size(); ++i)
            {
                resolvedNodes[i].desc.id = m_nextEntityId++;
                resolvedNodes[i].desc.ecsEntity = FCT::ECS::null;
                resolvedNodes[i].desc.registryName = resolvedReg;
                resolvedNodes[i].desc.name = resolveUniqueNameAgainstSet(resolvedNodes[i].desc.name, usedNames);
                resolvedNodes[i].desc.uuid = generateEntityUuid();
                if (resolvedNodes[i].parentIndex < 0)
                {
                    resolvedNodes[i].desc.parentId = targetParentId;
                }
            }

            if (pushUndoCommand && m_editorUiComposite)
            {
                auto cmd = makeUnique<SubtreeInstantiateCommand>(
                    this,
                    resolvedNodes,
                    resolvedReg,
                    commandLabel);
                m_editorUiComposite->getCommandStack().executeCommand(std::move(cmd));
                std::vector<uint32_t> createdIds;
                createdIds.reserve(resolvedNodes.size());
                // 匹配最近创建的根节点与其子孙（execute 后 m_selectedEntityId 指向根节点）
                for (const auto& rNode : resolvedNodes)
                {
                    for (const auto& ent : m_entities)
                    {
                        if (ent.uuid == rNode.desc.uuid)
                        {
                            createdIds.push_back(ent.id);
                            break;
                        }
                    }
                }
                return createdIds;
            }

            return commitSubtreeInstantiationDirect(resolvedNodes, resolvedReg);
        }

        std::vector<uint32_t> SceneViewerComposite::commitSubtreeInstantiationDirect(
            const std::vector<EntityClipboardNode>& resolvedNodes,
            const std::string& targetRegistry)
        {
            ZoneScoped();
            std::vector<uint32_t> createdIds;
            createdIds.reserve(resolvedNodes.size());

            const std::string resolvedReg = targetRegistry.empty() ? "Main" : targetRegistry;
            for (size_t i = 0; i < resolvedNodes.size(); ++i)
            {
                SceneEntityDesc newEnt = resolvedNodes[i].desc;
                if (newEnt.id == 0 || findEntityById(newEnt.id) != nullptr)
                {
                    newEnt.id = m_nextEntityId++;
                }
                else if (newEnt.id >= m_nextEntityId)
                {
                    m_nextEntityId = newEnt.id + 1;
                }

                newEnt.ecsEntity = FCT::ECS::null;
                newEnt.registryName = resolvedReg;
                if (resolvedNodes[i].parentIndex >= 0 &&
                    static_cast<size_t>(resolvedNodes[i].parentIndex) < createdIds.size())
                {
                    newEnt.parentId = createdIds[static_cast<size_t>(resolvedNodes[i].parentIndex)];
                }
                else if (newEnt.parentId != 0 && findEntityById(newEnt.parentId) == nullptr)
                {
                    newEnt.parentId = 0;
                }

                if (newEnt.uuid.empty())
                {
                    newEnt.uuid = generateEntityUuid();
                }

                syncEntityToDomain(newEnt);

                if (FCT::ECS::registry* reg = resolveEntityRegistry(newEnt);
                    reg && newEnt.ecsEntity != FCT::ECS::null && reg->valid(newEnt.ecsEntity))
                {
                    for (const auto& blob : resolvedNodes[i].dynamicComponents)
                    {
                        if (blob.valid() && blob.descriptor() && blob.descriptor()->ecsEmplaceOrReplace)
                        {
                            blob.descriptor()->ecsEmplaceOrReplace(*reg, newEnt.ecsEntity, blob.data());
                        }
                    }
                }

                m_entities.push_back(newEnt);
                createdIds.push_back(newEnt.id);
            }

            for (uint32_t createdId : createdIds)
            {
                if (const auto* entPtr = findEntityById(createdId); entPtr && entPtr->parentId != 0)
                {
                    setEntityParent(createdId, entPtr->parentId);
                }
            }

            if (!createdIds.empty())
            {
                m_selectedEntityId = createdIds.front();
                if (m_editorUiComposite)
                {
                    const auto* rootEnt = findEntityById(m_selectedEntityId);
                    EditorBase::SelectEntitySignal sig;
                    sig.entityId = m_selectedEntityId;
                    sig.entityName = rootEnt ? rootEnt->name : std::string();
                    m_editorUiComposite->getUiContext().emit(std::move(sig));
                }
            }
            return createdIds;
        }

        void SceneViewerComposite::removeSubtreeEntitiesDirect(const std::vector<uint32_t>& entityIds)
        {
            ZoneScoped();
            // 按逆序删除，确保先解绑/销毁子节点再销毁父节点
            for (auto it = entityIds.rbegin(); it != entityIds.rend(); ++it)
            {
                deleteEntity(*it);
            }
        }

        void SceneViewerComposite::copyEntityToClipboard(uint32_t rootEntityId)
        {
            ZoneScoped();
            const SceneEntityDesc* rootEnt = findEntityById(rootEntityId);
            if (!rootEnt)
            {
                return;
            }
            m_clipboardSourceParentId = rootEnt->parentId;
            m_clipboardSourceRegistry = rootEnt->registryName.empty() ? "Main" : rootEnt->registryName;
            collectSubtreeSnapshot(rootEntityId, m_entityClipboard);
            if (m_entityClipboard.empty())
            {
                return;
            }

            const std::string summary =
                "[MQEntitySubtree] " + m_entityClipboard.front().desc.name +
                " (" + std::to_string(m_entityClipboard.size()) + " nodes)";
            ImGui::SetClipboardText(summary.c_str());

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    m_editorUiComposite->tr("已复制实体", "Entity Copied"),
                    m_entityClipboard.front().desc.name + " (" + std::to_string(m_entityClipboard.size()) + ")",
                    EditorBase::LogEntry::Level::Info,
                    1.6f);
            }
        }

        void SceneViewerComposite::pasteEntityFromClipboard(uint32_t overrideParentId, bool keepSourceParent)
        {
            ZoneScoped();
            if (m_entityClipboard.empty())
            {
                return;
            }

            uint32_t targetParentId = overrideParentId;
            std::string targetRegistry = m_activeTargetRegistry.empty()
                                             ? m_clipboardSourceRegistry
                                             : m_activeTargetRegistry;
            if (targetParentId != 0)
            {
                if (const auto* parentEnt = findEntityById(targetParentId))
                {
                    targetRegistry = parentEnt->registryName.empty() ? "Main" : parentEnt->registryName;
                }
                else
                {
                    targetParentId = 0;
                }
            }
            else if (keepSourceParent)
            {
                if (m_clipboardSourceParentId != 0)
                {
                    if (const auto* srcParent = findEntityById(m_clipboardSourceParentId))
                    {
                        targetParentId = srcParent->id;
                        targetRegistry = srcParent->registryName.empty() ? "Main" : srcParent->registryName;
                    }
                }
                else if (!m_clipboardSourceRegistry.empty())
                {
                    targetRegistry = m_clipboardSourceRegistry;
                }
            }

            const std::vector<uint32_t> createdIds = instantiateSubtreeSnapshot(
                m_entityClipboard,
                targetParentId,
                targetRegistry,
                true,
                "Paste Entity Subtree");
            if (!createdIds.empty() && m_editorUiComposite)
            {
                const SceneEntityDesc* rootEnt = findEntityById(createdIds.front());
                m_editorUiComposite->showToast(
                    m_editorUiComposite->tr("已粘贴实体", "Entity Pasted"),
                    rootEnt ? rootEnt->name : std::string(),
                    EditorBase::LogEntry::Level::Info,
                    1.6f);
            }
        }

        void SceneViewerComposite::duplicateEntitySubtree(uint32_t rootEntityId)
        {
            ZoneScoped();
            const SceneEntityDesc* srcRoot = findEntityById(rootEntityId);
            if (!srcRoot)
            {
                return;
            }

            const uint32_t parentId = srcRoot->parentId;
            const std::string targetRegistry = srcRoot->registryName.empty() ? "Main" : srcRoot->registryName;
            std::vector<EntityClipboardNode> snapshot;
            collectSubtreeSnapshot(rootEntityId, snapshot);
            if (snapshot.empty())
            {
                return;
            }

            const std::vector<uint32_t> createdIds = instantiateSubtreeSnapshot(
                snapshot,
                parentId,
                targetRegistry,
                true,
                "Duplicate Entity Subtree");
            if (!createdIds.empty() && m_editorUiComposite)
            {
                const SceneEntityDesc* rootEnt = findEntityById(createdIds.front());
                m_editorUiComposite->showToast(
                    m_editorUiComposite->tr("已克隆实体", "Entity Duplicated"),
                    rootEnt ? rootEnt->name : std::string(),
                    EditorBase::LogEntry::Level::Info,
                    1.6f);
            }
        }

        void SceneViewerComposite::handleHierarchyAndViewportShortcuts(bool allowShortcut)
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (!allowShortcut || io.WantTextInput || isPiePlaying())
            {
                return;
            }
            if (!io.KeyCtrl || io.KeyAlt || io.KeyShift)
            {
                return;
            }

            const uint64_t currentFrame = static_cast<uint64_t>(ImGui::GetFrameCount());
            if (m_lastShortcutFrame == currentFrame)
            {
                return;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_C, false) && m_selectedEntityId != 0)
            {
                m_lastShortcutFrame = currentFrame;
                copyEntityToClipboard(m_selectedEntityId);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_V, false) && !m_entityClipboard.empty())
            {
                m_lastShortcutFrame = currentFrame;
                pasteEntityFromClipboard(0, true);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_D, false) && m_selectedEntityId != 0)
            {
                m_lastShortcutFrame = currentFrame;
                duplicateEntitySubtree(m_selectedEntityId);
            }
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::SceneViewerComposite)
