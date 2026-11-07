#include "ConfigDocument.h"
#include "../thirdparty/TomlBackend.h"
#include "../resource/core/DiskResourceLoader.h"

namespace MQEngine {

namespace {

std::vector<std::string> SplitKeyPath(const std::string& keyPath) {
    std::vector<std::string> segments;
    size_t start = 0;
    while (start <= keyPath.size()) {
        size_t dotPos = keyPath.find('.', start);
        if (dotPos == std::string::npos) {
            std::string tail = keyPath.substr(start);
            if (!tail.empty()) {
                segments.push_back(std::move(tail));
            }
            break;
        }
        if (dotPos > start) {
            segments.push_back(keyPath.substr(start, dotPos - start));
        }
        start = dotPos + 1;
    }
    return segments;
}

ConfigFormat DetectFormatFromContent(const std::string& content, ConfigFormat requestedFormat) {
    if (requestedFormat != ConfigFormat::Auto) {
        return requestedFormat;
    }
    for (char ch : content) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            continue;
        }
        if (ch == '{') {
            return ConfigFormat::Json;
        }
        break;
    }
    return ConfigFormat::Toml;
}

ConfigFormat DetectFormatFromPath(const std::string& filePath, ConfigFormat requestedFormat) {
    if (requestedFormat != ConfigFormat::Auto) {
        return requestedFormat;
    }
    std::string ext = SAI::Path::extension(filePath);
    if (ext == ".json" || ext == ".JSON") {
        return ConfigFormat::Json;
    }
    return ConfigFormat::Auto;
}

ConfigNode ConvertTomlNodeToConfigNode(const toml::node& node);

ConfigNode ConvertTomlTableToConfigNode(const toml::table& tbl) {
    ConfigTable resultTable;
    for (const auto& [k, v] : tbl) {
        resultTable.emplace_back(std::string(k.str()), ConvertTomlNodeToConfigNode(v));
    }
    return ConfigNode(std::move(resultTable));
}

ConfigNode ConvertTomlArrayToConfigNode(const toml::array& arr) {
    ConfigArray resultArray;
    resultArray.reserve(arr.size());
    for (const auto& elem : arr) {
        resultArray.push_back(ConvertTomlNodeToConfigNode(elem));
    }
    return ConfigNode(std::move(resultArray));
}

ConfigNode ConvertTomlNodeToConfigNode(const toml::node& node) {
    if (const auto* tbl = node.as_table()) {
        return ConvertTomlTableToConfigNode(*tbl);
    }
    if (const auto* arr = node.as_array()) {
        return ConvertTomlArrayToConfigNode(*arr);
    }
    if (const auto* strVal = node.as_string()) {
        return ConfigNode(std::string(strVal->get()));
    }
    if (const auto* intVal = node.as_integer()) {
        return ConfigNode(intVal->get());
    }
    if (const auto* fltVal = node.as_floating_point()) {
        return ConfigNode(fltVal->get());
    }
    if (const auto* boolVal = node.as_boolean()) {
        return ConfigNode(boolVal->get());
    }
    return ConfigNode();
}

toml::table ConvertConfigNodeToTomlTable(const ConfigNode& node);

toml::array ConvertConfigNodeToTomlArray(const ConfigNode& node) {
    toml::array arr;
    if (!node.isArray()) {
        return arr;
    }
    for (const auto& elem : node.asArray()) {
        if (elem.isString()) {
            arr.push_back(elem.asString());
        } else if (elem.isInt()) {
            arr.push_back(elem.asInt());
        } else if (elem.isFloat()) {
            arr.push_back(elem.asFloat());
        } else if (elem.isBool()) {
            arr.push_back(elem.asBool());
        } else if (elem.isArray()) {
            arr.push_back(ConvertConfigNodeToTomlArray(elem));
        } else if (elem.isTable()) {
            arr.push_back(ConvertConfigNodeToTomlTable(elem));
        }
    }
    return arr;
}

toml::table ConvertConfigNodeToTomlTable(const ConfigNode& node) {
    toml::table tbl;
    if (!node.isTable()) {
        return tbl;
    }

    for (const auto& [k, v] : node.asTable()) {
        if (v.isTable()) {
            continue;
        }
        if (v.isString()) {
            tbl.insert_or_assign(k, v.asString());
        } else if (v.isInt()) {
            tbl.insert_or_assign(k, v.asInt());
        } else if (v.isFloat()) {
            tbl.insert_or_assign(k, v.asFloat());
        } else if (v.isBool()) {
            tbl.insert_or_assign(k, v.asBool());
        } else if (v.isArray()) {
            tbl.insert_or_assign(k, ConvertConfigNodeToTomlArray(v));
        }
    }

    for (const auto& [k, v] : node.asTable()) {
        if (v.isTable()) {
            tbl.insert_or_assign(k, ConvertConfigNodeToTomlTable(v));
        }
    }

    return tbl;
}

ConfigNode ConvertJsonValueToConfigNode(const boost::json::value& jv) {
    if (jv.is_object()) {
        ConfigTable tbl;
        for (const auto& [k, v] : jv.as_object()) {
            tbl.emplace_back(std::string(k), ConvertJsonValueToConfigNode(v));
        }
        return ConfigNode(std::move(tbl));
    }
    if (jv.is_array()) {
        ConfigArray arr;
        const auto& jarr = jv.as_array();
        arr.reserve(jarr.size());
        for (const auto& elem : jarr) {
            arr.push_back(ConvertJsonValueToConfigNode(elem));
        }
        return ConfigNode(std::move(arr));
    }
    if (jv.is_string()) {
        return ConfigNode(std::string(jv.as_string().c_str()));
    }
    if (jv.is_int64()) {
        return ConfigNode(jv.as_int64());
    }
    if (jv.is_uint64()) {
        return ConfigNode(static_cast<int64_t>(jv.as_uint64()));
    }
    if (jv.is_double()) {
        return ConfigNode(jv.as_double());
    }
    if (jv.is_bool()) {
        return ConfigNode(jv.as_bool());
    }
    return ConfigNode();
}

boost::json::value ConvertConfigNodeToJsonValue(const ConfigNode& node) {
    if (node.isTable()) {
        boost::json::object obj;
        for (const auto& [k, v] : node.asTable()) {
            obj[k] = ConvertConfigNodeToJsonValue(v);
        }
        return obj;
    }
    if (node.isArray()) {
        boost::json::array arr;
        for (const auto& elem : node.asArray()) {
            arr.push_back(ConvertConfigNodeToJsonValue(elem));
        }
        return arr;
    }
    if (node.isString()) {
        return boost::json::string(node.asString());
    }
    if (node.isInt()) {
        return node.asInt();
    }
    if (node.isFloat()) {
        return node.asFloat();
    }
    if (node.isBool()) {
        return node.asBool();
    }
    return nullptr;
}

StatusOr<ConfigDocument> ParseTomlContent(const std::string& content) {
    try {
        toml::table tbl = toml::parse(content);
        return ConfigDocument(ConvertTomlTableToConfigNode(tbl));
    } catch (const toml::parse_error& err) {
        return InvalidArgumentError(StrCat(
            "解析配置失败 (行 ", err.source().begin.line, "): ", err.description()));
    }
}

StatusOr<ConfigDocument> ParseJsonContent(const std::string& content) {
    boost::system::error_code ec;
    auto jv = boost::json::parse(content, ec);
    if (ec) {
        return InvalidArgumentError(StrCat("解析 JSON 配置失败: ", ec.message()));
    }
    if (!jv.is_object()) {
        return InvalidArgumentError("配置文件根节点必须为对象");
    }
    return ConfigDocument(ConvertJsonValueToConfigNode(jv));
}

} // namespace

ConfigNode::ConfigNode() : m_value(ConfigTable{}) {}
ConfigNode::ConfigNode(bool val) : m_value(val) {}
ConfigNode::ConfigNode(int64_t val) : m_value(val) {}
ConfigNode::ConfigNode(int val) : m_value(static_cast<int64_t>(val)) {}
ConfigNode::ConfigNode(double val) : m_value(val) {}
ConfigNode::ConfigNode(std::string val) : m_value(std::move(val)) {}
ConfigNode::ConfigNode(const char* val) : m_value(std::string(val ? val : "")) {}
ConfigNode::ConfigNode(ConfigArray arr) : m_value(std::move(arr)) {}
ConfigNode::ConfigNode(ConfigTable table) : m_value(std::move(table)) {}

bool ConfigNode::isNull() const { return std::holds_alternative<std::monostate>(m_value); }
bool ConfigNode::isBool() const { return std::holds_alternative<bool>(m_value); }
bool ConfigNode::isInt() const { return std::holds_alternative<int64_t>(m_value); }
bool ConfigNode::isFloat() const { return std::holds_alternative<double>(m_value); }
bool ConfigNode::isString() const { return std::holds_alternative<std::string>(m_value); }
bool ConfigNode::isArray() const { return std::holds_alternative<ConfigArray>(m_value); }
bool ConfigNode::isTable() const { return std::holds_alternative<ConfigTable>(m_value); }

bool ConfigNode::asBool(bool defaultVal) const {
    if (const auto* p = std::get_if<bool>(&m_value)) {
        return *p;
    }
    return defaultVal;
}

int64_t ConfigNode::asInt(int64_t defaultVal) const {
    if (const auto* p = std::get_if<int64_t>(&m_value)) {
        return *p;
    }
    if (const auto* f = std::get_if<double>(&m_value)) {
        return static_cast<int64_t>(*f);
    }
    return defaultVal;
}

double ConfigNode::asFloat(double defaultVal) const {
    if (const auto* p = std::get_if<double>(&m_value)) {
        return *p;
    }
    if (const auto* i = std::get_if<int64_t>(&m_value)) {
        return static_cast<double>(*i);
    }
    return defaultVal;
}

std::string ConfigNode::asString(const std::string& defaultVal) const {
    if (const auto* p = std::get_if<std::string>(&m_value)) {
        return *p;
    }
    return defaultVal;
}

std::string ConfigNode::toValueString() const {
    if (isString()) {
        return asString();
    }
    if (isInt()) {
        return std::to_string(asInt());
    }
    if (isFloat()) {
        return std::to_string(asFloat());
    }
    if (isBool()) {
        return asBool() ? "true" : "false";
    }
    return "";
}

const ConfigArray& ConfigNode::asArray() const {
    static const ConfigArray kEmptyArray;
    if (const auto* p = std::get_if<ConfigArray>(&m_value)) {
        return *p;
    }
    return kEmptyArray;
}

ConfigArray& ConfigNode::asArrayMutable() {
    if (!isArray()) {
        m_value = ConfigArray{};
    }
    return std::get<ConfigArray>(m_value);
}

const ConfigTable& ConfigNode::asTable() const {
    static const ConfigTable kEmptyTable;
    if (const auto* p = std::get_if<ConfigTable>(&m_value)) {
        return *p;
    }
    return kEmptyTable;
}

ConfigTable& ConfigNode::asTableMutable() {
    if (!isTable()) {
        m_value = ConfigTable{};
    }
    return std::get<ConfigTable>(m_value);
}

const ConfigNode* ConfigNode::findDirectKey(const std::string& key) const {
    if (!isTable()) {
        return nullptr;
    }
    for (const auto& [k, v] : asTable()) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

ConfigNode* ConfigNode::findDirectKey(const std::string& key) {
    if (!isTable()) {
        return nullptr;
    }
    for (auto& [k, v] : asTableMutable()) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

ConfigNode& ConfigNode::setDirectKey(const std::string& key, ConfigNode node) {
    auto& tbl = asTableMutable();
    for (auto& [k, v] : tbl) {
        if (k == key) {
            v = std::move(node);
            return v;
        }
    }
    tbl.emplace_back(key, std::move(node));
    return tbl.back().second;
}

const ConfigNode* ConfigNode::findNode(const std::string& keyPath) const {
    if (const ConfigNode* direct = findDirectKey(keyPath)) {
        return direct;
    }
    auto segments = SplitKeyPath(keyPath);
    if (segments.empty()) {
        return nullptr;
    }
    const ConfigNode* current = this;
    for (const auto& seg : segments) {
        current = current->findDirectKey(seg);
        if (!current) {
            return nullptr;
        }
    }
    return current;
}

ConfigNode* ConfigNode::findNode(const std::string& keyPath) {
    if (ConfigNode* direct = findDirectKey(keyPath)) {
        return direct;
    }
    auto segments = SplitKeyPath(keyPath);
    if (segments.empty()) {
        return nullptr;
    }
    ConfigNode* current = this;
    for (const auto& seg : segments) {
        current = current->findDirectKey(seg);
        if (!current) {
            return nullptr;
        }
    }
    return current;
}

const ConfigNode* ConfigNode::findSection(const std::string& keyPath) const {
    const ConfigNode* node = findNode(keyPath);
    return (node && node->isTable()) ? node : nullptr;
}

ConfigNode* ConfigNode::findSection(const std::string& keyPath) {
    ConfigNode* node = findNode(keyPath);
    return (node && node->isTable()) ? node : nullptr;
}

ConfigNode& ConfigNode::section(const std::string& keyPath) {
    auto segments = SplitKeyPath(keyPath);
    if (segments.empty()) {
        return *this;
    }
    ConfigNode* current = this;
    for (const auto& seg : segments) {
        ConfigNode* next = current->findDirectKey(seg);
        if (!next) {
            next = &current->setDirectKey(seg, ConfigNode(ConfigTable{}));
        } else if (!next->isTable()) {
            *next = ConfigNode(ConfigTable{});
        }
        current = next;
    }
    return *current;
}

bool ConfigNode::hasKey(const std::string& keyPath) const {
    return findNode(keyPath) != nullptr;
}

bool ConfigNode::hasSection(const std::string& keyPath) const {
    return findSection(keyPath) != nullptr;
}

std::vector<std::string> ConfigNode::keys() const {
    std::vector<std::string> result;
    if (!isTable()) {
        return result;
    }
    const auto& tbl = asTable();
    result.reserve(tbl.size());
    for (const auto& [k, _] : tbl) {
        result.push_back(k);
    }
    return result;
}

std::string ConfigNode::getString(const std::string& keyPath, const std::string& defaultVal) const {
    const ConfigNode* node = findNode(keyPath);
    if (node && node->isString()) {
        return node->asString();
    }
    return defaultVal;
}

std::string ConfigNode::getStringOr(std::initializer_list<const char*> candidatePaths,
                                    const std::string& defaultVal) const {
    for (const char* path : candidatePaths) {
        if (!path) {
            continue;
        }
        const ConfigNode* node = findNode(path);
        if (node && node->isString()) {
            return node->asString();
        }
    }
    return defaultVal;
}

int64_t ConfigNode::getInt(const std::string& keyPath, int64_t defaultVal) const {
    const ConfigNode* node = findNode(keyPath);
    if (node && (node->isInt() || node->isFloat())) {
        return node->asInt(defaultVal);
    }
    return defaultVal;
}

double ConfigNode::getFloat(const std::string& keyPath, double defaultVal) const {
    const ConfigNode* node = findNode(keyPath);
    if (node && (node->isFloat() || node->isInt())) {
        return node->asFloat(defaultVal);
    }
    return defaultVal;
}

bool ConfigNode::getBool(const std::string& keyPath, bool defaultVal) const {
    const ConfigNode* node = findNode(keyPath);
    if (node && node->isBool()) {
        return node->asBool(defaultVal);
    }
    return defaultVal;
}

std::vector<std::string> ConfigNode::getStringArray(const std::string& keyPath) const {
    std::vector<std::string> result;
    const ConfigNode* node = findNode(keyPath);
    if (!node || !node->isArray()) {
        return result;
    }
    for (const auto& elem : node->asArray()) {
        if (elem.isString()) {
            result.push_back(elem.asString());
        }
    }
    return result;
}

std::vector<std::string> ConfigNode::getStringArrayOr(std::initializer_list<const char*> candidatePaths) const {
    for (const char* path : candidatePaths) {
        if (!path) {
            continue;
        }
        const ConfigNode* node = findNode(path);
        if (node && node->isArray()) {
            return getStringArray(path);
        }
    }
    return {};
}

std::unordered_map<std::string, std::string> ConfigNode::getStringMap(const std::string& keyPath) const {
    std::unordered_map<std::string, std::string> result;
    const ConfigNode* sec = findSection(keyPath);
    if (!sec) {
        return result;
    }
    for (const auto& [k, v] : sec->asTable()) {
        result[k] = v.toValueString();
    }
    return result;
}

void ConfigNode::setNode(const std::string& keyPath, ConfigNode node) {
    auto segments = SplitKeyPath(keyPath);
    if (segments.empty()) {
        return;
    }
    ConfigNode* current = this;
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        current = &current->section(segments[i]);
    }
    current->setDirectKey(segments.back(), std::move(node));
}

void ConfigNode::setString(const std::string& keyPath, const std::string& val) {
    setNode(keyPath, ConfigNode(val));
}

void ConfigNode::setInt(const std::string& keyPath, int64_t val) {
    setNode(keyPath, ConfigNode(val));
}

void ConfigNode::setFloat(const std::string& keyPath, double val) {
    setNode(keyPath, ConfigNode(val));
}

void ConfigNode::setBool(const std::string& keyPath, bool val) {
    setNode(keyPath, ConfigNode(val));
}

void ConfigNode::setStringArray(const std::string& keyPath, const std::vector<std::string>& values) {
    ConfigArray arr;
    arr.reserve(values.size());
    for (const auto& item : values) {
        arr.emplace_back(item);
    }
    setNode(keyPath, ConfigNode(std::move(arr)));
}

void ConfigNode::setStringMap(const std::string& keyPath,
                              const std::unordered_map<std::string, std::string>& values) {
    ConfigTable tbl;
    tbl.reserve(values.size());
    for (const auto& [k, v] : values) {
        tbl.emplace_back(k, ConfigNode(v));
    }
    setNode(keyPath, ConfigNode(std::move(tbl)));
}

bool ConfigNode::removeKey(const std::string& keyPath) {
    auto segments = SplitKeyPath(keyPath);
    if (segments.empty() || !isTable()) {
        return false;
    }
    ConfigNode* parent = this;
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        parent = parent->findSection(segments[i]);
        if (!parent) {
            return false;
        }
    }
    auto& tbl = parent->asTableMutable();
    const std::string& targetKey = segments.back();
    for (auto it = tbl.begin(); it != tbl.end(); ++it) {
        if (it->first == targetKey) {
            tbl.erase(it);
            return true;
        }
    }
    return false;
}

void ConfigNode::clear() {
    m_value = ConfigTable{};
}

ConfigDocument::ConfigDocument() : m_root(ConfigTable{}) {}

ConfigDocument::ConfigDocument(ConfigNode rootNode) : m_root(std::move(rootNode)) {}

StatusOr<ConfigDocument> ConfigDocument::parse(const std::string& content, ConfigFormat format) {
    ConfigFormat resolvedFormat = DetectFormatFromContent(content, format);
    if (resolvedFormat == ConfigFormat::Json) {
        return ParseJsonContent(content);
    }
    return ParseTomlContent(content);
}

StatusOr<ConfigDocument> ConfigDocument::loadFromFile(ResourceLoader* loader,
                                                      const std::string& filePath,
                                                      ConfigFormat format) {
    UniquePtr<DiskResourceLoader> fallbackLoader;
    if (!loader) {
        fallbackLoader = makeUnique<DiskResourceLoader>();
        loader = fallbackLoader.get();
    }

    auto contentRes = loader->loadFile(filePath);
    CHECK_STATUS(contentRes);

    ConfigFormat pathHintFormat = DetectFormatFromPath(filePath, format);
    return parse(contentRes.value(), pathHintFormat);
}

std::string ConfigDocument::serialize(ConfigFormat format) const {
    if (format == ConfigFormat::Json) {
        return boost::json::serialize(ConvertConfigNodeToJsonValue(m_root));
    }
    toml::table tbl = ConvertConfigNodeToTomlTable(m_root);
    std::ostringstream oss;
    oss << tbl << "\n";
    return oss.str();
}

Status ConfigDocument::saveToFile(ResourceLoader* loader,
                                  const std::string& filePath,
                                  ConfigFormat format) const {
    UniquePtr<DiskResourceLoader> fallbackLoader;
    if (!loader) {
        fallbackLoader = makeUnique<DiskResourceLoader>();
        loader = fallbackLoader.get();
    }

    ConfigFormat resolvedFormat = format;
    if (resolvedFormat == ConfigFormat::Auto) {
        resolvedFormat = DetectFormatFromPath(filePath, ConfigFormat::Auto);
        if (resolvedFormat == ConfigFormat::Auto) {
            resolvedFormat = ConfigFormat::Toml;
        }
    }

    std::string serializedContent = serialize(resolvedFormat);
    return loader->saveFile(filePath, serializedContent);
}

} // namespace MQEngine
