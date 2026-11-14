/**
 * @file domain.h
 * @brief Domain 模块公共聚合头文件
 */
#ifndef MQENGINE_DOMAIN_H
#define MQENGINE_DOMAIN_H

#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/script/js/JSSubsystem.h>
#include <MQEngine/domain/script/py/PySubsystem.h>
#include <MQEngine/domain/physics/PhysicsSubsystem.h>
#include <MQEngine/domain/hid/InputAction.h>
#include <MQEngine/domain/hid/InputModifier.h>
#include <MQEngine/domain/hid/InputTrigger.h>
#include <MQEngine/domain/hid/InputMappingContext.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/debug/DebugSubsystem.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <MQEngine/domain/renderer/ShadowAtlas.h>
#include <MQEngine/domain/renderer/clustering/ClusteredForwardPlus.h>
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/renderer/RenderPacket.h>
#include <MQEngine/domain/renderer/ShaderGenerator.h>
#include <MQEngine/domain/renderer/Pass/GBufferPass.h>
#include <MQEngine/domain/renderer/Pass/DeferredLightingPass.h>
#include <MQEngine/domain/renderer/Tech/RenderTechnique.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/domain/scene/SpatialGraph.h>
#include <MQEngine/domain/scene/SceneTrunk.h>
#include <MQEngine/domain/scene/SceneReplay.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/animation/AnimationSubsystem.h>
#include <MQEngine/domain/effect/EffectSubsystem.h>
#include <MQEngine/domain/gameplay/GamePlaySubsystem.h>
#include <MQEngine/domain/core/Domain.h>

#endif // MQENGINE_DOMAIN_H
