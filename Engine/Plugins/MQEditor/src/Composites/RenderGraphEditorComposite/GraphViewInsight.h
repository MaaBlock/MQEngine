/**
 * @file GraphViewInsight.h
 * @brief GraphViewInsight 渲染目标与中间纹理实时悬浮预览窗口组件
 */
#ifndef MQENGINE_MQ_EDITOR_GRAPH_VIEW_INSIGHT_H
#define MQENGINE_MQ_EDITOR_GRAPH_VIEW_INSIGHT_H

#include "../../MQEditorApi.h"
#include "RenderGraphEditorTypes.h"

#include <string>
#include <unordered_map>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief GraphViewInsight：实时采样并渲染当前活跃 RenderGraph 中各 Pass 产出的中间纹理（带 RGBA 通道过滤与曝光调节）
         */
        class RENDER_GRAPH_EDITOR_COMPOSITE_API GraphViewInsight
        {
        public:
            /**
             * @brief 遍历 ImguiContext 中绑定的所有 RenderGraph Pass 纹理并绘制实时检视窗口
             */
            static void renderWindows(
                EditorBase::EditorUiComposite* editorUi,
                std::unordered_map<std::string, TextureInsightSettings>& settingsMap);
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_GRAPH_VIEW_INSIGHT_H
