#include "Lexer.h"
#include <cctype>

namespace MQReflect {

Lexer::Lexer(std::string_view source, std::string filePath)
    : m_source(source), m_filePath(std::move(filePath)) {}

char Lexer::peek(std::size_t offset) const {
    if (m_pos + offset >= m_source.size()) return '\0';
    return m_source[m_pos + offset];
}

char Lexer::advance() {
    if (isAtEnd()) return '\0';
    char c = m_source[m_pos++];
    if (c == '\n') {
        m_line++;
        m_col = 1;
    } else {
        m_col++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || m_source[m_pos] != expected) return false;
    advance();
    return true;
}

bool Lexer::isAtEnd() const {
    return m_pos >= m_source.size();
}

SourceLocation Lexer::currentLocation() const {
    return SourceLocation{m_filePath, m_line, m_col};
}

void Lexer::skipLineComment() {
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

void Lexer::skipBlockComment() {
    // 已经消费了 /*
    while (!isAtEnd()) {
        if (peek() == '*' && peek(1) == '/') {
            advance(); // *
            advance(); // /
            break;
        }
        advance();
    }
}

void Lexer::skipDisabledIfZero() {
    int disabledDepth = 1;
    while (!isAtEnd() && disabledDepth > 0) {
        if (peek() == '\n') {
            advance();
            // 扫描行首空白
            while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
                advance();
            }
            if (!isAtEnd() && peek() == '#') {
                advance(); // #
                while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
                    advance();
                }
                std::string directive;
                while (!isAtEnd() && isIdentifierBody(peek())) {
                    directive.push_back(advance());
                }
                if (directive == "if" || directive == "ifdef" || directive == "ifndef") {
                    disabledDepth++;
                } else if (directive == "endif") {
                    disabledDepth--;
                }
            }
        } else {
            advance();
        }
    }
}

void Lexer::skipPreprocessorDirective() {
    advance(); // #
    while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
        advance();
    }
    std::string directive;
    while (!isAtEnd() && isIdentifierBody(peek())) {
        directive.push_back(advance());
    }

    if (directive == "if") {
        while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
            advance();
        }
        if (peek() == '0' && !isIdentifierBody(peek(1))) {
            skipDisabledIfZero();
            return;
        }
    }

    // 普通宏指令，跳过该行（考虑反斜杠续行符）
    while (!isAtEnd()) {
        if (peek() == '\\' && peek(1) == '\n') {
            advance();
            advance();
            continue;
        }
        if (peek() == '\n') {
            advance();
            break;
        }
        advance();
    }
}

void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            advance(); advance();
            skipLineComment();
        } else if (c == '/' && peek(1) == '*') {
            advance(); advance();
            skipBlockComment();
        } else if (c == '#' && (m_col == 1 || m_pos == 0 || m_source[m_pos - 1] == '\n')) {
            skipPreprocessorDirective();
        } else {
            break;
        }
    }
}

bool Lexer::isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::isIdentifierBody(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::isKnownExportMacro(std::string_view ident) {
    if (ident == "ENGINE_API" || ident == "FCT_API" ||
        ident == "FCT_RHI_API" || ident == "FCT_BASE_API" ||
        ident == "FCT_NODE_API" || ident == "FCT_PYBRI_API" ||
        ident == "FCT_NBR_API" || ident == "FORCEINLINE" ||
        ident == "NODISCARD" || ident == "MQ_NODISCARD" ||
        ident == "MQ_API" || ident == "HOME_LBM_API") {
        return true;
    }
    if (ident.size() > 4 && ident.ends_with("_API")) {
        for (char ch : ident) {
            if (!(std::isupper(static_cast<unsigned char>(ch)) ||
                  std::isdigit(static_cast<unsigned char>(ch)) ||
                  ch == '_')) {
                return false;
            }
        }
        return true;
    }
    return false;
}

TokenType Lexer::classifyKeyword(std::string_view ident) {
    if (ident == "namespace") return TokenType::KwNamespace;
    if (ident == "class") return TokenType::KwClass;
    if (ident == "struct") return TokenType::KwStruct;
    if (ident == "enum") return TokenType::KwEnum;
    if (ident == "union") return TokenType::KwUnion;
    if (ident == "public") return TokenType::KwPublic;
    if (ident == "protected") return TokenType::KwProtected;
    if (ident == "private") return TokenType::KwPrivate;
    if (ident == "template") return TokenType::KwTemplate;
    if (ident == "const") return TokenType::KwConst;
    if (ident == "constexpr") return TokenType::KwConstexpr;
    if (ident == "static") return TokenType::KwStatic;
    if (ident == "virtual") return TokenType::KwVirtual;
    if (ident == "override") return TokenType::KwOverride;
    if (ident == "final") return TokenType::KwFinal;
    if (ident == "inline") return TokenType::KwInline;
    if (ident == "noexcept") return TokenType::KwNoexcept;
    if (ident == "explicit") return TokenType::KwExplicit;
    if (ident == "friend") return TokenType::KwFriend;
    if (ident == "using") return TokenType::KwUsing;
    if (ident == "typedef") return TokenType::KwTypedef;
    if (ident == "operator") return TokenType::KwOperator;
    if (ident == "delete") return TokenType::KwDelete;
    if (ident == "default") return TokenType::KwDefault;
    if (ident == "typename") return TokenType::KwTypeName;
    if (ident == "auto") return TokenType::KwAuto;
    if (ident == "void") return TokenType::KwVoid;
    return TokenType::Identifier;
}

Token Lexer::scanIdentifierOrKeyword() {
    SourceLocation loc = currentLocation();
    std::size_t start = m_pos;
    while (!isAtEnd() && isIdentifierBody(peek())) {
        advance();
    }
    std::string text(m_source.substr(start, m_pos - start));

    // 如果是已知导出宏，直接递归跳过获取下一个有效 Token
    if (isKnownExportMacro(text)) {
        return scanToken();
    }

    TokenType type = classifyKeyword(text);
    return Token{type, text, loc};
}

Token Lexer::scanRawStringLiteral() {
    // 已经消费了 R"
    SourceLocation loc = currentLocation();
    std::string delim;
    while (!isAtEnd() && peek() != '(') {
        delim.push_back(advance());
    }
    if (!isAtEnd() && peek() == '(') {
        advance(); // (
    }
    std::string content;
    std::string closing = ")" + delim + "\"";
    while (!isAtEnd()) {
        if (m_source.substr(m_pos).starts_with(closing)) {
            for (std::size_t i = 0; i < closing.size(); ++i) advance();
            break;
        }
        content.push_back(advance());
    }
    return Token{TokenType::StringLiteral, content, loc};
}

Token Lexer::scanStringLiteral() {
    SourceLocation loc = currentLocation();
    advance(); // "
    std::string text;
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) text.push_back(advance());
        } else {
            text.push_back(advance());
        }
    }
    if (!isAtEnd() && peek() == '"') {
        advance(); // "
    }
    return Token{TokenType::StringLiteral, text, loc};
}

Token Lexer::scanCharLiteral() {
    SourceLocation loc = currentLocation();
    advance(); // '
    std::string text;
    while (!isAtEnd() && peek() != '\'') {
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) text.push_back(advance());
        } else {
            text.push_back(advance());
        }
    }
    if (!isAtEnd() && peek() == '\'') {
        advance(); // '
    }
    return Token{TokenType::CharLiteral, text, loc};
}

Token Lexer::scanNumberLiteral() {
    SourceLocation loc = currentLocation();
    std::size_t start = m_pos;
    bool isFloat = false;

    // 处理 0x 0b 前缀
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B')) {
        advance(); advance();
        while (!isAtEnd() && (std::isxdigit(static_cast<unsigned char>(peek())) || peek() == '\'')) {
            advance();
        }
    } else {
        while (!isAtEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '\'')) {
            advance();
        }
        if (!isAtEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
            isFloat = true;
            advance(); // .
            while (!isAtEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '\'')) {
                advance();
            }
        }
        if (!isAtEnd() && (peek() == 'e' || peek() == 'E')) {
            isFloat = true;
            advance();
            if (peek() == '+' || peek() == '-') advance();
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
    }

    // 后缀 f, F, u, U, l, L
    while (!isAtEnd() && (peek() == 'f' || peek() == 'F' || peek() == 'u' || peek() == 'U' ||
                         peek() == 'l' || peek() == 'L')) {
        if (peek() == 'f' || peek() == 'F') isFloat = true;
        advance();
    }

    std::string text(m_source.substr(start, m_pos - start));
    return Token{isFloat ? TokenType::FloatLiteral : TokenType::IntegerLiteral, text, loc};
}

Token Lexer::scanToken() {
    skipWhitespaceAndComments();
    if (isAtEnd()) {
        return Token{TokenType::EndOfFile, "", currentLocation()};
    }

    char c = peek();
    SourceLocation loc = currentLocation();

    // 属性语法 [[ 和 ]]
    if (c == '[' && peek(1) == '[') {
        advance(); advance();
        return Token{TokenType::LAttribute, "[[", loc};
    }
    if (c == ']' && peek(1) == ']') {
        advance(); advance();
        return Token{TokenType::RAttribute, "]]", loc};
    }

    // 字符串字面量
    if (c == 'R' && peek(1) == '"') {
        advance(); advance();
        return scanRawStringLiteral();
    }
    if (c == '"') {
        return scanStringLiteral();
    }
    if (c == '\'') {
        return scanCharLiteral();
    }

    // 数字字面量
    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
        return scanNumberLiteral();
    }

    // 标识符或关键字
    if (isIdentifierStart(c)) {
        return scanIdentifierOrKeyword();
    }

    // 双字符标点
    if (c == ':' && peek(1) == ':') {
        advance(); advance();
        return Token{TokenType::DoubleColon, "::", loc};
    }
    if (c == '&' && peek(1) == '&') {
        advance(); advance();
        return Token{TokenType::DoubleAmp, "&&", loc};
    }
    if (c == '-' && peek(1) == '>') {
        advance(); advance();
        return Token{TokenType::Arrow, "->", loc};
    }
    if (c == '.' && peek(1) == '.' && peek(2) == '.') {
        advance(); advance(); advance();
        return Token{TokenType::Ellipsis, "...", loc};
    }

    // 单字符标点
    advance();
    switch (c) {
        case '{': return Token{TokenType::LBrace, "{", loc};
        case '}': return Token{TokenType::RBrace, "}", loc};
        case '(': return Token{TokenType::LParen, "(", loc};
        case ')': return Token{TokenType::RParen, ")", loc};
        case '[': return Token{TokenType::LBracket, "[", loc};
        case ']': return Token{TokenType::RBracket, "]", loc};
        case '<': return Token{TokenType::Less, "<", loc};
        case '>': return Token{TokenType::Greater, ">", loc};
        case ';': return Token{TokenType::Semicolon, ";", loc};
        case ':': return Token{TokenType::Colon, ":", loc};
        case ',': return Token{TokenType::Comma, ",", loc};
        case '=': return Token{TokenType::Equal, "=", loc};
        case '*': return Token{TokenType::Star, "*", loc};
        case '&': return Token{TokenType::Ampersand, "&", loc};
        case '~': return Token{TokenType::Tilde, "~", loc};
        case '!': return Token{TokenType::Exclamation, "!", loc};
        case '+': return Token{TokenType::Plus, "+", loc};
        case '-': return Token{TokenType::Minus, "-", loc};
        case '/': return Token{TokenType::Slash, "/", loc};
        case '%': return Token{TokenType::Percent, "%", loc};
        case '.': return Token{TokenType::Dot, ".", loc};
        default: return Token{TokenType::Unknown, std::string(1, c), loc};
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!isAtEnd()) {
        Token tok = scanToken();
        if (tok.type == TokenType::EndOfFile) {
            tokens.push_back(tok);
            break;
        }
        tokens.push_back(tok);
    }
    if (tokens.empty() || tokens.back().type != TokenType::EndOfFile) {
        tokens.push_back(Token{TokenType::EndOfFile, "", currentLocation()});
    }
    return tokens;
}

} // namespace MQReflect
