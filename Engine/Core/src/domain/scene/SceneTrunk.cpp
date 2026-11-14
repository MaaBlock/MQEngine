/**
 * @file SceneTrunk.cpp
 * @brief 分块子场景实现
 */
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/scene/SceneTrunk.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/resource/core/Resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/runtime/core/Engine.h>
#include <cassert>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            SceneTrunk::SceneTrunk(std::string name)
                : m_name(std::move(name)),
                  m_registry(makeUnique<FCT::ECS::registry>())
            {
            }

            SceneTrunk::~SceneTrunk() = default;

            Status SceneTrunk::load(const std::string& directoryPath)
            {
                assert(m_registry != nullptr && "SceneTrunk Registry 不能为空");
                auto* res = Resource::Resource::InstancePtr();
                ResourceLoader* loader = res ? res->getResourceLoader() : nullptr;
                if (!loader) {
                    return FailedPreconditionError("ResourceLoader 未就绪");
                }
                if (!directoryPath.empty() && !loader->exists(directoryPath)) {
                    (void)loader->makeDir(directoryPath);
                }
                const std::string filePath = directoryPath.empty()
                    ? (m_name + ".mqtrunk")
                    : (directoryPath + "/" + m_name + ".mqtrunk");

                if (!loader->exists(filePath)) {
                    return NotFoundError(StrCat("SceneTrunk 文件不存在: ", filePath));
                }

                auto contentOr = loader->loadFile(filePath);
                CHECK_STATUS(contentOr);

                boost::system::error_code ec;
                boost::json::value jv = boost::json::parse(*contentOr, ec);
                if (ec || !jv.is_object()) {
                    return InvalidArgumentError("SceneTrunk JSON 格式无效: " + filePath);
                }

                m_registry->clear();
                bool attachEditorMetadata = false;
                if (const auto* engine = Runtime::Engine::getInstance()) {
                    attachEditorMetadata =
                        engine->hasApplicationTag("Editor") || engine->hasApplicationTag("CliEditor");
                } else if (const auto* domain = Domain::InstancePtr(); domain && domain->getScene()) {
                    attachEditorMetadata = domain->getScene()->shouldAttachEditorMetadata();
                }

                const auto& rootObj = jv.as_object();
                if (auto it = rootObj.find("entities"); it != rootObj.end() && it->value().is_array()) {
                    int idx = 0;
                    for (const auto& item : it->value().as_array()) {
                        if (!item.is_object()) continue;
                        const auto& obj = item.as_object();

                        auto entity = m_registry->create();
                        if (attachEditorMetadata) {
                            auto& editorName = m_registry->emplace<EditorNameComponent>(entity);
                            if (auto nIt = obj.find("name"); nIt != obj.end() && nIt->value().is_string()) {
                                editorName.name = std::string(nIt->value().as_string().c_str());
                            } else {
                                editorName.name = m_name + "_Entity_" + std::to_string(idx);
                            }
                        }
                        if (auto uIt = obj.find("uuid");
                            uIt != obj.end() && uIt->value().is_string() && !uIt->value().as_string().empty()) {
                            m_registry->emplace<UuidComponent>(
                                entity, UuidComponent{std::string(uIt->value().as_string().c_str())});
                        }
                        if (auto aIt = obj.find("active");
                            aIt != obj.end() && aIt->value().is_bool() && !aIt->value().as_bool()) {
                            m_registry->emplace<DisabledTag>(entity);
                        }

                        auto& rel = m_registry->emplace<HierarchyRelation>(entity);
                        auto& local = m_registry->emplace<LocalTransform>(entity);
                        auto& world = m_registry->emplace<WorldTransform>(entity);

                        if (auto dIt = obj.find("depth"); dIt != obj.end() && dIt->value().is_int64()) {
                            rel.depth = static_cast<uint32_t>(dIt->value().as_int64());
                        }
                        if (auto pIt = obj.find("pos"); pIt != obj.end() && pIt->value().is_array() && pIt->value().as_array().size() == 3) {
                            const auto& arr = pIt->value().as_array();
                            local.position.x = static_cast<float>(arr[0].to_number<double>());
                            local.position.y = static_cast<float>(arr[1].to_number<double>());
                            local.position.z = static_cast<float>(arr[2].to_number<double>());
                        }
                        if (auto rIt = obj.find("rot"); rIt != obj.end() && rIt->value().is_array() && rIt->value().as_array().size() == 3) {
                            const auto& arr = rIt->value().as_array();
                            local.rotation.x = static_cast<float>(arr[0].to_number<double>());
                            local.rotation.y = static_cast<float>(arr[1].to_number<double>());
                            local.rotation.z = static_cast<float>(arr[2].to_number<double>());
                        }
                        if (auto sIt = obj.find("scale"); sIt != obj.end() && sIt->value().is_array() && sIt->value().as_array().size() == 3) {
                            const auto& arr = sIt->value().as_array();
                            local.scale.x = static_cast<float>(arr[0].to_number<double>());
                            local.scale.y = static_cast<float>(arr[1].to_number<double>());
                            local.scale.z = static_cast<float>(arr[2].to_number<double>());
                        }
                        local.isDirty = true;
                        world.isVisible = true;

                        auto& mesh = m_registry->emplace<StaticMeshComponent>(entity);
                        mesh.primitiveType = (idx % 2 == 0) ? 0 : 2;
                        mesh.meshAssetPath = (mesh.primitiveType == 0) ? "Builtin_Cube" : "Builtin_Sphere";
                        mesh.visible = true;

                        auto& mat = m_registry->emplace<PbrMaterialComponent>(entity);
                        mat.baseColor = FCT::Vec3<>(0.35f + 0.15f * (idx % 4), 0.65f, 0.45f);
                        mat.metallic = 0.4f;
                        mat.roughness = 0.35f;
                        ++idx;
                    }
                }

                TransformGraph tg;
                tg.evaluateParallel(*m_registry);
                m_loaded = true;
                return OkStatus();
            }

            Status SceneTrunk::save(const std::string& directoryPath)
            {
                assert(m_registry != nullptr && "SceneTrunk Registry 不能为空");
                auto* res = Resource::Resource::InstancePtr();
                ResourceLoader* loader = res ? res->getResourceLoader() : nullptr;
                if (!loader) {
                    return FailedPreconditionError("ResourceLoader 未就绪");
                }
                if (!directoryPath.empty() && !loader->exists(directoryPath)) {
                    (void)loader->makeDir(directoryPath);
                }
                const std::string filePath = directoryPath.empty()
                    ? (m_name + ".mqtrunk")
                    : (directoryPath + "/" + m_name + ".mqtrunk");

                boost::json::object rootObj;
                rootObj["trunkName"] = m_name;

                boost::json::array entitiesArr;
                auto view = m_registry->view<LocalTransform, WorldTransform>();
                for (auto entity : view) {
                    const auto& local = view.get<LocalTransform>(entity);
                    boost::json::object obj;
                    obj["id"] = static_cast<uint32_t>(FCT::ECS::to_integral(entity));
                    if (const auto* nameComp = m_registry->try_get<EditorNameComponent>(entity)) {
                        obj["name"] = nameComp->name;
                    }
                    if (const auto* uuidComp = m_registry->try_get<UuidComponent>(entity);
                        uuidComp && !uuidComp->uuid.empty()) {
                        obj["uuid"] = uuidComp->uuid;
                    }
                    if (m_registry->all_of<DisabledTag>(entity)) {
                        obj["active"] = false;
                    }
                    if (const auto* rel = m_registry->try_get<HierarchyRelation>(entity)) {
                        obj["depth"] = rel->depth;
                    } else {
                        obj["depth"] = 0;
                    }
                    obj["pos"] = boost::json::array{local.position.x, local.position.y, local.position.z};
                    obj["rot"] = boost::json::array{local.rotation.x, local.rotation.y, local.rotation.z};
                    obj["scale"] = boost::json::array{local.scale.x, local.scale.y, local.scale.z};
                    entitiesArr.push_back(std::move(obj));
                }
                rootObj["entities"] = std::move(entitiesArr);

                std::string serialized = boost::json::serialize(rootObj);
                CHECK_STATUS(loader->saveFile(filePath, serialized));
                return OkStatus();
            }

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine
