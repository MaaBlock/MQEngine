#pragma once

#include <string>
#include <string_view>

namespace MQReflect {

enum class TokenType {
    EndOfFile,
    Identifier,
    StringLiteral,
    CharLiteral,
    IntegerLiteral,
    FloatLiteral,

    // 关键字
    KwNamespace,
    KwClass,
    KwStruct,
    KwEnum,
    KwUnion,
    KwPublic,
    KwProtected,
    KwPrivate,
    KwTemplate,
    KwConst,
    KwConstexpr,
    KwStatic,
    KwVirtual,
    KwOverride,
    KwFinal,
    KwInline,
    KwNoexcept,
    KwExplicit,
    KwFriend,
    KwUsing,
    KwTypedef,
    KwOperator,
    KwDelete,
    KwDefault,
    KwTypeName,
    KwAuto,
    KwVoid,

    // 标点与运算符
    LBrace,        // {
    RBrace,        // }
    LParen,        // (
    RParen,        // )
    LBracket,      // [
    RBracket,      // ]
    LAttribute,    // [[
    RAttribute,    // ]]
    Less,          // <
    Greater,       // >
    Semicolon,     // ;
    Colon,         // :
    DoubleColon,   // ::
    Comma,         // ,
    Equal,         // =
    Star,          // *
    Ampersand,     // &
    DoubleAmp,     // &&
    Tilde,         // ~
    Ellipsis,      // ...
    Exclamation,   // !
    Plus,          // +
    Minus,         // -
    Slash,         // /
    Percent,       // %
    Arrow,         // ->
    Dot,           // .

    Unknown
};

struct SourceLocation {
    std::string filePath;
    int line = 1;
    int column = 1;
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string text;
    SourceLocation loc;
};

inline std::string_view tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::EndOfFile: return "EndOfFile";
        case TokenType::Identifier: return "Identifier";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::CharLiteral: return "CharLiteral";
        case TokenType::IntegerLiteral: return "IntegerLiteral";
        case TokenType::FloatLiteral: return "FloatLiteral";
        case TokenType::KwNamespace: return "namespace";
        case TokenType::KwClass: return "class";
        case TokenType::KwStruct: return "struct";
        case TokenType::KwEnum: return "enum";
        case TokenType::KwUnion: return "union";
        case TokenType::KwPublic: return "public";
        case TokenType::KwProtected: return "protected";
        case TokenType::KwPrivate: return "private";
        case TokenType::KwTemplate: return "template";
        case TokenType::KwConst: return "const";
        case TokenType::KwConstexpr: return "constexpr";
        case TokenType::KwStatic: return "static";
        case TokenType::KwVirtual: return "virtual";
        case TokenType::KwOverride: return "override";
        case TokenType::KwFinal: return "final";
        case TokenType::KwInline: return "inline";
        case TokenType::KwNoexcept: return "noexcept";
        case TokenType::KwExplicit: return "explicit";
        case TokenType::KwFriend: return "friend";
        case TokenType::KwUsing: return "using";
        case TokenType::KwTypedef: return "typedef";
        case TokenType::KwOperator: return "operator";
        case TokenType::KwDelete: return "delete";
        case TokenType::KwDefault: return "default";
        case TokenType::KwTypeName: return "typename";
        case TokenType::KwAuto: return "auto";
        case TokenType::KwVoid: return "void";
        case TokenType::LBrace: return "{";
        case TokenType::RBrace: return "}";
        case TokenType::LParen: return "(";
        case TokenType::RParen: return ")";
        case TokenType::LBracket: return "[";
        case TokenType::RBracket: return "]";
        case TokenType::LAttribute: return "[[";
        case TokenType::RAttribute: return "]]";
        case TokenType::Less: return "<";
        case TokenType::Greater: return ">";
        case TokenType::Semicolon: return ";";
        case TokenType::Colon: return ":";
        case TokenType::DoubleColon: return "::";
        case TokenType::Comma: return ",";
        case TokenType::Equal: return "=";
        case TokenType::Star: return "*";
        case TokenType::Ampersand: return "&";
        case TokenType::DoubleAmp: return "&&";
        case TokenType::Tilde: return "~";
        case TokenType::Ellipsis: return "...";
        case TokenType::Exclamation: return "!";
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::Arrow: return "->";
        case TokenType::Dot: return ".";
        default: return "Unknown";
    }
}

} // namespace MQReflect
