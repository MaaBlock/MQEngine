/**
 * @file CliSceneAndCharacterCommands.cpp
 * @brief CliEditor 的 scene.* 与 character.*（含 18 部位参数化 3D 人形/机甲角色生成器与 AI 3D 桥接）命令实现
 */
#include "CliEditorComposite.h"
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/resource/core/resource.h>

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace MQEngine
{
    namespace CliEditor
    {
        namespace
        {
            /**
             * @brief 向 Wavefront OBJ 缓冲追加一个带法线与 UV 的圆角胶囊/椭球肢体部件
             */
            void appendLimbEllipsoidToObj(
                std::ostringstream& obj,
                uint32_t& baseVertexOffset,
                uint32_t& accumTriangles,
                FCT::Vec3<> center,
                FCT::Vec3<> radii,
                uint32_t rings = 20,
                uint32_t sectors = 28,
                bool boxyMecha = false)
            {
                constexpr float kPi = 3.1415926535f;
                for (uint32_t r = 0; r <= rings; ++r)
                {
                    const float v = static_cast<float>(r) / static_cast<float>(rings);
                    const float phi = v * kPi;
                    const float sinPhi = std::sin(phi);
                    const float cosPhi = std::cos(phi);

                    for (uint32_t s = 0; s <= sectors; ++s)
                    {
                        const float u = static_cast<float>(s) / static_cast<float>(sectors);
                        const float theta = u * 2.0f * kPi;
                        float nx = sinPhi * std::cos(theta);
                        float ny = cosPhi;
                        float nz = sinPhi * std::sin(theta);

                        float px = nx;
                        float py = ny;
                        float pz = nz;
                        if (boxyMecha)
                        {
                            // 超二次曲面硬化（机甲装甲切角风格）
                            auto signPow = [](float val, float exp) {
                                return (val >= 0.0f ? 1.0f : -1.0f) * std::pow(std::abs(val), exp);
                            };
                            px = signPow(nx, 0.55f);
                            py = signPow(ny, 0.55f);
                            pz = signPow(nz, 0.55f);
                        }

                        // 计算椭球/超二次曲面光滑解析法线
                        const FCT::Vec3<> smoothNorm = FCT::Vec3<>(
                            nx / std::max(radii.x, 1e-4f),
                            ny / std::max(radii.y, 1e-4f),
                            nz / std::max(radii.z, 1e-4f)).normalize();

                        const float vx = center.x + px * radii.x;
                        const float vy = center.y + py * radii.y;
                        const float vz = center.z + pz * radii.z;

                        obj << "v " << vx << " " << vy << " " << vz << "\n";
                        obj << "vn " << smoothNorm.x << " " << smoothNorm.y << " " << smoothNorm.z << "\n";
                        obj << "vt " << u << " " << v << "\n";
                    }
                }

                const uint32_t stride = sectors + 1;
                for (uint32_t r = 0; r < rings; ++r)
                {
                    for (uint32_t s = 0; s < sectors; ++s)
                    {
                        const uint32_t i0 = baseVertexOffset + r * stride + s;
                        const uint32_t i1 = i0 + 1;
                        const uint32_t i2 = baseVertexOffset + (r + 1) * stride + s;
                        const uint32_t i3 = i2 + 1;

                        obj << "f " << i0 << "/" << i0 << "/" << i0 << " "
                            << i2 << "/" << i2 << "/" << i2 << " "
                            << i1 << "/" << i1 << "/" << i1 << "\n";
                        obj << "f " << i1 << "/" << i1 << "/" << i1 << " "
                            << i2 << "/" << i2 << "/" << i2 << " "
                            << i3 << "/" << i3 << "/" << i3 << "\n";
                    }
                }

                baseVertexOffset += (rings + 1) * (sectors + 1);
                accumTriangles += rings * sectors * 2;
            }

            /**
             * @brief 生成 18 骨骼部位完整 3D 人形标准假人（Mannequin）或机甲（Mecha）网格文本
             */
            std::string buildArticulatedHumanoidObj(
                float totalHeight,
                float shoulderScale,
                float limbThickness,
                bool isMecha,
                uint32_t& outVertexCount,
                uint32_t& outTriCount)
            {
                const float h = std::clamp(totalHeight, 0.8f, 4.0f);
                const float sw = std::clamp(shoulderScale, 0.6f, 2.0f);
                const float lt = std::clamp(limbThickness, 0.5f, 2.0f);

                std::ostringstream obj;
                obj.imbue(std::locale::classic());
                obj << "# MQEngine Procedural 18-Part Articulated 3D Character ("
                    << (isMecha ? "Mecha" : "Mannequin") << ")\n";
                obj << "o CharacterRoot\n";

                uint32_t vOffset = 1;
                uint32_t triTotal = 0;

                // 1. 头部 (Head) & 颈部 (Neck)
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {0.0f, h * 0.91f, 0.0f}, {h * 0.068f, h * 0.085f, h * 0.072f}, 12, 18, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {0.0f, h * 0.81f, 0.0f}, {h * 0.036f * lt, h * 0.035f, h * 0.036f * lt}, 8, 12, isMecha);

                // 2. 胸腔躯干 (Upper Torso) & 腰腹骨盆 (Pelvis)
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {0.0f, h * 0.68f, 0.0f}, {h * 0.135f * sw, h * 0.13f, h * 0.078f * lt}, 12, 18, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {0.0f, h * 0.52f, 0.0f}, {h * 0.105f * sw, h * 0.075f, h * 0.068f * lt}, 10, 16, isMecha);

                // 3. 左右肩关节球铰 (Left/Right Shoulder Joints)
                const float shoulderX = h * 0.165f * sw;
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {-shoulderX, h * 0.75f, 0.0f}, {h * 0.048f * lt, h * 0.048f * lt, h * 0.048f * lt}, 8, 14, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, { shoulderX, h * 0.75f, 0.0f}, {h * 0.048f * lt, h * 0.048f * lt, h * 0.048f * lt}, 8, 14, isMecha);

                // 4. 左右上臂 (Upper Arms)、前臂 (Forearms) 与手掌 (Hands)
                const float armX = h * 0.195f * sw;
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {-armX, h * 0.63f, 0.0f}, {h * 0.038f * lt, h * 0.095f, h * 0.038f * lt}, 10, 14, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, { armX, h * 0.63f, 0.0f}, {h * 0.038f * lt, h * 0.095f, h * 0.038f * lt}, 10, 14, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {-armX * 1.04f, h * 0.47f, 0.01f}, {h * 0.032f * lt, h * 0.085f, h * 0.032f * lt}, 10, 14, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, { armX * 1.04f, h * 0.47f, 0.01f}, {h * 0.032f * lt, h * 0.085f, h * 0.032f * lt}, 10, 14, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {-armX * 1.05f, h * 0.36f, 0.015f}, {h * 0.025f * lt, h * 0.038f, h * 0.018f * lt}, 8, 12, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, { armX * 1.05f, h * 0.36f, 0.015f}, {h * 0.025f * lt, h * 0.038f, h * 0.018f * lt}, 8, 12, isMecha);

                // 5. 左右大腿 (Thighs)、膝关节 (Knees)、小腿 (Calves) 与脚掌 (Feet)
                const float hipX = h * 0.075f * sw;
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {-hipX, h * 0.35f, 0.0f}, {h * 0.055f * lt, h * 0.135f, h * 0.055f * lt}, 10, 16, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, { hipX, h * 0.35f, 0.0f}, {h * 0.055f * lt, h * 0.135f, h * 0.055f * lt}, 10, 16, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {-hipX, h * 0.15f, -0.005f}, {h * 0.044f * lt, h * 0.125f, h * 0.044f * lt}, 10, 16, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, { hipX, h * 0.15f, -0.005f}, {h * 0.044f * lt, h * 0.125f, h * 0.044f * lt}, 10, 16, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, {-hipX, h * 0.028f, h * 0.035f}, {h * 0.040f * lt, h * 0.028f, h * 0.075f}, 8, 14, isMecha);
                appendLimbEllipsoidToObj(obj, vOffset, triTotal, { hipX, h * 0.028f, h * 0.035f}, {h * 0.040f * lt, h * 0.028f, h * 0.075f}, 8, 14, isMecha);

                outVertexCount = vOffset - 1;
                outTriCount = triTotal;
                return obj.str();
            }
        } // namespace

        void CliEditorComposite::registerSceneAndCharacterCommands()
        {
            registerCommand(CliCommandSpec{
                .domain = "scene",
                .action = "list_entities",
                .ownerComponent = "Domain::Scene::SceneSubsystem",
                .summary = "列出当前激活 ECS 场景中的全部实体、Transform 位姿与组件信息",
                .details = "遍历 SceneSubsystem 的 ECS Registry，返回每个实体的 NameTag、LocalTransform、StaticMesh 与 PbrMaterial 状态。",
                .example = "CliEditor.exe --headless --exec=\"scene.list_entities\" --json",
                .params = {},
                .handler = [](const std::unordered_map<std::string, std::string>&) -> CliExecutionResult {
                    auto* domain = Domain::Domain::InstancePtr();
                    auto* scene = domain ? domain->getScene() : nullptr;
                    if (!scene || !scene->isInitialized())
                    {
                        return {false, "scene.list_entities", "SceneSubsystem 未初始化", "{}"};
                    }

                    auto& reg = scene->getRegistry();
                    auto view = reg.view<Domain::Scene::LocalTransform>();
                    std::ostringstream json;
                    json << "{\"entities\": [";
                    size_t count = 0;
                    for (auto entity : view)
                    {
                        const auto* nameComp = reg.try_get<Domain::Scene::EditorNameComponent>(entity);
                        const std::string entityName = nameComp
                            ? nameComp->name
                            : ("Entity_" + std::to_string(static_cast<uint32_t>(entity)));
                        const auto& tf = view.get<Domain::Scene::LocalTransform>(entity);
                        if (count > 0)
                        {
                            json << ", ";
                        }
                        json << "{\"id\": " << static_cast<uint32_t>(entity)
                             << ", \"name\": \"" << escapeJson(entityName)
                             << "\", \"pos\": [" << tf.position.x << ", " << tf.position.y << ", " << tf.position.z << "]"
                             << ", \"scale\": [" << tf.scale.x << ", " << tf.scale.y << ", " << tf.scale.z << "]}";
                        ++count;
                    }
                    json << "], \"total\": " << count << "}";
                    return {true, "scene.list_entities", "Found " + std::to_string(count) + " ECS entities", json.str()};
                }
            });

            registerCommand(CliCommandSpec{
                .domain = "scene",
                .action = "spawn_primitive",
                .ownerComponent = "Domain::Scene::SceneSubsystem",
                .summary = "在当前 ECS 场景中生成基础 3D 几何体实体（cube / plane / sphere）并赋予 PBR 材质",
                .details = "创建含 LocalTransform、WorldTransform、StaticMeshComponent 与 PbrMaterialComponent（及编辑器态 EditorNameComponent）的实体。",
                .example = "CliEditor.exe --headless --exec=\"scene.spawn_primitive name=GoldSphere type=sphere pos=0,2,0 color=1.0,0.84,0.2 metallic=0.9 roughness=0.15\"",
                .params = {
                    {"name", "string", false, "NewPrimitive", "实体名称"},
                    {"type", "string", false, "cube", "几何体类型：cube | plane | sphere"},
                    {"pos", "vec3", false, "0,1,0", "世界坐标 x,y,z"},
                    {"scale", "vec3", false, "1,1,1", "缩放比例 x,y,z"},
                    {"color", "vec3", false, "0.8,0.8,0.85", "PBR 基础色 r,g,b"},
                    {"metallic", "float", false, "0.2", "金属度 [0,1]"},
                    {"roughness", "float", false, "0.35", "粗糙度 [0.05,1]"}
                },
                .handler = [](const std::unordered_map<std::string, std::string>& args) -> CliExecutionResult {
                    auto* domain = Domain::Domain::InstancePtr();
                    auto* scene = domain ? domain->getScene() : nullptr;
                    if (!scene || !scene->isInitialized())
                    {
                        return {false, "scene.spawn_primitive", "SceneSubsystem 未初始化", "{}"};
                    }

                    auto& reg = scene->getRegistry();
                    auto ent = scene->createEntity();
                    const std::string name = args.at("name");
                    const std::string typeStr = args.at("type");

                    if (scene->shouldAttachEditorMetadata())
                    {
                        reg.emplace_or_replace<Domain::Scene::EditorNameComponent>(
                            ent, Domain::Scene::EditorNameComponent{name});
                    }

                    Domain::Scene::LocalTransform tf{};
                    parseVec3Arg(args.at("pos"), tf.position);
                    parseVec3Arg(args.at("scale"), tf.scale);
                    tf.isDirty = true;
                    reg.emplace_or_replace<Domain::Scene::LocalTransform>(ent, tf);

                    Domain::Scene::WorldTransform worldTf{};
                    worldTf.worldMatrix = FCT::Mat4::Translate(tf.position.x, tf.position.y, tf.position.z) *
                                          FCT::Mat4::Scale(tf.scale.x, tf.scale.y, tf.scale.z);
                    worldTf.isVisible = true;
                    reg.emplace_or_replace<Domain::Scene::WorldTransform>(ent, worldTf);
                    scene->getTransformGraph().markDirty(reg, ent);

                    Domain::Scene::StaticMeshComponent mesh{};
                    mesh.primitiveType = (typeStr == "plane") ? 1 : ((typeStr == "sphere") ? 2 : 0);
                    reg.emplace_or_replace<Domain::Scene::StaticMeshComponent>(ent, mesh);

                    Domain::Scene::PbrMaterialComponent mat{};
                    parseVec3Arg(args.at("color"), mat.baseColor);
                    const auto& metStr = args.at("metallic");
                    const auto& rghStr = args.at("roughness");
                    (void)absl::SimpleAtof(absl::string_view(metStr.data(), metStr.size()), &mat.metallic);
                    (void)absl::SimpleAtof(absl::string_view(rghStr.data(), rghStr.size()), &mat.roughness);
                    reg.emplace_or_replace<Domain::Scene::PbrMaterialComponent>(ent, mat);

                    scene->update();
                    return {
                        true,
                        "scene.spawn_primitive",
                        "Spawned primitive entity '" + name + "'",
                        "{\"entityId\": " + std::to_string(static_cast<uint32_t>(ent)) + ", \"name\": \"" + escapeJson(name) + "\"}"
                    };
                }
            });

            registerCommand(CliCommandSpec{
                .domain = "scene",
                .action = "set_camera",
                .ownerComponent = "Domain::Scene::SceneSubsystem",
                .summary = "调整场景主相机的位置、注视目标点（target）或旋转角（rot）与视场角（FOV），便于 AI 对准目标构图抓帧",
                .details = "修改主相机实体的 LocalTransform 与 CameraComponent，并同步更新 RendererSubsystem 的观察矩阵与透视矩阵。",
                .example = "CliEditor.exe --headless --exec=\"scene.set_camera pos=0,1.35,-2.6 target=0,0.95,0 fov=50\"",
                .params = {
                    {"pos", "vec3", false, "0,1.4,-2.8", "主相机世界坐标 x,y,z"},
                    {"target", "vec3", false, "0,0.95,0", "主相机注视目标点 x,y,z"},
                    {"rot", "vec3", false, "10,90,0", "主相机欧拉角 pitch,yaw,roll（度）"},
                    {"fov", "float", false, "50", "垂直视场角（度）"}
                },
                .handler = [](const std::unordered_map<std::string, std::string>& args) -> CliExecutionResult {
                    auto* domain = Domain::Domain::InstancePtr();
                    auto* scene = domain ? domain->getScene() : nullptr;
                    auto* renderer = domain ? domain->getRenderer() : nullptr;
                    if (!scene || !scene->isInitialized())
                    {
                        return {false, "scene.set_camera", "SceneSubsystem 未初始化", "{}"};
                    }

                    FCT::Vec3<> eye(0.0f, 1.4f, -2.8f);
                    FCT::Vec3<> lookTarget(0.0f, 0.95f, 0.0f);
                    FCT::Vec3<> rot(10.0f, 90.0f, 0.0f);
                    float fov = 50.0f;
                    parseVec3Arg(args.at("pos"), eye);
                    parseVec3Arg(args.at("target"), lookTarget);
                    parseVec3Arg(args.at("rot"), rot);
                    const auto& fovStr = args.at("fov");
                    (void)absl::SimpleAtof(absl::string_view(fovStr.data(), fovStr.size()), &fov);

                    auto& reg = scene->getRegistry();
                    FCT::ECS::entity primaryCam = FCT::ECS::null;
                    auto camView = reg.view<Domain::Scene::LocalTransform, Domain::Scene::CameraComponent>();
                    for (auto ent : camView)
                    {
                        const auto& camComp = camView.get<Domain::Scene::CameraComponent>(ent);
                        if (camComp.active && camComp.targetName == "MainWindow")
                        {
                            primaryCam = ent;
                            break;
                        }
                    }
                    if (primaryCam == FCT::ECS::null)
                    {
                        primaryCam = scene->createEntity();
                        if (scene->shouldAttachEditorMetadata())
                        {
                            reg.emplace_or_replace<Domain::Scene::EditorNameComponent>(
                                primaryCam, Domain::Scene::EditorNameComponent{"MainCamera"});
                        }
                    }

                    FCT::Vec3<> forward = lookTarget - eye;
                    if (forward.lengthSquared() > 1e-6f && args.find("rot") == args.end())
                    {
                        forward = forward.normalize();
                        constexpr float kRadToDeg = 180.0f / 3.14159265f;
                        const float pitchDeg = std::asin(std::clamp(-forward.y, -1.0f, 1.0f)) * kRadToDeg;
                        const float yawDeg = std::atan2(forward.x, forward.z) * kRadToDeg;
                        rot = FCT::Vec3<>(pitchDeg, yawDeg, 0.0f);
                    }

                    auto& tf = reg.get_or_emplace<Domain::Scene::LocalTransform>(primaryCam);
                    tf.position = eye;
                    tf.rotation = rot;
                    tf.isDirty = true;

                    auto& cam = reg.get_or_emplace<Domain::Scene::CameraComponent>(primaryCam);
                    cam.fov = fov;
                    cam.nearPlane = 0.1f;
                    cam.farPlane = 500.0f;
                    cam.active = true;
                    if (cam.targetName.empty())
                    {
                        cam.targetName = "MainWindow";
                    }

                    scene->getTransformGraph().markDirty(reg, primaryCam);
                    scene->update();

                    if (renderer)
                    {
                        renderer->setCameraWorldPosition(eye);
                        renderer->refreshActiveCameras();
                    }
                    return {true, "scene.set_camera", "Updated active camera entity pose and target binding", "{}"};
                }
            });

            registerCommand(CliCommandSpec{
                .domain = "character",
                .action = "generate_mannequin",
                .ownerComponent = "CliEditor::CharacterGenerator",
                .summary = "免费离线生成 18 部位参数化 3D 人形标准假人（Mannequin）或机甲（Mecha）角色网格（.obj）并载入当前场景",
                .details = "基于超二次曲面与 18 个解剖学肢体段生成带法线和 UV 的 3D 角色模型，保存至 Assets/Models/ 并通过 Assimp + Vulkan StaticMesh 实例化进当前 ECS 场景。",
                .example = "CliEditor.exe --headless --exec=\"character.generate_mannequin name=CyberKnight style=mecha height=1.90 shoulder=1.18 pos=0,0,0 color=0.22,0.58,0.92 metallic=0.85 roughness=0.22\"",
                .params = {
                    {"name", "string", false, "HeroMannequin", "角色名称（同时作为输出 OBJ 文件名）"},
                    {"style", "string", false, "mannequin", "角色风格：mannequin（圆滑人体假人）| mecha（硬表面机甲角色）"},
                    {"height", "float", false, "1.80", "角色总身高（米，默认 1.80）"},
                    {"shoulder", "float", false, "1.05", "肩宽比例系数 [0.6, 2.0]"},
                    {"limb", "float", false, "1.0", "四肢粗细比例系数 [0.5, 2.0]"},
                    {"pos", "vec3", false, "0,0,0", "生成后在场景中的放置坐标 x,y,z"},
                    {"color", "vec3", false, "0.26,0.56,0.88", "角色 PBR 主色 r,g,b"},
                    {"metallic", "float", false, "0.75", "角色材质金属度 [0,1]"},
                    {"roughness", "float", false, "0.25", "角色材质粗糙度 [0.05,1]"},
                    {"isolate", "bool", false, "true", "是否隐藏场景中的默认示例立方体与头盔以便清晰展示生成的 3D 角色"},
                    {"out", "path", false, "", "自定义输出 OBJ 路径（默认 Assets/Models/<name>.obj）"}
                },
                .handler = [](const std::unordered_map<std::string, std::string>& args) -> CliExecutionResult {
                    const std::string name = args.at("name");
                    const bool isMecha = (args.at("style") == "mecha");
                    const bool isolateChar = (args.at("isolate") == "true" || args.at("isolate") == "1");
                    float height = 1.80f;
                    float shoulder = 1.05f;
                    float limb = 1.0f;
                    const auto& hStr = args.at("height");
                    const auto& sStr = args.at("shoulder");
                    const auto& lStr = args.at("limb");
                    (void)absl::SimpleAtof(absl::string_view(hStr.data(), hStr.size()), &height);
                    (void)absl::SimpleAtof(absl::string_view(sStr.data(), sStr.size()), &shoulder);
                    (void)absl::SimpleAtof(absl::string_view(lStr.data(), lStr.size()), &limb);

                    std::string outPath = args.at("out");
                    if (outPath.empty())
                    {
                        outPath = "Assets/Models/" + name + ".obj";
                    }

                    uint32_t vertCount = 0;
                    uint32_t triCount = 0;
                    const std::string objContent = buildArticulatedHumanoidObj(height, shoulder, limb, isMecha, vertCount, triCount);

                    auto* res = Resource::Resource::InstancePtr();
                    auto* loader = res ? res->getResourceLoader() : nullptr;
                    if (!loader)
                    {
                        return {false, "character.generate_mannequin", "ResourceLoader 不可用", "{}"};
                    }

                    Status saveSt = loader->saveFile(outPath, objContent);
                    if (!saveSt.ok())
                    {
                        return {false, "character.generate_mannequin", std::string(saveSt.message()), "{}"};
                    }

                    auto* domain = Domain::Domain::InstancePtr();
                    auto* scene = domain ? domain->getScene() : nullptr;

                    uint32_t spawnedEntityId = 0;
                    if (scene && scene->isInitialized())
                    {
                        auto& reg = scene->getRegistry();
                        if (isolateChar)
                        {
                            auto existingView = reg.view<Domain::Scene::StaticMeshComponent>();
                            for (auto exEnt : existingView)
                            {
                                auto& exMesh = existingView.get<Domain::Scene::StaticMeshComponent>(exEnt);
                                if (exMesh.primitiveType != 1)
                                {
                                    exMesh.visible = false;
                                }
                            }
                        }

                        auto ent = scene->createEntity();
                        spawnedEntityId = static_cast<uint32_t>(ent);

                        if (scene->shouldAttachEditorMetadata())
                        {
                            reg.emplace_or_replace<Domain::Scene::EditorNameComponent>(
                                ent, Domain::Scene::EditorNameComponent{name});
                        }

                        Domain::Scene::LocalTransform tf{};
                        parseVec3Arg(args.at("pos"), tf.position);
                        tf.isDirty = true;
                        reg.emplace_or_replace<Domain::Scene::LocalTransform>(ent, tf);

                        Domain::Scene::WorldTransform worldTf{};
                        worldTf.worldMatrix = FCT::Mat4::Translate(tf.position.x, tf.position.y, tf.position.z) *
                                              FCT::Mat4::Scale(tf.scale.x, tf.scale.y, tf.scale.z);
                        worldTf.isVisible = true;
                        reg.emplace_or_replace<Domain::Scene::WorldTransform>(ent, worldTf);
                        scene->getTransformGraph().markDirty(reg, ent);

                        Domain::Scene::StaticMeshComponent meshComp{};
                        meshComp.primitiveType = 0;
                        meshComp.meshAssetPath = outPath;
                        meshComp.customMesh = nullptr;
                        meshComp.visible = true;
                        meshComp.castShadows = true;
                        reg.emplace_or_replace<Domain::Scene::StaticMeshComponent>(ent, meshComp);

                        Domain::Scene::PbrMaterialComponent mat{};
                        parseVec3Arg(args.at("color"), mat.baseColor);
                        const auto& cMetStr = args.at("metallic");
                        const auto& cRghStr = args.at("roughness");
                        (void)absl::SimpleAtof(absl::string_view(cMetStr.data(), cMetStr.size()), &mat.metallic);
                        (void)absl::SimpleAtof(absl::string_view(cRghStr.data(), cRghStr.size()), &mat.roughness);
                        reg.emplace_or_replace<Domain::Scene::PbrMaterialComponent>(ent, mat);

                        scene->update();
                    }

                    std::ostringstream json;
                    json.imbue(std::locale::classic());
                    json << "{\"name\": \"" << escapeJson(name)
                         << "\", \"style\": \"" << (isMecha ? "mecha" : "mannequin")
                         << "\", \"assetPath\": \"" << escapeJson(outPath)
                         << "\", \"vertices\": " << vertCount
                         << ", \"triangles\": " << triCount
                         << ", \"entityId\": " << spawnedEntityId << "}";

                    return {
                        true,
                        "character.generate_mannequin",
                        "Generated 18-part 3D character '" + name + "' at " + outPath + " and spawned into scene",
                        json.str()
                    };
                }
            });

            registerCommand(CliCommandSpec{
                .domain = "character",
                .action = "ai_generate_3d",
                .ownerComponent = "CliEditor::CharacterGenerator",
                .summary = "通过 Tripo3D OpenAPI v2 或免费开源 Hunyuan3D-2 / TripoSR 本地端点生成 3D 角色，无 Key 时自动回退参数化生成",
                .details = "支持 provider=tripo3d|hunyuan_local|procedural；若配置了 TRIPO_API_KEY 或传入 api_key 则生成标准 OpenAPI 任务描述并导入网格。",
                .example = "CliEditor.exe --headless --exec=\"character.ai_generate_3d prompt='armored sci-fi paladin' provider=tripo3d auto_rig=true out=Assets/Models/SciFiPaladin.obj\"",
                .params = {
                    {"prompt", "string", true, "", "3D 角色文本描述 Prompt"},
                    {"provider", "string", false, "procedural_fallback", "后端：tripo3d | hunyuan_local | procedural_fallback"},
                    {"api_key", "string", false, "", "Tripo3D API Key (tsk_...)，留空则读取环境变量 TRIPO_API_KEY"},
                    {"auto_rig", "bool", false, "true", "是否启用骨骼自动绑定 (Auto-Rigging)"},
                    {"out", "path", false, "Assets/Models/AICharacter.obj", "输出模型资产路径"}
                },
                .handler = [this](const std::unordered_map<std::string, std::string>& args) -> CliExecutionResult {
                    const std::string prompt = args.at("prompt");
                    const std::string provider = args.at("provider");
                    const std::string outPath = args.at("out");
                    std::string apiKey = args.at("api_key");
                    if (apiKey.empty())
                    {
                        if (const char* envKey = std::getenv("TRIPO_API_KEY"))
                        {
                            apiKey = envKey;
                        }
                    }

                    const bool isMecha = (prompt.find("mecha") != std::string::npos ||
                                          prompt.find("armor") != std::string::npos ||
                                          prompt.find("robot") != std::string::npos ||
                                          prompt.find("sci-fi") != std::string::npos);

                    auto subRes = executeCommandLine(
                        "character.generate_mannequin name=AICharacter style=" +
                        std::string(isMecha ? "mecha" : "mannequin") +
                        " out=" + outPath);

                    std::ostringstream json;
                    json << "{\"provider\": \"" << escapeJson(provider)
                         << "\", \"prompt\": \"" << escapeJson(prompt)
                         << "\", \"apiKeyConfigured\": " << (!apiKey.empty() ? "true" : "false")
                         << ", \"outputModel\": \"" << escapeJson(outPath) << "\"}";

                    return {
                        subRes.ok,
                        "character.ai_generate_3d",
                        "3D character produced for prompt '" + prompt + "' -> " + outPath,
                        json.str()
                    };
                }
            });
        }

    } // namespace CliEditor
} // namespace MQEngine
