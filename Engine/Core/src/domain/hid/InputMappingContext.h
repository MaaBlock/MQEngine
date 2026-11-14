/**
 * @file InputMappingContext.h
 * @brief 输入映射上下文 (IMC) 与优先级栈配置资产 (FEAT-64)
 */
#ifndef MQENGINE_DOMAIN_HID_INPUTMAPPINGCONTEXT_H
#define MQENGINE_DOMAIN_HID_INPUTMAPPINGCONTEXT_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/hid/InputAction.h>
#include <MQEngine/domain/hid/InputModifier.h>
#include <MQEngine/domain/hid/InputTrigger.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine::Domain::HID
{
    /**
     * @brief 键源到语义动作的映射配置项
     */
    struct ActionBindingMapping
    {
        std::string inputSourceKey;
        InputActionHandle actionHandle{0};
        std::string actionName;
        bool consumesInput{true};
        std::vector<UniquePtr<InputModifier>> modifiers;
        std::vector<UniquePtr<InputTrigger>> triggers;

        ActionBindingMapping() = default;
        ~ActionBindingMapping() = default;
        ActionBindingMapping(const ActionBindingMapping&) = delete;
        ActionBindingMapping& operator=(const ActionBindingMapping&) = delete;
        ActionBindingMapping(ActionBindingMapping&&) noexcept = default;
        ActionBindingMapping& operator=(ActionBindingMapping&&) noexcept = default;
    };

    /**
     * @brief 输入映射上下文 (IMC)
     */
    class ENGINE_API InputMappingContext
    {
    public:
        InputMappingContext() = default;
        explicit InputMappingContext(std::string name, int32_t priority = 0)
            : m_name(std::move(name))
            , m_priority(priority)
        {
        }

        ~InputMappingContext() = default;
        InputMappingContext(const InputMappingContext&) = delete;
        InputMappingContext& operator=(const InputMappingContext&) = delete;
        InputMappingContext(InputMappingContext&&) noexcept = default;
        InputMappingContext& operator=(InputMappingContext&&) noexcept = default;

        [[nodiscard]] const std::string& getName() const noexcept { return m_name; }
        void setName(std::string name) noexcept { m_name = std::move(name); }

        [[nodiscard]] int32_t getPriority() const noexcept { return m_priority; }
        void setPriority(int32_t priority) noexcept { m_priority = priority; }

        void addBinding(ActionBindingMapping mapping);
        void clearBindings() noexcept { m_bindings.clear(); }
        [[nodiscard]] const std::vector<ActionBindingMapping>& getBindings() const noexcept { return m_bindings; }
        [[nodiscard]] std::vector<ActionBindingMapping>& getMutableBindings() noexcept { return m_bindings; }
        [[nodiscard]] uint32_t getActionBindingCount() const noexcept { return static_cast<uint32_t>(m_bindings.size()); }

        Status loadFromToml(std::string_view tomlContent);
        Status loadFromFile(std::string_view assetPath);

    private:
        std::string m_name;
        int32_t m_priority{0};
        std::vector<ActionBindingMapping> m_bindings;
    };
}

#endif // MQENGINE_DOMAIN_HID_INPUTMAPPINGCONTEXT_H
