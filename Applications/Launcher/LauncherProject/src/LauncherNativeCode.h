#ifndef MQENGINE_LAUNCHER_NATIVE_CODE_H
#define MQENGINE_LAUNCHER_NATIVE_CODE_H

#include "thirdparty/thirdparty.h"
#include "LauncherComposite.h"

namespace MQEngine {

/**
 * @brief 启动器 Native 用户代码模块，向引擎注入 LauncherComposite 组合体
 */
class LAUNCHER_NATIVE_API LauncherNativeCode : public Runtime::NativeCode {
public:
    LauncherNativeCode();
    ~LauncherNativeCode() override;

    void registerInfo() override;
    Status onLoad(Runtime::Engine* engine) override;
    Status onInit(Runtime::Engine* engine) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

    std::string getName() const override { return "LauncherNative"; }
    std::string getVersion() const override { return "1.0.0"; }

    LauncherComposite* getLauncherComposite() const { return m_launcherComposite; }

private:
    Runtime::Engine* m_engine = nullptr;
    LauncherComposite* m_launcherComposite = nullptr;
};

} // namespace MQEngine

extern "C" LAUNCHER_NATIVE_API MQEngine::Runtime::NativeCode* CreateUserCode();

#endif // MQENGINE_LAUNCHER_NATIVE_CODE_H
