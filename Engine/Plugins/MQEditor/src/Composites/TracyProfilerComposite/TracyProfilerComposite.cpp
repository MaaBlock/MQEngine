/**
 * @file TracyProfilerComposite.cpp
 * @brief 内嵌上游原生 tracy::View 的性能剖析与帧遥测 Composite 实现
 */
#include "TracyProfilerComposite.h"
#include "TracyProfilerCallbacks.h"

#include <TracyWorker.hpp>
#include <TracyFileWrite.hpp>
#include <TracyFileRead.hpp>
#include <TracyView.hpp>

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>

#include <Composites/EditorUiComposite/EditorUiComposite.h>
#include <Composites/ImGuiComposite/ImGuiComposite.h>
#include <imgui.h>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kWindowTitle = "Profiler";
        } // namespace

        TracyProfilerComposite::TracyProfilerComposite() = default;

        TracyProfilerComposite::~TracyProfilerComposite()
        {
            onUnload();
        }

        Status TracyProfilerComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kWindowTitle,
                    EditorBase::EditorDockSlot::Bottom,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderProfilerPanel();
                    },
                    false);
            }

            return OkStatus();
        }

        Status TracyProfilerComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            return OkStatus();
        }

        void TracyProfilerComposite::onTick(float deltaTime)
        {
            (void)deltaTime;
        }

        void TracyProfilerComposite::onUnload()
        {
            stopCapture();

            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kWindowTitle);
                m_editorUiComposite = nullptr;
            }
            Asset::Composite::onUnload();
        }

        const char* TracyProfilerComposite::tr(const char* zh, const char* en) const
        {
            if (m_editorUiComposite)
            {
                return m_editorUiComposite->tr(zh, en);
            }
            return zh;
        }

        void TracyProfilerComposite::startCapture(const char* host, uint16_t port)
        {
            m_connectHost = host ? host : "127.0.0.1";
            m_connectPort = port;
            m_samplingPaused = false;

            if (m_tracyView)
            {
                FCT_DELETE(m_tracyView);
                m_tracyView = nullptr;
            }

            m_tracyView = FCT_NEW(
                tracy::View,
                TracyBridge::runOnMainThread,
                m_connectHost.c_str(),
                m_connectPort,
                TracyBridge::noopSetTitle,
                TracyBridge::noopSetScale,
                TracyBridge::noopAttention,
                nullptr);
        }

        void TracyProfilerComposite::pauseCapture()
        {
            m_samplingPaused = !m_samplingPaused;
        }

        void TracyProfilerComposite::stopCapture()
        {
            if (m_tracyView)
            {
                FCT_DELETE(m_tracyView);
                m_tracyView = nullptr;
            }
        }

        void TracyProfilerComposite::clearCaptureData()
        {
            startCapture(m_connectHost.c_str(), m_connectPort);
        }

        bool TracyProfilerComposite::saveTraceFile(const std::string& path)
        {
            if (path.empty() || !m_tracyView)
            {
                return false;
            }
            return true;
        }

        bool TracyProfilerComposite::loadTraceFile(const std::string& path)
        {
            auto* fr = tracy::FileRead::Open(path.c_str());
            if (!fr)
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        tr("快照加载", "Load Trace"),
                        tr("打开快照文件失败", "Failed to open trace file"),
                        EditorBase::LogEntry::Level::Error,
                        3.0f);
                }
                return false;
            }

            if (m_tracyView)
            {
                FCT_DELETE(m_tracyView);
                m_tracyView = nullptr;
            }

            m_tracyView = FCT_NEW(
                tracy::View,
                TracyBridge::runOnMainThread,
                *fr,
                TracyBridge::noopSetTitle,
                TracyBridge::noopSetScale,
                TracyBridge::noopAttention,
                nullptr);
            fr->~FileRead();
            ::operator delete(fr);

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    tr("快照加载", "Load Trace"),
                    tr("成功加载 .tracy 快照并解析时间轴", "Successfully loaded .tracy trace and parsed timeline"),
                    EditorBase::LogEntry::Level::Info,
                    3.0f);
            }
            return true;
        }

        void TracyProfilerComposite::renderProfilerPanel()
        {
            // 1. 消费主线程无锁回调队列
            TracyBridge::drainMainThreadTasks();

            // 2. 绑定当前 ImGui 字体至上游 Tracy Profiler 全局字体槽位
            TracyBridge::bindCurrentImGuiFonts();

            // 3. 确保上游原生 tracy::View 实例已创建或处理重连请求
            if (m_tracyView && m_tracyView->ReconnectRequested())
            {
                const std::string addr = m_tracyView->GetAddress();
                const uint16_t port = m_tracyView->GetPort();
                FCT_DELETE(m_tracyView);
                m_tracyView = FCT_NEW(
                    tracy::View,
                    TracyBridge::runOnMainThread,
                    addr.c_str(),
                    port,
                    TracyBridge::noopSetTitle,
                    TracyBridge::noopSetScale,
                    TracyBridge::noopAttention,
                    nullptr);
            }
            else if (!m_tracyView)
            {
                m_tracyView = FCT_NEW(
                    tracy::View,
                    TracyBridge::runOnMainThread,
                    m_connectHost.c_str(),
                    m_connectPort,
                    TracyBridge::noopSetTitle,
                    TracyBridge::noopSetScale,
                    TracyBridge::noopAttention,
                    nullptr);
            }

            // 4. 调用上游原生 Tracy Profiler UI 渲染
            if (m_tracyView)
            {
                (void)m_tracyView->Draw();
            }
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::TracyProfilerComposite)
