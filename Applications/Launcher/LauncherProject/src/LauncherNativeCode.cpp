#include "LauncherNativeCode.h"

namespace MQEngine {

LauncherNativeCode::LauncherNativeCode() {
    m_launcherComposite = emplaceComposite<LauncherComposite>();
}

LauncherNativeCode::~LauncherNativeCode() {
    onUnload();
}

void LauncherNativeCode::registerInfo() {
}

Status LauncherNativeCode::onLoad(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    spdlog::info("LauncherNativeCode::onLoad 完成");
    return OkStatus();
}

Status LauncherNativeCode::onInit(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    spdlog::info("LauncherNativeCode::onInit 完成");
    return OkStatus();
}

void LauncherNativeCode::onLogicTick(float /*dt*/) {
}

void LauncherNativeCode::onRenderTick() {
}

void LauncherNativeCode::onUnload() {
}

} // namespace MQEngine

extern "C" LAUNCHER_NATIVE_API MQEngine::Runtime::NativeCode* CreateUserCode() {
    return new MQEngine::LauncherNativeCode();
}
