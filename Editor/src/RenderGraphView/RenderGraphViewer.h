#pragma once
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/array.hpp>
#include <imgui.h>
#include <imnodes.h>
#include <vector>
#include <string>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include "../Thirdparty/thirdparty.h"
namespace FCT
{
    struct PassDesc;
    class Context;
}

namespace MQEngine {
    struct PassTargetDesc
    {
        bool enabled = false;
        bool isWindow = false;
        bool useCustomFormat = false;
        std::string format = "RGBA8";
        bool useCustomSize = false;
        int customWidth = 1920;
        int customHeight = 1080;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & enabled & useCustomFormat & format & useCustomSize & customWidth & customHeight;
            ar & isWindow;
        }
    };
    struct TextureDesc
    {
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
        }
    };
    struct DepthStencilDesc
    {
        bool enabled = false;
        bool isWindow = false;
        bool useCustomFormat = false;
        std::string format = "D32_SFLOAT";
        bool useCustomSize = false;
        int customWidth = 1920;
        int customHeight = 1080;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & enabled & useCustomFormat & format & useCustomSize & customWidth & customHeight;
            ar & isWindow;
        }
    };
    struct PassNode
    {
        uint32_t id;
        std::string name;
        bool hasLinkTarget[9] = {};
        bool hasLinkDepthStencil = false;
        PassTargetDesc targetDesc[9];
        DepthStencilDesc depthStencilDesc;
        std::map<uint32_t, TextureDesc> texturesDesc;
        bool enableClear = false;
        bool enableClearTarget = false;
        bool enableClearDepth = false;
        bool enableClearStencil = false;
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float clearDepth = 1.0f;
        int clearStencil = 0;
        std::vector<int> texturePins;
        //std::set<int> texturePins;
        size_t texturePinIndex = 0;
        void removeTexturePin(int pinId) {
            auto it = std::find(texturePins.begin(), texturePins.end(), pinId);
            if (it != texturePins.end()) {
                texturePins.erase(it);
            }
        }
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & id & name & hasLinkTarget & hasLinkDepthStencil & targetDesc & depthStencilDesc;
            ar & texturesDesc & enableClear & enableClearTarget & enableClearDepth & enableClearStencil;
            ar & clearColor & clearDepth & clearStencil & texturePins & texturePinIndex;

        }
        int lastTexturePinId() const
        {
            return texturePins.back();
        }
        int targetPinId(int index) const;
        int depthStencilPinId();
    };

    struct ImageNode
    {
        uint32_t id;
        std::string name;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & id & name;
        }
        int texturePinId();
        int targetPinId();
        int depthStencilPinId();
    };
    struct PinInfo
    {
        uint32_t nodeId;
        std::string pinType;
        int index;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & nodeId & pinType & index;
        }
    };
    struct LinkInfo
    {
        int id;
        int startPinId;
        int endPinId;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & id & startPinId & endPinId;
        }
    };

    class RenderGraphViewer {
    public:
        /**
          * @name 模拟界面操作
          * @{
          */
        void addTextureLink(int imageId, int passId);
        void addTargetLink(int passId, int index,int imageId);
        void addDepthStencilLink(int passId,int imageId);
        /**
         * @brief 创建一个新的Pass
         * @param name 没用的参数
         * @return Pass id
         */
        int newPassNode(const std::string& name = "Pass");
        /**
         * @brief 创建一个新的Image节点
         * @param name
         * @return Image id
         */
        int newImageNode(const std::string& name = "Image");
        /**
         * @brief 从PassDesc创建图表
         * @param passDescs Pass描述数组
         */
        void createGraphFromPassDescs(const std::vector<FCT::PassDesc>& passDescs);
        
        /**
         * @brief 将当前图表转换为PassDesc数组
         * @return PassDesc数组
         */
        std::vector<FCT::PassDesc> convertCurrentGraphToPassDescs();
        
        /** @} */
        /**
          * @name 辅助函数
          * @{
        */
        /**
         * @brief 找一个Image节点
         * @param name  Image节点名字
         * @return id，找不到返回-1
         */
        int findImageNode(const std::string& name);
        int findPassNode(const std::string& name);
        /** @} */
    public:
        RenderGraphViewer(FCT::Context* ctx,FCT::Window* wnd);
        void removePassPin(int pinHash);
        void removeImagePin(int pinHash);
        void render();
        /**
         * 创建一个新的pin节点
         * @param pass
         */
        void newTexturePin(PassNode& pass);
        /**
         * 保存图表到文件中
         * @param filename
         */
        void saveToFile(const std::string& filename);
        /**
         * 生成所有pass的代码
         * @return
         */
        std::string generatorCode();
        /**
         * @brief 生成指定pass的代码
         * @param pass
         * @return
         */
        std::string generatePassCode(const PassNode& pass);
    private:
        void autoLayoutGraph();

        /**
         * @brief 添加一个 链接，从指定的pin hash 到指定的 pin  hash
         * @param startHash
         * @param endHash
         */
        void addLink(int startHash,int endHash);
        void addPassNode(const PassNode& passNode);
        /**
         * @brief 将图表保存到文件中
         * @param filename
         */
        /**
         * 删除右键菜单指向的 节点
         * @param contextMenuNodeId
         */
        void deleteNode(int contextMenuNodeId);
        void deletePass(int contextMenuNodeId);
        void deleteImage(int contextMenuNodeId);
        size_t m_nextNodeId = 0;
        size_t m_linkId = 0;

        std::map<int,LinkInfo> m_passOutputlinks;
        std::map<int,LinkInfo> m_passInputLinks;
        std::map<int,PassNode> m_passes;
        std::map<int,ImageNode> m_images;
        std::unordered_map<int,PinInfo> m_pinInfoMap;
        void loadFromFile(const std::string& filename);
        void renderPassNode(PassNode& pass);
        void renderImageNode(ImageNode& image);
        int generatePinId(int nodeId, const std::string& pinType, int index = 0);
        int getNextNodeId() { return ++m_nextNodeId; }
        int getNextLinkId() { return ++m_linkId; }
        int m_contextMenuNodeId = -1;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & m_nextNodeId & m_linkId;
            ar & m_passOutputlinks & m_passInputLinks;
            ar & m_passes & m_images & m_pinInfoMap;
        }
        friend class boost::serialization::access;
        std::string m_generatedCode;
        FCT::Context* m_ctx;
        FCT::Window* m_wnd;
    };

}
