#ifndef ENGINE_CONFIGDOCUMENT_H
#define ENGINE_CONFIGDOCUMENT_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/resource/core/ResourceLoader.h>

namespace MQEngine {

enum class ConfigFormat {
    Auto,
    Toml,
    Json
};

class ConfigNode;

using ConfigArray = std::vector<ConfigNode>;
using ConfigTable = std::vector<std::pair<std::string, ConfigNode>>;

/**
 * @brief 引擎通用配置节点抽象，屏蔽底层配置库（如 toml++）实现细节，支持树状层级访问与编译期反射绑定
 */
class ENGINE_API ConfigNode {
public:
    using ValueVariant = std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        ConfigArray,
        ConfigTable
    >;

    ConfigNode();
    explicit ConfigNode(bool val);
    explicit ConfigNode(int64_t val);
    explicit ConfigNode(int val);
    explicit ConfigNode(double val);
    explicit ConfigNode(std::string val);
    explicit ConfigNode(const char* val);
    explicit ConfigNode(ConfigArray arr);
    explicit ConfigNode(ConfigTable table);

    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isFloat() const;
    bool isString() const;
    bool isArray() const;
    bool isTable() const;

    bool asBool(bool defaultVal = false) const;
    int64_t asInt(int64_t defaultVal = 0) const;
    double asFloat(double defaultVal = 0.0) const;
    std::string asString(const std::string& defaultVal = "") const;
    std::string toValueString() const;

    const ConfigArray& asArray() const;
    ConfigArray& asArrayMutable();
    const ConfigTable& asTable() const;
    ConfigTable& asTableMutable();

    bool hasKey(const std::string& keyPath) const;
    bool hasSection(const std::string& keyPath) const;
    std::vector<std::string> keys() const;

    const ConfigNode* findNode(const std::string& keyPath) const;
    ConfigNode* findNode(const std::string& keyPath);
    const ConfigNode* findSection(const std::string& keyPath) const;
    ConfigNode* findSection(const std::string& keyPath);
    ConfigNode& section(const std::string& keyPath);

    std::string getString(const std::string& keyPath, const std::string& defaultVal = "") const;
    std::string getStringOr(std::initializer_list<const char*> candidatePaths,
                            const std::string& defaultVal = "") const;
    int64_t getInt(const std::string& keyPath, int64_t defaultVal = 0) const;
    double getFloat(const std::string& keyPath, double defaultVal = 0.0) const;
    bool getBool(const std::string& keyPath, bool defaultVal = false) const;
    std::vector<std::string> getStringArray(const std::string& keyPath) const;
    std::vector<std::string> getStringArrayOr(std::initializer_list<const char*> candidatePaths) const;
    std::unordered_map<std::string, std::string> getStringMap(const std::string& keyPath) const;

    void setNode(const std::string& keyPath, ConfigNode node);
    void setString(const std::string& keyPath, const std::string& val);
    void setInt(const std::string& keyPath, int64_t val);
    void setFloat(const std::string& keyPath, double val);
    void setBool(const std::string& keyPath, bool val);
    void setStringArray(const std::string& keyPath, const std::vector<std::string>& values);
    void setStringMap(const std::string& keyPath, const std::unordered_map<std::string, std::string>& values);

    bool removeKey(const std::string& keyPath);
    void clear();

    template<typename T>
    void writeValue(const T& val) {
        using Decayed = std::decay_t<T>;
        if constexpr (std::is_same_v<Decayed, bool>) {
            m_value = val;
        } else if constexpr (std::is_integral_v<Decayed>) {
            m_value = static_cast<int64_t>(val);
        } else if constexpr (std::is_floating_point_v<Decayed>) {
            m_value = static_cast<double>(val);
        } else if constexpr (std::is_enum_v<Decayed>) {
            m_value = static_cast<int64_t>(val);
        } else if constexpr (std::is_same_v<Decayed, std::string>) {
            m_value = val;
        } else if constexpr (NB::is_std_vector<Decayed>::value) {
            using ElemType = typename NB::is_std_vector<Decayed>::element_type;
            ConfigArray arr;
            arr.reserve(val.size());
            for (const auto& elem : val) {
                ConfigNode elemNode;
                elemNode.writeValue<ElemType>(elem);
                arr.push_back(std::move(elemNode));
            }
            m_value = std::move(arr);
        } else if constexpr (NB::is_string_map<Decayed>::value) {
            using ValType = typename NB::is_string_map<Decayed>::value_type;
            m_value = ConfigTable{};
            for (const auto& [k, v] : val) {
                ConfigNode child;
                child.writeValue<ValType>(v);
                setDirectKey(k, std::move(child));
            }
        } else if constexpr (NB::is_reflectable_struct_v<Decayed>) {
            writeStruct(val);
        }
    }

    template<typename T>
    void readValue(T& outVal) const {
        using Decayed = std::decay_t<T>;
        if constexpr (std::is_same_v<Decayed, bool>) {
            if (isBool()) {
                outVal = asBool();
            }
        } else if constexpr (std::is_integral_v<Decayed>) {
            if (isInt() || isFloat()) {
                outVal = static_cast<Decayed>(asInt());
            }
        } else if constexpr (std::is_floating_point_v<Decayed>) {
            if (isFloat() || isInt()) {
                outVal = static_cast<Decayed>(asFloat());
            }
        } else if constexpr (std::is_enum_v<Decayed>) {
            if (isInt()) {
                outVal = static_cast<Decayed>(asInt());
            }
        } else if constexpr (std::is_same_v<Decayed, std::string>) {
            if (isString()) {
                outVal = asString();
            }
        } else if constexpr (NB::is_std_vector<Decayed>::value) {
            using ElemType = typename NB::is_std_vector<Decayed>::element_type;
            if (isArray()) {
                outVal.clear();
                const auto& arr = asArray();
                outVal.reserve(arr.size());
                for (const auto& elemNode : arr) {
                    ElemType elem{};
                    elemNode.readValue<ElemType>(elem);
                    outVal.push_back(std::move(elem));
                }
            }
        } else if constexpr (NB::is_string_map<Decayed>::value) {
            using ValType = typename NB::is_string_map<Decayed>::value_type;
            if (isTable()) {
                outVal.clear();
                for (const auto& [k, vNode] : asTable()) {
                    if constexpr (std::is_same_v<ValType, std::string>) {
                        outVal[k] = vNode.toValueString();
                    } else {
                        ValType val{};
                        vNode.readValue<ValType>(val);
                        outVal[k] = std::move(val);
                    }
                }
            }
        } else if constexpr (NB::is_reflectable_struct_v<Decayed>) {
            readStruct(outVal);
        }
    }

    /**
     * @brief 通过 Boost.Describe 或 Boost.PFR 将 C++ 结构体自动序列化至当前配置节点
     */
    template<typename T>
    void writeStruct(const T& inStruct) {
        if (!isTable()) {
            m_value = ConfigTable{};
        }
        NB::forEachReflectedField(inStruct, [&](std::string_view fieldName, const auto& fieldRef) {
            using FieldType = std::remove_cvref_t<decltype(fieldRef)>;
            if constexpr (NB::is_reflectable_field_v<FieldType>) {
                ConfigNode childNode;
                childNode.writeValue<FieldType>(fieldRef);
                setDirectKey(std::string(fieldName), std::move(childNode));
            }
        });
    }

    /**
     * @brief 通过 Boost.Describe 或 Boost.PFR 从当前配置节点自动反序列化填充 C++ 结构体
     */
    template<typename T>
    void readStruct(T& outStruct) const {
        if (!isTable()) {
            return;
        }
        NB::forEachReflectedField(outStruct, [&](std::string_view fieldName, auto& fieldRef) {
            using FieldType = std::remove_cvref_t<decltype(fieldRef)>;
            if constexpr (NB::is_reflectable_field_v<FieldType>) {
                const ConfigNode* childNode = findDirectKey(std::string(fieldName));
                if (childNode) {
                    childNode->readValue<FieldType>(fieldRef);
                }
            }
        });
    }

private:
    const ConfigNode* findDirectKey(const std::string& key) const;
    ConfigNode* findDirectKey(const std::string& key);
    ConfigNode& setDirectKey(const std::string& key, ConfigNode node);

    ValueVariant m_value;
};

/**
 * @brief 引擎统一配置文档封装，负责将 ConfigNode 树与底层存储格式（默认 TOML）相互转换并配合 ResourceLoader 读写
 */
class ENGINE_API ConfigDocument {
public:
    ConfigDocument();
    explicit ConfigDocument(ConfigNode rootNode);

    ConfigNode& root() { return m_root; }
    const ConfigNode& root() const { return m_root; }

    static StatusOr<ConfigDocument> parse(const std::string& content, ConfigFormat format = ConfigFormat::Auto);
    static StatusOr<ConfigDocument> loadFromFile(ResourceLoader* loader,
                                                 const std::string& filePath,
                                                 ConfigFormat format = ConfigFormat::Auto);

    std::string serialize(ConfigFormat format = ConfigFormat::Toml) const;
    Status saveToFile(ResourceLoader* loader,
                      const std::string& filePath,
                      ConfigFormat format = ConfigFormat::Toml) const;

    bool hasKey(const std::string& keyPath) const { return m_root.hasKey(keyPath); }
    bool hasSection(const std::string& keyPath) const { return m_root.hasSection(keyPath); }
    ConfigNode& section(const std::string& keyPath) { return m_root.section(keyPath); }
    const ConfigNode* findSection(const std::string& keyPath) const { return m_root.findSection(keyPath); }

    std::string getString(const std::string& keyPath, const std::string& defaultVal = "") const {
        return m_root.getString(keyPath, defaultVal);
    }
    std::string getStringOr(std::initializer_list<const char*> candidatePaths,
                            const std::string& defaultVal = "") const {
        return m_root.getStringOr(candidatePaths, defaultVal);
    }
    int64_t getInt(const std::string& keyPath, int64_t defaultVal = 0) const {
        return m_root.getInt(keyPath, defaultVal);
    }
    double getFloat(const std::string& keyPath, double defaultVal = 0.0) const {
        return m_root.getFloat(keyPath, defaultVal);
    }
    bool getBool(const std::string& keyPath, bool defaultVal = false) const {
        return m_root.getBool(keyPath, defaultVal);
    }
    std::vector<std::string> getStringArray(const std::string& keyPath) const {
        return m_root.getStringArray(keyPath);
    }
    std::vector<std::string> getStringArrayOr(std::initializer_list<const char*> candidatePaths) const {
        return m_root.getStringArrayOr(candidatePaths);
    }
    std::unordered_map<std::string, std::string> getStringMap(const std::string& keyPath) const {
        return m_root.getStringMap(keyPath);
    }

    void setString(const std::string& keyPath, const std::string& val) { m_root.setString(keyPath, val); }
    void setInt(const std::string& keyPath, int64_t val) { m_root.setInt(keyPath, val); }
    void setFloat(const std::string& keyPath, double val) { m_root.setFloat(keyPath, val); }
    void setBool(const std::string& keyPath, bool val) { m_root.setBool(keyPath, val); }
    void setStringArray(const std::string& keyPath, const std::vector<std::string>& values) {
        m_root.setStringArray(keyPath, values);
    }
    void setStringMap(const std::string& keyPath, const std::unordered_map<std::string, std::string>& values) {
        m_root.setStringMap(keyPath, values);
    }

    template<typename T>
    void writeStruct(const T& inStruct) {
        m_root.writeStruct(inStruct);
    }

    template<typename T>
    void readStruct(T& outStruct) const {
        m_root.readStruct(outStruct);
    }

    template<typename T>
    static StatusOr<T> loadStructFromFile(ResourceLoader* loader,
                                          const std::string& filePath,
                                          ConfigFormat format = ConfigFormat::Auto) {
        auto docRes = loadFromFile(loader, filePath, format);
        CHECK_STATUS(docRes);
        T result{};
        docRes.value().readStruct(result);
        return result;
    }

    template<typename T>
    static Status saveStructToFile(ResourceLoader* loader,
                                   const std::string& filePath,
                                   const T& inStruct,
                                   ConfigFormat format = ConfigFormat::Toml) {
        ConfigDocument doc;
        doc.writeStruct(inStruct);
        return doc.saveToFile(loader, filePath, format);
    }

private:
    ConfigNode m_root;
};

} // namespace MQEngine

#endif // ENGINE_CONFIGDOCUMENT_H
