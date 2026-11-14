#include "GameApplication.h"
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/resource/core/resource.h>
#include <spdlog/spdlog.h>

namespace MQEngine
{
    void GameApplication::init()
    {
        spdlog::info("[GameApplication] 初始化独立游戏运行时，激活标签: [Runtime, Game]");

        auto* res = Resource::Resource::InstancePtr();
        auto* domain = Domain::Domain::InstancePtr();
        if (!res || !domain)
        {
            return;
        }

        const auto* cfg = res->getConfig();
        auto* loader = res->getResourceLoader();
        if (!cfg || !loader)
        {
            return;
        }

        domain->renderer.setPrimaryPresentTargetName("MainWindow");

        std::string targetScene = cfg->getScenePath();
        if (targetScene.empty())
        {
            const auto& proj = cfg->getCurrentProject();
            targetScene = !proj.startupScene.empty() ? proj.startupScene : proj.initialSceneUuid;
        }

        if (targetScene.empty())
        {
            spdlog::info("[GameApplication] 未配置主场景，启动纯净空场景");
            return;
        }

        std::string resolvedScene = targetScene;
        if (!loader->exists(resolvedScene) && !cfg->getProjectDir().empty())
        {
            const std::string candidate = SAI::Path::resolve(cfg->getProjectDir(), targetScene);
            if (loader->exists(candidate))
            {
                resolvedScene = candidate;
            }
        }

        if (!loader->exists(resolvedScene))
        {
            spdlog::warn("[GameApplication] 配置的主场景文件不存在，保持空场景启动: {}", resolvedScene);
            return;
        }

        Status loadSt = domain->scene.loadSceneFromFile(resolvedScene, loader);
        if (!loadSt.ok())
        {
            spdlog::error("[GameApplication] 加载主场景失败 [{}]: {}",
                          resolvedScene, std::string(loadSt.message()));
            return;
        }
        domain->renderer.refreshActiveCameras();
        spdlog::info("[GameApplication] 已加载工程主场景: {}", resolvedScene);
    }

    void GameApplication::logicTick()
    {
        ZoneScopedN("GameApplication::logicTick");

        // 检查退出快捷键 (Escape)
        auto* domain = Domain::Domain::InstancePtr();
        if (domain && domain->hid.isKeyJustPressed(Domain::HID::KeyCode::Escape))
        {
            if (auto* logicHost = Host::LogicHost::InstancePtr())
            {
                spdlog::info("[GameApplication] 收到 Escape 按键，触发优雅退出流程");
                logicHost->requestExit();
            }
        }
    }
} // namespace MQEngine
