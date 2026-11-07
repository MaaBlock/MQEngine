#include "Parser.h"
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <iostream>

namespace MQReflect {

namespace {
double parseDoubleSafe(std::string_view text, double defaultVal = 0.0) {
    if (text.empty()) {
        return defaultVal;
    }
    while (!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                             text.back() == 'l' || text.back() == 'L' ||
                             text.back() == 'u' || text.back() == 'U')) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return defaultVal;
    }
    std::string cleanText;
    if (text.find('\'') != std::string_view::npos) {
        cleanText.reserve(text.size());
        for (char c : text) {
            if (c != '\'') {
                cleanText.push_back(c);
            }
        }
        text = cleanText;
    }

    double val = defaultVal;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), val);
    if (ec == std::errc{}) {
        return val;
    }
    return defaultVal;
}
}


Parser::Parser(std::vector<Token> tokens, std::string filePath)
    : m_tokens(std::move(tokens)), m_filePath(std::move(filePath)) {
    m_ast.headerPath = m_filePath;
}

const Token& Parser::peek(std::size_t offset) const {
    if (m_pos + offset >= m_tokens.size()) {
        return m_tokens.back(); // EndOfFile
    }
    return m_tokens[m_pos + offset];
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        return m_tokens[m_pos++];
    }
    return m_tokens.back();
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return type == TokenType::EndOfFile;
    return peek().type == type;
}

bool Parser::isAtEnd() const {
    return m_pos >= m_tokens.size() || m_tokens[m_pos].type == TokenType::EndOfFile;
}

void Parser::skipUntil(TokenType type) {
    while (!isAtEnd() && !check(type)) {
        advance();
    }
}

void Parser::skipBalanced(TokenType openType, TokenType closeType) {
    int depth = 1;
    while (!isAtEnd() && depth > 0) {
        if (check(openType)) {
            depth++;
        } else if (check(closeType)) {
            depth--;
            if (depth == 0) {
                advance();
                return;
            }
        }
        advance();
    }
}

std::string Parser::currentNamespace() const {
    std::string ns;
    for (const auto& part : m_namespaceStack) {
        if (!part.empty()) {
            ns += part + "::";
        }
    }
    return ns;
}

CommonAttributes Parser::parseAttributes() {
    CommonAttributes attrs;
    while (check(TokenType::LAttribute)) {
        advance(); // 消费 [[
        while (!isAtEnd() && !check(TokenType::RAttribute)) {
            if (check(TokenType::Identifier)) {
                std::string attrName;
                while (!isAtEnd() && (check(TokenType::Identifier) || check(TokenType::DoubleColon))) {
                    attrName += advance().text;
                }

                if (attrName == "mq::reflect" || attrName == "reflect") {
                    attrs.reflect = true;
                } else if (attrName == "mq::service" || attrName == "service") {
                    attrs.service = true;
                } else if (attrName == "mq::script" || attrName == "script") {
                    attrs.script = true;
                } else if (attrName == "mq::ignore" || attrName == "mq::no_reflect" ||
                           attrName == "ignore" || attrName == "no_reflect") {
                    attrs.ignore = true;
                } else if (attrName == "mq::readonly" || attrName == "readonly") {
                    attrs.readOnly = true;
                } else if (attrName == "mq::range" || attrName == "range") {
                    if (match(TokenType::LParen)) {
                        // 解析 min
                        bool minNeg = match(TokenType::Minus);
                        if (check(TokenType::FloatLiteral) || check(TokenType::IntegerLiteral)) {
                            double minVal = parseDoubleSafe(advance().text);
                            if (minNeg) minVal = -minVal;
                            attrs.range.min = minVal;
                        }
                        match(TokenType::Comma);
                        // 解析 max
                        bool maxNeg = match(TokenType::Minus);
                        if (check(TokenType::FloatLiteral) || check(TokenType::IntegerLiteral)) {
                            double maxVal = parseDoubleSafe(advance().text);
                            if (maxNeg) maxVal = -maxVal;
                            attrs.range.max = maxVal;
                        }
                        attrs.range.enabled = true;
                        match(TokenType::RParen);
                    }
                } else if (attrName == "mq::tooltip" || attrName == "tooltip") {
                    if (match(TokenType::LParen)) {
                        if (check(TokenType::StringLiteral)) {
                            attrs.tooltip = advance().text;
                        }
                        match(TokenType::RParen);
                    }
                } else {
                    // 其他未知属性，如果有参数括号则跳过括号
                    if (check(TokenType::LParen)) {
                        advance();
                        skipBalanced(TokenType::LParen, TokenType::RParen);
                    }
                }
            } else if (check(TokenType::Comma)) {
                advance();
            } else {
                advance();
            }
        }
        match(TokenType::RAttribute); // 消费 ]]
    }
    return attrs;
}

void Parser::parseNamespace() {
    advance(); // 消费 namespace
    std::string nsName;
    while (!isAtEnd() && (check(TokenType::Identifier) || check(TokenType::DoubleColon))) {
        nsName += advance().text;
    }

    if (match(TokenType::Equal)) {
        // namespace 别名：namespace fs = std::filesystem;
        skipUntil(TokenType::Semicolon);
        match(TokenType::Semicolon);
        return;
    }

    if (match(TokenType::LBrace)) {
        m_namespaceStack.push_back(nsName);
    }
}

void Parser::parseEnum(CommonAttributes attrs) {
    EnumAST enumAst;
    enumAst.attrs = attrs;
    enumAst.loc = peek().loc;
    advance(); // 消费 enum

    if (match(TokenType::KwClass) || match(TokenType::KwStruct)) {
        enumAst.isScoped = true;
    } else {
        enumAst.isScoped = false;
    }

    if (check(TokenType::Identifier)) {
        enumAst.name = advance().text;
    } else {
        // 匿名枚举
        enumAst.name = "";
    }

    std::string prefix = currentNamespace();
    for (const auto& rName : m_recordStack) {
        prefix += rName + "::";
    }
    enumAst.enclosingNamespace = prefix;
    if (!enumAst.name.empty()) {
        enumAst.qualifiedName = enumAst.enclosingNamespace + enumAst.name;
    }

    // 处理底层类型：enum class Key : uint32_t
    if (match(TokenType::Colon)) {
        while (!isAtEnd() && !check(TokenType::LBrace) && !check(TokenType::Semicolon)) {
            enumAst.underlyingType += advance().text;
        }
    }

    // 前向声明检查：enum class KeyCode;
    if (match(TokenType::Semicolon)) {
        enumAst.isForwardDecl = true;
        if (!enumAst.name.empty()) {
            m_ast.enums.push_back(std::move(enumAst));
        }
        return;
    }

    if (match(TokenType::LBrace)) {
        while (!isAtEnd() && !check(TokenType::RBrace)) {
            CommonAttributes itemAttrs = parseAttributes();
            if (check(TokenType::Identifier)) {
                EnumeratorAST item;
                item.attrs = itemAttrs;
                item.name = advance().text;
                if (match(TokenType::Equal)) {
                    std::string valExpr;
                    while (!isAtEnd() && !check(TokenType::Comma) && !check(TokenType::RBrace)) {
                        valExpr += advance().text;
                    }
                    item.value = valExpr;
                }
                enumAst.enumerators.push_back(std::move(item));
            }
            if (check(TokenType::Comma)) {
                advance();
            }
        }
        match(TokenType::RBrace);
        match(TokenType::Semicolon);
        if (!enumAst.name.empty()) {
            m_ast.enums.push_back(std::move(enumAst));
        }
    }
}

ParameterAST Parser::parseParameter(const std::vector<Token>& paramTokens) {
    ParameterAST param;
    if (paramTokens.empty()) return param;

    // 1. 分离默认值
    std::size_t eqPos = std::string::npos;
    int parenDepth = 0, angleDepth = 0, bracketDepth = 0, braceDepth = 0;
    for (std::size_t i = 0; i < paramTokens.size(); ++i) {
        if (paramTokens[i].type == TokenType::LParen) parenDepth++;
        else if (paramTokens[i].type == TokenType::RParen) parenDepth--;
        else if (paramTokens[i].type == TokenType::Less) angleDepth++;
        else if (paramTokens[i].type == TokenType::Greater) angleDepth--;
        else if (paramTokens[i].type == TokenType::LBracket) bracketDepth++;
        else if (paramTokens[i].type == TokenType::RBracket) bracketDepth--;
        else if (paramTokens[i].type == TokenType::LBrace) braceDepth++;
        else if (paramTokens[i].type == TokenType::RBrace) braceDepth--;
        else if (paramTokens[i].type == TokenType::Equal && parenDepth == 0 && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
            eqPos = i;
            break;
        }
    }

    std::vector<Token> declTokens;
    if (eqPos != std::string::npos) {
        for (std::size_t i = 0; i < eqPos; ++i) declTokens.push_back(paramTokens[i]);
        std::string defVal;
        for (std::size_t i = eqPos + 1; i < paramTokens.size(); ++i) {
            if (!defVal.empty() && paramTokens[i].type != TokenType::Comma) defVal += " ";
            defVal += paramTokens[i].text;
        }
        param.defaultValue = defVal;
    } else {
        declTokens = paramTokens;
    }

    if (declTokens.empty()) return param;

    // 2. 提取精确参数名称
    if (declTokens.size() >= 3 &&
        declTokens.back().type == TokenType::RBracket &&
        declTokens[declTokens.size() - 2].type == TokenType::LBracket) {
        declTokens.pop_back(); // 消费 ]
        declTokens.pop_back(); // 消费 [
        if (!declTokens.empty() && declTokens.back().type == TokenType::Identifier) {
            param.name = declTokens.back().text;
            declTokens.pop_back();
        }
        // C++ 形参中 T arr[] 等价于 T*
        Token starTok;
        starTok.type = TokenType::Star;
        starTok.text = "*";
        declTokens.push_back(starTok);
    } else {
        const Token& lastTok = declTokens.back();
        if (lastTok.type == TokenType::Identifier) {
            // 如果倒数第二个是 ::，则属于限定类型名称（如 MQEngine::EngineState），无实参名
            bool isQualifiedType = (declTokens.size() >= 2 && declTokens[declTokens.size() - 2].type == TokenType::DoubleColon);
            if (!isQualifiedType && declTokens.size() > 1) {
                param.name = lastTok.text;
                declTokens.pop_back();
            }
        }
    }

    // 3. 组装参数类型字符串
    std::string typeStr;
    for (std::size_t i = 0; i < declTokens.size(); ++i) {
        const auto& tok = declTokens[i];
        if (i > 0 && tok.type != TokenType::Star && tok.type != TokenType::Ampersand &&
            tok.type != TokenType::DoubleAmp && tok.type != TokenType::DoubleColon &&
            declTokens[i - 1].type != TokenType::DoubleColon) {
            typeStr += " ";
        }
        typeStr += tok.text;
    }
    param.type = typeStr;
    return param;
}

void Parser::parseTemplateDeclaration(RecordAST* enclosingRecord) {
    advance(); // 消费 template
    if (match(TokenType::Less)) {
        skipBalanced(TokenType::Less, TokenType::Greater);
    }
    // 模板定义不产生特化，跳过其声明
    CommonAttributes attrs = parseAttributes();
    if (check(TokenType::KwClass) || check(TokenType::KwStruct)) {
        parseRecord(attrs);
        if (!m_ast.records.empty()) {
            m_ast.records.back().isTemplate = true;
        }
    } else {
        // 可能是模板函数（带函数体 {...}）或模板变量/声明（以分号结束）
        std::vector<Token> declTokens;
        int parenDepth = 0, angleDepth = 0, bracketDepth = 0;
        while (!isAtEnd()) {
            if (check(TokenType::LParen)) parenDepth++;
            else if (check(TokenType::RParen)) parenDepth--;
            else if (check(TokenType::Less)) angleDepth++;
            else if (check(TokenType::Greater)) angleDepth--;
            else if (check(TokenType::LBracket)) bracketDepth++;
            else if (check(TokenType::RBracket)) bracketDepth--;
            else if (parenDepth == 0 && angleDepth == 0 && bracketDepth == 0) {
                if (check(TokenType::LBrace)) {
                    advance(); // 消费 {
                    skipBalanced(TokenType::LBrace, TokenType::RBrace);
                    break;
                }
                if (check(TokenType::Semicolon)) {
                    advance(); // 消费 ;
                    break;
                }
            }
            declTokens.push_back(advance());
        }

        if (enclosingRecord && !declTokens.empty()) {
            for (std::size_t i = 0; i < declTokens.size(); ++i) {
                if (declTokens[i].type == TokenType::LParen && i > 0) {
                    MethodAST tmplMethod;
                    tmplMethod.name = declTokens[i - 1].text;
                    tmplMethod.isTemplate = true;
                    enclosingRecord->methods.push_back(std::move(tmplMethod));
                    break;
                }
            }
        }
    }
}

void Parser::parseMemberDeclaration(RecordAST& record, AccessSpecifier currentAccess, CommonAttributes attrs) {
    std::vector<Token> declTokens;
    int parenDepth = 0, bracketDepth = 0, angleDepth = 0, braceDepth = 0;
    bool isMethod = false;
    bool hasSeenEqual = false;

    SourceLocation loc = peek().loc;

    while (!isAtEnd()) {
        if (check(TokenType::Equal)) {
            if (parenDepth == 0 && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                hasSeenEqual = true;
            }
        } else if (check(TokenType::LParen)) {
            parenDepth++;
            if (!hasSeenEqual && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                isMethod = true;
            }
        } else if (check(TokenType::RParen)) {
            parenDepth--;
        } else if (check(TokenType::Less)) {
            angleDepth++;
        } else if (check(TokenType::Greater)) {
            angleDepth--;
        } else if (check(TokenType::LBracket)) {
            bracketDepth++;
        } else if (check(TokenType::RBracket)) {
            bracketDepth--;
        } else if (check(TokenType::LBrace)) {
            if (isMethod && parenDepth == 0 && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                advance(); // 消费 {
                skipBalanced(TokenType::LBrace, TokenType::RBrace);
                break;
            } else {
                braceDepth++;
                declTokens.push_back(advance());
                continue;
            }
        } else if (check(TokenType::RBrace)) {
            if (braceDepth > 0) {
                braceDepth--;
                declTokens.push_back(advance());
                continue;
            } else {
                // 属于包含此成员声明的类定义闭合括号，终止此声明解析
                break;
            }
        }

        // 遇到方法内联定义体或分号结束
        if (parenDepth == 0 && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
            if (check(TokenType::Semicolon)) {
                advance(); // 消费 ;
                break;
            }
            if (check(TokenType::KwPublic) || check(TokenType::KwProtected) || check(TokenType::KwPrivate)) {
                break;
            }
        }

        declTokens.push_back(advance());
    }
    if (declTokens.empty()) return;

    if (isMethod) {
        MethodAST method;
        method.attrs = attrs;
        method.access = currentAccess;
        method.loc = loc;

        // 查找外层参数括号 ( ... )
        std::size_t openParen = std::string::npos;
        std::size_t closeParen = std::string::npos;
        int pDepth = 0;
        int aDepth = 0;
        for (std::size_t i = 0; i < declTokens.size(); ++i) {
            if (declTokens[i].type == TokenType::Less) aDepth++;
            else if (declTokens[i].type == TokenType::Greater) aDepth--;
            else if (declTokens[i].type == TokenType::LParen) {
                if (pDepth == 0 && aDepth == 0 && openParen == std::string::npos) openParen = i;
                pDepth++;
            } else if (declTokens[i].type == TokenType::RParen) {
                pDepth--;
                if (pDepth == 0 && aDepth == 0 && closeParen == std::string::npos) {
                    closeParen = i;
                    break;
                }
            }
        }

        if (openParen != std::string::npos && closeParen != std::string::npos) {
            // 提取方法名称
            std::size_t nameIdx = openParen;
            if (nameIdx > 0) {
                method.name = declTokens[nameIdx - 1].text;
                // 检查前置运算符关键字 operator
                if (nameIdx >= 2 && declTokens[nameIdx - 2].type == TokenType::KwOperator) {
                    method.isOperator = true;
                    method.name = "operator" + method.name;
                }
            }

            // 构造返回类型
            std::string retType;
            std::size_t retEnd = (nameIdx > 0) ? (nameIdx - 1) : 0;
            if (method.isOperator && nameIdx >= 2) {
                retEnd = nameIdx - 2;
            }
            for (std::size_t i = 0; i < retEnd; ++i) {
                const auto& t = declTokens[i];
                if (t.type == TokenType::KwVirtual) { method.isVirtual = true; continue; }
                if (t.type == TokenType::KwStatic) { method.isStatic = true; continue; }
                if (t.type == TokenType::KwInline) continue;
                if (t.type == TokenType::KwExplicit) continue;

                if (!retType.empty() && t.type != TokenType::Star && t.type != TokenType::Ampersand &&
                    t.type != TokenType::DoubleAmp && t.type != TokenType::DoubleColon &&
                    declTokens[i - 1].type != TokenType::DoubleColon) {
                    retType += " ";
                }
                retType += t.text;
            }
            method.returnType = retType;

            // 解析修饰符（closeParen 之后）
            for (std::size_t i = closeParen + 1; i < declTokens.size(); ++i) {
                const auto& t = declTokens[i];
                if (t.type == TokenType::KwConst) method.isConst = true;
                else if (t.type == TokenType::KwNoexcept) method.isNoexcept = true;
                else if (t.type == TokenType::KwOverride) method.isOverride = true;
                else if (t.type == TokenType::KwDelete) method.isDeleted = true;
                else if (t.type == TokenType::KwDefault) method.isDefaulted = true;
            }

            // 判断构造函数与析构函数
            if (method.name == record.name) {
                method.isConstructor = true;
            } else if (!method.name.empty() && method.name[0] == '~') {
                method.isDestructor = true;
            }

            // 拆分与提取参数列表
            std::vector<Token> currentParam;
            int aDepth2 = 0, pDepth2 = 0, bDepth = 0, braceDepth2 = 0;
            for (std::size_t i = openParen + 1; i < closeParen; ++i) {
                const auto& tok = declTokens[i];
                if (tok.type == TokenType::LParen) pDepth2++;
                else if (tok.type == TokenType::RParen) pDepth2--;
                else if (tok.type == TokenType::Less) aDepth2++;
                else if (tok.type == TokenType::Greater) aDepth2--;
                else if (tok.type == TokenType::LBracket) bDepth++;
                else if (tok.type == TokenType::RBracket) bDepth--;
                else if (tok.type == TokenType::LBrace) braceDepth2++;
                else if (tok.type == TokenType::RBrace) braceDepth2--;
                if (tok.type == TokenType::Comma && pDepth2 == 0 && aDepth2 == 0 && bDepth == 0 && braceDepth2 == 0) {
                    if (!currentParam.empty()) {
                        ParameterAST p = parseParameter(currentParam);
                        if (!p.type.empty() && p.type != "void") {
                            method.parameters.push_back(std::move(p));
                        }
                        currentParam.clear();
                    }
                    continue;
                }
                currentParam.push_back(tok);
            }
            if (!currentParam.empty()) {
                ParameterAST p = parseParameter(currentParam);
                if (!p.type.empty() && p.type != "void") {
                    method.parameters.push_back(std::move(p));
                }
            }

            if (!method.returnType.empty() || method.isConstructor || method.isDestructor || method.isOperator) {
                record.methods.push_back(std::move(method));
            }
        }
    } else {
        // 字段声明
        FieldAST field;
        field.attrs = attrs;
        field.access = currentAccess;
        field.loc = loc;

        // 查找等号、大括号或冒号分离默认值
        std::size_t eqIdx = std::string::npos;
        std::size_t braceInitIdx = std::string::npos;
        int colonIdx = -1;
        for (std::size_t i = 0; i < declTokens.size(); ++i) {
            if (declTokens[i].type == TokenType::Equal && eqIdx == std::string::npos) {
                eqIdx = i;
            }
            if (declTokens[i].type == TokenType::LBrace && braceInitIdx == std::string::npos) {
                braceInitIdx = i;
            }
            if (declTokens[i].type == TokenType::Colon && colonIdx == -1) {
                colonIdx = static_cast<int>(i);
            }
        }

        std::size_t initIdx = std::string::npos;
        if (eqIdx != std::string::npos) initIdx = eqIdx;
        if (braceInitIdx != std::string::npos && (initIdx == std::string::npos || braceInitIdx < initIdx)) initIdx = braceInitIdx;
        if (colonIdx != -1 && (initIdx == std::string::npos || static_cast<std::size_t>(colonIdx) < initIdx)) initIdx = static_cast<std::size_t>(colonIdx);

        std::size_t declEnd = (initIdx != std::string::npos) ? initIdx : declTokens.size();

        if (colonIdx != -1 && initIdx == static_cast<std::size_t>(colonIdx)) {
            field.isBitfield = true;
        }

        if (declEnd > 0) {
            // 最后一个标识符为字段名
            std::size_t namePos = declEnd - 1;
            while (namePos > 0 && declTokens[namePos].type != TokenType::Identifier) {
                namePos--;
            }
            if (declTokens[namePos].type == TokenType::Identifier) {
                field.name = declTokens[namePos].text;

                // 组装字段类型
                std::string typeStr;
                for (std::size_t i = 0; i < namePos; ++i) {
                    const auto& t = declTokens[i];
                    if (t.type == TokenType::KwStatic) { field.isStatic = true; continue; }
                    if (t.type == TokenType::KwConstexpr) { field.isConstexpr = true; continue; }
                    if (t.type == TokenType::Ampersand || t.type == TokenType::DoubleAmp) {
                        field.isReference = true;
                    }

                    if (!typeStr.empty() && t.type != TokenType::Star && t.type != TokenType::Ampersand &&
                        t.type != TokenType::DoubleAmp && t.type != TokenType::DoubleColon &&
                        declTokens[i - 1].type != TokenType::DoubleColon) {
                        typeStr += " ";
                    }
                    typeStr += t.text;
                }
                field.type = typeStr;

                if (!field.name.empty() && !field.type.empty()) {
                    record.fields.push_back(std::move(field));
                }
            }
        }
    }
}

void Parser::parseRecordBody(RecordAST& record) {
    AccessSpecifier currentAccess = (record.kind == RecordKind::Struct) ?
                                    AccessSpecifier::Public : AccessSpecifier::Private;

    m_recordStack.push_back(record.name);

    while (!isAtEnd() && !check(TokenType::RBrace)) {
        if (check(TokenType::KwPublic)) {
            advance();
            match(TokenType::Colon);
            currentAccess = AccessSpecifier::Public;
            continue;
        }
        if (check(TokenType::KwProtected)) {
            advance();
            match(TokenType::Colon);
            currentAccess = AccessSpecifier::Protected;
            continue;
        }
        if (check(TokenType::KwPrivate)) {
            advance();
            match(TokenType::Colon);
            currentAccess = AccessSpecifier::Private;
            continue;
        }

        CommonAttributes attrs = parseAttributes();

        if (check(TokenType::KwEnum)) {
            if (currentAccess != AccessSpecifier::Public) {
                advance(); // 消费 enum
                while (!isAtEnd() && !check(TokenType::LBrace) && !check(TokenType::Semicolon)) {
                    advance();
                }
                if (match(TokenType::LBrace)) {
                    skipBalanced(TokenType::LBrace, TokenType::RBrace);
                    while (!isAtEnd() && !check(TokenType::Semicolon)) {
                        advance();
                    }
                    match(TokenType::Semicolon);
                } else {
                    match(TokenType::Semicolon);
                }
                continue;
            }
            parseEnum(attrs);
            continue;
        }

        if (check(TokenType::KwStruct) || check(TokenType::KwClass)) {
            if (currentAccess != AccessSpecifier::Public) {
                advance(); // 消费 struct / class
                while (!isAtEnd() && !check(TokenType::LBrace) && !check(TokenType::Semicolon)) {
                    advance();
                }
                if (match(TokenType::LBrace)) {
                    skipBalanced(TokenType::LBrace, TokenType::RBrace);
                    while (!isAtEnd() && !check(TokenType::Semicolon)) {
                        advance();
                    }
                    match(TokenType::Semicolon);
                } else {
                    match(TokenType::Semicolon);
                }
                continue;
            }
            parseRecord(attrs);
            continue;
        }

        if (check(TokenType::KwUsing) || check(TokenType::KwTypedef) || check(TokenType::KwFriend)) {
            skipUntil(TokenType::Semicolon);
            match(TokenType::Semicolon);
            continue;
        }

        if (check(TokenType::KwTemplate)) {
            parseTemplateDeclaration(&record);
            continue;
        }

        parseMemberDeclaration(record, currentAccess, attrs);
    }

    m_recordStack.pop_back();

    match(TokenType::RBrace);
    match(TokenType::Semicolon);
}

void Parser::parseRecord(CommonAttributes attrs) {
    RecordAST record;
    record.attrs = attrs;
    record.loc = peek().loc;

    if (match(TokenType::KwStruct)) {
        record.kind = RecordKind::Struct;
    } else if (match(TokenType::KwClass)) {
        record.kind = RecordKind::Class;
    }

    if (check(TokenType::Identifier)) {
        record.name = advance().text;
    } else {
        record.name = "";
    }

    std::string prefix = currentNamespace();
    for (const auto& rName : m_recordStack) {
        prefix += rName + "::";
    }
    record.enclosingNamespace = prefix;
    if (!record.name.empty()) {
        record.qualifiedName = record.enclosingNamespace + record.name;
    }

    // 前向声明检查：class Foo;
    if (match(TokenType::Semicolon)) {
        record.isForwardDecl = true;
        if (!record.name.empty()) {
            m_ast.records.push_back(std::move(record));
        }
        return;
    }

    // 基类继承列表处理
    if (match(TokenType::Colon)) {
        while (!isAtEnd() && !check(TokenType::LBrace) && !check(TokenType::Semicolon)) {
            if (check(TokenType::Identifier)) {
                record.baseClasses.push_back(advance().text);
            } else {
                advance();
            }
        }
    }

    if (match(TokenType::LBrace)) {
        parseRecordBody(record);
        if (!record.name.empty()) {
            m_ast.records.push_back(std::move(record));
        }
    }
}

ParsedHeaderAST Parser::parse() {
    while (!isAtEnd()) {
        CommonAttributes attrs = parseAttributes();

        if (check(TokenType::KwNamespace)) {
            parseNamespace();
            continue;
        }

        if (check(TokenType::KwEnum)) {
            parseEnum(attrs);
            continue;
        }

        if (check(TokenType::KwStruct) || check(TokenType::KwClass)) {
            parseRecord(attrs);
            continue;
        }

        if (check(TokenType::KwTemplate)) {
            parseTemplateDeclaration();
            continue;
        }

        // 遇到顶级代码块/自由函数体/初始化列表，跳过内部花括号以防破坏命名空间栈
        if (check(TokenType::LBrace)) {
            advance();
            skipBalanced(TokenType::LBrace, TokenType::RBrace);
            continue;
        }

        if (check(TokenType::RBrace)) {
            advance();
            if (!m_namespaceStack.empty()) {
                m_namespaceStack.pop_back();
            }
            continue;
        }

        advance();
    }
    return m_ast;
}

} // namespace MQReflect
