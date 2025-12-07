#ifndef ENGINE_SHADERGRAPHMANAGER_H
#define ENGINE_SHADERGRAPHMANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <algorithm>
#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"

namespace MQEngine {

    enum class ShaderType {
        Vertex,
        Pixel
    };

    struct ShaderGraphConfig {
        ShaderType type = ShaderType::Vertex;
        std::string vertexLayoutName;
        std::string pixelLayoutName;
        std::vector<std::string> bindedSystemNames;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & type;
            ar & vertexLayoutName;
            ar & pixelLayoutName;
            ar & bindedSystemNames;
        }
    };

    struct ENGINE_API ShaderGraphPin {
        std::string name;
        std::string type;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & name;
            ar & type;
        }
    };

    struct ENGINE_API ShaderGraphNode {
        int id;
        std::string name;
        std::string uuid;
        std::vector<ShaderGraphPin> inputs;
        std::vector<ShaderGraphPin> inouts;
        std::vector<ShaderGraphPin> outputs;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & id;
            ar & name;
            ar & uuid;
            ar & inputs;
            ar & inouts;
            ar & outputs;
        }
    };

    struct ENGINE_API ShaderGraphLink {
        int id;
        int beginNodeId;
        std::string beginPinName;
        int endNodeId;
        std::string endPinName;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & id;
            ar & beginNodeId;
            ar & beginPinName;
            ar & endNodeId;
            ar & endPinName;
        }
    };

    class ENGINE_API ShaderGraphManager {
    public:
        static constexpr int INPUT_NODE_ID = 1;
        static constexpr int OUTPUT_NODE_ID = 2;

        ShaderGraphManager() = default;
        ~ShaderGraphManager();

        // 节点创建方法 - 引擎管理ID
        ShaderGraphNode* createSnippetNode(const std::string& uuid);
        ShaderGraphNode* createParameterNode(const std::string& name, const std::string& type, const std::string& systemName);
        
        // 已弃用/底层：如果手动管理ID请谨慎使用
        ShaderGraphNode* createNode(int id, const std::string& name, const std::string& uuid);
        
        void deleteNode(int id);
        ShaderGraphNode* getNode(int id);
        const std::vector<ShaderGraphNode*>& getNodes() const;

        void createLink(int id, int beginNodeId, const std::string& beginPinName, int endNodeId, const std::string& endPinName);
        void deleteLink(int id);
        const std::vector<ShaderGraphLink>& getLinks() const;

        void clear();

        // 配置
        void setConfig(const ShaderGraphConfig& config);
        const ShaderGraphConfig& getConfig() const;

        // 注册表
        void registerVertexLayout(const std::string& name, const FCT::VertexLayout& layout);
        void registerPixelLayout(const std::string& name, const FCT::PixelLayout& layout);

        const std::map<std::string, FCT::VertexLayout>& getRegisteredVertexLayouts() const;
        const std::map<std::string, FCT::PixelLayout>& getRegisteredPixelLayouts() const;

        // 序列化
        Status save(const std::string& name);
        Status load(const std::string& name);
        
        void setName(const std::string& name) { m_graphName = name; }
        std::string getName() const { return m_graphName; }

    private:
        void updateMasterNodes();
        std::string getDataTypeString(FCT::DataType type) const;

        std::string m_graphName = "NewShaderGraph";
        std::vector<ShaderGraphNode*> m_nodes;
        std::vector<ShaderGraphLink> m_links;

        int m_nextNodeId = 1000; // 用户节点ID从1000开始
        ShaderGraphConfig m_config;
        std::map<std::string, FCT::VertexLayout> m_registeredVertexLayouts;
        std::map<std::string, FCT::PixelLayout> m_registeredPixelLayouts;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & m_graphName;
            ar & m_nodes;
            ar & m_links;
            ar & m_nextNodeId;
            ar & m_config;
        }
    };
}

#endif // ENGINE_SHADERGRAPHMANAGER_H
