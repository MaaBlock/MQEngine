/**
 * @file classdef.h
 * @brief Domain 模块前向类型声明与基础结构定义
 */
#ifndef MQENGINE_DOMAIN_CLASSDEF_H
#define MQENGINE_DOMAIN_CLASSDEF_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace MQEngine
{
    namespace Domain
    {
        class Domain;

        namespace Script
        {
            class JSSubsystem;
            class PySubsystem;
        } // namespace Script

        namespace Physics
        {
            class PhysicsSubsystem;
            struct RaycastHit;
            struct CollisionEvent;
            struct TriggerEvent;
        } // namespace Physics

        namespace HID
        {
            class HIDSubsystem;
            class InputActionManager;
            enum class KeyCode : uint32_t;
            enum class MouseButton : uint32_t;
        } // namespace HID

        namespace Debug
        {
            class DebugSubsystem;
            class Console;
            class GizmoRenderer;
        } // namespace Debug

        namespace Renderer
        {
            class RendererSubsystem;
            class PassManager;
            class PipelineManager;
        } // namespace Renderer

        namespace Scene
        {
            class SceneSubsystem;
            class TransformGraph;
            class SpatialGraph;
            class SceneTrunk;
            class SceneReplay;
            struct HierarchyRelation;
            struct LocalTransform;
            struct WorldTransform;
        } // namespace Scene

        namespace Animation
        {
            class AnimationSubsystem;
            struct SkeletalPose;
            class BlendTree;
        } // namespace Animation

        namespace Effect
        {
            class EffectSubsystem;
            class ParticleEmitter;
        } // namespace Effect

        namespace GamePlay
        {
            class GamePlaySubsystem;
            class GameStateMachine;
            class BehaviorComponent;
        } // namespace GamePlay

    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_CLASSDEF_H
