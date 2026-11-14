/**
 * @file DouyinMiniGame.h
 * @brief DouyinMiniGame 官方插件统一公开导出聚合头
 */
#ifndef MQENGINE_DOUYINMINIGAME_HEADERS_H
#define MQENGINE_DOUYINMINIGAME_HEADERS_H

#include "DouyinMiniGameApi.h"

// 离线烘焙配置与五步发布流水线 (DouyinPackagerCliComposite)
#include "Composites/DouyinPackagerCliComposite/DouyinPackagerConfig.h"
#include "Composites/DouyinPackagerCliComposite/DouyinShaderCooker.h"
#include "Composites/DouyinPackagerCliComposite/DouyinAssetCooker.h"
#include "Composites/DouyinPackagerCliComposite/DouyinScriptAndWasmBuilder.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackageExporter.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackagerCliComposite.h"

// 桌面编辑器可视化配置与预览面板 (DouyinPackagerEditorComposite)
#include "Composites/DouyinPackagerEditorComposite/DouyinPackagerEditorComposite.h"

// 抖音小游戏宿主生态运行时桥接服务 (DouyinPlatformRuntimeComposite)
#include "Composites/DouyinPlatformRuntimeComposite/DouyinWebJsAdapter.h"
#include "Composites/DouyinPlatformRuntimeComposite/SidebarRetentionService.h"
#include "Composites/DouyinPlatformRuntimeComposite/RewardedVideoAdService.h"
#include "Composites/DouyinPlatformRuntimeComposite/GameRecorderService.h"
#include "Composites/DouyinPlatformRuntimeComposite/CapsuleAndOpenDataService.h"
#include "Composites/DouyinPlatformRuntimeComposite/MemoryWarningCoordinator.h"
#include "Composites/DouyinPlatformRuntimeComposite/DouyinPlatformRuntimeComposite.h"

#endif // MQENGINE_DOUYINMINIGAME_HEADERS_H
