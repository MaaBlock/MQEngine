/**
 * @file RenderGraphEditorTypes.h
 * @brief RenderGraph 拓扑图编辑器节点、引脚、连线、资源统计与屏障视图数据结构定义
 */
#ifndef MQENGINE_MQ_EDITOR_RENDER_GRAPH_EDITOR_TYPES_H
#define MQENGINE_MQ_EDITOR_RENDER_GRAPH_EDITOR_TYPES_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        /**
         * @brief 渲染图拓扑节点类型枚举
         */
        enum class NodeType : uint8_t
        {
            Pass = 0,
            Image = 1
        };

        /**
         * @brief 渲染图节点引脚语义类别枚举
         */
        enum class PinKind : uint8_t
        {
            TextureInput = 0,
            TargetOutput = 1,
            DepthStencilOutput = 2,
            ImageTargetInput = 3,
            ImageDepthInput = 4,
            ImageTextureOutput = 5
        };

        /**
         * @brief Pass Target 渲染目标属性配置
         */
        struct PassTargetDesc
        {
            bool enabled = false;
            bool isWindow = false;
            bool useCustomFormat = false;
            std::string format = "R8G8B8A8_UNORM";
            bool useCustomSize = false;
            int customWidth = 1920;
            int customHeight = 1080;
        };

        /**
         * @brief Pass 深度模板目标属性配置
         */
        struct DepthStencilDesc
        {
            bool enabled = false;
            bool isWindow = false;
            bool useCustomFormat = false;
            std::string format = "D32_SFLOAT";
            bool useCustomSize = false;
            int customWidth = 1920;
            int customHeight = 1080;
        };

        /**
         * @brief 交互式 Pass 节点数据结构
         */
        struct PassNode
        {
            uint32_t id = 0;
            std::string name;
            bool hasLinkTarget[9] = {};
            bool hasLinkDepthStencil = false;
            PassTargetDesc targetDesc[9];
            DepthStencilDesc depthStencilDesc;
            bool enableClear = false;
            bool enableClearTarget = false;
            bool enableClearDepth = false;
            bool enableClearStencil = false;
            float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            float clearDepth = 1.0f;
            int clearStencil = 0;
            std::vector<int> texturePins;
            size_t texturePinIndex = 0;
            float pos[2] = {100.0f, 100.0f};

            void removeTexturePin(int pinId)
            {
                auto it = std::find(texturePins.begin(), texturePins.end(), pinId);
                if (it != texturePins.end())
                {
                    texturePins.erase(it);
                }
            }

            int lastTexturePinId() const
            {
                return texturePins.empty() ? 0 : texturePins.back();
            }

            int targetPinId(int index) const
            {
                std::string s = std::to_string(id) + "_target_" + std::to_string(index);
                return static_cast<int>(static_cast<uint32_t>(std::hash<std::string>{}(s)) & 0x7FFFFFFF);
            }

            int depthStencilPinId() const
            {
                std::string s = std::to_string(id) + "_depth_0";
                return static_cast<int>(static_cast<uint32_t>(std::hash<std::string>{}(s)) & 0x7FFFFFFF);
            }
        };

        /**
         * @brief 交互式 Image / ResourceGroup 节点数据结构
         */
        struct ImageNode
        {
            uint32_t id = 0;
            std::string name;
            std::string format = "R8G8B8A8_UNORM";
            int width = 1920;
            int height = 1080;
            size_t estimatedMemoryBytes = 8294400; // 1920 * 1080 * 4
            bool isAliased = false;
            float pos[2] = {350.0f, 100.0f};

            int texturePinId() const
            {
                std::string s = std::to_string(id) + "_texture_0";
                return static_cast<int>(static_cast<uint32_t>(std::hash<std::string>{}(s)) & 0x7FFFFFFF);
            }

            int targetPinId() const
            {
                std::string s = std::to_string(id) + "_target_0";
                return static_cast<int>(static_cast<uint32_t>(std::hash<std::string>{}(s)) & 0x7FFFFFFF);
            }

            int depthStencilPinId() const
            {
                std::string s = std::to_string(id) + "_depth_0";
                return static_cast<int>(static_cast<uint32_t>(std::hash<std::string>{}(s)) & 0x7FFFFFFF);
            }
        };

        /**
         * @brief 引脚元数据信息
         */
        struct PinInfo
        {
            uint32_t nodeId = 0;
            std::string pinType; // "texture", "target", "depth"
            int index = 0;
        };
        using PinData = PinInfo;

        /**
         * @brief 连线数据信息
         */
        struct LinkInfo
        {
            int id = 0;
            int startPinId = 0;
            int endPinId = 0;
        };
        using LinkData = LinkInfo;

        /**
         * @brief Pass 资源占用与读写统计信息
         */
        struct PassResourceStat
        {
            std::string passName;
            size_t inputTextureCount = 0;
            size_t outputTargetCount = 0;
            bool hasDepthStencil = false;
            size_t estimatedTransientBytes = 0;
        };

        /**
         * @brief RDG 图像布局屏障转换视图信息
         */
        struct BarrierTransitionView
        {
            std::string resourceName;
            std::string producerPass;
            std::string consumerPass;
            std::string beforeState = "ColorAttachmentWrite";
            std::string afterState = "ShaderResourceReadOnly";
        };

        /**
         * @brief 中间纹理实时预览窗口配置 (GraphViewInsight)
         */
        struct TextureInsightSettings
        {
            int channelMode = 0; // 0: RGBA, 1: R, 2: G, 3: B, 4: A
            float exposure = 1.0f;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_RENDER_GRAPH_EDITOR_TYPES_H
