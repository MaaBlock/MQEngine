#include "ShaderGraphManager.h"
#include "../core/EngineGlobal.h"
#include "ShaderSnippetManager.h"
#include "./SystemManager.h"
#include "../system/BindedSystem.h"
#include "../data/ResourceLoader.h"
#include <algorithm>
#include <sstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/unordered_set.hpp>
// Note: m_nodes中的ShaderGraphNode*将作为指针序列化，需要显式注册或跟踪

namespace MQEngine {

    std::string GetTextureTypeString(FCT::TextureType type) {
        switch (type) {
            case FCT::TextureType::Texture2D: return "Texture2D";
            case FCT::TextureType::TextureCube: return "TextureCube";
            case FCT::TextureType::Texture3D: return "Texture3D";
            case FCT::TextureType::Texture2DArray: return "Texture2DArray";
            default: return "Texture2D";
        }
    }

    ShaderGraphManager::~ShaderGraphManager() {
        clear();
    }

    ShaderGraphNode* ShaderGraphManager::createNode(int id, const std::string& name, const std::string& uuid) {

        if (getNode(id)) return nullptr;

        auto node = new ShaderGraphNode();
        node->id = id;
        node->name = name;
        node->uuid = uuid;
        m_nodes.push_back(node);
        
        if (id >= m_nextNodeId) m_nextNodeId = id + 1;
        
        return node;
    }

    ShaderGraphNode* ShaderGraphManager::createSnippetNode(const std::string& uuid) {
        if (!g_engineGlobal.shaderSnippetManager) return nullptr;
        const Snippet* snippet = g_engineGlobal.shaderSnippetManager->getSnippetByUuid(uuid);
        if (!snippet) return nullptr;

        int newId = m_nextNodeId++;
        auto node = new ShaderGraphNode();
        node->id = newId;
        node->name = snippet->name;
        node->uuid = snippet->uuid;

        for (const auto& param : snippet->inputs) {
            node->inputs.push_back({param.name, param.type});
        }
        for (const auto& param : snippet->inouts) {
            node->inouts.push_back({param.name, param.type});
        }
        for (const auto& param : snippet->outputs) {
            node->outputs.push_back({param.name, param.type});
        }

        m_nodes.push_back(node);
        return node;
    }

    ShaderGraphNode* ShaderGraphManager::createParameterNode(const std::string& name, const std::string& type, const std::string& systemName) {
        int newId = m_nextNodeId++;

        auto node = new ShaderGraphNode();
        node->id = newId;
        node->name = systemName + "::" + name;
        node->uuid = "PARAMETER";

        ShaderGraphPin pin;
        pin.name = name;
        pin.type = type;
        node->outputs.push_back(pin);

        m_nodes.push_back(node);
        return node;
    }

    void ShaderGraphManager::deleteNode(int id) {
        if (id == INPUT_NODE_ID || id == OUTPUT_NODE_ID) return;

        auto it = std::remove_if(m_nodes.begin(), m_nodes.end(),
            [id](ShaderGraphNode* node) {
                if (node->id == id) {
                    delete node;
                    return true;
                }
                return false;
            });
        m_nodes.erase(it, m_nodes.end());

        auto linkIt = std::remove_if(m_links.begin(), m_links.end(),
            [id](const ShaderGraphLink& link) {
                return link.beginNodeId == id || link.endNodeId == id;
            });
        m_links.erase(linkIt, m_links.end());
    }

    ShaderGraphNode* ShaderGraphManager::getNode(int id) {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
            [id](ShaderGraphNode* node) {
                return node->id == id;
            });
        if (it != m_nodes.end()) {
            return *it;
        }
        return nullptr;
    }

    const std::vector<ShaderGraphNode*>& ShaderGraphManager::getNodes() const {
        return m_nodes;
    }

    void ShaderGraphManager::createLink(int id, int beginNodeId, const std::string& beginPinName, int endNodeId, const std::string& endPinName) {
        ShaderGraphLink link;
        link.id = id;
        link.beginNodeId = beginNodeId;
        link.beginPinName = beginPinName;
        link.endNodeId = endNodeId;
        link.endPinName = endPinName;
        m_links.push_back(link);
    }

    void ShaderGraphManager::deleteLink(int id) {
        auto it = std::remove_if(m_links.begin(), m_links.end(),
            [id](const ShaderGraphLink& link) {
                return link.id == id;
            });
        m_links.erase(it, m_links.end());
    }

    const std::vector<ShaderGraphLink>& ShaderGraphManager::getLinks() const {
        return m_links;
    }

    void ShaderGraphManager::clear() {
        for (auto node : m_nodes) {
            delete node;
        }
        m_nodes.clear();
        m_links.clear();
        m_nextNodeId = 1000;
    }

    void ShaderGraphManager::setConfig(const ShaderGraphConfig& config) {
        m_config = config;
        updateMasterNodes();
    }

    const ShaderGraphConfig& ShaderGraphManager::getConfig() const {
        return m_config;
    }

    void ShaderGraphManager::registerVertexLayout(const std::string& name, const FCT::VertexLayout& layout) {
        m_registeredVertexLayouts[name] = layout;
    }

    void ShaderGraphManager::registerPixelLayout(const std::string& name, const FCT::PixelLayout& layout) {
        m_registeredPixelLayouts[name] = layout;
    }

    const std::map<std::string, FCT::VertexLayout>& ShaderGraphManager::getRegisteredVertexLayouts() const {
        return m_registeredVertexLayouts;
    }

    const std::map<std::string, FCT::PixelLayout>& ShaderGraphManager::getRegisteredPixelLayouts() const {
        return m_registeredPixelLayouts;
    }

    Status ShaderGraphManager::save(const std::string& name) {
        std::string path = "res/shadergraph/" + name + ".sg";

        if (g_engineGlobal.resourceLoader) {
            CHECK_STATUS(g_engineGlobal.resourceLoader->ensureDir("res/shadergraph/"));
        } else {
            return InternalError("ResourceLoader not available");
        }

        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        oa << *this;

        CHECK_STATUS(g_engineGlobal.resourceLoader->saveFile(path, ss.str()));
        spdlog::info("Shader graph saved to {}", path);
        return OkStatus();
    }

    Status ShaderGraphManager::load(const std::string& name) {
        std::string path = "res/shadergraph/" + name + ".sg";
        if (!g_engineGlobal.resourceLoader) {
            return InternalError("ResourceLoader not available");
        }

        auto contentStatus = g_engineGlobal.resourceLoader->loadFile(path);
        if (!contentStatus.ok()) {
            return contentStatus.status();
        }

        clear();
        std::stringstream ss(contentStatus.value());
        boost::archive::text_iarchive ia(ss);
        ia >> *this;
        updateMasterNodes(); // 根据加载的配置重建主节点
        spdlog::info("Shader graph loaded from {}", path);
        return OkStatus();
    }

    void ShaderGraphManager::updateMasterNodes() {
        auto it = std::remove_if(m_nodes.begin(), m_nodes.end(),
            [](ShaderGraphNode* node) {
                if (node->id == INPUT_NODE_ID || node->id == OUTPUT_NODE_ID) {
                    delete node;
                    return true;
                }
                return false;
            });
        m_nodes.erase(it, m_nodes.end());

        auto linkIt = std::remove_if(m_links.begin(), m_links.end(),
            [](const ShaderGraphLink& link) {
                return link.beginNodeId == INPUT_NODE_ID || link.endNodeId == INPUT_NODE_ID ||
                       link.beginNodeId == OUTPUT_NODE_ID || link.endNodeId == OUTPUT_NODE_ID;
            });
        m_links.erase(linkIt, m_links.end());

        // 更新绑定系统节点
        if (g_engineGlobal.systemManager) {
            const auto& systemConfigs = g_engineGlobal.systemManager->getConfigs();
            for (const auto& sysName : m_config.bindedSystemNames) {
                auto it = systemConfigs.find(sysName);
                if (it != systemConfigs.end()) {
                    BindedSystem* bs = dynamic_cast<BindedSystem*>(it->second.system);
                    if (bs) {
                        std::string systemNodeUuid = "SYSTEM_" + sysName;
                        
                        bool exists = false;
                        for (const auto* node : m_nodes) {
                            if (node->uuid == systemNodeUuid) {
                                exists = true;
                                break;
                            }
                        }

                        if (!exists) {
                            auto node = new ShaderGraphNode();
                            node->id = m_nextNodeId++;
                            node->name = sysName;
                            node->uuid = systemNodeUuid;

                            // Uniforms
                            for (const auto& slot : bs->getUniformSlots()) {
                                for(size_t i=0; i<slot.getElementCount(); ++i) {
                                    const auto& element = slot.getElement(i);
                                    node->outputs.push_back({element.getName(), FCT::GetUniformTypeName(element.getType())});
                                }
                            }
                            // Samplers
                            for (const auto& slot : bs->getSamplerSlots()) {
                                node->outputs.push_back({std::string(slot.getName()), "SamplerState"});
                            }
                            // Textures
                            for (const auto& slot : bs->getTextureSlots()) {
                                node->outputs.push_back({slot.name, GetTextureTypeString(slot.type)});
                            }
                            m_nodes.push_back(node);
                        }
                    }
                }
            }
        }

        // 创建输入节点
        auto inputNode = new ShaderGraphNode();
        inputNode->id = INPUT_NODE_ID;
        inputNode->uuid = "MASTER_INPUT";
        
        if (m_config.type == ShaderType::Vertex) {
            inputNode->name = "Vertex Input";
            auto itLayout = m_registeredVertexLayouts.find(m_config.vertexLayoutName);
            if (itLayout != m_registeredVertexLayouts.end()) {
                const auto& layout = itLayout->second;
                for (size_t i = 0; i < layout.getElementCount(); ++i) {
                    const auto& elem = layout.getElement(i);
                    ShaderGraphPin pin;
                    pin.name = elem.getSemantic();
                    pin.type = FCT::FormatToShaderType(elem.getFormat());
                    inputNode->outputs.push_back(pin);
                }
            }
        } else {
            inputNode->name = "Pixel Input";
            auto itLayout = m_registeredPixelLayouts.find(m_config.pixelLayoutName);
            if (itLayout != m_registeredPixelLayouts.end()) {
                const auto& layout = itLayout->second;
                for (size_t i = 0; i < layout.getElementCount(); ++i) {
                    const auto& elem = layout.getElement(i);
                    ShaderGraphPin pin;
                    pin.name = elem.getSemantic();
                    pin.type = FCT::FormatToShaderType(elem.getFormat());
                    inputNode->outputs.push_back(pin);
                }
            }
        }
        m_nodes.push_back(inputNode);

        // 创建输出节点
        auto outputNode = new ShaderGraphNode();
        outputNode->id = OUTPUT_NODE_ID;
        outputNode->uuid = "MASTER_OUTPUT";

        if (m_config.type == ShaderType::Vertex) {
            outputNode->name = "Vertex Output";
            // 顶点输出匹配像素布局（传递给像素着色器）
            auto itLayout = m_registeredPixelLayouts.find(m_config.pixelLayoutName);
            if (itLayout != m_registeredPixelLayouts.end()) {
                const auto& layout = itLayout->second;
                for (size_t i = 0; i < layout.getElementCount(); ++i) {
                    const auto& elem = layout.getElement(i);
                    ShaderGraphPin pin;
                    pin.name = elem.getSemantic();
                    pin.type = FCT::FormatToShaderType(elem.getFormat());
                    outputNode->inputs.push_back(pin);
                }
            }
        } else {
            outputNode->name = "Pixel Output";
            // 像素输出是渲染目标
            for (int i = 0; i < 8; ++i) {
                ShaderGraphPin pin;
                pin.name = "Target" + std::to_string(i);
                pin.type = "float4";
                outputNode->inputs.push_back(pin);
            }
        }
        m_nodes.push_back(outputNode);
    }
}
