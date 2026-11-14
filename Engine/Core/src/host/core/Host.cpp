#include <MQEngine/host/core/Host.h>
#include <MQEngine/resource/core/CommandLineParser.h>
#include <MQEngine/resource/core/resource.h>

namespace MQEngine
{
    namespace Host
    {
        namespace
        {
            /**
             * @brief Host 层命令行参数职责链处理器：解析并提取无头模式（headless）参数存入 Host::Config
             */
            class HostCommandLineHandler : public Resource::ICommandLineHandler
            {
            public:
                explicit HostCommandLineHandler(Config& config) noexcept
                    : m_config(config)
                {
                }

            protected:
                bool onHandle(const Resource::CommandLineArg& arg) override
                {
                    if (arg.matchesKey("headless") || arg.matchesKey("no-window")) {
                        m_config.headless = arg.asBool(true);
                        return true;
                    }
                    if (arg.matchesKey("test-frames") || arg.matchesKey("test_frames") || arg.matchesKey("max-frames") || arg.matchesKey("max_frames")) {
                        m_config.hasMaxFrames = true;
                        m_config.maxFrames = static_cast<int32_t>(arg.asInt(30));
                        return true;
                    }
                    return false;
                }

            private:
                Config& m_config;
            };
        } // namespace

        Host::Host() = default;

        Host::~Host()
        {
            term();
        }

        void Host::registerInfo()
        {
            physicsHost.registerInfo();
            uiHost.registerInfo();
            logicHost.registerInfo();
            renderHost.registerInfo();
            workHost.registerInfo();

            Resource::Resource::Instance()->registerCommandLineHandler(makeUnique<HostCommandLineHandler>(config));
        }

        Status Host::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            if (config.headless || config.hasMaxFrames() || config.maxFrames > 0) {
                FCT::SetHeadlessDialogMode(true);
            }
#ifdef _WIN32
            SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
            _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
            _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif

            m_fctRuntime = makeUnique<FCT::Runtime>();

            // 按照架构契约顺序初始化各 Host: WorkHost -> UIHost -> PhysicsHost -> LogicHost -> RenderHost
            CHECK_STATUS(workHost.init());
            CHECK_STATUS(uiHost.init(m_fctRuntime.get(), config));
            CHECK_STATUS(physicsHost.init());
            CHECK_STATUS(logicHost.init());
            CHECK_STATUS(renderHost.init(m_fctRuntime.get(), uiHost.getWindow(), config));

            m_initialized = true;
            return OkStatus();
        }

        void Host::term() noexcept
        {
            if (!m_initialized) {
                return;
            }
            m_initialized = false;

            // 先确保渲染提交线程与 GPU 队列静止，再释放窗口 Swapchain，最后销毁 VkDevice 与工作线程池
            renderHost.requestExit();
            logicHost.term();
            physicsHost.term();
            uiHost.term();
            renderHost.term();
            workHost.term();

            if (m_fctRuntime) {
                m_fctRuntime.reset();
            }
        }

    } // namespace Host
} // namespace MQEngine
