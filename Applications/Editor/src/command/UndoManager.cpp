#include "UndoManager.h"

namespace MQEngine {

    UndoManager::UndoManager(size_t maxHistory)
        : m_maxHistory(maxHistory == 0 ? 1 : maxHistory) {
    }

    Status UndoManager::executeCommand(UniquePtr<ICommand> cmd) {
        if (!cmd) {
            return InvalidArgumentError("Cannot execute null command in UndoManager");
        }

        CHECK_STATUS(cmd->execute());

        m_undoStack.push_back(std::move(cmd));
        while (m_undoStack.size() > m_maxHistory) {
            m_undoStack.pop_front();
        }

        m_redoStack.clear();
        return OkStatus();
    }

    void UndoManager::pushExecutedCommand(UniquePtr<ICommand> cmd) {
        if (!cmd) {
            return;
        }

        m_undoStack.push_back(std::move(cmd));
        while (m_undoStack.size() > m_maxHistory) {
            m_undoStack.pop_front();
        }

        m_redoStack.clear();
    }

    Status UndoManager::undo() {
        if (m_undoStack.empty()) {
            return absl::FailedPreconditionError("Undo stack is empty");
        }

        UniquePtr<ICommand> cmd = std::move(m_undoStack.back());
        m_undoStack.pop_back();

        Status status = cmd->undo();
        if (!status.ok()) {
            m_undoStack.push_back(std::move(cmd));
            return status;
        }

        m_redoStack.push_back(std::move(cmd));
        return OkStatus();
    }

    Status UndoManager::redo() {
        if (m_redoStack.empty()) {
            return absl::FailedPreconditionError("Redo stack is empty");
        }

        UniquePtr<ICommand> cmd = std::move(m_redoStack.back());
        m_redoStack.pop_back();

        Status status = cmd->redo();
        if (!status.ok()) {
            m_redoStack.push_back(std::move(cmd));
            return status;
        }

        m_undoStack.push_back(std::move(cmd));
        while (m_undoStack.size() > m_maxHistory) {
            m_undoStack.pop_front();
        }

        return OkStatus();
    }

    void UndoManager::setMaxHistory(size_t maxHistory) {
        m_maxHistory = (maxHistory == 0) ? 1 : maxHistory;
        while (m_undoStack.size() > m_maxHistory) {
            m_undoStack.pop_front();
        }
        while (m_redoStack.size() > m_maxHistory) {
            m_redoStack.pop_front();
        }
    }

    void UndoManager::clear() noexcept {
        m_undoStack.clear();
        m_redoStack.clear();
    }

} // namespace MQEngine
