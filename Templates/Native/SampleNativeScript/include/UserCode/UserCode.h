#ifndef MQENGINE_USER_CODE_H
#define MQENGINE_USER_CODE_H

#include "thirdparty/thirdparty.h"
#include <string>

#ifdef _WIN32
    #ifdef BUILD_USER_CODE
        #define USER_CODE_API __declspec(dllexport)
    #else
        #define USER_CODE_API __declspec(dllimport)
    #endif
#else
    #define USER_CODE_API
#endif

namespace MQEngine {

/**
 * @brief 用户代码自定义自转组件（基于 C++20 Boost.PFR 零宏全自动反射，无需编写 BOOST_DESCRIBE_STRUCT 与 serialize）
 */
struct USER_CODE_API UserRotateComponent {
    float speed = 45.0f;
    FCT::Vec3<> axis = FCT::Vec3(0.0f, 1.0f, 0.0f);
    bool enabled = true;
};

/**
 * @brief 用户代码自定义自转系统（驱动携带 UserRotateComponent 的实体旋转）
 */
class USER_CODE_API UserRotateSystem : public ISystem {
public:
    explicit UserRotateSystem(DataManager* dataManager);
    ~UserRotateSystem() override = default;

    void updateLogic() override;
    void updateRender() override;

    void setDeltaTime(float dt) { m_deltaTime = dt; }

private:
    DataManager* m_dataManager = nullptr;
    float m_deltaTime = 0.016f;
};

/**
 * @brief 默认工程 C++ 用户代码模块实现（位于 Engine 与 Plugin 之上）
 */
class USER_CODE_API UserCodeModule : public Runtime::NativeCode {
public:
    UserCodeModule();
    ~UserCodeModule() override;

    Status onLoad(Runtime::Engine* engine) override;
    Status onInit(Runtime::Engine* engine) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

    std::string getName() const override { return "UserCode"; }
    std::string getVersion() const override { return "1.0.0"; }

    Status initRuntime(Runtime::Engine* engine);
    void tickRuntime(float dt);
    void unloadRuntime();

private:
    Runtime::Engine* m_engine = nullptr;
    UniquePtr<UserRotateSystem> m_rotateSystem;
    bool m_systemRegistered = false;
};

} // namespace MQEngine

extern "C" USER_CODE_API MQEngine::Runtime::NativeCode* CreateUserCode();

#endif // MQENGINE_USER_CODE_H
