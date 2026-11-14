/**
 * @file InputAction.h
 * @brief 强类型语义动作与多维动作值 (FEAT-60, FEAT-61)
 */
#ifndef MQENGINE_DOMAIN_HID_INPUTACTION_H
#define MQENGINE_DOMAIN_HID_INPUTACTION_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine::Domain::HID
{
    using InputActionHandle = uint32_t;

    enum class EInputActionValueType : uint8_t
    {
        Digital = 0, // bool
        Axis1D  = 1, // float
        Axis2D  = 2, // FCT::Vec2
        Axis3D  = 3  // FCT::Vec3f
    };

    enum class EInputAccumulationPolicy : uint8_t
    {
        Cumulative   = 0, // 向量相加 (多源累加融合)
        MaxMagnitude = 1  // 取最大绝对模长 (保留峰值)
    };

    /**
     * @brief 强类型动作值载体 (FEAT-61)
     */
    class ENGINE_API InputValue
    {
    public:
        InputValue() noexcept : m_type(EInputActionValueType::Digital), m_b(false) {}
        explicit InputValue(bool v) noexcept
            : m_type(EInputActionValueType::Digital)
            , m_b(v)
            , m_f(v ? 1.0f : 0.0f)
            , m_v2{v ? 1.0f : 0.0f, 0.0f}
            , m_v3{v ? 1.0f : 0.0f, 0.0f, 0.0f}
        {
        }
        explicit InputValue(float v) noexcept
            : m_type(EInputActionValueType::Axis1D)
            , m_b(std::abs(v) > 1e-6f)
            , m_f(sanitize(v))
            , m_v2{sanitize(v), 0.0f}
            , m_v3{sanitize(v), 0.0f, 0.0f}
        {
        }
        explicit InputValue(FCT::Vec2 v) noexcept
            : m_type(EInputActionValueType::Axis2D)
            , m_b((v.x * v.x + v.y * v.y) > 1e-12f)
            , m_f(sanitize(v.x))
            , m_v2{sanitize(v.x), sanitize(v.y)}
            , m_v3{sanitize(v.x), sanitize(v.y), 0.0f}
        {
        }
        InputValue(float x, float y) noexcept : InputValue(FCT::Vec2{x, y}) {}
        explicit InputValue(FCT::Vec3f v) noexcept
            : m_type(EInputActionValueType::Axis3D)
            , m_b((v.x * v.x + v.y * v.y + v.z * v.z) > 1e-12f)
            , m_f(sanitize(v.x))
            , m_v2{sanitize(v.x), sanitize(v.y)}
            , m_v3{sanitize(v.x), sanitize(v.y), sanitize(v.z)}
        {
        }
        InputValue(float x, float y, float z) noexcept : InputValue(FCT::Vec3f{x, y, z}) {}

        static InputValue zeroForType(EInputActionValueType type) noexcept
        {
            switch (type)
            {
            case EInputActionValueType::Digital: return InputValue(false);
            case EInputActionValueType::Axis1D:  return InputValue(0.0f);
            case EInputActionValueType::Axis2D:  return InputValue(FCT::Vec2{0.0f, 0.0f});
            case EInputActionValueType::Axis3D:  return InputValue(FCT::Vec3f{0.0f, 0.0f, 0.0f});
            }
            return InputValue(false);
        }

        [[nodiscard]] EInputActionValueType getValueType() const noexcept { return m_type; }

        template <typename T>
        [[nodiscard]] T get() const noexcept;

        [[nodiscard]] float magnitudeSquared() const noexcept
        {
            switch (m_type)
            {
            case EInputActionValueType::Digital: return m_b ? 1.0f : 0.0f;
            case EInputActionValueType::Axis1D:  return m_f * m_f;
            case EInputActionValueType::Axis2D:  return m_v2.x * m_v2.x + m_v2.y * m_v2.y;
            case EInputActionValueType::Axis3D:  return m_v3.x * m_v3.x + m_v3.y * m_v3.y + m_v3.z * m_v3.z;
            }
            return 0.0f;
        }

        [[nodiscard]] float magnitude() const noexcept
        {
            switch (m_type)
            {
            case EInputActionValueType::Digital: return m_b ? 1.0f : 0.0f;
            case EInputActionValueType::Axis1D:  return std::abs(m_f);
            case EInputActionValueType::Axis2D:  return std::sqrt(m_v2.x * m_v2.x + m_v2.y * m_v2.y);
            case EInputActionValueType::Axis3D:  return std::sqrt(m_v3.x * m_v3.x + m_v3.y * m_v3.y + m_v3.z * m_v3.z);
            }
            return 0.0f;
        }

        [[nodiscard]] InputValue convertTo(EInputActionValueType targetType) const noexcept
        {
            switch (targetType)
            {
            case EInputActionValueType::Digital:
                return InputValue(magnitudeSquared() > 1e-12f);
            case EInputActionValueType::Axis1D:
                return InputValue(m_f);
            case EInputActionValueType::Axis2D:
                return InputValue(m_v2);
            case EInputActionValueType::Axis3D:
                return InputValue(m_v3);
            }
            return *this;
        }

        void accumulate(
            const InputValue& other,
            EInputAccumulationPolicy policy = EInputAccumulationPolicy::Cumulative) noexcept
        {
            const EInputActionValueType targetType =
                (static_cast<uint8_t>(other.m_type) > static_cast<uint8_t>(m_type)) ? other.m_type : m_type;
            const InputValue lhs = convertTo(targetType);
            const InputValue rhs = other.convertTo(targetType);

            switch (targetType)
            {
            case EInputActionValueType::Digital:
                *this = InputValue(lhs.m_b || rhs.m_b);
                break;
            case EInputActionValueType::Axis1D:
                if (policy == EInputAccumulationPolicy::Cumulative)
                {
                    *this = InputValue(lhs.m_f + rhs.m_f);
                }
                else
                {
                    *this = InputValue(std::abs(rhs.m_f) > std::abs(lhs.m_f) ? rhs.m_f : lhs.m_f);
                }
                break;
            case EInputActionValueType::Axis2D:
                if (policy == EInputAccumulationPolicy::Cumulative)
                {
                    *this = InputValue(FCT::Vec2{lhs.m_v2.x + rhs.m_v2.x, lhs.m_v2.y + rhs.m_v2.y});
                }
                else
                {
                    *this = (rhs.magnitudeSquared() > lhs.magnitudeSquared()) ? rhs : lhs;
                }
                break;
            case EInputActionValueType::Axis3D:
                if (policy == EInputAccumulationPolicy::Cumulative)
                {
                    *this = InputValue(FCT::Vec3f{lhs.m_v3.x + rhs.m_v3.x, lhs.m_v3.y + rhs.m_v3.y, lhs.m_v3.z + rhs.m_v3.z});
                }
                else
                {
                    *this = (rhs.magnitudeSquared() > lhs.magnitudeSquared()) ? rhs : lhs;
                }
                break;
            }
        }

    private:
        static float sanitize(float v) noexcept
        {
            return std::isfinite(v) ? v : 0.0f;
        }

        EInputActionValueType m_type;
        bool m_b = false;
        float m_f = 0.0f;
        FCT::Vec2 m_v2{0.0f, 0.0f};
        FCT::Vec3f m_v3{0.0f, 0.0f, 0.0f};
    };

    template <> inline bool InputValue::get<bool>() const noexcept { return m_b; }
    template <> inline float InputValue::get<float>() const noexcept { return m_f; }
    template <> inline FCT::Vec2 InputValue::get<FCT::Vec2>() const noexcept { return m_v2; }
    template <> inline FCT::Vec3f InputValue::get<FCT::Vec3f>() const noexcept { return m_v3; }

    class InputModifier;
    class InputTrigger;

    /**
     * @brief 强类型语义动作定义 (FEAT-60)
     */
    class ENGINE_API InputAction
    {
    public:
        InputAction(
            std::string name,
            InputActionHandle handle,
            EInputActionValueType valueType,
            EInputAccumulationPolicy policy = EInputAccumulationPolicy::Cumulative)
            : m_name(std::move(name))
            , m_handle(handle)
            , m_valueType(valueType)
            , m_accumulationPolicy(policy)
        {
        }

        ~InputAction();
        InputAction(const InputAction&) = delete;
        InputAction& operator=(const InputAction&) = delete;
        InputAction(InputAction&&) noexcept;
        InputAction& operator=(InputAction&&) noexcept;

        [[nodiscard]] const std::string& getName() const noexcept { return m_name; }
        [[nodiscard]] InputActionHandle getHandle() const noexcept { return m_handle; }
        void setHandle(InputActionHandle handle) noexcept { m_handle = handle; }
        [[nodiscard]] EInputActionValueType getValueType() const noexcept { return m_valueType; }
        [[nodiscard]] EInputAccumulationPolicy getAccumulationPolicy() const noexcept { return m_accumulationPolicy; }

        void addModifier(UniquePtr<InputModifier> modifier);
        void addTrigger(UniquePtr<InputTrigger> trigger);

        [[nodiscard]] const std::vector<UniquePtr<InputModifier>>& getModifiers() const noexcept { return m_modifiers; }
        [[nodiscard]] const std::vector<UniquePtr<InputTrigger>>& getTriggers() const noexcept { return m_triggers; }

    private:
        std::string m_name;
        InputActionHandle m_handle{0};
        EInputActionValueType m_valueType{EInputActionValueType::Digital};
        EInputAccumulationPolicy m_accumulationPolicy{EInputAccumulationPolicy::Cumulative};
        std::vector<UniquePtr<InputModifier>> m_modifiers;
        std::vector<UniquePtr<InputTrigger>> m_triggers;
    };
}

#endif // MQENGINE_DOMAIN_HID_INPUTACTION_H
