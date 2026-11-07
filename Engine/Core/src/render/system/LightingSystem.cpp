//
// Created by Administrator on 2025/1/22.
//

#include "./LightingSystem.h"
#include "../../runtime/core/Engine.h"

namespace MQEngine
{
    LightingSystem::LightingSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager)
    {
        m_directionalLightUniform = FCT::Uniform(m_ctx, DirectionalLightUniformSlot);
        m_pointLightsUniform = FCT::Uniform(m_ctx, PointLightUniformSlot);
        m_shadowUniform = FCT::Uniform(m_ctx, ShadowUniformSlot);
        m_shadowSampler = m_ctx->createResource<Sampler>();
        m_shadowSampler->setShadowMap();
        m_shadowSampler->create();
        updatePointLights();
    }

    void LightingSystem::updateLogic()
    {
        ZoneScoped();
        m_hasDirectionalLight = false;
        updateDirectionalLight();
        updatePointLights();
        updateShadowMatrix();

        if (!m_hasDirectionalLight)
            bindDefaultDirectionalLight();
    }

    void LightingSystem::updateRender()
    {
        ZoneScoped();
        m_directionalLightUniform.update();
        m_pointLightsUniform.update();
        m_shadowUniform.update();
    }

    void LightingSystem::updateDirectionalLight()
    {
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            auto view = registry->view<DirectionalLightComponent>();
            for (auto entity : view)
            {
                auto& light = view.get<DirectionalLightComponent>(entity);

                if (light.enabled) {
                    setDirectionalLightUniformValues(light);
                    m_hasDirectionalLight = true;
                    return; // 只使用第一个启用的方向光
                }
            }
        }
    }

    void LightingSystem::updatePointLights()
    {
        std::vector<FCT::Vec4> positions(MAX_POINT_LIGHTS, FCT::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
        std::vector<FCT::Vec4> colors(MAX_POINT_LIGHTS, FCT::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
        std::vector<FCT::Vec4> attenuations(MAX_POINT_LIGHTS, FCT::Vec4(1.0f, 0.09f, 0.032f, 0.0f));
        int lightCount = 0;

        if (m_dataManager)
        {
            auto registries = m_dataManager->currentRegistries();
            for (auto* registry : registries)
            {
                if (!registry)
                    continue;
                auto view = registry->view<PointLightComponent>();
                for (auto entity : view)
                {
                    if (static_cast<size_t>(lightCount) >= MAX_POINT_LIGHTS)
                        break;
                    const auto& light = view.get<PointLightComponent>(entity);
                    if (!light.enabled)
                        continue;

                    FCT::Vec3 pos(0.0f, 0.0f, 0.0f);
                    if (registry->all_of<PositionComponent>(entity))
                        pos = registry->get<PositionComponent>(entity).position;

                    positions[lightCount] = FCT::Vec4(pos.x, pos.y, pos.z, light.range);
                    colors[lightCount] = FCT::Vec4(light.color.x, light.color.y, light.color.z, light.intensity);
                    attenuations[lightCount] = FCT::Vec4(light.constant, light.linear, light.quadratic, 1.0f);
                    ++lightCount;
                }
            }
        }

        m_pointLightsUniform.setValue("pointLightPositions", positions);
        m_pointLightsUniform.setValue("pointLightColors", colors);
        m_pointLightsUniform.setValue("pointLightAttenuations", attenuations);
        m_pointLightsUniform.setValue("pointLightCount", lightCount);
    }

    void LightingSystem::bindDefaultDirectionalLight()
    {
        setDefaultDirectionalLightUniformValues();
    }

    void LightingSystem::setDirectionalLightUniformValues(const DirectionalLightComponent& light)
    {
        FCT::Vec3 normalizedDirection = light.direction;
        normalizedDirection.normalize();

        m_directionalLightUniform.setValue("directionalLightDirection",
            FCT::Vec4(normalizedDirection.x, normalizedDirection.y, normalizedDirection.z, 0.0f));
        
        m_directionalLightUniform.setValue("directionalLightColor", light.color);
        
        m_directionalLightUniform.setValue("directionalLightIntensity", light.intensity);
        
        m_directionalLightUniform.setValue("directionalLightEnable", true);
    }

    void LightingSystem::setDefaultDirectionalLightUniformValues()
    {
        FCT::Vec3 defaultDir(-0.4f, -0.85f, -0.35f);
        defaultDir.normalize();
        m_directionalLightUniform.setValue("directionalLightDirection",
            FCT::Vec4(defaultDir.x, defaultDir.y, defaultDir.z, 0.0f));
        
        m_directionalLightUniform.setValue("directionalLightColor", FCT::Vec3(1.0f, 0.98f, 0.92f));
        
        m_directionalLightUniform.setValue("directionalLightIntensity", 1.8f);
        
        m_directionalLightUniform.setValue("directionalLightEnable", true);
    }

    void LightingSystem::setShadowUniformValues(const FCT::Mat4& directionalLightMvp)
    {
        m_shadowUniform.setValue("directionalLightMvp", directionalLightMvp);
    }

    void LightingSystem::bindUniforms(FCT::Layout* layout)
    {
        layout->bindUniform(m_directionalLightUniform);
        layout->bindUniform(m_pointLightsUniform);
        layout->bindUniform(m_shadowUniform);
    }
    void LightingSystem::bindResources(FCT::Layout* layout)
    {
        layout->bindSampler("shadowSampler",m_shadowSampler);
    }
    std::vector<FCT::UniformSlot> LightingSystem::getUniformSlots() const
    {
        return {DirectionalLightUniformSlot, PointLightUniformSlot, ShadowUniformSlot};
    }
    std::vector<FCT::SamplerSlot> LightingSystem::getSamplerSlots() const
    {
        return {
            {"shadowSampler"}
        };
    }

    void LightingSystem::updateShadowMatrix()
    {
        FCT::Vec3 lightDirection(0.0f, -1.0f, 0.0f);

        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            auto view = registry->view<DirectionalLightComponent>();
            for (auto entity : view)
            {
                auto& light = view.get<DirectionalLightComponent>(entity);
                if (light.enabled) {
                    lightDirection = light.direction;
                    lightDirection.normalize();
                    break;
                }
            }
        }

        FCT::Vec3 lightPos = FCT::Vec3(0.0f, 10.0f, 0.0f) - lightDirection * 20.0f;
        FCT::Vec3 lightTarget = FCT::Vec3(0.0f, 0.0f, 0.0f);
        FCT::Vec3 lightUp = FCT::Vec3(0.0f, 1.0f, 0.0f);

        if (abs(lightDirection.dot(lightUp)) > 0.99f) {
            lightUp = FCT::Vec3(1.0f, 0.0f, 0.0f);
        }

        FCT::Mat4 lightView = FCT::Mat4::LookAt(lightPos, lightTarget, lightUp);

        float orthoSize = 20.0f;
        FCT::Mat4 lightProjection = FCT::Mat4::Ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, 50.0f);

        FCT::Mat4 directionalLightMvp = lightProjection * lightView;

        setShadowUniformValues(directionalLightMvp);
    }
}