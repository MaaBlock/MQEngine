/**
 * @file CliEditorApplication.cpp
 * @brief CliEditor 宿主应用壳实现（支持渐进式 CLI 编辑器与 --douyin-cook 离线烘焙路由检查）
 */
#include "CliEditorApplication.h"
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/host/logic/LogicHost.h>

#include <cstdio>

namespace MQEngine
{
    void CliEditorApplication::init()
    {
    }

    void CliEditorApplication::logicTick()
    {
        auto* res = Resource::Resource::InstancePtr();
        if (!res || !res->getCommandLineParser())
        {
            return;
        }

        bool douyinCookFlag = false;
        for (const auto& arg : res->getCommandLineParser()->getParsedArgs())
        {
            if (arg.matchesKey("douyin-cook"))
            {
                douyinCookFlag = arg.asBool(true);
                break;
            }
        }
        if (!douyinCookFlag)
        {
            return;
        }

        auto* assetMod = Asset::Asset::InstancePtr();
        if (!assetMod || !assetMod->compositeManager.getComposite("DouyinPackagerCliComposite"))
        {
            std::fprintf(stderr, "[CliEditor][ERROR] 未发现 DouyinPackagerCliComposite，请确认 DouyinMiniGame 插件已构建并部署。\n");
            std::fflush(stderr);
            if (auto* logicHost = Host::LogicHost::InstancePtr())
            {
                logicHost->requestExit();
            }
        }
    }
} // namespace MQEngine
