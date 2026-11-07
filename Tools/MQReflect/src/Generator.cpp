#include "Generator.h"
#include "Filter.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace MQReflect {

namespace {

std::string qualifyType(std::string_view typeStr, std::string_view enclosingNs) {
    std::string s(typeStr);
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();

    bool isConst = false;
    if (s.rfind("const ", 0) == 0) {
        isConst = true;
        s = s.substr(6);
    }
    std::string suffix;
    while (!s.empty() && (s.back() == '&' || s.back() == '*' || s.back() == ' ')) {
        suffix = s.back() + suffix;
        s.pop_back();
    }

    if (s.empty()) {
        return (isConst ? "const " : "") + suffix;
    }

    static const std::unordered_set<std::string> fundamentals = {
        "void", "bool", "char", "int", "float", "double", "short", "long",
        "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
        "int64_t", "uint64_t", "size_t", "unsigned", "signed"
    };

    if (fundamentals.contains(s) || s.starts_with("std::") || s.starts_with("FCT::") ||
        s.starts_with("physx::") || s.starts_with("absl::") || s.starts_with("::")) {
        return (isConst ? "const " : "") + s + suffix;
    }

    if (!enclosingNs.empty()) {
        s = "::" + std::string(enclosingNs) + s;
    }
    return (isConst ? "const " : "") + s + suffix;
}

} // namespace

void Generator::generateRecordMeta(std::string& out, const RecordAST& record, bool autoReflectMode) {
    if (!Filter::isRecordEligible(record, autoReflectMode)) {
        return;
    }

    std::vector<FieldAST> eligibleFields;
    for (const auto& f : record.fields) {
        if (Filter::isFieldEligible(f)) {
            eligibleFields.push_back(f);
        }
    }

    // 统计所有同名声明（包含模板和其它重载）以准确判定是否需要显式成员指针类型强转消歧
    std::unordered_map<std::string, int> allNameCounts;
    for (const auto& m : record.methods) {
        allNameCounts[m.name]++;
    }

    std::unordered_map<std::string, std::vector<const MethodAST*>> methodMap;
    for (const auto& m : record.methods) {
        if (Filter::isMethodEligible(m)) {
            methodMap[m.name].push_back(&m);
        }
    }

    std::vector<std::pair<const MethodAST*, bool>> eligibleMethods;
    for (const auto& [name, list] : methodMap) {
        bool needsDisambiguation = (list.size() > 1) || (allNameCounts[name] > 1);
        if (list.size() == 1) {
            eligibleMethods.push_back({list[0], needsDisambiguation});
        } else {
            // 如果存在重载：优先选取标记了 [[mq::script]] 的重载；若无则保留首个
            const MethodAST* selected = nullptr;
            for (const auto* m : list) {
                if (m->attrs.script) {
                    selected = m;
                    break;
                }
            }
            if (!selected) {
                for (const auto* m : list) {
                    bool hasRawPtr = false;
                    for (const auto& p : m->parameters) {
                        if (p.type.find('*') != std::string::npos) {
                            hasRawPtr = true;
                            break;
                        }
                    }
                    if (!hasRawPtr) {
                        selected = m;
                        break;
                    }
                }
            }
            if (!selected) {
                selected = list[0];
            }
            eligibleMethods.push_back({selected, true});
        }
    }

    // 确定是否具备显式标记的 script 方法
    bool hasExplicitScriptMethod = false;
    for (const auto& [m, isOv] : eligibleMethods) {
        if (m->attrs.script) {
            hasExplicitScriptMethod = true;
            break;
        }
    }

    bool isService = record.attrs.service || (!eligibleMethods.empty());
    bool isScript = record.attrs.script || isService;

    out += "// =============================================================================\n";
    out += "// Specialization for " + record.qualifiedName + "\n";
    out += "// =============================================================================\n";
    out += "namespace FCT::Reflect {\n\n";
    out += "template<>\n";
    out += "struct TypeMeta<::" + record.qualifiedName + "> {\n";
    out += "    using Type = ::" + record.qualifiedName + ";\n";
    out += "    static constexpr std::string_view name = \"" + record.name + "\";\n";
    out += "    static constexpr std::string_view qualifiedName = \"" + record.qualifiedName + "\";\n";
    out += "    static constexpr bool isClass = " + std::string(record.kind == RecordKind::Class ? "true" : "false") + ";\n";
    out += "    static constexpr bool isStruct = " + std::string(record.kind == RecordKind::Struct ? "true" : "false") + ";\n";
    out += "    static constexpr bool isService = " + std::string(isService ? "true" : "false") + ";\n";
    out += "    static constexpr bool isScript = " + std::string(isScript ? "true" : "false") + ";\n";
    out += "    static constexpr bool isEnum = false;\n";
    out += "    static constexpr bool isScoped = false;\n\n";

    // 生成 fields 元组
    out += "    static constexpr std::size_t fieldCount = " + std::to_string(eligibleFields.size()) + ";\n";
    out += "    static constexpr auto fields = std::make_tuple(";
    for (std::size_t i = 0; i < eligibleFields.size(); ++i) {
        const auto& f = eligibleFields[i];
        if (i > 0) out += ",";
        out += "\n        ::FCT::Reflect::makeFieldMeta(\n";
        out += "            \"" + f.name + "\",\n";
        out += "            &Type::" + f.name + ",\n";
        out += "            ::FCT::Reflect::FieldRange{ " +
               std::string(f.attrs.range.enabled ? "true" : "false") + ", " +
               std::to_string(f.attrs.range.min) + ", " +
               std::to_string(f.attrs.range.max) + " },\n";
        out += "            " + std::string(f.attrs.readOnly ? "true" : "false") + ",\n";
        out += "            \"" + f.attrs.tooltip + "\"\n";
        out += "        )";
    }
    if (!eligibleFields.empty()) out += "\n    ";
    out += ");\n\n";

    // 生成 methods 元组
    out += "    static constexpr std::size_t methodCount = " + std::to_string(eligibleMethods.size()) + ";\n";
    out += "    static constexpr auto methods = std::make_tuple(";
    for (std::size_t i = 0; i < eligibleMethods.size(); ++i) {
        const auto* m = eligibleMethods[i].first;
        bool isOverloaded = eligibleMethods[i].second;
        if (i > 0) out += ",";
        out += "\n        ::FCT::Reflect::makeMethodMeta(\n";
        out += "            \"" + m->name + "\",\n";

        if (isOverloaded) {
            std::string retType = qualifyType(m->returnType, record.enclosingNamespace);
            if (retType.empty()) retType = "void";
            std::string paramTypes;
            for (std::size_t p = 0; p < m->parameters.size(); ++p) {
                if (p > 0) paramTypes += ", ";
                paramTypes += qualifyType(m->parameters[p].type, record.enclosingNamespace);
            }
            std::string constQual = m->isConst ? " const" : "";
            out += "            static_cast<" + retType + " (Type::*)(" + paramTypes + ")" + constQual + ">(&Type::" + m->name + "),\n";
        } else {
            out += "            &Type::" + m->name + ",\n";
        }

        // 参数名称提取
        std::size_t arity = m->parameters.size();
        out += "            std::array<std::string_view, " + std::to_string(arity) + ">{";
        for (std::size_t p = 0; p < arity; ++p) {
            if (p > 0) out += ", ";
            out += "\"" + m->parameters[p].name + "\"";
        }
        out += "},\n";

        bool methodIsScript = m->attrs.script || (!hasExplicitScriptMethod && isService);
        out += "            " + std::string(methodIsScript ? "true" : "false") + ",\n";
        out += "            \"" + m->attrs.tooltip + "\"\n";
        out += "        )";
    }
    if (!eligibleMethods.empty()) out += "\n    ";
    out += ");\n\n";

    // 枚举元组占位
    out += "    static constexpr std::size_t enumCount = 0;\n";
    out += "    static constexpr auto enumerators = std::make_tuple();\n";
    out += "};\n\n";
    out += "} // namespace FCT::Reflect\n\n";
}

void Generator::generateEnumMeta(std::string& out, const EnumAST& enumAst, bool autoReflectMode) {
    if (!Filter::isEnumEligible(enumAst, autoReflectMode)) {
        return;
    }

    out += "// =============================================================================\n";
    out += "// Specialization for Enum " + enumAst.qualifiedName + "\n";
    out += "// =============================================================================\n";
    out += "namespace FCT::Reflect {\n\n";
    out += "template<>\n";
    out += "struct TypeMeta<::" + enumAst.qualifiedName + "> {\n";
    out += "    using Type = ::" + enumAst.qualifiedName + ";\n";
    out += "    static constexpr std::string_view name = \"" + enumAst.name + "\";\n";
    out += "    static constexpr std::string_view qualifiedName = \"" + enumAst.qualifiedName + "\";\n";
    out += "    static constexpr bool isClass = false;\n";
    out += "    static constexpr bool isStruct = false;\n";
    out += "    static constexpr bool isService = false;\n";
    out += "    static constexpr bool isScript = false;\n";
    out += "    static constexpr bool isEnum = true;\n";
    out += "    static constexpr bool isScoped = " + std::string(enumAst.isScoped ? "true" : "false") + ";\n\n";

    out += "    static constexpr std::size_t fieldCount = 0;\n";
    out += "    static constexpr auto fields = std::make_tuple();\n\n";

    out += "    static constexpr std::size_t methodCount = 0;\n";
    out += "    static constexpr auto methods = std::make_tuple();\n\n";

    out += "    static constexpr std::size_t enumCount = " + std::to_string(enumAst.enumerators.size()) + ";\n";
    out += "    static constexpr auto enumerators = std::make_tuple(";
    for (std::size_t i = 0; i < enumAst.enumerators.size(); ++i) {
        const auto& item = enumAst.enumerators[i];
        if (i > 0) out += ",";
        out += "\n        ::FCT::Reflect::makeEnumItemMeta(Type::" + item.name + ", \"" + item.name + "\")";
    }
    if (!enumAst.enumerators.empty()) out += "\n    ";
    out += ");\n";
    out += "};\n\n";
    out += "} // namespace FCT::Reflect\n\n";
}

std::string Generator::generateHeader(const std::vector<ParsedHeaderAST>& headers, bool autoReflectMode) {
    std::string out;
    out += "// =============================================================================\n";
    out += "// Generated by MQReflect Native Static Reflection Preprocessor\n";
    out += "// DO NOT MANUALLY EDIT THIS FILE\n";
    out += "// =============================================================================\n";
    out += "#pragma once\n\n";
    out += "#include <tuple>\n";
    out += "#include <string_view>\n";
    out += "#include <array>\n";
    out += "#include <type_traits>\n";
    out += "#include <cstddef>\n";
    out += "#if __has_include(<FCT_Base/Reflect/ReflectMeta.h>)\n";
    out += "#include <FCT_Base/Reflect/ReflectMeta.h>\n";
    out += "#elif __has_include(\"Reflect/ReflectMeta.h\")\n";
    out += "#include \"Reflect/ReflectMeta.h\"\n";
    out += "#endif\n\n";

    out += "namespace FCT::Reflect {\n";
    out += "    template<typename T>\n";
    out += "    struct TypeMeta;\n";
    out += "} // namespace FCT::Reflect\n\n";

    for (const auto& header : headers) {
        // 先生成所有枚举
        for (const auto& e : header.enums) {
            generateEnumMeta(out, e, autoReflectMode);
        }
        // 再生成所有结构体与类
        for (const auto& r : header.records) {
            generateRecordMeta(out, r, autoReflectMode);
        }
    }

    return out;
}

bool Generator::writeIfChanged(const std::filesystem::path& outputPath, const std::string& newContent) {
    if (std::filesystem::exists(outputPath)) {
        std::ifstream inFile(outputPath, std::ios::binary);
        if (inFile.is_open()) {
            std::string existing((std::istreambuf_iterator<char>(inFile)),
                                  std::istreambuf_iterator<char>());
            if (existing == newContent) {
                // 文件内容一致，跳过写入以保持时间戳
                return true;
            }
        }
    }

    if (outputPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);
    }

    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        return false;
    }
    outFile << newContent;
    return outFile.good();
}

} // namespace MQReflect
