#ifndef ENGINE_EDITOR_ICOMMAND_H
#define ENGINE_EDITOR_ICOMMAND_H

#include <Engine/thirdparty/thirdparty.h>

namespace MQEngine {

    /**
     * @brief 编辑器可撤销/重做命令抽象接口基类
     */
    class ICommand {
    public:
        virtual ~ICommand() = default;

        /**
         * @brief 执行命令
         */
        virtual Status execute() = 0;

        /**
         * @brief 撤销命令
         */
        virtual Status undo() = 0;

        /**
         * @brief 重做命令，默认行为与 execute 相同
         */
        virtual Status redo() {
            return execute();
        }

        /**
         * @brief 获取命令名称，供 UI 菜单显示及调试检视
         */
        virtual std::string_view getName() const = 0;
    };

} // namespace MQEngine

#endif // ENGINE_EDITOR_ICOMMAND_H
