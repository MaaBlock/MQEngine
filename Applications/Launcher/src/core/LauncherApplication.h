#ifndef ENGINE_LAUNCHER_APPLICATION_H
#define ENGINE_LAUNCHER_APPLICATION_H

#include "../thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief 启动器宿主应用程序极简纯壳（具体业务与界面逻辑由 LauncherProject Native 模块承载）
 */
class LauncherApplication : public Runtime::Application {
public:
    LauncherApplication() = default;
    ~LauncherApplication() override = default;

    void registerInfo() override;
    void init() override {}
    void logicTick() override {}
};

} // namespace MQEngine

#endif // ENGINE_LAUNCHER_APPLICATION_H
