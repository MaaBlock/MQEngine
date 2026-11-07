#pragma once

#include "Token.h"
#include "AST.h"
#include <string>
#include <vector>

namespace MQReflect {

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string filePath);

    ParsedHeaderAST parse();

private:
    const Token& peek(std::size_t offset = 0) const;
    const Token& advance();
    bool match(TokenType type);
    bool check(TokenType type) const;
    bool isAtEnd() const;

    void skipUntil(TokenType type);
    void skipBalanced(TokenType openType, TokenType closeType);

    CommonAttributes parseAttributes();
    void parseNamespace();
    void parseEnum(CommonAttributes attrs);
    void parseRecord(CommonAttributes attrs);
    void parseTemplateDeclaration(RecordAST* enclosingRecord = nullptr);

    void parseRecordBody(RecordAST& record);
    void parseMemberDeclaration(RecordAST& record, AccessSpecifier currentAccess, CommonAttributes attrs);

    ParameterAST parseParameter(const std::vector<Token>& paramTokens);

    std::string currentNamespace() const;

    std::vector<Token> m_tokens;
    std::string m_filePath;
    std::size_t m_pos = 0;

    std::vector<std::string> m_namespaceStack;
    std::vector<std::string> m_recordStack;
    ParsedHeaderAST m_ast;
};

} // namespace MQReflect
