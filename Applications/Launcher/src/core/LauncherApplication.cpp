#include "LauncherApplication.h"

using namespace FCT;

namespace MQEngine {

void LauncherApplication::registerInfo() {
    registerInfoImpl();
    applicationRegisterInfo = ApplicationRegisterInfo{
        .platformRequire = {
            .target = RenderTarget::Texture,
            .windowTitle = "MQEngine Launcher",
        },
        .defaultProjectPath = "Applications/Launcher/LauncherProject/project.mq",
        .tags = {"Runtime", "Launcher", "HtmlUi"},
    };
}

} // namespace MQEngine
