/**
 * @file TracyProfilerComposite.h
 * @brief Tracy 性能剖析与帧遥测 Composite：
 *        内嵌上游原生 tracy::View 与 tracy::Worker，在编辑器 Dock 面板中直接渲染原生 Tracy Profiler 视图
 */
#ifndef MQENGINE_MQ_EDITOR_TRACY_PROFILER_COMPOSITE_H
#define MQENGINE_MQ_EDITOR_TRACY_PROFILER_COMPOSITE_H

#include "../../MQEditorApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <string>
#include <vector>
#include <cstdint>

namespace tracy
{
    class View;
} // namespace tracy

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief 内嵌式原生 Tracy 性能剖析器 Composite
         */
        class TRACY_PROFILER_COMPOSITE_API TracyProfilerComposite : public Asset::Composite
        {
        public:
            TracyProfilerComposite();
            ~TracyProfilerComposite() override;

            TracyProfilerComposite(const TracyProfilerComposite&) = delete;
            TracyProfilerComposite& operator=(const TracyProfilerComposite&) = delete;

            // ==================== Composite 接口实现 ====================

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "TracyProfilerComposite"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            // ==================== 遥测控制接口 ====================

            void startCapture(const char* host = "127.0.0.1", uint16_t port = 8086);
            void pauseCapture();
            void stopCapture();
            void clearCaptureData();
            bool saveTraceFile(const std::string& path);
            bool loadTraceFile(const std::string& path);

        private:
            void renderProfilerPanel();
            const char* tr(const char* zh, const char* en) const;

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;
            tracy::View* m_tracyView = nullptr;
            std::string m_connectHost = "127.0.0.1";
            uint16_t m_connectPort = 8086;
            bool m_samplingPaused = false;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_TRACY_PROFILER_COMPOSITE_H
