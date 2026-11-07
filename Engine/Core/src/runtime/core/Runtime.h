//
// Created by MaaBlock on 2026/11/6.
//

#ifndef MQENGINE_RUNTIME_CORE_RUNTIME_H
#define MQENGINE_RUNTIME_CORE_RUNTIME_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../EnginePCH.h"
#include "../../base/classdef.h"
#include "../../effect/classdef.h"
#include "../../platform/classdef.h"
#include "../../render/classdef.h"
#include "../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "./CreateApplication.h"

namespace MQEngine
{
    namespace Runtime
    {
        /**
         * @brief 引擎运行时总控类：负责模块与 Manager 生命周期流程管理，
         *        通过编译期拓扑图 StaticTokenGraph 驱动模块初始化/逆序销毁以及 Manager 在进入 loop 前（prepare）与帧汇聚点的队列同步
         */
        class ENGINE_API Runtime
        {
        public:
            Runtime();
            ~Runtime();

            Runtime(const Runtime&) = delete;
            Runtime& operator=(const Runtime&) = delete;

            static Runtime& instance();
            static Runtime* s_instance;
            static void RegisterApplicationFactory(CreateApplicationFn fn);

            /**
             * @brief 解析命令行参数（委托 Engine::foundation->cliOption 职责链）
             */
            void parseCommandLine(int argc, char* argv[]);
            void parseCommandLine(const std::vector<std::string>& args);

            /**
             * @brief 完整初始化流程：
             * 1. registerModules()：接口 Application 最先 registerInfo()，随后按 ModuleStaticGraph 拓扑序调用 12 个核心模块 registerInfo()
             * 2. initModules()：按 ModuleStaticGraph 拓扑序自底向上调用 12 个核心模块 init()
             * 3. loadPluginsAndUserCode()：全部核心模块初始化完成后，由 Runtime 统一加载插件与用户代码、打开初始场景并启动脚本
             * 4. m_engine->application->init()：子类 Application 处于最上层，最后调用 init()
             */
            void init(int argc = 0, char* argv[] = nullptr);

            /**
             * @brief 启动运行主循环（内部先后调用 prepare() 与 loop()）
             */
            void run();

            /**
             * @brief 进入 loop() 前的准备阶段：按编译期拓扑图 ManagerStaticGraph 顺序执行全部 Manager 的 syncTicker() 并挂载渲染 Tickers
             */
            void prepare();

            /**
             * @brief 驱动引擎主循环（调度逻辑帧、插件/Composite 拓扑 Tick、热重载检查与渲染帧同步）
             */
            void loop();
            void flushLastFrame();
            void finalize();

            /**
             * @brief 驱动引擎退出与全模块逆序销毁流程（内部调用 termModules()）
             */
            void term();

            /**
             * @brief 按编译期拓扑图 ManagerStaticGraph 顺序执行全部 Manager 的 syncTicker()
             *        （在 prepare() 中进入 loop() 前执行一次，并在每帧逻辑与渲染线程汇聚点 ManagerSyncTicker 执行）
             */
            void flushManagers();

            void sync();
            void onSync();
            void syncRenderThread();

            Engine* getEngine() const noexcept { return m_engine.get(); }
            Application* getApplication() const noexcept;
            Base::Foundation* getFoundation() const noexcept;
            Host* getHost() const noexcept;
            EngineCLIOption* getCLIOption() const noexcept;
            Platform::Platform* getPlatform() const noexcept;
            Effect::Effector* getEffect() const noexcept;
            Render::Renderer* getRenderer() const noexcept;
            SystemManager& getSystemManager() noexcept;

            Status loadPlugin(const std::string& pluginIdent);
            Status unloadPlugin(const std::string& pluginName);
            Status reloadPlugin(const std::string& pluginName);
            void requestLoadPlugin(const std::string& pluginIdent);
            void requestUnloadPlugin(const std::string& pluginName);
            void requestReloadPlugin(const std::string& pluginName);

            Status loadUserCode(const std::string& userCodeIdent);
            Status reloadUserCode(const std::string& moduleName = "");
            void requestLoadUserCode(const std::string& userCodeIdent);
            void requestUnloadUserCode(const std::string& moduleName);
            void requestReloadUserCode(const std::string& moduleName = "");

            void hotReloadAll();
            bool isAutoHotReloadEnabled() const;
            void setAutoHotReloadEnabled(bool enabled);
            std::string getProjectName() const;
            void requestExit() { setShuttingDown(true); }

            int getArgc() const noexcept { return m_argc; }
            char** getArgv() const noexcept { return m_argv; }
            float getFixedTimestep() const noexcept { return m_fixedTimestep; }

            void alignWorkingDirectory();

            bool hasTag(const std::string& tag) const;
            bool isPlaying() const { return m_playing; }
            void setPlayMode(bool playing);

            bool isShuttingDown() const { return m_shuttingDown; }
            void setShuttingDown(bool shuttingDown) { m_shuttingDown = shuttingDown; }

            bool isHeadless() const { return m_headless; }
            void setHeadless(bool headless);

            int32_t getTestFrames() const { return m_testFrames; }
            void setTestFrames(int32_t frames);
            bool hasTestFramesLimit() const { return m_testFrames > 0; }

            bool hasScreenshotPath() const { return !m_screenshotPath.empty(); }
            const std::string& getScreenshotPath() const { return m_screenshotPath; }
            void setScreenshotPath(const std::string& path);

            int32_t getCaptureFrame() const { return m_captureFrame; }
            void setCaptureFrame(int32_t frame);

            const std::string& getCaptureTarget() const { return m_captureTarget; }
            void setCaptureTarget(const std::string& target);

            bool hasProjectPath() const { return !m_projectPath.empty(); }
            const std::string& getProjectPath() const { return m_projectPath; }
            void setProjectPath(const std::string& path);

            bool isProjectOpened() const { return m_projectOpened; }
            void setProjectOpened(bool opened) { m_projectOpened = opened; }

            bool shouldOpenProjectOnInit() const { return !m_projectPath.empty(); }

            bool hasLocal() const { return !m_localName.empty(); }
            const std::string& getLocal() const { return m_localName; }
            void setLocal(const std::string& localName);

            bool hasOption(const std::string& key) const;
            std::string getOption(const std::string& key, const std::string& defaultVal = "") const;
            void setOption(const std::string& key, const std::string& value);

            const std::vector<std::string>& getRawArgs() const;

        private:
            void registerModules();
            void initModules();
            void loadPluginsAndUserCode();
            void termModules();
            void unregisterModules();
            void unloadPluginsAndUserCode();
            void logicTick();

        private:
            UniquePtr<Engine> m_engine;
            int m_argc = 0;
            char** m_argv = nullptr;

            uint64_t m_frameCount = 0;
            float m_fixedTimestep = 1.0f / 60.0f;
            float m_accumulator = 0.0f;
            int m_maxSubSteps = 4;

            bool m_playing = false;
            bool m_shuttingDown = false;
            bool m_headless = false;
            int32_t m_testFrames = 0;
            int32_t m_captureFrame = 0;
            std::string m_screenshotPath;
            std::string m_captureTarget = "SceneColorTarget";
            bool m_projectOpened = false;
            std::string m_projectPath;
            std::string m_localName;
        };

    } // namespace Runtime
} // namespace MQEngine

#endif // MQENGINE_RUNTIME_CORE_RUNTIME_H
