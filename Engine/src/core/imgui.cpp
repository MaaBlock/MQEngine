#include "../engineapi.h"
#include "./Uniform.h"
#define TEXT(str) (const char*)u8##str

namespace MQEngine
{
    void Engine::settingUpImgui()
    {
        auto graph = m_ctx->getModule<RenderGraph>();

        m_imguiCtx = m_imguiModule.createContext(m_wnd,m_ctx);
        m_imguiCtx->pass(graph->getPass("ImguiPass"));
        m_imguiCtx->create(ImguiContextCreateFlag::Docking);
        m_imguiCtx->enableChinese();
        m_imguiCtx->addTexture("shadowPos",m_shadowPosTarget);
        m_imguiCtx->addTexture("shadowDepth",m_shadowRetTarget);
        m_imguiCtx->addTexture("lightWorld",m_lightDepthImage);
        m_wnd->swapchain()->subscribe<SwapchainEvent::Recreate>([this](SwapchainEvent::Recreate)
        {
            m_imguiCtx->updateTexture("shadowPos");
            m_imguiCtx->updateTexture("shadowDepth");
        });
    }
    void Engine::imguiLogicTick()
    {

        m_imguiCtx->push([this]()
        {
            ImGui::Begin("MQ Engine");
            ImGui::Text("Version: %s", getEngineVersion());
            ImGui::Text(TEXT("灯光类型:"));
            const char* lightTypes[] = {
                TEXT("点光源"),
                TEXT("方向光"),
                TEXT("聚光灯")
            };
            ImGui::Combo("##LightType", &m_lightType, lightTypes, 3);
            ImGui::Separator();

            if (m_lightType == 0 || m_lightType == 2) {
                ImGui::Text(TEXT("光源距离:"));
                if (ImGui::SliderFloat("##LightDistance", &m_lightDistance, 10.0f, 100.0f)) {
                    Vec3 currentDir = Vec3(m_lightPos.x, m_lightPos.y, m_lightPos.z).normalize();
                    m_lightPos = Vec4(currentDir * m_lightDistance, 1.0f);
                    m_baseUniform->setValue("lightPos", m_lightPos);
                    m_baseUniform->setValue("lightDirection", (-m_lightPos).normalize());
                }
                ImGui::Text(TEXT("当前位置: (%.1f, %.1f, %.1f)"), m_lightPos.x, m_lightPos.y, m_lightPos.z);
            }

            ImGui::Text(TEXT("环境光颜色:"));
            if (ImGui::ColorEdit3("##AmbientColor", m_ambientColor)) {
                m_baseUniform->setValue("ambientColor", Vec3(m_ambientColor[0], m_ambientColor[1], m_ambientColor[2]));
            }

            ImGui::Text(TEXT("漫反射颜色:"));
            if (ImGui::ColorEdit3("##DiffuseColor", m_diffuseColor)) {
                m_baseUniform->setValue("diffuseColor", Vec3(m_diffuseColor[0], m_diffuseColor[1], m_diffuseColor[2]));
            }

            ImGui::Text(TEXT("镜面反射颜色:"));
            if (ImGui::ColorEdit3("##SpecularColor", m_specularColor)) {
                m_baseUniform->setValue("specularColor", Vec3(m_specularColor[0], m_specularColor[1], m_specularColor[2]));
            }

            ImGui::Text(TEXT("光泽度:"));
            if (ImGui::SliderFloat("##Shininess", &m_shininess, 1.0f, 256.0f)) {
                m_baseUniform->setValue("shininess", m_shininess);
            }

            if (m_lightType == 0) {
                ImGui::Separator();
                ImGui::Text(TEXT("点光源衰减参数:"));

                if (ImGui::SliderFloat(TEXT("常数项"), &m_constant, 0.1f, 2.0f)) {
                    m_baseUniform->setValue("constant", m_constant);
                }

                if (ImGui::SliderFloat(TEXT("线性项"), &m_linearAttenuation, 0.01f, 0.5f)) {
                    m_baseUniform->setValue("linearAttenuation", m_linearAttenuation);
                }

                if (ImGui::SliderFloat(TEXT("二次项"), &m_quadratic, 0.001f, 0.1f)) {
                    m_baseUniform->setValue("quadratic", m_quadratic);
                }
            }

            if (m_lightType == 2)
            {
                ImGui::Separator();
                ImGui::Text(TEXT("聚光灯参数:"));

                if (ImGui::SliderFloat(TEXT("切光角度"), &m_cutOffAngle, 10.0f, 90.0f)) {
                    float cutOffRad = m_cutOffAngle * 3.1415926535f / 180.0f; // 转换为弧度
                    m_baseUniform->setValue("cutOff", cos(cutOffRad));
                }
            }

            ImGui::Separator();
            if (ImGui::Button(TEXT("重置为默认值"))) {
                initUniformValue();
            }

            ImGui::End();

            ImGui::Begin(TEXT("阴影位置贴图"));
            auto shadowPosTextureId = m_imguiCtx->getTexture("shadowPos");
            if (shadowPosTextureId) {
                ImVec2 windowSize = ImGui::GetContentRegionAvail();
                float aspectRatio = 800.0f / 600.0f;
                float imageWidth = windowSize.x;
                float imageHeight = imageWidth / aspectRatio;

                if (imageHeight > windowSize.y) {
                    imageHeight = windowSize.y;
                    imageWidth = imageHeight * aspectRatio;
                }

                ImGui::Image(shadowPosTextureId, ImVec2(imageWidth, imageHeight));
            } else {
                ImGui::Text(TEXT("阴影位置贴图未找到"));
            }
            ImGui::End();

            ImGui::Begin(TEXT("阴影深度贴图"));
            auto shadowDepthTextureId = m_imguiCtx->getTexture("shadowDepth");
            if (shadowDepthTextureId) {
                ImVec2 windowSize = ImGui::GetContentRegionAvail();
                float aspectRatio = 800.0f / 600.0f;
                float imageWidth = windowSize.x;
                float imageHeight = imageWidth / aspectRatio;

                if (imageHeight > windowSize.y) {
                    imageHeight = windowSize.y;
                    imageWidth = imageHeight * aspectRatio;
                }

                ImGui::Image(shadowDepthTextureId, ImVec2(imageWidth, imageHeight));
            } else {
                ImGui::Text(TEXT("阴影深度贴图未找到"));
            }
            ImGui::End();
            ImGui::Begin(TEXT("光源深度贴图"));
            auto lightDepthTextureId = m_imguiCtx->getTexture("lightWorld");
            if (lightDepthTextureId) {
                ImVec2 windowSize = ImGui::GetContentRegionAvail();
                float imageSize = std::min(windowSize.x, windowSize.y);

                ImGui::Text(TEXT("从光源视角看到的深度图"));
                ImGui::Text(TEXT("分辨率: 2048x2048"));
                ImGui::Separator();

                ImGui::Image(lightDepthTextureId, ImVec2(imageSize, imageSize));

                ImGui::Separator();
                ImGui::Text(TEXT("光源位置: (%.1f, %.1f, %.1f)"), m_lightPos.x, m_lightPos.y, m_lightPos.z);

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(TEXT("这是从光源视角渲染的深度图\n用于阴影映射计算"));
                }
            } else {
                ImGui::Text(TEXT("光源深度贴图未找到"));
                ImGui::Text(TEXT("请检查纹理是否正确创建和绑定"));
            }
            ImGui::End();
        });
    }
}