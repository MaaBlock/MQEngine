#pragma once

#include "Token.h"
#include <string>
#include <string_view>
#include <vector>

namespace MQReflect {

class Lexer {
public:
    Lexer(std::string_view source, std::string filePath);

    std::vector<Token> tokenize();

private:
    char peek(std::size_t offset = 0) const;
    char advance();
    bool match(char expected);
    bool isAtEnd() const;

    SourceLocation currentLocation() const;

    void skipWhitespaceAndComments();
    void skipLineComment();
    void skipBlockComment();
    void skipDisabledIfZero();
    void skipPreprocessorDirective();

    Token scanToken();
    Token scanIdentifierOrKeyword();
    Token scanStringLiteral();
    Token scanRawStringLiteral();
    Token scanCharLiteral();
    Token scanNumberLiteral();

    static bool isIdentifierStart(char c);
    static bool isIdentifierBody(char c);
    static bool isKnownExportMacro(std::string_view ident);
    static TokenType classifyKeyword(std::string_view ident);

    std::string_view m_source;
    std::string m_filePath;
    std::size_t m_pos = 0;
    int m_line = 1;
    int m_col = 1;
};

} // namespace MQReflect
