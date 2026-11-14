/**
 * @file CommandStack.h
 * @brief 编辑器通用撤销/重做命令栈
 */
#ifndef MQENGINE_EDITOR_BASE_COMMAND_STACK_H
#define MQENGINE_EDITOR_BASE_COMMAND_STACK_H

#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/resource/table/SchemaTable.h>
#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/runtime/interface/Application.h>

#include <string>
#include <vector>
#include <functional>
#include <cassert>

namespace MQEngine
{
    namespace EditorBase
    {
        inline bool IsEditorEnglishLocale() noexcept
        {
            if (auto* eng = Runtime::Engine::getInstance(); eng && eng->application)
            {
                return eng->application->applicationRegisterInfo.local == FCT::Local::en_US;
            }
            return false;
        }
        /**
         * @brief 可撤销命令接口
         */
        class IUndoableCommand
        {
        public:
            virtual ~IUndoableCommand() = default;
            virtual void execute() = 0;
            virtual void undo() = 0;
            virtual std::string getName() const = 0;
        };

        /**
         * @brief C++20 泛型 ECS 组件修改命令（ComponentModifyCommand<TComponent>）：
         *        利用编译期类型名提取与快照替换，对任意聚合 ECS 组件提供统一 Undo/Redo 支持，
         *        新增任何组件均无需重复编写专属 Command 子类。
         */
        template <typename TComponent>
        class ComponentModifyCommand : public IUndoableCommand
        {
        public:
            using SyncCallback = std::function<void(const TComponent&)>;

            ComponentModifyCommand(
                FCT::ECS::registry* registry,
                FCT::ECS::entity entity,
                TComponent oldState,
                TComponent newState,
                std::string fieldLabel = "",
                SyncCallback onSync = nullptr)
                : m_registry(registry)
                , m_entity(entity)
                , m_oldState(std::move(oldState))
                , m_newState(std::move(newState))
                , m_onSync(std::move(onSync))
            {
                const std::string compName = Resource::ExtractShortTypeName<TComponent>();
                const bool isEn = IsEditorEnglishLocale();
                if (fieldLabel.empty())
                {
                    m_commandName = (isEn ? "Modify " : "修改组件 ") + compName;
                }
                else
                {
                    m_commandName = (isEn ? "Modify " : "修改组件 ") + compName + "::" + fieldLabel;
                }
            }

            void execute() override
            {
                if (m_registry && m_entity != FCT::ECS::null && m_registry->valid(m_entity))
                {
                    m_registry->emplace_or_replace<TComponent>(m_entity, m_newState);
                }
                if (m_onSync)
                {
                    m_onSync(m_newState);
                }
            }

            void undo() override
            {
                if (m_registry && m_entity != FCT::ECS::null && m_registry->valid(m_entity))
                {
                    m_registry->emplace_or_replace<TComponent>(m_entity, m_oldState);
                }
                if (m_onSync)
                {
                    m_onSync(m_oldState);
                }
            }

            std::string getName() const override
            {
                return m_commandName;
            }

        private:
            FCT::ECS::registry* m_registry = nullptr;
            FCT::ECS::entity m_entity = FCT::ECS::null;
            TComponent m_oldState{};
            TComponent m_newState{};
            std::string m_commandName;
            SyncCallback m_onSync;
        };

        /**
         * @brief C++20 泛型 ECS 组件挂载/卸载可撤销命令（ComponentAddRemoveCommand<TComponent>）
         */
        template <typename TComponent>
        class ComponentAddRemoveCommand : public IUndoableCommand
        {
        public:
            ComponentAddRemoveCommand(
                FCT::ECS::registry* registry,
                FCT::ECS::entity entity,
                bool isAddAction,
                TComponent initialOrSavedState = TComponent{},
                std::function<void()> onSync = nullptr)
                : m_registry(registry)
                , m_entity(entity)
                , m_isAddAction(isAddAction)
                , m_state(std::move(initialOrSavedState))
                , m_onSync(std::move(onSync))
            {
                const std::string compName = Resource::ExtractShortTypeName<TComponent>();
                const bool isEn = IsEditorEnglishLocale();
                if (isEn)
                {
                    m_commandName = (m_isAddAction ? "Add " : "Remove ") + compName;
                }
                else
                {
                    m_commandName = (m_isAddAction ? "添加组件 " : "移除组件 ") + compName;
                }
            }

            void execute() override
            {
                applyState(m_isAddAction);
            }

            void undo() override
            {
                applyState(!m_isAddAction);
            }

            std::string getName() const override
            {
                return m_commandName;
            }

        private:
            void applyState(bool shouldExist)
            {
                if (m_registry && m_entity != FCT::ECS::null && m_registry->valid(m_entity))
                {
                    if (shouldExist)
                    {
                        m_registry->emplace_or_replace<TComponent>(m_entity, m_state);
                    }
                    else if (m_registry->all_of<TComponent>(m_entity))
                    {
                        m_state = m_registry->get<TComponent>(m_entity);
                        m_registry->remove<TComponent>(m_entity);
                    }
                }
                if (m_onSync)
                {
                    m_onSync();
                }
            }

        private:
            FCT::ECS::registry* m_registry = nullptr;
            FCT::ECS::entity m_entity = FCT::ECS::null;
            bool m_isAddAction = true;
            TComponent m_state{};
            std::string m_commandName;
            std::function<void()> m_onSync;
        };

        /**
         * @brief 类型擦除的动态组件内存快照容器（DynamicComponentBlob）：
         *        利用 TypeDescriptor 内置的 defaultConstruct / copyConstruct / destruct 函数指针管理任意组件（含 std::string 等非平凡成员）的生命周期，
         *        使编辑器无需在编译期获知插件组件的 C++ 类型即可安全保存与恢复任意插件组件快照。
         */
        class DynamicComponentBlob
        {
        public:
            DynamicComponentBlob() = default;

            DynamicComponentBlob(const Resource::TypeDescriptor* desc, const void* srcInstance = nullptr)
                : m_desc(desc)
            {
                if (m_desc && m_desc->typeSize > 0)
                {
                    m_storage.resize(m_desc->typeSize, 0);
                    if (srcInstance && m_desc->copyConstruct)
                    {
                        m_desc->copyConstruct(m_storage.data(), srcInstance);
                        m_constructed = true;
                    }
                    else if (m_desc->defaultConstruct)
                    {
                        m_desc->defaultConstruct(m_storage.data());
                        m_constructed = true;
                    }
                    else if (srcInstance)
                    {
                        std::memcpy(m_storage.data(), srcInstance, m_desc->typeSize);
                    }
                }
            }

            ~DynamicComponentBlob()
            {
                reset();
            }

            DynamicComponentBlob(const DynamicComponentBlob& other)
            {
                copyFrom(other);
            }

            DynamicComponentBlob& operator=(const DynamicComponentBlob& other)
            {
                if (this != &other)
                {
                    reset();
                    copyFrom(other);
                }
                return *this;
            }

            DynamicComponentBlob(DynamicComponentBlob&& other) noexcept
            {
                moveFrom(std::move(other));
            }

            DynamicComponentBlob& operator=(DynamicComponentBlob&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    moveFrom(std::move(other));
                }
                return *this;
            }

            void reset() noexcept
            {
                if (m_constructed && m_desc && m_desc->destruct && !m_storage.empty())
                {
                    m_desc->destruct(m_storage.data());
                }
                m_storage.clear();
                m_desc = nullptr;
                m_constructed = false;
            }

            bool valid() const noexcept { return m_desc != nullptr && !m_storage.empty(); }
            const void* data() const noexcept { return m_storage.empty() ? nullptr : m_storage.data(); }
            void* data() noexcept { return m_storage.empty() ? nullptr : m_storage.data(); }
            const Resource::TypeDescriptor* descriptor() const noexcept { return m_desc; }

        private:
            void copyFrom(const DynamicComponentBlob& other)
            {
                m_desc = other.m_desc;
                if (m_desc && other.valid())
                {
                    m_storage.resize(m_desc->typeSize, 0);
                    if (m_desc->copyConstruct)
                    {
                        m_desc->copyConstruct(m_storage.data(), other.data());
                        m_constructed = true;
                    }
                    else
                    {
                        std::memcpy(m_storage.data(), other.data(), m_desc->typeSize);
                    }
                }
            }

            void moveFrom(DynamicComponentBlob&& other) noexcept
            {
                m_desc = other.m_desc;
                m_storage = std::move(other.m_storage);
                m_constructed = other.m_constructed;
                other.m_desc = nullptr;
                other.m_constructed = false;
            }

        private:
            const Resource::TypeDescriptor* m_desc = nullptr;
            std::vector<uint8_t> m_storage;
            bool m_constructed = false;
        };

        /**
         * @brief 运行时类型擦除的通用组件修改命令（DynamicComponentModifyCommand）：
         *        通过 SchemaTable / TypeDescriptor 的类型擦除函数指针操作任意 ECS 组件，
         *        无论是引擎内置组件还是外部插件（Plugin DLL）动态注册的组件，均使用同一个命令类完成 Undo/Redo。
         */
        class DynamicComponentModifyCommand : public IUndoableCommand
        {
        public:
            DynamicComponentModifyCommand(
                FCT::ECS::registry* registry,
                FCT::ECS::entity entity,
                const Resource::TypeDescriptor* desc,
                DynamicComponentBlob oldState,
                DynamicComponentBlob newState,
                const std::string& fieldLabel = "",
                std::function<void()> onSync = nullptr)
                : m_registry(registry)
                , m_entity(entity)
                , m_desc(desc)
                , m_oldState(std::move(oldState))
                , m_newState(std::move(newState))
                , m_onSync(std::move(onSync))
            {
                const std::string compName = m_desc ? m_desc->typeName : "UnknownComponent";
                const bool isEn = IsEditorEnglishLocale();
                if (fieldLabel.empty())
                {
                    m_commandName = (isEn ? "Modify " : "修改组件 ") + compName;
                }
                else
                {
                    m_commandName = (isEn ? "Modify " : "修改组件 ") + compName + "::" + fieldLabel;
                }
            }

            void execute() override
            {
                if (m_registry && m_desc && m_desc->ecsEmplaceOrReplace && m_newState.valid())
                {
                    m_desc->ecsEmplaceOrReplace(*m_registry, m_entity, m_newState.data());
                }
                if (m_onSync)
                {
                    m_onSync();
                }
            }

            void undo() override
            {
                if (m_registry && m_desc && m_desc->ecsEmplaceOrReplace && m_oldState.valid())
                {
                    m_desc->ecsEmplaceOrReplace(*m_registry, m_entity, m_oldState.data());
                }
                if (m_onSync)
                {
                    m_onSync();
                }
            }

            std::string getName() const override
            {
                return m_commandName;
            }

        private:
            FCT::ECS::registry* m_registry = nullptr;
            FCT::ECS::entity m_entity = FCT::ECS::null;
            const Resource::TypeDescriptor* m_desc = nullptr;
            DynamicComponentBlob m_oldState;
            DynamicComponentBlob m_newState;
            std::string m_commandName;
            std::function<void()> m_onSync;
        };

        /**
         * @brief 运行时类型擦除的通用组件挂载/卸载命令（DynamicComponentAddRemoveCommand）：
         *        支持任意插件动态注册组件的添加与删除撤销重做。
         */
        class DynamicComponentAddRemoveCommand : public IUndoableCommand
        {
        public:
            DynamicComponentAddRemoveCommand(
                FCT::ECS::registry* registry,
                FCT::ECS::entity entity,
                const Resource::TypeDescriptor* desc,
                bool isAddAction,
                DynamicComponentBlob initialOrSavedBlob = {},
                std::function<void()> onSync = nullptr)
                : m_registry(registry)
                , m_entity(entity)
                , m_desc(desc)
                , m_isAddAction(isAddAction)
                , m_savedBlob(std::move(initialOrSavedBlob))
                , m_onSync(std::move(onSync))
            {
                const std::string compName = m_desc ? m_desc->typeName : "UnknownComponent";
                const bool isEn = IsEditorEnglishLocale();
                if (isEn)
                {
                    m_commandName = (m_isAddAction ? "Add " : "Remove ") + compName;
                }
                else
                {
                    m_commandName = (m_isAddAction ? "添加组件 " : "移除组件 ") + compName;
                }
            }

            void execute() override
            {
                applyState(m_isAddAction);
            }

            void undo() override
            {
                applyState(!m_isAddAction);
            }

            std::string getName() const override
            {
                return m_commandName;
            }

        private:
            void applyState(bool shouldExist)
            {
                if (m_registry && m_desc && m_entity != FCT::ECS::null && m_registry->valid(m_entity))
                {
                    if (shouldExist)
                    {
                        if (m_desc->ecsEmplaceOrReplace)
                        {
                            m_desc->ecsEmplaceOrReplace(
                                *m_registry,
                                m_entity,
                                m_savedBlob.valid() ? m_savedBlob.data() : nullptr);
                        }
                    }
                    else
                    {
                        if (m_desc->ecsGetConst)
                        {
                            if (const void* currentPtr = m_desc->ecsGetConst(*m_registry, m_entity))
                            {
                                m_savedBlob = DynamicComponentBlob(m_desc, currentPtr);
                            }
                        }
                        if (m_desc->ecsRemove)
                        {
                            m_desc->ecsRemove(*m_registry, m_entity);
                        }
                    }
                }
                if (m_onSync)
                {
                    m_onSync();
                }
            }

        private:
            FCT::ECS::registry* m_registry = nullptr;
            FCT::ECS::entity m_entity = FCT::ECS::null;
            const Resource::TypeDescriptor* m_desc = nullptr;
            bool m_isAddAction = true;
            DynamicComponentBlob m_savedBlob;
            std::string m_commandName;
            std::function<void()> m_onSync;
        };

        /**
         * @brief 撤销/重做命令历史栈
         */
        class CommandStack
        {
        public:
            using CommandEventCallback = std::function<void(const std::string& actionType, const std::string& commandName)>;

            CommandStack() = default;
            ~CommandStack() = default;

            CommandStack(const CommandStack&) = delete;
            CommandStack& operator=(const CommandStack&) = delete;

            void setEventCallback(CommandEventCallback callback)
            {
                m_eventCallback = std::move(callback);
            }

            void executeCommand(UniquePtr<IUndoableCommand> command)
            {
                assert(command != nullptr && "不可执行空命令");
                const std::string cmdName = command->getName();
                command->execute();
                m_undoStack.push_back(std::move(command));
                m_redoStack.clear();

                if (m_undoStack.size() > m_maxHistorySize)
                {
                    m_undoStack.erase(m_undoStack.begin());
                }

                if (m_eventCallback)
                {
                    m_eventCallback("Execute", cmdName);
                }
            }

            bool undo()
            {
                if (m_undoStack.empty())
                {
                    if (m_eventCallback)
                    {
                        m_eventCallback("UndoEmpty", "");
                    }
                    return false;
                }
                auto cmd = std::move(m_undoStack.back());
                m_undoStack.pop_back();
                const std::string cmdName = cmd->getName();
                cmd->undo();
                m_redoStack.push_back(std::move(cmd));
                if (m_eventCallback)
                {
                    m_eventCallback("Undo", cmdName);
                }
                return true;
            }

            bool redo()
            {
                if (m_redoStack.empty())
                {
                    if (m_eventCallback)
                    {
                        m_eventCallback("RedoEmpty", "");
                    }
                    return false;
                }
                auto cmd = std::move(m_redoStack.back());
                m_redoStack.pop_back();
                const std::string cmdName = cmd->getName();
                cmd->execute();
                m_undoStack.push_back(std::move(cmd));
                if (m_eventCallback)
                {
                    m_eventCallback("Redo", cmdName);
                }
                return true;
            }

            bool canUndo() const noexcept { return !m_undoStack.empty(); }
            bool canRedo() const noexcept { return !m_redoStack.empty(); }

            std::string getUndoName() const
            {
                return m_undoStack.empty() ? "" : m_undoStack.back()->getName();
            }

            std::string getRedoName() const
            {
                return m_redoStack.empty() ? "" : m_redoStack.back()->getName();
            }

            void clear()
            {
                m_undoStack.clear();
                m_redoStack.clear();
            }

            void setMaxHistorySize(size_t size) noexcept { m_maxHistorySize = size; }

        private:
            std::vector<UniquePtr<IUndoableCommand>> m_undoStack;
            std::vector<UniquePtr<IUndoableCommand>> m_redoStack;
            size_t m_maxHistorySize = 100;
            CommandEventCallback m_eventCallback;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_COMMAND_STACK_H
