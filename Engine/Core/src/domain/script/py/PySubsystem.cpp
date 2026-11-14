/**
 * @file PySubsystem.cpp
 * @brief Python 脚本子域实现：基于 FCT_PyBri 与 CPython 解释器，
 *        通过反射零样板绑定 ECS 组件与核心业务子系统
 */
#include <MQEngine/domain/script/py/PySubsystem.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/domain/physics/PhysicsSubsystem.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/gameplay/GamePlaySubsystem.h>
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
            PySubsystem::PySubsystem() = default;

            PySubsystem::~PySubsystem()
            {
                term();
            }

            void PySubsystem::registerInfo()
            {
            }

            Status PySubsystem::init()
            {
                if (m_available.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

#ifndef __ANDROID__
                m_pyEnv = makeUnique<FCT::PyBri::PyEnvironment>();
                assert(m_pyEnv != nullptr && "创建 FCT::PyBri::PyEnvironment 失败");
                if (!m_pyEnv->setup()) {
                    m_available.store(false, std::memory_order_release);
                    return OkStatus();
                }

                setupPyBriEnginePackage();
                m_available.store(true, std::memory_order_release);
#else
                m_available.store(false, std::memory_order_release);
#endif
                return OkStatus();
            }

            void PySubsystem::term() noexcept
            {
                if (!m_available.load(std::memory_order_relaxed)) {
                    return;
                }

#ifndef __ANDROID__
                m_enginePackage.reset();
                m_pyEnv.reset();
#endif
                m_available.store(false, std::memory_order_release);
            }

            Status PySubsystem::executeScript(const std::string& script)
            {
                if (!m_available.load(std::memory_order_acquire)) {
                    return FailedPreconditionError("Python 子系统未就绪或当前平台不支持");
                }

#ifndef __ANDROID__
                if (!m_pyEnv) {
                    return FailedPreconditionError("PyEnvironment 为空");
                }

                ZoneScopedN("PySubsystem::executeScript");
                m_pyEnv->executeScript(script);
                return OkStatus();
#else
                (void)script;
                return UnimplementedError("Android 平台禁用 Python 脚本");
#endif
            }

            Status PySubsystem::executeFile(const std::string& filePath)
            {
                if (!m_available.load(std::memory_order_acquire)) {
                    return FailedPreconditionError("Python 子系统未就绪");
                }

                auto* resMod = Resource::Resource::InstancePtr();
                auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
                if (!loader) {
                    return FailedPreconditionError("ResourceLoader 未就绪");
                }
                auto contentStatus = loader->loadFile(filePath);
                if (!contentStatus.ok()) {
                    return contentStatus.status();
                }

                ZoneScopedN("PySubsystem::executeFile");
                return executeScript(*contentStatus);
            }

            void PySubsystem::setupPyBriEnginePackage()
            {
#ifndef __ANDROID__
                if (!m_pyEnv) {
                    return;
                }
                m_enginePackage = makeUnique<FCT::PyBri::Package>("mqengine");

                // 1. 注册核心 ECS 组件元数据
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

                // 2. 绑定实体生命周期管理
                m_enginePackage->bindFunction(*m_pyEnv, "create_entity", [](std::string name) -> uint32_t {
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

                m_enginePackage->bindFunction(*m_pyEnv, "destroy_entity", [](uint32_t entity_id) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        domain->scene.destroyEntity(static_cast<FCT::ECS::entity>(entity_id));
                    }
                }, {"entity_id"});

                m_enginePackage->bindFunction(*m_pyEnv, "is_entity_valid", [](uint32_t entity_id) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->scene.valid(static_cast<FCT::ECS::entity>(entity_id)) : false;
                }, {"entity_id"});

                // 3. 绑定 Transform 变换组件
                m_enginePackage->bindFunction(*m_pyEnv, "get_local_position", [](uint32_t entity_id) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {0.0f, 0.0f, 0.0f};
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (!tf) return {0.0f, 0.0f, 0.0f};
                    return {tf->position.x, tf->position.y, tf->position.z};
                }, {"entity_id"});

                m_enginePackage->bindFunction(*m_pyEnv, "set_local_position", [](uint32_t entity_id, float x, float y, float z) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (tf) {
                        tf->position = FCT::Vec3<>{x, y, z};
                        tf->isDirty = true;
                    }
                }, {"entity_id", "x", "y", "z"});

                m_enginePackage->bindFunction(*m_pyEnv, "get_local_rotation", [](uint32_t entity_id) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {0.0f, 0.0f, 0.0f};
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (!tf) return {0.0f, 0.0f, 0.0f};
                    return {tf->rotation.x, tf->rotation.y, tf->rotation.z};
                }, {"entity_id"});

                m_enginePackage->bindFunction(*m_pyEnv, "set_local_rotation", [](uint32_t entity_id, float pitch, float yaw, float roll) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (tf) {
                        tf->rotation = FCT::Vec3<>{pitch, yaw, roll};
                        tf->isDirty = true;
                    }
                }, {"entity_id", "pitch", "yaw", "roll"});

                m_enginePackage->bindFunction(*m_pyEnv, "get_local_scale", [](uint32_t entity_id) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {1.0f, 1.0f, 1.0f};
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (!tf) return {1.0f, 1.0f, 1.0f};
                    return {tf->scale.x, tf->scale.y, tf->scale.z};
                }, {"entity_id"});

                m_enginePackage->bindFunction(*m_pyEnv, "set_local_scale", [](uint32_t entity_id, float sx, float sy, float sz) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto* tf = domain->scene.getRegistry().try_get<Scene::LocalTransform>(entity);
                    if (tf) {
                        tf->scale = FCT::Vec3<>{sx, sy, sz};
                        tf->isDirty = true;
                    }
                }, {"entity_id", "sx", "sy", "sz"});

                // 4. 绑定 Camera 与 Mesh 设置
                m_enginePackage->bindFunction(*m_pyEnv, "set_camera", [](uint32_t entity_id, float fov, float near_plane, float far_plane) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto& reg = domain->scene.getRegistry();
                    auto& cam = reg.emplace_or_replace<Scene::CameraComponent>(entity);
                    cam.fov = fov;
                    cam.nearPlane = near_plane;
                    cam.farPlane = far_plane;
                }, {"entity_id", "fov", "near_plane", "far_plane"});

                m_enginePackage->bindFunction(*m_pyEnv, "set_mesh", [](uint32_t entity_id, int primitive_type, std::string asset_path, bool visible) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto& reg = domain->scene.getRegistry();
                    auto& mesh = reg.emplace_or_replace<Scene::StaticMeshComponent>(entity);
                    mesh.primitiveType = primitive_type;
                    mesh.meshAssetPath = std::move(asset_path);
                    mesh.visible = visible;
                }, {"entity_id", "primitive_type", "asset_path", "visible"});

                // 5. 绑定 Light 光照组件设置
                m_enginePackage->bindFunction(*m_pyEnv, "set_directional_light", [](uint32_t entity_id, float dir_x, float dir_y, float dir_z, float r, float g, float b, float intensity) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto& reg = domain->scene.getRegistry();
                    auto& light = reg.emplace_or_replace<Scene::DirectionalLightComponent>(entity);
                    light.direction = FCT::Vec3<>{dir_x, dir_y, dir_z};
                    light.color = FCT::Vec3<>{r, g, b};
                    light.intensity = intensity;
                }, {"entity_id", "dir_x", "dir_y", "dir_z", "r", "g", "b", "intensity"});

                m_enginePackage->bindFunction(*m_pyEnv, "set_point_light", [](uint32_t entity_id, float r, float g, float b, float intensity, float range) {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return;
                    auto entity = static_cast<FCT::ECS::entity>(entity_id);
                    auto& reg = domain->scene.getRegistry();
                    auto& light = reg.emplace_or_replace<Scene::PointLightComponent>(entity);
                    light.color = FCT::Vec3<>{r, g, b};
                    light.intensity = intensity;
                    light.range = range;
                }, {"entity_id", "r", "g", "b", "intensity", "range"});

                // 6. 绑定 Physics 射线检测
                m_enginePackage->bindFunction(*m_pyEnv, "raycast", [](float orig_x, float orig_y, float orig_z, float dir_x, float dir_y, float dir_z, float max_dist) -> std::vector<float> {
                    auto* domain = Domain::InstancePtr();
                    if (!domain) return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    Physics::RaycastHit hit;
                    FCT::Vec3<> origin{orig_x, orig_y, orig_z};
                    FCT::Vec3<> direction{dir_x, dir_y, dir_z};
                    bool hasHit = domain->physics.raycast(origin, direction, max_dist, hit);
                    return {
                        hasHit ? 1.0f : 0.0f,
                        hit.point.x, hit.point.y, hit.point.z,
                        hit.normal.x, hit.normal.y, hit.normal.z,
                        hit.distance,
                        static_cast<float>(static_cast<uint32_t>(hit.entity))
                    };
                }, {"orig_x", "orig_y", "orig_z", "dir_x", "dir_y", "dir_z", "max_dist"});

                // 7. 绑定 HID 输入查询
                m_enginePackage->bindFunction(*m_pyEnv, "is_key_pressed", [](int key_code) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isKeyPressed(static_cast<HID::KeyCode>(key_code)) : false;
                }, {"key_code"});

                m_enginePackage->bindFunction(*m_pyEnv, "is_key_just_pressed", [](int key_code) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isKeyJustPressed(static_cast<HID::KeyCode>(key_code)) : false;
                }, {"key_code"});

                m_enginePackage->bindFunction(*m_pyEnv, "is_mouse_button_pressed", [](int button) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isMouseButtonPressed(static_cast<HID::MouseButton>(button)) : false;
                }, {"button"});

                m_enginePackage->bindFunction(*m_pyEnv, "get_mouse_x", []() -> float {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.getMousePosition().x : 0.0f;
                });

                m_enginePackage->bindFunction(*m_pyEnv, "get_mouse_y", []() -> float {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.getMousePosition().y : 0.0f;
                });

                m_enginePackage->bindFunction(*m_pyEnv, "get_axis", [](std::string axis_name) -> float {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.getAxis(axis_name) : 0.0f;
                }, {"axis_name"});

                m_enginePackage->bindFunction(*m_pyEnv, "is_action_pressed", [](std::string action_name) -> bool {
                    auto* domain = Domain::InstancePtr();
                    return domain ? domain->hid.isActionPressed(action_name) : false;
                }, {"action_name"});

                // 8. 绑定 GamePlay 状态机与主循环控制
                m_enginePackage->bindFunction(*m_pyEnv, "get_game_state", []() -> int {
                    auto* domain = Domain::InstancePtr();
                    return domain ? static_cast<int>(domain->gameplay.getStateMachine().getCurrentState()) : 0;
                });

                m_enginePackage->bindFunction(*m_pyEnv, "set_game_state", [](int state) {
                    auto* domain = Domain::InstancePtr();
                    if (domain) {
                        domain->gameplay.getStateMachine().transitionTo(static_cast<GamePlay::GameState>(state));
                    }
                }, {"state"});

                m_enginePackage->bindFunction(*m_pyEnv, "request_exit", []() {
                    auto* logicHost = Host::LogicHost::InstancePtr();
                    if (logicHost) {
                        logicHost->requestExit();
                    }
                });

                // 9. 绑定标准日志输出
                m_enginePackage->bindFunction(*m_pyEnv, "log_info", [](std::string message) {
                    spdlog::info("[PySubsystem] {}", message);
                }, {"message"});

                m_enginePackage->bindFunction(*m_pyEnv, "log_warn", [](std::string message) {
                    spdlog::warn("[PySubsystem] {}", message);
                }, {"message"});

                m_enginePackage->bindFunction(*m_pyEnv, "log_error", [](std::string message) {
                    spdlog::error("[PySubsystem] {}", message);
                }, {"message"});

                // 挂载至 Python 解释器运行时环境，支持 import mqengine
                m_enginePackage->mountToEnvironment(*m_pyEnv, "mqengine");
#endif
            }

        } // namespace Script
    } // namespace Domain
} // namespace MQEngine
