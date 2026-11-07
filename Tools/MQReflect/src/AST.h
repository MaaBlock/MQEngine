#pragma once

#include "Token.h"
#include <string>
#include <vector>

namespace MQReflect {

enum class AccessSpecifier {
    Public,
    Protected,
    Private
};

struct RangeAttribute {
    bool enabled = false;
    double min = 0.0;
    double max = 0.0;
};

struct CommonAttributes {
    bool reflect = false;       // [[mq::reflect]]
    bool service = false;       // [[mq::service]]
    bool script = false;        // [[mq::script]]
    bool ignore = false;        // [[mq::ignore]] 或 [[mq::no_reflect]]
    bool readOnly = false;      // [[mq::readonly]]
    RangeAttribute range;       // [[mq::range(min, max)]]
    std::string tooltip;        // [[mq::tooltip("...")]]
};

struct ParameterAST {
    std::string type;          // 参数类型，例如 "const std::string&"
    std::string name;          // 参数精确名称，例如 "scenePath"
    std::string defaultValue;  // 默认值表达式（可选）
};

struct MethodAST {
    std::string name;
    std::string returnType;
    std::vector<ParameterAST> parameters;
    bool isConst = false;
    bool isStatic = false;
    bool isVirtual = false;
    bool isOverride = false;
    bool isNoexcept = false;
    bool isDeleted = false;
    bool isDefaulted = false;
    bool isTemplate = false;
    bool isConstructor = false;
    bool isDestructor = false;
    bool isOperator = false;
    AccessSpecifier access = AccessSpecifier::Public;
    CommonAttributes attrs;
    SourceLocation loc;
};

struct FieldAST {
    std::string name;
    std::string type;
    std::string defaultValue;
    bool isStatic = false;
    bool isConstexpr = false;
    bool isReference = false;
    bool isBitfield = false;
    AccessSpecifier access = AccessSpecifier::Public;
    CommonAttributes attrs;
    SourceLocation loc;
};

struct EnumeratorAST {
    std::string name;
    std::string value;
    CommonAttributes attrs;
};

struct EnumAST {
    std::string name;
    std::string enclosingNamespace;
    std::string qualifiedName;
    bool isScoped = true;       // enum class vs enum
    std::string underlyingType;
    std::vector<EnumeratorAST> enumerators;
    bool isForwardDecl = false;
    CommonAttributes attrs;
    SourceLocation loc;
};

enum class RecordKind {
    Struct,
    Class
};

struct RecordAST {
    RecordKind kind = RecordKind::Struct;
    std::string name;
    std::string enclosingNamespace;
    std::string qualifiedName;
    std::vector<std::string> baseClasses;
    std::vector<FieldAST> fields;
    std::vector<MethodAST> methods;
    std::vector<EnumAST> nestedEnums;
    bool isTemplate = false;
    bool isForwardDecl = false;
    CommonAttributes attrs;
    SourceLocation loc;
};

struct ParsedHeaderAST {
    std::string headerPath;
    std::vector<RecordAST> records;
    std::vector<EnumAST> enums;
};

} // namespace MQReflect
