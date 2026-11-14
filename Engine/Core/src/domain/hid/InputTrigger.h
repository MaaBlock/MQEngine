/**
 * @file InputTrigger.h
 * @brief 五态触发器状态机 (Triggers) (FEAT-63)
 */
#ifndef MQENGINE_DOMAIN_HID_INPUTTRIGGER_H
#define MQENGINE_DOMAIN_HID_INPUTTRIGGER_H

#include <MQEngine/domain/hid/InputAction.h>

namespace MQEngine::Domain::HID
{
    /**
     * @brief 触发器五态枚举 (None 空闲 + Started/Ongoing/Triggered/Completed/Canceled 五态)
     */
    enum class ETriggerState : uint8_t
    {
        None      = 0,
        Started   = 1, // 交互刚启动首帧
        Ongoing   = 2, // 持续按住 / 蓄力推进中
        Triggered = 3, // 达到触发条件发射动作
        Completed = 4, // 完整交互释放完成
        Canceled  = 5  // 超时或提前释放导致中断取消
    };

    class ENGINE_API InputTrigger
    {
    public:
        virtual ~InputTrigger() = default;
        virtual ETriggerState update(const InputValue& input, float deltaTime) = 0;
        virtual void reset() = 0;
    };

    /**
     * @brief Down 持续触发器 (首帧 Triggered, 按住 Ongoing, 释放 Completed)
     */
    class ENGINE_API InputTriggerDown : public InputTrigger
    {
    public:
        ETriggerState update(const InputValue& input, float) override
        {
            const bool isActuated = input.magnitude() > 0.0f;
            if (isActuated)
            {
                if (!m_wasActuated)
                {
                    m_wasActuated = true;
                    return ETriggerState::Triggered;
                }
                return ETriggerState::Ongoing;
            }
            if (m_wasActuated)
            {
                m_wasActuated = false;
                return ETriggerState::Completed;
            }
            return ETriggerState::None;
        }

        void reset() override { m_wasActuated = false; }

    private:
        bool m_wasActuated{false};
    };

    /**
     * @brief Pressed 单帧按下边沿触发器
     */
    class ENGINE_API InputTriggerPressed : public InputTrigger
    {
    public:
        ETriggerState update(const InputValue& input, float) override
        {
            const bool isActuated = input.magnitude() > 0.0f;
            if (isActuated)
            {
                if (!m_wasActuated)
                {
                    m_wasActuated = true;
                    return ETriggerState::Triggered;
                }
                return ETriggerState::None;
            }
            if (m_wasActuated)
            {
                m_wasActuated = false;
                return ETriggerState::Completed;
            }
            return ETriggerState::None;
        }

        void reset() override { m_wasActuated = false; }

    private:
        bool m_wasActuated{false};
    };

    /**
     * @brief Released 释放边沿触发器
     */
    class ENGINE_API InputTriggerReleased : public InputTrigger
    {
    public:
        ETriggerState update(const InputValue& input, float) override
        {
            const bool isActuated = input.magnitude() > 0.0f;
            if (isActuated)
            {
                if (!m_wasActuated)
                {
                    m_wasActuated = true;
                    return ETriggerState::Started;
                }
                return ETriggerState::Ongoing;
            }
            if (m_wasActuated)
            {
                m_wasActuated = false;
                return ETriggerState::Triggered;
            }
            return ETriggerState::None;
        }

        void reset() override { m_wasActuated = false; }

    private:
        bool m_wasActuated{false};
    };

    /**
     * @brief Tap 轻触触发器 (按下时长 <= 阈值内释放触发，超时立即转为 Canceled)
     */
    class ENGINE_API InputTriggerTap : public InputTrigger
    {
    public:
        explicit InputTriggerTap(float tapThreshold = 0.20f) noexcept
            : m_tapThreshold(std::max(0.0f, tapThreshold))
        {
        }

        ETriggerState update(const InputValue& input, float deltaTime) override
        {
            const bool isActuated = input.magnitude() > 0.0f;
            if (isActuated)
            {
                m_wasActuated = true;
                m_duration += std::max(0.0f, deltaTime);
                if (m_duration > m_tapThreshold)
                {
                    m_canceledByTimeout = true;
                    return ETriggerState::Canceled;
                }
                return ETriggerState::Ongoing;
            }

            if (m_wasActuated)
            {
                const bool qualified = (!m_canceledByTimeout && m_duration <= m_tapThreshold);
                m_wasActuated = false;
                m_canceledByTimeout = false;
                m_duration = 0.0f;
                return qualified ? ETriggerState::Triggered : ETriggerState::Canceled;
            }
            return ETriggerState::None;
        }

        void reset() override
        {
            m_duration = 0.0f;
            m_wasActuated = false;
            m_canceledByTimeout = false;
        }

    private:
        float m_tapThreshold{0.20f};
        float m_duration{0.0f};
        bool m_wasActuated{false};
        bool m_canceledByTimeout{false};
    };

    /**
     * @brief Hold 长按触发器 (未达阈值前 Ongoing，达到阈值后 Triggered，释放 Completed/Canceled)
     */
    class ENGINE_API InputTriggerHold : public InputTrigger
    {
    public:
        explicit InputTriggerHold(float holdThreshold = 0.40f, bool isOneShot = false) noexcept
            : m_holdThreshold(std::max(0.0f, holdThreshold))
            , m_isOneShot(isOneShot)
        {
        }

        ETriggerState update(const InputValue& input, float deltaTime) override
        {
            const bool isActuated = input.magnitude() > 0.0f;
            if (isActuated)
            {
                m_wasActuated = true;
                m_duration += std::max(0.0f, deltaTime);
                if (m_duration >= m_holdThreshold)
                {
                    if (!m_triggered)
                    {
                        m_triggered = true;
                        return ETriggerState::Triggered;
                    }
                    return m_isOneShot ? ETriggerState::Ongoing : ETriggerState::Triggered;
                }
                return ETriggerState::Ongoing;
            }

            if (m_wasActuated)
            {
                const bool wasTriggered = m_triggered;
                m_wasActuated = false;
                m_triggered = false;
                m_duration = 0.0f;
                return wasTriggered ? ETriggerState::Completed : ETriggerState::Canceled;
            }
            return ETriggerState::None;
        }

        void reset() override
        {
            m_duration = 0.0f;
            m_wasActuated = false;
            m_triggered = false;
        }

    private:
        float m_holdThreshold{0.40f};
        float m_duration{0.0f};
        bool m_isOneShot{false};
        bool m_wasActuated{false};
        bool m_triggered{false};
    };

    /**
     * @brief HoldAndRelease 蓄力释放触发器 (蓄力期间 Ongoing，达标后释放 Triggered，提前释放 Canceled)
     */
    class ENGINE_API InputTriggerHoldAndRelease : public InputTrigger
    {
    public:
        explicit InputTriggerHoldAndRelease(float holdThreshold = 0.50f) noexcept
            : m_holdThreshold(std::max(0.0f, holdThreshold))
        {
        }

        ETriggerState update(const InputValue& input, float deltaTime) override
        {
            const bool isActuated = input.magnitude() > 0.0f;
            if (isActuated)
            {
                m_wasActuated = true;
                m_duration += std::max(0.0f, deltaTime);
                return ETriggerState::Ongoing;
            }

            if (m_wasActuated)
            {
                const float totalDur = m_duration;
                m_wasActuated = false;
                m_duration = 0.0f;
                return (totalDur >= m_holdThreshold) ? ETriggerState::Triggered : ETriggerState::Canceled;
            }
            return ETriggerState::None;
        }

        void reset() override
        {
            m_duration = 0.0f;
            m_wasActuated = false;
        }

    private:
        float m_holdThreshold{0.50f};
        float m_duration{0.0f};
        bool m_wasActuated{false};
    };

    /**
     * @brief ChordedAction 组合键先决动作触发器
     */
    class ENGINE_API InputTriggerChordedAction : public InputTrigger
    {
    public:
        explicit InputTriggerChordedAction(InputActionHandle chordHandle = 0, std::string chordActionName = {})
            : m_chordHandle(chordHandle)
            , m_chordActionName(std::move(chordActionName))
        {
        }

        ETriggerState update(const InputValue& input, float) override
        {
            const bool isActuated = input.magnitude() > 0.0f;
            if (isActuated && m_chordActive)
            {
                m_wasTriggered = true;
                return ETriggerState::Triggered;
            }
            if (!isActuated && m_wasTriggered)
            {
                m_wasTriggered = false;
                return ETriggerState::Completed;
            }
            return ETriggerState::None;
        }

        void setChordActive(bool active) noexcept { m_chordActive = active; }
        void setPrerequisite(bool active) noexcept { m_chordActive = active; }
        [[nodiscard]] bool isChordActive() const noexcept { return m_chordActive; }
        [[nodiscard]] InputActionHandle getChordHandle() const noexcept { return m_chordHandle; }
        [[nodiscard]] const std::string& getChordActionName() const noexcept { return m_chordActionName; }

        void reset() override
        {
            m_chordActive = false;
            m_wasTriggered = false;
        }

    private:
        InputActionHandle m_chordHandle{0};
        std::string m_chordActionName;
        bool m_chordActive{false};
        bool m_wasTriggered{false};
    };
}

#endif // MQENGINE_DOMAIN_HID_INPUTTRIGGER_H
