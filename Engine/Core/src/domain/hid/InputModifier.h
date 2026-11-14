/**
 * @file InputModifier.h
 * @brief 顺序输入修饰器链 (Modifiers) (FEAT-62)
 */
#ifndef MQENGINE_DOMAIN_HID_INPUTMODIFIER_H
#define MQENGINE_DOMAIN_HID_INPUTMODIFIER_H

#include <MQEngine/domain/hid/InputAction.h>

namespace MQEngine::Domain::HID
{
    enum class ESwizzleAxisOrder : uint8_t
    {
        YXZ = 0, // 1D -> (0, X), 2D -> (Y, X), 3D -> (Y, X, Z)
        ZYX = 1, // 1D -> (0, 0, X), 3D -> (Z, Y, X)
        XZY = 2  // 2D -> (X, 0, Y), 3D -> (X, Z, Y)
    };

    class ENGINE_API InputModifier
    {
    public:
        virtual ~InputModifier() = default;
        virtual InputValue modify(const InputValue& input, float deltaTime) const = 0;
    };

    /**
     * @brief 轴置换修饰器 (默认 YXZ：将 W/S 的 1D 标量映射至 2D Y 轴)
     */
    class ENGINE_API InputModifierSwizzleAxis : public InputModifier
    {
    public:
        explicit InputModifierSwizzleAxis(ESwizzleAxisOrder order = ESwizzleAxisOrder::YXZ) noexcept
            : m_order(order)
        {
        }

        InputValue modify(const InputValue& input, float) const override
        {
            const auto type = input.getValueType();
            if (type == EInputActionValueType::Digital || type == EInputActionValueType::Axis1D)
            {
                const float v = (type == EInputActionValueType::Digital)
                    ? (input.get<bool>() ? 1.0f : 0.0f)
                    : input.get<float>();
                switch (m_order)
                {
                case ESwizzleAxisOrder::YXZ: return InputValue(FCT::Vec2{0.0f, v});
                case ESwizzleAxisOrder::ZYX: return InputValue(FCT::Vec3f{0.0f, 0.0f, v});
                case ESwizzleAxisOrder::XZY: return InputValue(FCT::Vec3f{v, 0.0f, 0.0f});
                }
            }
            if (type == EInputActionValueType::Axis2D)
            {
                const auto v = input.get<FCT::Vec2>();
                switch (m_order)
                {
                case ESwizzleAxisOrder::YXZ: return InputValue(FCT::Vec2{v.y, v.x});
                case ESwizzleAxisOrder::ZYX: return InputValue(FCT::Vec3f{0.0f, v.y, v.x});
                case ESwizzleAxisOrder::XZY: return InputValue(FCT::Vec3f{v.x, 0.0f, v.y});
                }
            }
            if (type == EInputActionValueType::Axis3D)
            {
                const auto v = input.get<FCT::Vec3f>();
                switch (m_order)
                {
                case ESwizzleAxisOrder::YXZ: return InputValue(FCT::Vec3f{v.y, v.x, v.z});
                case ESwizzleAxisOrder::ZYX: return InputValue(FCT::Vec3f{v.z, v.y, v.x});
                case ESwizzleAxisOrder::XZY: return InputValue(FCT::Vec3f{v.x, v.z, v.y});
                }
            }
            return input;
        }

    private:
        ESwizzleAxisOrder m_order{ESwizzleAxisOrder::YXZ};
    };

    /**
     * @brief 轴反转修饰器
     */
    class ENGINE_API InputModifierNegate : public InputModifier
    {
    public:
        InputModifierNegate(bool x = true, bool y = true, bool z = true) noexcept
            : m_negX(x), m_negY(y), m_negZ(z)
        {
        }

        InputValue modify(const InputValue& input, float) const override
        {
            if (input.getValueType() == EInputActionValueType::Digital)
            {
                const float v = input.get<bool>() ? 1.0f : 0.0f;
                return InputValue(m_negX ? -v : v);
            }
            if (input.getValueType() == EInputActionValueType::Axis1D)
            {
                const float v = input.get<float>();
                return InputValue(m_negX ? -v : v);
            }
            if (input.getValueType() == EInputActionValueType::Axis2D)
            {
                const auto v = input.get<FCT::Vec2>();
                return InputValue(FCT::Vec2{m_negX ? -v.x : v.x, m_negY ? -v.y : v.y});
            }
            if (input.getValueType() == EInputActionValueType::Axis3D)
            {
                const auto v = input.get<FCT::Vec3f>();
                return InputValue(FCT::Vec3f{m_negX ? -v.x : v.x, m_negY ? -v.y : v.y, m_negZ ? -v.z : v.z});
            }
            return input;
        }

    private:
        bool m_negX{true};
        bool m_negY{true};
        bool m_negZ{true};
    };

    /**
     * @brief 死区过滤修饰器
     */
    class ENGINE_API InputModifierDeadZone : public InputModifier
    {
    public:
        explicit InputModifierDeadZone(float threshold = 0.2f) noexcept
            : m_threshold(std::max(0.0f, threshold))
        {
        }

        InputValue modify(const InputValue& input, float) const override
        {
            if (input.magnitude() < m_threshold)
            {
                return InputValue::zeroForType(input.getValueType());
            }
            return input;
        }

    private:
        float m_threshold{0.2f};
    };

    /**
     * @brief 2D 向量单位圆钳制归一化修饰器 (防根号2倍对角线加速，零向量安全无除零)
     */
    class ENGINE_API InputModifierNormalize2D : public InputModifier
    {
    public:
        InputValue modify(const InputValue& input, float) const override
        {
            if (input.getValueType() == EInputActionValueType::Axis2D)
            {
                const auto v = input.get<FCT::Vec2>();
                const float mag = std::sqrt(v.x * v.x + v.y * v.y);
                if (mag > 1.0f && std::isfinite(mag))
                {
                    return InputValue(FCT::Vec2{v.x / mag, v.y / mag});
                }
                return InputValue(FCT::Vec2{v.x, v.y});
            }
            return input;
        }
    };

    /**
     * @brief 标量乘子与灵敏度修饰器
     */
    class ENGINE_API InputModifierScalar : public InputModifier
    {
    public:
        explicit InputModifierScalar(float factor) noexcept : m_factor(factor) {}

        InputValue modify(const InputValue& input, float) const override
        {
            if (input.getValueType() == EInputActionValueType::Axis1D)
            {
                return InputValue(input.get<float>() * m_factor);
            }
            if (input.getValueType() == EInputActionValueType::Axis2D)
            {
                const auto v = input.get<FCT::Vec2>();
                return InputValue(FCT::Vec2{v.x * m_factor, v.y * m_factor});
            }
            if (input.getValueType() == EInputActionValueType::Axis3D)
            {
                const auto v = input.get<FCT::Vec3f>();
                return InputValue(FCT::Vec3f{v.x * m_factor, v.y * m_factor, v.z * m_factor});
            }
            return input;
        }

    private:
        float m_factor{1.0f};
    };

    /**
     * @brief 帧耗时缩放修饰器 (将轴向速率转换为帧无关位移量)
     */
    class ENGINE_API InputModifierDeltaTimeScale : public InputModifier
    {
    public:
        InputValue modify(const InputValue& input, float deltaTime) const override
        {
            const float dt = std::max(0.0f, deltaTime);
            InputModifierScalar scaler(dt);
            return scaler.modify(input, deltaTime);
        }
    };
}

#endif // MQENGINE_DOMAIN_HID_INPUTMODIFIER_H
