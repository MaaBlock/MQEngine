#ifndef ENGINE_EDITOR_UNDOMANAGER_H
#define ENGINE_EDITOR_UNDOMANAGER_H

#include <Engine/thirdparty/thirdparty.h>
#include "ICommand.h"
#include <deque>

namespace MQEngine {

    /**
     * @brief 编辑器撤销/重做命令历史栈管理器
     */
    class UndoManager {
    public:
        static constexpr size_t kDefaultMaxHistory = 100;

        explicit UndoManager(size_t maxHistory = kDefaultMaxHistory);
        ~UndoManager() = default;

        UndoManager(const UndoManager&) = delete;
        UndoManager& operator=(const UndoManager&) = delete;
        UndoManager(UndoManager&&) noexcept = default;
        UndoManager& operator=(UndoManager&&) noexcept = default;

        /**
         * @brief 执行命令并将其压入撤销栈，同时清空重做栈
         */
        Status executeCommand(UniquePtr<ICommand> cmd);

        /**
         * @brief 接收已完成执行的交互命令并压入撤销栈，清空重做栈
         */
        void pushExecutedCommand(UniquePtr<ICommand> cmd);

        /**
         * @brief 撤销最近一次命令
         */
        Status undo();

        /**
         * @brief 重做最近一次撤销的命令
         */
        Status redo();

        /**
         * @brief 是否可撤销
         */
        bool canUndo() const noexcept {
            return !m_undoStack.empty();
        }

        /**
         * @brief 是否可重做
         */
        bool canRedo() const noexcept {
            return !m_redoStack.empty();
        }

        /**
         * @brief 获取当前撤销栈命令数量
         */
        size_t getUndoCount() const noexcept {
            return m_undoStack.size();
        }

        /**
         * @brief 获取当前撤销栈命令数量（别名）
         */
        size_t getUndoStackSize() const noexcept {
            return getUndoCount();
        }

        /**
         * @brief 获取当前重做栈命令数量
         */
        size_t getRedoCount() const noexcept {
            return m_redoStack.size();
        }

        /**
         * @brief 获取当前重做栈命令数量（别名）
         */
        size_t getRedoStackSize() const noexcept {
            return getRedoCount();
        }

        /**
         * @brief 获取下一个可撤销命令的名称
         */
        std::string_view getUndoCommandName() const noexcept {
            return m_undoStack.empty() ? std::string_view{} : m_undoStack.back()->getName();
        }

        /**
         * @brief 获取下一个可重做命令的名称
         */
        std::string_view getRedoCommandName() const noexcept {
            return m_redoStack.empty() ? std::string_view{} : m_redoStack.back()->getName();
        }

        /**
         * @brief 获取最大历史步数
         */
        size_t getMaxHistory() const noexcept {
            return m_maxHistory;
        }

        /**
         * @brief 动态调整历史步数容量上限
         */
        void setMaxHistory(size_t maxHistory);

        /**
         * @brief 清空撤销与重做栈
         */
        void clear() noexcept;

    private:
        std::deque<UniquePtr<ICommand>> m_undoStack;
        std::deque<UniquePtr<ICommand>> m_redoStack;
        size_t m_maxHistory;
    };

} // namespace MQEngine

#endif // ENGINE_EDITOR_UNDOMANAGER_H
