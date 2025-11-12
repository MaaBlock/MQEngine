//
// Created by Administrator on 2025/1/22.
//

#include "./LightingSystem.h"
#include "../core/engine.h"

namespace MQEngine
{
    LightingSystem::LightingSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager)
    {
        m_directionalLightUniform = FCT::Uniform(m_ctx, DirectionalLightUniformSlot);
        m_shadowUniform = FCT::Uniform(m_ctx, ShadowUniformSlot);
        m_shadowSampler = m_ctx->createResource<Sampler>();
        m_shadowSampler->setShadowMap();
        m_shadowSampler->create();
    }

    void LightingSystem::updateLogic()
    {
        m_hasDirectionalLight = false;
        updateDirectionalLight();
        updateShadowMatrix();

        if (!m_hasDirectionalLight)
        {
            bindDefaultDirectionalLight();
        }
    }
    void LightingSystem::updateRender()
    {
        m_directionalLightUniform.update();
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
        m_directionalLightUniform.setValue("directionalLightDirection",
            FCT::Vec4(0.0f, -1.0f, 0.0f, 0.0f));
        
        m_directionalLightUniform.setValue("directionalLightColor", FCT::Vec3(1.0f, 1.0f, 1.0f));
        
        m_directionalLightUniform.setValue("directionalLightIntensity", 1.0f);
        
        m_directionalLightUniform.setValue("directionalLightEnable", false);
    }

    void LightingSystem::setShadowUniformValues(const FCT::Mat4& directionalLightMvp)
    {
        m_shadowUniform.setValue("directionalLightMvp", directionalLightMvp);
    }

    void LightingSystem::bindUniforms(FCT::Layout* layout)
    {
        layout->bindUniform(m_directionalLightUniform);
        layout->bindUniform(m_shadowUniform);
    }
    void LightingSystem::bindResources(FCT::Layout* layout)
    {
        layout->bindSampler("shadowSampler",m_shadowSampler);
    }
    std::vector<FCT::UniformSlot> LightingSystem::getUniformSlots() const
    {
        return {DirectionalLightUniformSlot, ShadowUniformSlot};
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