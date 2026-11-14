/**
 * @file JSSubsystem.cpp
 * @brief JavaScript/TypeScript 脚本子域实现：基于 V8/FCT_Node 虚拟环境，
 *        通过 FCT::NB 零样板反射绑定 ECS 组件与核心业务子系统
 */
#include <MQEngine/domain/script/js/JSSubsystem.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/domain/physics/PhysicsSubsystem.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/gameplay/GamePlaySubsystem.h>
#include <MQEngine/domain/effect/EffectSubsystem.h>
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <cassert>

namespace MQEngine
{
    namespace Domain
    {
        namespace Script
        {
            namespace
            {
                void openModelOnAnchorEntity(const std::string& path)
                {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) {
                        return;
                    }
                    auto& reg = domain->scene.getRegistry();
                    FCT::ECS::entity anchor = FCT::ECS::null;
                    auto view = reg.view<Scene::ModelAnchorTag>();
                    for (auto e : view) {
                        anchor = e;
                        break;
                    }
                    if (anchor == FCT::ECS::null) {
                        anchor = domain->scene.createEntity();
                        reg.emplace_or_replace<Scene::ModelAnchorTag>(anchor);
                        if (domain->scene.shouldAttachEditorMetadata()) {
                            reg.emplace_or_replace<Scene::EditorNameComponent>(
                                anchor, Scene::EditorNameComponent{"ModelAnchorEntity"});
                        }
                    }
                    auto& mesh = reg.emplace_or_replace<Scene::StaticMeshComponent>(anchor);
                    mesh.meshAssetPath = path;
                    mesh.primitiveType = -1;
                    mesh.visible = true;
                    mesh.castShadows = true;
                }
            } // namespace

            JSSubsystem::JSSubsystem() = default;

            JSSubsystem::~JSSubsystem()
            {
                term();
            }

            void JSSubsystem::registerInfo()
            {
            }

            Status JSSubsystem::init()
            {
                if (m_running.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                FCT::JS::NodeCommon::Init();
                m_nodeEnv = makeUnique<FCT::JS::NodeEnvironment>();
                assert(m_nodeEnv != nullptr && "创建 FCT::JS::NodeEnvironment 失败");
                if (!m_nodeEnv->setup()) {
                    return InternalError("启动 FCT::JS::NodeEnvironment 失败");
                }

                setupEnginePackage();
                registerWithWorkHost();

                m_running.store(true, std::memory_order_release);
                return OkStatus();
            }

            void JSSubsystem::term() noexcept
            {
                if (!m_running.load(std::memory_order_relaxed)) {
                    return;
                }

                unregisterFromWorkHost();

                m_enginePackage.reset();
                if (m_nodeEnv) {
                    m_nodeEnv->stop();
                }
                m_nodeEnv.reset();
                m_running.store(false, std::memory_order_release);
            }

            void JSSubsystem::pollEvents()
            {
                if (!m_running.load(std::memory_order_acquire) || !m_nodeEnv) {
                    return;
                }

                ZoneScopedN("JSSubsystem::pollEvents");
                m_nodeEnv->tick();
            }

            Status JSSubsystem::evaluateScript(const std::string& source, const std::string& filename)
            {
                (void)filename;
                if (!m_running.load(std::memory_order_acquire) || !m_nodeEnv) {
                    return FailedPreconditionError("JSSubsystem 未就绪或已被销毁");
                }

                ZoneScopedN("JSSubsystem::evaluateScript");
                m_nodeEnv->executeScript(source);
                return OkStatus();
            }

            Status JSSubsystem::loadModule(const std::string& modulePath)
            {
                if (!m_running.load(std::memory_order_acquire) || !m_nodeEnv) {
                    return FailedPreconditionError("JSSubsystem 未就绪");
                }

                auto* resMod = Resource::Resource::InstancePtr();
                auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
                if (!loader) {
                    return FailedPreconditionError("ResourceLoader 未就绪");
                }
                auto contentStatus = loader->loadFile(modulePath);
                if (!contentStatus.ok()) {
                    return contentStatus.status();
                }

                return evaluateScript(*contentStatus, modulePath);
            }

            void JSSubsystem::setupEnginePackage()
            {
                if (!m_nodeEnv) {
                    return;
                }

                m_enginePackage = makeUnique<FCT::NB::Package>("MQEngine");

                // 1. 注册核心 ECS 组件模式元数据
                m_enginePackage->registerStruct<Scene::UuidComponent>("UuidComponent", true);
                m_enginePackage->registerStruct<Scene::EditorNameComponent>("EditorNameComponent", true);
                m_enginePackage->registerStruct<Scene::CameraComponent>("CameraComponent", true);
                m_enginePackage->registerStruct<Scene::DirectionalLightComponent>("DirectionalLightComponent", true);
                m_enginePackage->registerStruct<Scene::PointLightComponent>("PointLightComponent", true);
                m_enginePackage->registerStruct<Scene::StaticMeshComponent>("StaticMeshComponent", true);
                m_enginePackage->registerStruct<Scene::PbrMaterialComponent>("PbrMaterialComponent", true);
                m_enginePackage->registerStruct<Scene::RigidBodyComponent>("RigidBodyComponent", true);
                m_enginePackage->registerStruct<Scene::LocalTransform>("LocalTransform", true);
                m_enginePackage->registerStruct<Scene::WorldTransform>("WorldTransform", true);

                // 2. 绑定场景实体生命周期服务
                m_enginePackage->bindFunction(*m_nodeEnv, "createEntity", [](std::string name) -> uint32_t {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return 0;
                    auto entity = domain->scene.createEntity();
                    if (!name.empty() && domain->scene.shouldAttachEditorMetadata()) {
                        auto& reg = domain->scene.getRegistry();
                        reg.emplace_or_replace<Scene::EditorNameComponent>(
                            entity, Scene::EditorNameComponent{std::move(name)});
                    }
                    return static_cast<uint32_t>(entity);
                }, {"name"});

                m_enginePackage->bindFunction(*m_nodeEnv, "destroyEntity", [](uint32_t entityId) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        domain->scene.destroyEntity(static_cast<FCT::ECS::entity>(entityId));
                    }
                }, {"entityId"});

                m_enginePackage->bindFunction(*m_nodeEnv, "isEntityValid", [](uint32_t entityId) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->scene.valid(static_cast<FCT::ECS::entity>(entityId)) : false;
                }, {"entityId"});

                // 3. 绑定 Transform 变换组件读写
                m_enginePackage->bindFunction(*m_nodeEnv, "getLocalPosition", [](uint32_t entityId) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {0.0f, 0.0f, 0.0f};
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (!tf) return {0.0f, 0.0f, 0.0f};
                    return {tf->position.x, tf->position.y, tf->position.z};
                }, {"entityId"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setLocalPosition", [](uint32_t entityId, float x, float y, float z) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto& reg = domain->scene.getRegistry();
                    auto* tf = reg.try_get<Scene::LocalTransform>(entity);
                    if (tf) {
                        tf->position = FCT::Vec3<>{x, y, z};
                        tf->isDirty = true;
                    }
                }, {"entityId", "x", "y", "z"});

                m_enginePackage->bindFunction(*m_nodeEnv, "getLocalRotation", [](uint32_t entityId) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {0.0f, 0.0f, 0.0f};
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (!tf) return {0.0f, 0.0f, 0.0f};
                    return {tf->rotation.x, tf->rotation.y, tf->rotation.z};
                }, {"entityId"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setLocalRotation", [](uint32_t entityId, float pitch, float yaw, float roll) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (tf) {
                        tf->rotation = FCT::Vec3<>{pitch, yaw, roll};
                        tf->isDirty = true;
                    }
                }, {"entityId", "pitch", "yaw", "roll"});

                m_enginePackage->bindFunction(*m_nodeEnv, "getLocalScale", [](uint32_t entityId) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {1.0f, 1.0f, 1.0f};
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (!tf) return {1.0f, 1.0f, 1.0f};
                    return {tf->scale.x, tf->scale.y, tf->scale.z};
                }, {"entityId"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setLocalScale", [](uint32_t entityId, float sx, float sy, float sz) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (tf) {
                        tf->scale = FCT::Vec3<>{sx, sy, sz};
                        tf->isDirty = true;
                    }
                }, {"entityId", "sx", "sy", "sz"});

                // 4. 绑定 Camera 与 Mesh 组件设置
                m_enginePackage->bindFunction(*m_nodeEnv, "setCamera", [](uint32_t entityId, float fov, float nearPlane, float farPlane) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto& reg = domain->scene.getRegistry();
                    auto& cam = reg.emplace_or_replace<Scene::CameraComponent>(entity);
                    cam.fov = fov;
                    cam.nearPlane = nearPlane;
                    cam.farPlane = farPlane;
                }, {"entityId", "fov", "nearPlane", "farPlane"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setMesh", [](uint32_t entityId, int primitiveType, std::string assetPath, bool visible) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto& reg = domain->scene.getRegistry();
                    auto& mesh = reg.emplace_or_replace<Scene::StaticMeshComponent>(entity);
                    mesh.primitiveType = primitiveType;
                    mesh.meshAssetPath = std::move(assetPath);
                    mesh.visible = visible;
                }, {"entityId", "primitiveType", "assetPath", "visible"});

                // 5. 绑定 Light 光照组件设置
                m_enginePackage->bindFunction(*m_nodeEnv, "setDirectionalLight", [](uint32_t entityId, float dirX, float dirY, float dirZ, float r, float g, float b, float intensity) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto& reg = domain->scene.getRegistry();
                    auto& light = reg.emplace_or_replace<Scene::DirectionalLightComponent>(entity);
                    light.direction = FCT::Vec3<>{dirX, dirY, dirZ};
                    light.color = FCT::Vec3<>{r, g, b};
                    light.intensity = intensity;
                }, {"entityId", "dirX", "dirY", "dirZ", "r", "g", "b", "intensity"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setPointLight", [](uint32_t entityId, float r, float g, float b, float intensity, float range) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto& reg = domain->scene.getRegistry();
                    auto& light = reg.emplace_or_replace<Scene::PointLightComponent>(entity);
                    light.color = FCT::Vec3<>{r, g, b};
                    light.intensity = intensity;
                    light.range = range;
                }, {"entityId", "r", "g", "b", "intensity", "range"});

                // 6. 绑定 Physics 射线检测
                m_enginePackage->bindFunction(*m_nodeEnv, "raycast", [](float origX, float origY, float origZ, float dirX, float dirY, float dirZ, float maxDist) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    Physics::RaycastHit hit;
                    FCT::Vec3<> origin{origX, origY, origZ};
                    FCT::Vec3<> direction{dirX, dirY, dirZ};
                    bool hasHit = domain->physics.raycast(origin, direction, maxDist, hit);
                    return {
                        hasHit ? 1.0f : 0.0f,
                        hit.point.x, hit.point.y, hit.point.z,
                        hit.normal.x, hit.normal.y, hit.normal.z,
                        hit.distance,
                        static_cast<float>(static_cast<uint32_t>(hit.entity))
                    };
                }, {"origX", "origY", "origZ", "dirX", "dirY", "dirZ", "maxDist"});

                // 7. 绑定 HID 输入查询
                m_enginePackage->bindFunction(*m_nodeEnv, "isKeyPressed", [](int keyCode) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isKeyPressed(static_cast<HID::KeyCode>(keyCode)) : false;
                }, {"keyCode"});

                m_enginePackage->bindFunction(*m_nodeEnv, "isKeyJustPressed", [](int keyCode) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isKeyJustPressed(static_cast<HID::KeyCode>(keyCode)) : false;
                }, {"keyCode"});

                m_enginePackage->bindFunction(*m_nodeEnv, "isMouseButtonPressed", [](int button) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isMouseButtonPressed(static_cast<HID::MouseButton>(button)) : false;
                }, {"button"});

                m_enginePackage->bindFunction(*m_nodeEnv, "getMouseX", []() -> float {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.getMousePosition().x : 0.0f;
                });

                m_enginePackage->bindFunction(*m_nodeEnv, "getMouseY", []() -> float {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.getMousePosition().y : 0.0f;
                });

                m_enginePackage->bindFunction(*m_nodeEnv, "getAxis", [](std::string axisName) -> float {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.getAxis(axisName) : 0.0f;
                }, {"axisName"});

                m_enginePackage->bindFunction(*m_nodeEnv, "isActionPressed", [](std::string actionName) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isActionPressed(actionName) : false;
                }, {"actionName"});

                // 8. 绑定 GamePlay 状态机与主循环控制
                m_enginePackage->bindFunction(*m_nodeEnv, "getGameState", []() -> int {
                    auto* domain = Domain::InstancePtr();
                    return domain ? static_cast<int>(domain->gameplay.getStateMachine().getCurrentState()) : 0;
                });

                m_enginePackage->bindFunction(*m_nodeEnv, "setGameState", [](int state) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        domain->gameplay.getStateMachine().transitionTo(static_cast<GamePlay::GameState>(state));
                    }
                }, {"state"});

                m_enginePackage->bindFunction(*m_nodeEnv, "requestExit", []() {
                    auto* logicHost = Host::LogicHost::InstancePtr();
                    if (logicHost) {
                        logicHost->requestExit();
                    }
                });

                // 9. 绑定标准日志输出
                m_enginePackage->bindFunction(*m_nodeEnv, "logInfo", [](std::string message) {
                    spdlog::info("[JSSubsystem] {}", message);
                }, {"message"});

                m_enginePackage->bindFunction(*m_nodeEnv, "logWarn", [](std::string message) {
                    spdlog::warn("[JSSubsystem] {}", message);
                }, {"message"});

                m_enginePackage->bindFunction(*m_nodeEnv, "logError", [](std::string message) {
                    spdlog::error("[JSSubsystem] {}", message);
                }, {"message"});

                // 10. 绑定 App 运行期应用服务与启动文件路径接口 (App.getStartupFilePath())
                auto appObj = m_nodeEnv->createJSObject();
                FCT::NB::bindFunctionToObject(*m_nodeEnv, appObj, "getStartupFilePath", []() -> std::string {
                    auto* res = Resource::Resource::InstancePtr();
                    if (res && res->getConfig()) {
                        return res->getConfig()->getStartupFilePath();
                    }
                    return {};
                });

                FCT::NB::bindFunctionToObject(*m_nodeEnv, appObj, "openModel", [](std::string path) {
                    openModelOnAnchorEntity(path);
                    spdlog::info("[ModelViewer] App.openModel: 已载入模型: {}", path);
                });

                FCT::NB::bindFunctionToObject(*m_nodeEnv, appObj, "requestExit", []() {
                    auto* logicHost = Host::LogicHost::InstancePtr();
                    if (logicHost) {
                        logicHost->requestExit();
                    }
                });

                m_enginePackage->bindServiceObject(*m_nodeEnv, "App", appObj,
                    "    getStartupFilePath(): string;\n"
                    "    openModel(path: string): void;\n"
                    "    requestExit(): void;\n");

                m_enginePackage->bindFunction(*m_nodeEnv, "getStartupFilePath", []() -> std::string {
                    auto* res = Resource::Resource::InstancePtr();
                    if (res && res->getConfig()) {
                        return res->getConfig()->getStartupFilePath();
                    }
                    return {};
                });

                m_enginePackage->bindFunction(*m_nodeEnv, "openModel", [](std::string path) {
                    openModelOnAnchorEntity(path);
                }, {"path"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setDepthOfField", [](bool enabled, float aperture, float focusDistance) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        auto& dof = domain->effect.getDepthOfFieldSettings();
                        dof.enabled = enabled;
                        dof.aperture = aperture;
                        dof.focusDistance = focusDistance;
                    }
                }, {"enabled", "aperture", "focusDistance"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setBloom", [](bool enabled, float intensity, float threshold) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        auto& bloom = domain->effect.getBloomSettings();
                        bloom.enabled = enabled;
                        bloom.intensity = intensity;
                        bloom.threshold = threshold;
                    }
                }, {"enabled", "intensity", "threshold"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setDlss", [](bool enabled, float sharpness, int qualityMode) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        auto& dlss = domain->effect.getDlssSettings();
                        dlss.enabled = enabled;
                        dlss.sharpness = sharpness;
                        dlss.qualityMode = qualityMode;
                    }
                }, {"enabled", "sharpness", "qualityMode"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setToneMapping", [](int operatorType, float gamma, float whitePoint) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        auto& tm = domain->effect.getToneMappingSettings();
                        tm.operatorType = operatorType;
                        tm.gamma = gamma;
                        tm.whitePoint = whitePoint;
                    }
                }, {"operatorType", "gamma", "whitePoint"});

                m_enginePackage->bindFunction(*m_nodeEnv, "setPbrMaterial", [](uint32_t entityId, float r, float g, float b, float metallic, float roughness, float ao, std::string shaderAssetPath) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entityId);
                    auto& reg = domain->scene.getRegistry();
                    auto& pbr = reg.emplace_or_replace<Scene::PbrMaterialComponent>(entity);
                    pbr.baseColor = FCT::Vec3<>{r, g, b};
                    pbr.metallic = metallic;
                    pbr.roughness = roughness;
                    pbr.ao = ao;
                    if (!shaderAssetPath.empty()) {
                        pbr.shaderAssetPath = std::move(shaderAssetPath);
                    }
                }, {"entityId", "r", "g", "b", "metallic", "roughness", "ao", "shaderAssetPath"});

                m_enginePackage->addTypeScriptDeclaration(
                    "export namespace App {\n"
                    "    export function getStartupFilePath(): string;\n"
                    "    export function openModel(path: string): void;\n"
                    "    export function requestExit(): void;\n"
                    "}\n"
                    "export declare const App: {\n"
                    "    getStartupFilePath(): string;\n"
                    "    openModel(path: string): void;\n"
                    "    requestExit(): void;\n"
                    "};\n"
                    "declare global {\n"
                    "    const App: {\n"
                    "        getStartupFilePath(): string;\n"
                    "        openModel(path: string): void;\n"
                    "        requestExit(): void;\n"
                    "    };\n"
                    "}\n"
                );

                // 挂载至 Node.js 运行时环境，支持 globalThis.MQEngine 与 require('MQEngine')
                m_enginePackage->mountToEnvironment(*m_nodeEnv, "MQEngine");

                // 同时将 App 挂载至 Node.js 全局上下文 globalThis.App
                FCT::JS::Object globalObj = m_nodeEnv->global();
                globalObj["App"] = appObj;
            }

            void JSSubsystem::registerWithWorkHost()
            {
                auto* workHost = Host::WorkHost::InstancePtr();
                if (workHost) {
                    workHost->registerIoPoller("FCT_Node", [this]() {
                        pollEvents();
                    });
                }
            }

            void JSSubsystem::unregisterFromWorkHost() noexcept
            {
                auto* workHost = Host::WorkHost::InstancePtr();
                if (workHost) {
                    workHost->unregisterIoPoller("FCT_Node");
                }
            }

        } // namespace Script
    } // namespace Domain
} // namespace MQEngine
