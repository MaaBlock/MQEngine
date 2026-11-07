#ifndef ENGINE_EDITOR_TRANSLATEENTITYCOMMAND_H
#define ENGINE_EDITOR_TRANSLATEENTITYCOMMAND_H

#include <Engine/thirdparty/thirdparty.h>
#include "ICommand.h"

namespace MQEngine {

    class MatrixCacheSystem;

    /**
     * @brief 实体位置平移操作命令，记录初始坐标与最终坐标，支持双向撤销与重做
     */
    class TranslateEntityCommand : public ICommand {
    public:
        TranslateEntityCommand(entt::registry* registry, entt::entity entity,
                               const FCT::Vec3<>& initialPosition, const FCT::Vec3<>& finalPosition,
                               MatrixCacheSystem* matrixCacheSystem = nullptr);
        ~TranslateEntityCommand() override = default;

        Status execute() override;
        Status undo() override;
        Status redo() override;

        std::string_view getName() const override {
            return "移动实体";
        }

        entt::entity getEntity() const noexcept {
            return m_entity;
        }

        const FCT::Vec3<>& getInitialPosition() const noexcept {
            return m_initialPosition;
        }

        const FCT::Vec3<>& getFinalPosition() const noexcept {
            return m_finalPosition;
        }

        entt::registry* getRegistry() const noexcept {
            return m_registry;
        }

    private:
        Status applyPosition(const FCT::Vec3<>& targetPosition);

        entt::registry* m_registry = nullptr;
        entt::entity m_entity = entt::null;
        FCT::Vec3<> m_initialPosition;
        FCT::Vec3<> m_finalPosition;
        MatrixCacheSystem* m_matrixCacheSystem = nullptr;
    };

} // namespace MQEngine

#endif // ENGINE_EDITOR_TRANSLATEENTITYCOMMAND_H
