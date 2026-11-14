#include <MQEngine/runtime/core/Runtime.h>
#include <cassert>
#include <chrono>
#include <MQEngine/Engine.h>
#include <MQEngine/resource/table/GpuResourceTable.h>

namespace MQEngine
{
    namespace Runtime
    {
        namespace
        {
            /**
             * @brief 顶层核心模块的编译期拓扑依赖图（Resource -> Host -> Asset -> Domain）
             */
            using ModuleStaticGraph =
                StaticTokenGraph<
                    StaticTokenNode<Resource::Resource, TypeList<>, TypeList<Host::Host>>,
                    StaticTokenNode<Host::Host, TypeList<Resource::Resource>, TypeList<Asset::Asset>>,
                    StaticTokenNode<Asset::Asset, TypeList<Host::Host>, TypeList<Domain::Domain>>,
                    StaticTokenNode<Domain::Domain, TypeList<Asset::Asset>, TypeList<>>>;

            template <typename ModuleType>
            constexpr ModuleType& getEngineModule(Engine& engine) noexcept
            {
                if constexpr (std::is_same_v<ModuleType, Resource::Resource>)
                {
                    return engine.resource;
                }
                else if constexpr (std::is_same_v<ModuleType, Host::Host>)
                {
                    return engine.host;
                }
                else if constexpr (std::is_same_v<ModuleType, Asset::Asset>)
                {
                    return engine.asset;
                }
                else if constexpr (std::is_same_v<ModuleType, Domain::Domain>)
                {
                    return engine.domain;
                }
            }
        } // namespace

        void Runtime::RegisterApplicationFactory(CreateApplicationFn fn) { CreateApplication = fn; }

        void Runtime::init(int argc, char* argv[])
        {
            FCT::Profiling::init();
            ZoneScoped();

            registerModules();
            m_engine->resource.setCommandLineArgs(argc, argv);
            initModules();
            m_engine->application->init();
        }

        void Runtime::run()
        {
            prepare();
            loop();
            finalize();
        }

        void Runtime::term()
        {
            ZoneScoped();
            spdlog::info("[Runtime] term() started");
            termModules();
            unregisterModules();
        }

        void Runtime::prepare()
        {
            ZoneScoped();
        }

        void Runtime::loop()
        {
            ZoneScopedN("Runtime::loop");
            FCT::SetThreadName("Main/LogicThread");

            auto* uiHost = m_engine->host.getUIHost();
            auto* logicHost = m_engine->host.getLogicHost();
            auto* physicsHost = m_engine->host.getPhysicsHost();
            auto* renderHost = m_engine->host.getRenderHost();
            auto* workHost = m_engine->host.getWorkHost();

            while (logicHost->loopRunning())
            {
                ZoneScopedN("Engine::Frame");
                {
                    ZoneScopedN("WorkHost::sync");
                    workHost->sync();
                }
                {
                    ZoneScopedN("PhysicsHost::sync");
                    physicsHost->sync();
                }
                {
                    ZoneScopedN("UIHost::sync");
                    uiHost->sync();
                }
                {
                    ZoneScopedN("LogicHost::sync");
                    logicHost->sync();
                }
                {
                    ZoneScopedN("RenderHost::sync");
                    renderHost->sync();
                }
                FrameMark;
            }
            spdlog::info("[Runtime] loop() exited");
        }

        void Runtime::finalize()
        {
            ZoneScoped();
            spdlog::info("[Runtime] finalize() started");
            if (m_engine) {
                auto* renderHost = m_engine->host.getRenderHost();
                if (renderHost) {
                    renderHost->flushLastFrame();
                }
            }
            spdlog::info("[Runtime] finalize() completed");
        }

        void Runtime::registerModules()
        {
            ZoneScoped();
            m_engine = makeUnique<Engine>();
            Engine::s_instance = m_engine.get();

            assert(CreateApplication != nullptr && "Application 工厂函数未注册");
            m_engine->application = CreateApplication();
            assert(m_engine->application != nullptr && "CreateApplication 返回空指针");

            ModuleStaticGraph::forEachForward([this]<typename ModuleType>()
                                              { getEngineModule<ModuleType>(*m_engine).registerInfo(); });

            m_engine->application->registerInfo();
            m_engine->initializeActiveApplicationTags();
        }

        void Runtime::initModules()
        {
            ZoneScoped();
            assert(m_engine != nullptr && "Engine 不能为空");
            m_engine->initializeActiveApplicationTags();
            ModuleStaticGraph::forEachForward(
                [this]<typename ModuleType>()
                {
                    Status st = getEngineModule<ModuleType>(*m_engine).init();
                    if (!st.ok()) {
                        spdlog::error("[FATAL] Engine module init failed: {} - {}", typeid(ModuleType).name(), st.message());
                        std::fprintf(stderr, "[FATAL] Engine module init failed: %s - %s\n", typeid(ModuleType).name(), std::string(st.message()).c_str());
                        std::fflush(stderr);
                    }
                    assert(st.ok() && "Engine 核心模块初始化必须成功");
                });
        }

        void Runtime::termModules()
        {
            ZoneScoped();
            if (!m_engine) {
                return;
            }

            auto* logicHost = m_engine->host.getLogicHost();
            auto* workHost = m_engine->host.getWorkHost();
            auto* renderHost = m_engine->host.getRenderHost();
            FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;

            spdlog::info("[Runtime] termModules() Phase 1: Request Exit");
            // Phase 1: 停止接收新输入，主循环退出标志生效 (Input Cutoff & Loop Terminate)
            if (logicHost) {
                logicHost->requestExit();
            }

            spdlog::info("[Runtime] termModules() Phase 2: Drain Work & GPU");
            // Phase 2: 等待 WorkHost 后台任务排空与 GPU 提交线程停止与静止 (Drain Work & GPU)
            if (workHost) {
                workHost->waitAll();
            }
            if (ctx) {
                ctx->stopSubmitThread();
                ctx->waitGpu();
            }

            spdlog::info("[Runtime] termModules() Phase 3: Unload Composites Reverse");
            // Phase 3: 严格按拓扑逆序卸载与销毁 Composites (Unload Composites Reverse)
            if (auto* compositeMgr = m_engine->asset.getCompositeManager()) {
                compositeMgr->unloadComposites();
            }

            // Phase 4: 逆序销毁 Domain 业务子域 (Destroy Domain Subsystems)
            m_engine->domain.term();

            // Phase 5: 释放 Asset 与 GPU 资源，清空 GpuResourceTable (Release Assets & GPU)
            m_engine->asset.term();

            if (ctx) {
                ctx->flushAllDeferredReleases();
            }
            if (auto* gpuTable = Resource::GpuResourceTable::InstancePtr()) {
                gpuTable->clear();
            }
            if (ctx) {
                ctx->cleanupGpuResources();
            }

            // Phase 6: 逆序终止 Host 与 Resource 资源管理器 (Terminate Hosts & Resource)
            m_engine->host.term();
            m_engine->resource.term();
        }

        void Runtime::unregisterModules()
        {
            // 动态与 Native 插件 DLL 的 FreeLibrary 必须晚于 m_engine.reset()，确保 Resource::DataBase 与 Host 回调在模块代码/虚表卸载前彻底析构
            std::vector<void*> deferredLibraryHandles;
            if (m_engine)
            {
                deferredLibraryHandles = m_engine->asset.compositeManager.takeDeferredLibraryHandles();
                if (m_engine->application)
                {
                    FCT_DELETE(m_engine->application);
                    m_engine->application = nullptr;
                }
                if (Engine::s_instance == m_engine.get())
                {
                    Engine::s_instance = nullptr;
                }
                m_engine.reset();
            }
            Asset::CompositeManager::closeLibraryHandles(deferredLibraryHandles);
        }

    } // namespace Runtime
} // namespace MQEngine
