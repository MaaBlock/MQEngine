/**
 * @file Test_Tier4_S3_CrossPlatformInputEquivalence.cpp
 * @brief Tier 4 真实负载综合场景 S3：键鼠与小游戏虚拟摇杆零改码操控等价性、IMC 遮蔽与软键盘闭环
 */
#include "Common/E2ECommon.h"
#include "GameHtmlUI.h"
#include <FCT_SAI/VirtualKeyboard.h>

#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/hid/InputAction.h>
#include <MQEngine/domain/hid/InputModifier.h>
#include <MQEngine/domain/hid/InputTrigger.h>
#include <MQEngine/domain/hid/InputMappingContext.h>

#include <cmath>
#include <string>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        using namespace MQEngine::Domain::HID;
        using namespace MQEngine::GameHtmlUI;

        struct CharacterMoveSimulator
        {
            FCT::Vec2 position{0.0f, 0.0f};
            float moveSpeed{5.0f};

            void step(const InputValue& moveInput, float dt) noexcept
            {
                if (moveInput.getValueType() == EInputActionValueType::Axis2D)
                {
                    const FCT::Vec2 dir = moveInput.get<FCT::Vec2>();
                    position.x += dir.x * moveSpeed * dt;
                    position.y += dir.y * moveSpeed * dt;
                }
            }
        };

        UniquePtr<InputMappingContext> buildDesktopWasdMappingContext(InputActionHandle moveHandle, InputActionHandle attackHandle)
        {
            auto imc = makeUnique<InputMappingContext>("IMC_DefaultGameplay", 0);

            ActionBindingMapping mapD;
            mapD.inputSourceKey = "Key_D";
            mapD.actionHandle = moveHandle;
            mapD.actionName = "IA_Move";
            imc->addBinding(std::move(mapD));

            ActionBindingMapping mapA;
            mapA.inputSourceKey = "Key_A";
            mapA.actionHandle = moveHandle;
            mapA.actionName = "IA_Move";
            mapA.modifiers.push_back(makeUnique<InputModifierNegate>(true, false, false));
            imc->addBinding(std::move(mapA));

            ActionBindingMapping mapW;
            mapW.inputSourceKey = "Key_W";
            mapW.actionHandle = moveHandle;
            mapW.actionName = "IA_Move";
            mapW.modifiers.push_back(makeUnique<InputModifierSwizzleAxis>(ESwizzleAxisOrder::YXZ));
            imc->addBinding(std::move(mapW));

            ActionBindingMapping mapS;
            mapS.inputSourceKey = "Key_S";
            mapS.actionHandle = moveHandle;
            mapS.actionName = "IA_Move";
            mapS.modifiers.push_back(makeUnique<InputModifierSwizzleAxis>(ESwizzleAxisOrder::YXZ));
            mapS.modifiers.push_back(makeUnique<InputModifierNegate>(false, true, false));
            imc->addBinding(std::move(mapS));

            ActionBindingMapping mapAttack;
            mapAttack.inputSourceKey = "Key_J";
            mapAttack.actionHandle = attackHandle;
            mapAttack.actionName = "IA_Attack";
            imc->addBinding(std::move(mapAttack));

            return imc;
        }

        struct KeyboardConfirmCollector
        {
            std::string* outConfirmed{nullptr};

            void operator()(std::string_view text) const
            {
                if (outConfirmed)
                {
                    *outConfirmed = std::string(text);
                }
            }
        };
    } // namespace

    // S3-01: 桌面 WASD 对角线移动（Swizzle + Negate + Normalize2D 防根号2超速）与虚拟摇杆位移严格等价
    E2E_TEST(Tier4, FEAT_60, S3_01_DesktopWasdAndMobileVirtualJoystickZeroCodeDiffEquivalence)
    {
        const float dt = 1.0f / 60.0f;

        HIDSubsystem desktopHid;
        E2E_ASSERT_STATUS_OK(desktopHid.init());
        const InputActionHandle dMoveHandle = desktopHid.registerAction("IA_Move", EInputActionValueType::Axis2D);
        InputAction* desktopMove = desktopHid.findAction(dMoveHandle);
        E2E_ASSERT_NE(desktopMove, nullptr);
        desktopMove->addModifier(makeUnique<InputModifierDeadZone>(0.15f));
        desktopMove->addModifier(makeUnique<InputModifierNormalize2D>());
        const InputActionHandle dAtkHandle = desktopHid.registerAction("IA_Attack", EInputActionValueType::Digital);
        desktopHid.addMappingContext(buildDesktopWasdMappingContext(dMoveHandle, dAtkHandle), 0);

        desktopHid.setSourceInputValue("Key_W", InputValue(true));
        desktopHid.setSourceInputValue("Key_D", InputValue(true));
        CharacterMoveSimulator desktopChar;
        for (int frame = 0; frame < 30; ++frame)
        {
            desktopHid.update(dt);
            desktopChar.step(desktopHid.getActionValue(dMoveHandle), dt);
        }

        HIDSubsystem mobileHid;
        E2E_ASSERT_STATUS_OK(mobileHid.init());
        const InputActionHandle mMoveHandle = mobileHid.registerAction("IA_Move", EInputActionValueType::Axis2D);
        InputAction* mobileMove = mobileHid.findAction(mMoveHandle);
        E2E_ASSERT_NE(mobileMove, nullptr);
        mobileMove->addModifier(makeUnique<InputModifierDeadZone>(0.15f));
        mobileMove->addModifier(makeUnique<InputModifierNormalize2D>());

        VirtualTouchController touchCtrl(&mobileHid);
        touchCtrl.bindJoystick("IA_Move", 120.0f, 520.0f, 60.0f);
        const bool consumed = touchCtrl.onTouchDown(1, 180.0f, 580.0f);
        E2E_ASSERT_TRUE(consumed);

        CharacterMoveSimulator mobileChar;
        const auto deflection = touchCtrl.getJoystickDeflection("IA_Move");
        for (int frame = 0; frame < 30; ++frame)
        {
            E2E_ASSERT_STATUS_OK(mobileHid.injectInputForAction(
                mMoveHandle,
                InputValue(FCT::Vec2{deflection.first, deflection.second}),
                dt));
            mobileChar.step(mobileHid.getActionValue(mMoveHandle), dt);
        }

        const float desktopDist = std::hypot(desktopChar.position.x, desktopChar.position.y);
        const float mobileDist = std::hypot(mobileChar.position.x, mobileChar.position.y);
        E2E_ASSERT_NEAR(desktopDist, 2.5f, 0.05f);
        E2E_ASSERT_NEAR(desktopDist, mobileDist, 0.05f);

        desktopHid.term();
        mobileHid.term();
        return true;
    }

    // S3-02: 双指并发操作（左手虚拟摇杆移动 + 右手连击技能按钮）独立追踪互不串扰
    E2E_TEST(Tier4, FEAT_51, S3_02_MultiTouchJoystickAndSkillButtonConcurrentControl)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());
        (void)hid.registerAction("IA_Move", EInputActionValueType::Axis2D);
        const InputActionHandle skillHandle = hid.registerAction("IA_Ultimate", EInputActionValueType::Digital);

        VirtualTouchController touchCtrl(&hid);
        touchCtrl.bindJoystick("IA_Move", 100.0f, 500.0f, 50.0f);
        touchCtrl.bindButton("IA_Ultimate", 620.0f, 480.0f, 80.0f, 80.0f);

        E2E_ASSERT_TRUE(touchCtrl.onTouchDown(0, 140.0f, 500.0f));
        E2E_ASSERT_TRUE(touchCtrl.onTouchDown(1, 650.0f, 510.0f));
        E2E_ASSERT_EQ(touchCtrl.getActivePointerCount(), 2u);
        E2E_ASSERT_EQ(touchCtrl.getConsumedPointerCount(), 2u);
        E2E_ASSERT_TRUE(touchCtrl.getButtonState("IA_Ultimate"));
        E2E_ASSERT_TRUE(hid.getActionValue(skillHandle).get<bool>());

        (void)touchCtrl.onTouchUp(1, 650.0f, 510.0f);
        E2E_ASSERT_FALSE(touchCtrl.getButtonState("IA_Ultimate"));
        E2E_ASSERT_EQ(touchCtrl.getActivePointerCount(), 1u);
        E2E_ASSERT_GT(touchCtrl.getJoystickDeflection("IA_Move").first, 0.5f);

        (void)touchCtrl.onTouchUp(0, 140.0f, 500.0f);
        E2E_ASSERT_EQ(touchCtrl.getActivePointerCount(), 0u);
        hid.term();
        return true;
    }

    // S3-03: 弹出背包/商城模态窗口压入高优 IMC 吞噬底层战斗输入，关闭弹窗无缝恢复移动
    E2E_TEST(Tier4, FEAT_64, S3_03_ModalPopupImcPrioritySwallowsGameplayAndRecovers)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());
        const InputActionHandle jumpHandle = hid.registerAction("IA_Jump", EInputActionValueType::Digital);
        const InputActionHandle confirmHandle = hid.registerAction("IA_ModalConfirm", EInputActionValueType::Digital);

        auto gameplayImc = makeUnique<InputMappingContext>("IMC_Gameplay", 0);
        ActionBindingMapping jumpBind;
        jumpBind.inputSourceKey = "Key_Space";
        jumpBind.actionHandle = jumpHandle;
        jumpBind.actionName = "IA_Jump";
        jumpBind.consumesInput = true;
        gameplayImc->addBinding(std::move(jumpBind));
        hid.addMappingContext(std::move(gameplayImc), 0);

        const ResolvedKeyMapping beforePopup = hid.resolveKeyInput("Key_Space");
        E2E_ASSERT_EQ(beforePopup.actionName, "IA_Jump");

        auto modalImc = makeUnique<InputMappingContext>("IMC_ShopModal", 100);
        ActionBindingMapping modalBind;
        modalBind.inputSourceKey = "Key_Space";
        modalBind.actionHandle = confirmHandle;
        modalBind.actionName = "IA_ModalConfirm";
        modalBind.consumesInput = true;
        modalImc->addBinding(std::move(modalBind));
        hid.addMappingContext(std::move(modalImc), 100);

        const ResolvedKeyMapping duringPopup = hid.resolveKeyInput("Key_Space");
        E2E_ASSERT_EQ(duringPopup.actionName, "IA_ModalConfirm");
        E2E_ASSERT_EQ(duringPopup.contextName, "IMC_ShopModal");

        hid.setSourceInputValue("Key_Space", InputValue(true));
        hid.update(1.0f / 60.0f);
        E2E_ASSERT_TRUE(hid.getActionValue(confirmHandle).get<bool>());
        E2E_ASSERT_FALSE(hid.getActionValue(jumpHandle).get<bool>());

        hid.removeMappingContext("IMC_ShopModal");
        E2E_ASSERT_FALSE(hid.hasActiveContext("IMC_ShopModal"));
        hid.update(1.0f / 60.0f);
        E2E_ASSERT_TRUE(hid.getActionValue(jumpHandle).get<bool>());

        hid.term();
        return true;
    }

    // S3-04: 屏幕空闲区单指滑动旋转相机 + 双指捏合缩放视距，同时严格隔离被 UI 消费的触点防误触穿透
    E2E_TEST(Tier4, FEAT_66, S3_04_IdleScreenSwipePinchGestureWithUiPointerIsolation)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());

        hid.onTouchDown(10u, 80.0f, 520.0f, true);
        hid.onTouchMove(10u, 150.0f, 520.0f, true);
        const FCT::Vec2 uiDelta = hid.getTouchSwipeDeltaForPointer(10u);
        E2E_ASSERT_NEAR(uiDelta.x, 0.0f, 1e-4f);
        E2E_ASSERT_NEAR(uiDelta.y, 0.0f, 1e-4f);

        hid.onTouchDown(20u, 400.0f, 300.0f, false);
        hid.onTouchMove(20u, 445.0f, 280.0f, false);
        const FCT::Vec2 camDelta = hid.getTouchSwipeDeltaForPointer(20u);
        E2E_ASSERT_GT(std::abs(camDelta.x) + std::abs(camDelta.y), 0.0f);

        hid.onTouchDown(21u, 520.0f, 300.0f, false);
        hid.onTouchMove(21u, 585.0f, 300.0f, false);
        hid.processTouchPinch(120.0f, 180.0f, false);
        E2E_ASSERT_TRUE(hid.isTouchPinchActive());
        E2E_ASSERT_GT(hid.getTouchPinchDelta(), 0.0f);

        hid.onTouchUp(21u, 585.0f, 300.0f);
        hid.processTouchPinch(180.0f, 180.0f, true);
        E2E_ASSERT_FALSE(hid.isTouchPinchActive());

        hid.resetTouches();
        hid.term();
        return true;
    }

    // S3-05: 轻击闪避（Tap <=0.20s）、长按防御（Hold >=0.40s）与蓄力重击（HoldAndRelease >=0.50s）连招状态机
    E2E_TEST(Tier4, FEAT_63, S3_05_CombatComboTapHoldAndChargeReleaseStateMachine)
    {
        InputTriggerTap dodgeTap(0.20f);
        E2E_ASSERT_EQ(dodgeTap.update(InputValue(true), 0.12f), ETriggerState::Ongoing);
        E2E_ASSERT_EQ(dodgeTap.update(InputValue(false), 0.016f), ETriggerState::Triggered);

        E2E_ASSERT_EQ(dodgeTap.update(InputValue(true), 0.25f), ETriggerState::Canceled);
        E2E_ASSERT_EQ(dodgeTap.update(InputValue(false), 0.016f), ETriggerState::Canceled);

        InputTriggerHold guardHold(0.40f, true);
        E2E_ASSERT_EQ(guardHold.update(InputValue(true), 0.20f), ETriggerState::Ongoing);
        E2E_ASSERT_EQ(guardHold.update(InputValue(true), 0.22f), ETriggerState::Triggered);
        E2E_ASSERT_EQ(guardHold.update(InputValue(true), 0.10f), ETriggerState::Ongoing);
        E2E_ASSERT_EQ(guardHold.update(InputValue(false), 0.016f), ETriggerState::Completed);

        InputTriggerHoldAndRelease heavyCharge(0.50f);
        E2E_ASSERT_EQ(heavyCharge.update(InputValue(true), 0.30f), ETriggerState::Ongoing);
        E2E_ASSERT_EQ(heavyCharge.update(InputValue(false), 0.016f), ETriggerState::Canceled);

        E2E_ASSERT_EQ(heavyCharge.update(InputValue(true), 0.55f), ETriggerState::Ongoing);
        E2E_ASSERT_EQ(heavyCharge.update(InputValue(false), 0.016f), ETriggerState::Triggered);
        return true;
    }

    // S3-06: 组合键大招触发器（ChordedAction：先按住辅助键再按技能键发射，单独按技能键静默拦截）
    E2E_TEST(Tier4, FEAT_63, S3_06_ChordedActionUltimateComboPrerequisiteGate)
    {
        HIDSubsystem hid;
        E2E_ASSERT_STATUS_OK(hid.init());
        const InputActionHandle modHandle = hid.registerAction("IA_ModifierShift", EInputActionValueType::Digital);

        InputTriggerChordedAction ultChord(modHandle, "IA_ModifierShift");
        E2E_ASSERT_FALSE(ultChord.isChordActive());
        E2E_ASSERT_EQ(ultChord.update(InputValue(true), 0.016f), ETriggerState::None);

        ultChord.setChordActive(true);
        E2E_ASSERT_TRUE(ultChord.isChordActive());
        E2E_ASSERT_EQ(ultChord.update(InputValue(true), 0.016f), ETriggerState::Triggered);
        E2E_ASSERT_EQ(ultChord.update(InputValue(false), 0.016f), ETriggerState::Completed);

        hid.term();
        return true;
    }

    // S3-07: 时间戳语义动作序列录制回放（replayActionSequence）跨端确定性复现
    E2E_TEST(Tier4, FEAT_65, S3_07_DeterministicTimestampedActionReplayConsistency)
    {
        std::vector<HIDSubsystem::TimestampedAction> recordedSeq;
        HIDSubsystem hidA;
        E2E_ASSERT_STATUS_OK(hidA.init());
        const InputActionHandle moveA = hidA.registerAction("IA_Move", EInputActionValueType::Axis2D);

        recordedSeq.push_back(HIDSubsystem::TimestampedAction{16u, moveA, InputValue(FCT::Vec2{1.0f, 0.0f})});
        recordedSeq.push_back(HIDSubsystem::TimestampedAction{32u, moveA, InputValue(FCT::Vec2{0.6f, 0.8f})});
        recordedSeq.push_back(HIDSubsystem::TimestampedAction{48u, moveA, InputValue(FCT::Vec2{0.0f, 1.0f})});

        uint32_t countA = 0u;
        E2E_ASSERT_STATUS_OK(hidA.replayActionSequence(recordedSeq, countA));
        E2E_ASSERT_EQ(countA, 3u);
        const FCT::Vec2 finalA = hidA.getActionValue(moveA).get<FCT::Vec2>();

        HIDSubsystem hidB;
        E2E_ASSERT_STATUS_OK(hidB.init());
        const InputActionHandle moveB = hidB.registerAction("IA_Move", EInputActionValueType::Axis2D);
        E2E_ASSERT_EQ(moveA, moveB);

        uint32_t countB = 0u;
        E2E_ASSERT_STATUS_OK(hidB.replayActionSequence(recordedSeq, countB));
        E2E_ASSERT_EQ(countB, 3u);
        const FCT::Vec2 finalB = hidB.getActionValue(moveB).get<FCT::Vec2>();

        E2E_ASSERT_NEAR(finalA.x, finalB.x, 1e-5f);
        E2E_ASSERT_NEAR(finalA.y, finalB.y, 1e-5f);

        hidA.term();
        hidB.term();
        return true;
    }

    // S3-08: 聊天输入框聚焦联动 RmlUiKeyboardBridge 与 FCT::SAI::VirtualKeyboard UTF-8 截断回填收起闭环
    E2E_TEST(Tier4, FEAT_53, S3_08_ChatInputFocusVirtualKeyboardUtf8TruncateAndBlurClose)
    {
        FCT::SAI::VirtualKeyboard::clearCallbacks();
        (void)FCT::SAI::VirtualKeyboard::hide();

        std::string confirmedChat;
        RmlUiKeyboardBridge kbBridge;
        kbBridge.setElementMaxLength("chat_input", 6);
        kbBridge.onElementFocus("chat_input", "text", "", 6);
        E2E_ASSERT_TRUE(kbBridge.isKeyboardActive());
        E2E_ASSERT_TRUE(FCT::SAI::VirtualKeyboard::isShowing());

        FCT::SAI::VirtualKeyboard::setOnConfirm(KeyboardConfirmCollector{&confirmedChat});
        FCT::SAI::VirtualKeyboard::simulateInput("你好抖音小游戏世界12345");
        const std::string truncatedVal = kbBridge.getElementValue("chat_input");
        E2E_ASSERT_FALSE(truncatedVal.empty());
        E2E_ASSERT_EQ(RmlUiKeyboardBridge::truncateToMaxLength("你好抖音小游戏世界", 4), "你好抖音");

        FCT::SAI::VirtualKeyboard::simulateConfirm("组队开黑");
        E2E_ASSERT_EQ(confirmedChat, "组队开黑");

        kbBridge.onElementBlur("chat_input");
        E2E_ASSERT_FALSE(kbBridge.isKeyboardActive());
        FCT::SAI::VirtualKeyboard::clearCallbacks();
        return true;
    }
} // namespace MQEngine::Testing
