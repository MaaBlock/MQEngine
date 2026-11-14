/**
 * @file HtmlUiParserSerializer.cpp
 * @brief RML (XML) 树解析器、RCSS 复合选择器样式解析、1280x720 层级流式布局与无损增量覆盖序列化实现
 */
#include "HtmlUiEditorComposite.h"

#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string_view>

namespace MQEngine
{
    namespace GameHtmlUI
    {
        namespace
        {
            constexpr std::string_view kOverridesBeginMarker = "/* === MQ_DESIGNER_OVERRIDES_BEGIN === */";
            constexpr std::string_view kOverridesEndMarker = "/* === MQ_DESIGNER_OVERRIDES_END === */";

            std::string trimWhitespace(std::string_view sv)
            {
                const size_t first = sv.find_first_not_of(" \t\r\n");
                if (first == std::string_view::npos)
                {
                    return {};
                }
                const size_t last = sv.find_last_not_of(" \t\r\n");
                return std::string(sv.substr(first, last - first + 1));
            }

            std::string stripCssComments(std::string_view css)
            {
                std::string out;
                out.reserve(css.size());
                size_t pos = 0;
                while (pos < css.size())
                {
                    const size_t cStart = css.find("/*", pos);
                    if (cStart == std::string_view::npos)
                    {
                        out.append(css.substr(pos));
                        break;
                    }
                    out.append(css.substr(pos, cStart - pos));
                    const size_t cEnd = css.find("*/", cStart + 2);
                    if (cEnd == std::string_view::npos)
                    {
                        break;
                    }
                    pos = cEnd + 2;
                }
                return out;
            }

            float parseCssLength(std::string_view val)
            {
                const std::string s = trimWhitespace(val);
                if (s.empty())
                {
                    return 0.0f;
                }
                char* endPtr = nullptr;
                const float v = std::strtof(s.c_str(), &endPtr);
                return (endPtr != s.c_str() && std::isfinite(v)) ? v : 0.0f;
            }

            void parseCssBoxShorthand(
                std::string_view val,
                float& outTop,
                float& outRight,
                float& outBottom,
                float& outLeft)
            {
                std::vector<float> nums;
                std::istringstream iss(trimWhitespace(val));
                std::string token;
                while (iss >> token)
                {
                    nums.push_back(parseCssLength(token));
                }
                if (nums.empty())
                {
                    return;
                }
                if (nums.size() == 1)
                {
                    outTop = outRight = outBottom = outLeft = nums[0];
                }
                else if (nums.size() == 2)
                {
                    outTop = outBottom = nums[0];
                    outRight = outLeft = nums[1];
                }
                else if (nums.size() == 3)
                {
                    outTop = nums[0];
                    outRight = outLeft = nums[1];
                    outBottom = nums[2];
                }
                else
                {
                    outTop = nums[0];
                    outRight = nums[1];
                    outBottom = nums[2];
                    outLeft = nums[3];
                }
            }

            ImU32 parseCssColorValue(std::string_view rawVal)
            {
                const std::string val = trimWhitespace(rawVal);
                if (val.empty() || val == "transparent")
                {
                    return IM_COL32(0, 0, 0, 0);
                }

                const size_t openParen = val.find('(');
                const size_t closeParen = val.find(')', openParen == std::string::npos ? 0 : openParen);
                if ((val.starts_with("rgba") || val.starts_with("rgb")) &&
                    openParen != std::string::npos && closeParen != std::string::npos)
                {
                    std::vector<std::string> parts;
                    std::istringstream ss(val.substr(openParen + 1, closeParen - openParen - 1));
                    std::string item;
                    while (std::getline(ss, item, ','))
                    {
                        parts.push_back(trimWhitespace(item));
                    }
                    if (parts.size() >= 3)
                    {
                        const int r = std::clamp(static_cast<int>(std::lround(parseCssLength(parts[0]))), 0, 255);
                        const int g = std::clamp(static_cast<int>(std::lround(parseCssLength(parts[1]))), 0, 255);
                        const int b = std::clamp(static_cast<int>(std::lround(parseCssLength(parts[2]))), 0, 255);
                        int a = 255;
                        if (parts.size() >= 4)
                        {
                            const float rawA = parseCssLength(parts[3]);
                            const bool hasDecimal = (parts[3].find('.') != std::string::npos);
                            if (hasDecimal && rawA <= 1.0f)
                            {
                                a = std::clamp(static_cast<int>(std::lround(rawA * 255.0f)), 0, 255);
                            }
                            else
                            {
                                a = std::clamp(static_cast<int>(std::lround(rawA)), 0, 255);
                            }
                        }
                        return IM_COL32(r, g, b, a);
                    }
                }

                if (val[0] == '#')
                {
                    const std::string_view hex(val.data() + 1, val.size() - 1);
                    auto parseHexByte = [](std::string_view h) -> int {
                        uint32_t out = 0;
                        const auto [ptr, ec] = std::from_chars(h.data(), h.data() + h.size(), out, 16);
                        return (ec == std::errc{} && ptr == h.data() + h.size()) ? static_cast<int>(out) : 255;
                    };
                    if (hex.size() == 3)
                    {
                        const int r = parseHexByte(hex.substr(0, 1)) * 17;
                        const int g = parseHexByte(hex.substr(1, 1)) * 17;
                        const int b = parseHexByte(hex.substr(2, 1)) * 17;
                        return IM_COL32(r, g, b, 255);
                    }
                    if (hex.size() == 6 || hex.size() == 8)
                    {
                        const int r = parseHexByte(hex.substr(0, 2));
                        const int g = parseHexByte(hex.substr(2, 2));
                        const int b = parseHexByte(hex.substr(4, 2));
                        const int a = (hex.size() == 8) ? parseHexByte(hex.substr(6, 2)) : 255;
                        return IM_COL32(r, g, b, a);
                    }
                }
                return IM_COL32(255, 255, 255, 255);
            }

            bool elementHasClassToken(const std::string& classAttr, std::string_view targetClass)
            {
                if (classAttr.empty() || targetClass.empty())
                {
                    return false;
                }
                std::istringstream iss(classAttr);
                std::string tok;
                while (iss >> tok)
                {
                    if (tok == targetClass)
                    {
                        return true;
                    }
                }
                return false;
            }

            // 支持复合单节点选择器：tag、#id、.cls、div#top_bar、button.tool_btn.active（跳过 :hover/:active 伪类）
            bool singleCompoundSelectorMatches(std::string_view rawSel, const HtmlUiElement& elem)
            {
                const std::string sel = trimWhitespace(rawSel);
                if (sel.empty() || sel == "body" || sel.find(':') != std::string::npos)
                {
                    return false;
                }
                // 若为后代选择器（空格分隔），仅匹配末端复合段并校验祖先存在性简化兼容
                const size_t lastSpace = sel.find_last_of(" \t>");
                const std::string_view leaf = (lastSpace == std::string::npos)
                    ? std::string_view(sel)
                    : std::string_view(sel).substr(lastSpace + 1);
                if (leaf.empty())
                {
                    return false;
                }

                size_t i = 0;
                if (leaf[0] != '#' && leaf[0] != '.')
                {
                    const size_t nextSep = leaf.find_first_of("#.", 0);
                    const std::string_view reqTag = (nextSep == std::string_view::npos) ? leaf : leaf.substr(0, nextSep);
                    if (reqTag != "*" && elem.tag != reqTag)
                    {
                        return false;
                    }
                    i = (nextSep == std::string_view::npos) ? leaf.size() : nextSep;
                }

                while (i < leaf.size())
                {
                    const char prefix = leaf[i++];
                    const size_t nextSep = leaf.find_first_of("#.", i);
                    const std::string_view token = (nextSep == std::string_view::npos)
                        ? leaf.substr(i)
                        : leaf.substr(i, nextSep - i);
                    if (prefix == '#')
                    {
                        if (!elem.emitDomIdAttr || elem.domId != token)
                        {
                            return false;
                        }
                    }
                    else if (prefix == '.')
                    {
                        if (!elementHasClassToken(elem.classes, token))
                        {
                            return false;
                        }
                    }
                    i = (nextSep == std::string_view::npos) ? leaf.size() : nextSep;
                }
                return true;
            }

            std::vector<std::pair<std::string, std::string>> parseDeclarationBlock(std::string_view body)
            {
                std::vector<std::pair<std::string, std::string>> declarations;
                size_t start = 0;
                while (start < body.size())
                {
                    const size_t semi = body.find(';', start);
                    const std::string_view stmt = (semi == std::string_view::npos)
                        ? body.substr(start)
                        : body.substr(start, semi - start);
                    const size_t colon = stmt.find(':');
                    if (colon != std::string_view::npos)
                    {
                        std::string prop = trimWhitespace(stmt.substr(0, colon));
                        std::string val = trimWhitespace(stmt.substr(colon + 1));
                        if (!prop.empty() && !val.empty())
                        {
                            declarations.emplace_back(std::move(prop), std::move(val));
                        }
                    }
                    if (semi == std::string_view::npos)
                    {
                        break;
                    }
                    start = semi + 1;
                }
                return declarations;
            }

            // 解析标签属性表：提取已知核心属性，并将其余属性（如 data-input-action）原样保留至 extraAttrs
            void parseTagAttributes(const std::string& attrString, HtmlUiElement& elem)
            {
                std::string extras;
                size_t pos = 0;
                const size_t n = attrString.size();
                while (pos < n)
                {
                    while (pos < n && std::isspace(static_cast<unsigned char>(attrString[pos])))
                    {
                        ++pos;
                    }
                    if (pos >= n)
                    {
                        break;
                    }
                    const size_t keyStart = pos;
                    while (pos < n && attrString[pos] != '=' && !std::isspace(static_cast<unsigned char>(attrString[pos])))
                    {
                        ++pos;
                    }
                    const std::string key = attrString.substr(keyStart, pos - keyStart);
                    while (pos < n && std::isspace(static_cast<unsigned char>(attrString[pos])))
                    {
                        ++pos;
                    }
                    std::string val;
                    if (pos < n && attrString[pos] == '=')
                    {
                        ++pos;
                        while (pos < n && std::isspace(static_cast<unsigned char>(attrString[pos])))
                        {
                            ++pos;
                        }
                        if (pos < n && (attrString[pos] == '"' || attrString[pos] == '\''))
                        {
                            const char quote = attrString[pos++];
                            const size_t vStart = pos;
                            const size_t vEnd = attrString.find(quote, vStart);
                            if (vEnd != std::string::npos)
                            {
                                val = attrString.substr(vStart, vEnd - vStart);
                                pos = vEnd + 1;
                            }
                            else
                            {
                                val = attrString.substr(vStart);
                                pos = n;
                            }
                        }
                    }

                    if (key == "id")
                    {
                        elem.domId = val;
                        elem.emitDomIdAttr = !val.empty();
                    }
                    else if (key == "class")
                    {
                        elem.classes = val;
                    }
                    else if (key == "style")
                    {
                        elem.styleAttr = val;
                    }
                    else if (key == "src")
                    {
                        elem.srcAttr = val;
                    }
                    else if (!key.empty())
                    {
                        extras += " " + key + "=\"" + val + "\"";
                    }
                }
                elem.extraAttrs = std::move(extras);
            }

            float estimateTextPixelWidth(const std::string& utf8Text, float fontSize)
            {
                if (utf8Text.empty())
                {
                    return 0.0f;
                }
                if (ImGui::GetCurrentContext() != nullptr && ImGui::GetFont() != nullptr)
                {
                    const ImVec2 sz = ImGui::CalcTextSize(utf8Text.c_str());
                    if (sz.x > 0.0f)
                    {
                        const float baseSize = std::max(10.0f, ImGui::GetFontSize());
                        return sz.x * (fontSize / baseSize);
                    }
                }
                // 无 ImGui 上下文时的 UTF-8 宽窄字符宽度估算回退
                float width = 0.0f;
                size_t i = 0;
                while (i < utf8Text.size())
                {
                    const unsigned char c = static_cast<unsigned char>(utf8Text[i]);
                    if (c < 0x80u)
                    {
                        width += fontSize * 0.56f;
                        ++i;
                    }
                    else
                    {
                        width += fontSize * 1.02f;
                        if ((c & 0xE0u) == 0xC0u) i += 2;
                        else if ((c & 0xF0u) == 0xE0u) i += 3;
                        else i += 4;
                    }
                }
                return width;
            }
        } // namespace

        void HtmlUiEditorComposite::regenerateRmlAndRcssFromElements()
        {
            std::ostringstream rml;
            rml << "<rml>\n";
            rml << "  <head>\n";
            rml << "    <title>" << (m_documentTitle.empty() ? "Game HUD" : m_documentTitle) << "</title>\n";

            std::string rcssName = "CityHUD.rcss";
            const size_t slash = m_activeFilePath.find_last_of("/\\");
            const std::string base = (slash != std::string::npos) ? m_activeFilePath.substr(slash + 1) : m_activeFilePath;
            const size_t dot = base.rfind('.');
            if (dot != std::string::npos)
            {
                rcssName = base.substr(0, dot) + ".rcss";
            }
            else if (!base.empty())
            {
                rcssName = base + ".rcss";
            }

            rml << "    <link type=\"text/rcss\" href=\"" << rcssName << "\"/>\n";
            rml << "  </head>\n";
            rml << "  <body>\n";

            auto writeElemRecursive = [this, &rml](auto& self, const HtmlUiElement& elem, int indentLevel) -> void {
                const std::string ind(static_cast<size_t>(indentLevel * 2), ' ');
                const bool shouldEmitId = (elem.emitDomIdAttr || elem.layoutModifiedByUser) && !elem.domId.empty();
                const std::string idAttr = shouldEmitId ? (" id=\"" + elem.domId + "\"") : "";
                const std::string classAttr = !elem.classes.empty() ? (" class=\"" + elem.classes + "\"") : "";
                const std::string srcAttr = !elem.srcAttr.empty() ? (" src=\"" + elem.srcAttr + "\"") : "";
                const std::string styleAttr = !elem.styleAttr.empty() ? (" style=\"" + elem.styleAttr + "\"") : "";

                if (elem.tag == "img")
                {
                    rml << ind << "<img" << idAttr << classAttr << srcAttr << styleAttr << elem.extraAttrs << "/>\n";
                    return;
                }
                if (elem.tag == "input")
                {
                    rml << ind << "<input" << idAttr << classAttr << styleAttr << elem.extraAttrs << "/>\n";
                    return;
                }

                rml << ind << "<" << elem.tag << idAttr << classAttr << styleAttr << elem.extraAttrs << ">";
                if (!elem.innerText.empty())
                {
                    rml << elem.innerText;
                }

                if (!elem.childIds.empty())
                {
                    rml << "\n";
                    for (uint32_t cid : elem.childIds)
                    {
                        if (const HtmlUiElement* c = findElementById(cid))
                        {
                            self(self, *c, indentLevel + 1);
                        }
                    }
                    rml << ind;
                }
                rml << "</" << elem.tag << ">\n";
            };

            for (const auto& elem : m_elements)
            {
                if (elem.parentId == 0)
                {
                    writeElemRecursive(writeElemRecursive, elem, 2);
                }
            }
            rml << "  </body>\n";
            rml << "</rml>\n";

            std::ostringstream rcss;
            if (!m_baseRcssSource.empty())
            {
                rcss << m_baseRcssSource;
                if (m_baseRcssSource.back() != '\n')
                {
                    rcss << "\n";
                }
            }
            else
            {
                rcss << "body {\n"
                     << "    font-family: system-ui;\n"
                     << "    font-size: 13dp;\n"
                     << "    color: #eaf2ff;\n"
                     << "    margin: 0dp;\n"
                     << "    padding: 0dp;\n"
                     << "}\n";
            }

            // 计算含定位祖先的局部坐标（CSS absolute 子元素相对于最近非 static 祖先定位）
            auto findContainingBlockOrigin = [this](const HtmlUiElement& elem) -> ImVec2 {
                uint32_t pid = elem.parentId;
                while (pid != 0)
                {
                    const HtmlUiElement* p = findElementById(pid);
                    if (!p)
                    {
                        break;
                    }
                    if (p->positionMode == "absolute" || p->positionMode == "relative" || p->parentId == 0)
                    {
                        return ImVec2(p->x + p->boxModel.borderLeft, p->y + p->boxModel.borderTop);
                    }
                    pid = p->parentId;
                }
                return ImVec2(0.0f, 0.0f);
            };

            bool hasOverrides = false;
            std::ostringstream overridesOut;
            for (const auto& elem : m_elements)
            {
                if (!elem.layoutModifiedByUser || elem.domId.empty())
                {
                    continue;
                }
                hasOverrides = true;
                const ImVec2 cbOrigin = findContainingBlockOrigin(elem);
                const int localLeft = static_cast<int>(std::lround(elem.x - cbOrigin.x));
                const int localTop = static_cast<int>(std::lround(elem.y - cbOrigin.y));

                overridesOut << "#" << elem.domId << " {\n";
                overridesOut << "    position: absolute;\n";
                overridesOut << "    left: " << localLeft << "dp;\n";
                overridesOut << "    top: " << localTop << "dp;\n";
                overridesOut << "    right: auto;\n";
                overridesOut << "    bottom: auto;\n";
                overridesOut << "    width: " << static_cast<int>(std::lround(elem.width)) << "dp;\n";
                overridesOut << "    height: " << static_cast<int>(std::lround(elem.height)) << "dp;\n";
                overridesOut << "    margin: 0dp;\n";

                const int r = (elem.bgColor) & 0xFF;
                const int g = (elem.bgColor >> 8) & 0xFF;
                const int b = (elem.bgColor >> 16) & 0xFF;
                const int a = (elem.bgColor >> 24) & 0xFF;
                overridesOut << "    background-color: rgba(" << r << ", " << g << ", " << b << ", " << a << ");\n";

                const int tr = (elem.textColor) & 0xFF;
                const int tg = (elem.textColor >> 8) & 0xFF;
                const int tb = (elem.textColor >> 16) & 0xFF;
                const int ta = (elem.textColor >> 24) & 0xFF;
                overridesOut << "    color: rgba(" << tr << ", " << tg << ", " << tb << ", " << ta << ");\n";

                if (elem.borderRadius > 0.0f)
                {
                    overridesOut << "    border-radius: " << static_cast<int>(std::lround(elem.borderRadius)) << "dp;\n";
                }
                if (elem.boxModel.borderTop > 0.0f || elem.boxModel.borderRight > 0.0f ||
                    elem.boxModel.borderBottom > 0.0f || elem.boxModel.borderLeft > 0.0f)
                {
                    overridesOut << "    border-width: " << static_cast<int>(std::lround(elem.boxModel.borderTop)) << "dp;\n";
                    const int br = (elem.borderColor) & 0xFF;
                    const int bg = (elem.borderColor >> 8) & 0xFF;
                    const int bb = (elem.borderColor >> 16) & 0xFF;
                    const int ba = (elem.borderColor >> 24) & 0xFF;
                    overridesOut << "    border-color: rgba(" << br << ", " << bg << ", " << bb << ", " << ba << ");\n";
                }
                if (elem.boxModel.paddingTop > 0.0f || elem.boxModel.paddingRight > 0.0f ||
                    elem.boxModel.paddingBottom > 0.0f || elem.boxModel.paddingLeft > 0.0f)
                {
                    overridesOut << "    padding: " << static_cast<int>(std::lround(elem.boxModel.paddingTop)) << "dp "
                                 << static_cast<int>(std::lround(elem.boxModel.paddingRight)) << "dp "
                                 << static_cast<int>(std::lround(elem.boxModel.paddingBottom)) << "dp "
                                 << static_cast<int>(std::lround(elem.boxModel.paddingLeft)) << "dp;\n";
                }
                overridesOut << "}\n";
            }

            if (hasOverrides)
            {
                rcss << "\n" << kOverridesBeginMarker << "\n"
                     << overridesOut.str()
                     << kOverridesEndMarker << "\n";
            }

            m_rmlSource = rml.str();
            m_rcssSource = rcss.str();
        }

        void HtmlUiEditorComposite::parseRmlAndRcssToElements()
        {
            m_elements.clear();
            m_elementIdCounter = 100;

            if (m_rmlSource.empty())
            {
                return;
            }

            // 提取 <title>
            const size_t titleOpen = m_rmlSource.find("<title>");
            const size_t titleClose = m_rmlSource.find("</title>");
            if (titleOpen != std::string::npos && titleClose != std::string::npos && titleClose > titleOpen + 7)
            {
                m_documentTitle = trimWhitespace(
                    std::string_view(m_rmlSource).substr(titleOpen + 7, titleClose - (titleOpen + 7)));
            }

            struct TagContext
            {
                uint32_t elementId;
                std::string tagName;
            };
            std::vector<TagContext> tagStack;

            size_t i = 0;
            const size_t len = m_rmlSource.size();

            while (i < len)
            {
                const size_t tagStart = m_rmlSource.find('<', i);
                if (tagStart == std::string::npos)
                {
                    break;
                }

                if (tagStart > i && !tagStack.empty())
                {
                    const std::string trimmed = trimWhitespace(std::string_view(m_rmlSource).substr(i, tagStart - i));
                    if (!trimmed.empty())
                    {
                        if (HtmlUiElement* topElem = findElementById(tagStack.back().elementId))
                        {
                            if (!topElem->innerText.empty())
                            {
                                topElem->innerText += " ";
                            }
                            topElem->innerText += trimmed;
                        }
                    }
                }

                const size_t tagEnd = m_rmlSource.find('>', tagStart);
                if (tagEnd == std::string::npos)
                {
                    break;
                }

                std::string tagContent = m_rmlSource.substr(tagStart + 1, tagEnd - tagStart - 1);
                i = tagEnd + 1;

                if (tagContent.empty())
                {
                    continue;
                }

                if (tagContent[0] == '!' || tagContent[0] == '?')
                {
                    if (tagContent.rfind("!--", 0) == 0)
                    {
                        const size_t cEnd = m_rmlSource.find("-->", tagStart);
                        if (cEnd != std::string::npos)
                        {
                            i = cEnd + 3;
                        }
                    }
                    continue;
                }

                if (tagContent[0] == '/')
                {
                    std::string closeTagName = trimWhitespace(std::string_view(tagContent).substr(1));
                    const size_t sp = closeTagName.find_first_of(" \t\r\n");
                    if (sp != std::string::npos)
                    {
                        closeTagName = closeTagName.substr(0, sp);
                    }

                    while (!tagStack.empty())
                    {
                        const std::string topTag = tagStack.back().tagName;
                        tagStack.pop_back();
                        if (topTag == closeTagName)
                        {
                            break;
                        }
                    }
                    continue;
                }

                bool selfClosing = false;
                if (!tagContent.empty() && tagContent.back() == '/')
                {
                    selfClosing = true;
                    tagContent.pop_back();
                }

                const size_t firstSpace = tagContent.find_first_of(" \t\r\n");
                const std::string tagName = (firstSpace == std::string::npos)
                    ? trimWhitespace(tagContent)
                    : trimWhitespace(std::string_view(tagContent).substr(0, firstSpace));
                const std::string attrString = (firstSpace == std::string::npos)
                    ? std::string()
                    : tagContent.substr(firstSpace + 1);

                if (tagName.empty() || tagName == "rml" || tagName == "head" || tagName == "title" ||
                    tagName == "link" || tagName == "meta" || tagName == "body")
                {
                    continue;
                }

                HtmlUiElement elem;
                elem.id = ++m_elementIdCounter;
                elem.tag = tagName;
                elem.depth = static_cast<int>(tagStack.size());

                if (!tagStack.empty())
                {
                    elem.parentId = tagStack.back().elementId;
                    if (HtmlUiElement* parent = findElementById(elem.parentId))
                    {
                        parent->childIds.push_back(elem.id);
                    }
                }

                parseTagAttributes(attrString, elem);
                if (elem.domId.empty())
                {
                    elem.domId = elem.tag + "_" + std::to_string(elem.id);
                    elem.emitDomIdAttr = false;
                }

                // 按 HTML/RML 标签语义赋予基础默认显示模式与尺寸
                if (tagName == "span" || tagName == "label")
                {
                    elem.flexGrid.displayMode = "inline-block";
                    elem.width = 90.0f;
                    elem.height = 20.0f;
                    elem.bgColor = IM_COL32(0, 0, 0, 0);
                }
                else if (tagName == "button")
                {
                    elem.flexGrid.displayMode = "inline-block";
                    elem.textAlign = "center";
                    elem.width = 88.0f;
                    elem.height = 28.0f;
                    elem.bgColor = IM_COL32(36, 52, 80, 220);
                    elem.borderRadius = 4.0f;
                    elem.boxModel.borderTop = elem.boxModel.borderRight = elem.boxModel.borderBottom = elem.boxModel.borderLeft = 1.0f;
                }
                else if (tagName == "progress")
                {
                    elem.width = 220.0f;
                    elem.height = 20.0f;
                    elem.bgColor = IM_COL32(46, 204, 113, 210);
                    elem.borderRadius = 4.0f;
                }
                else if (tagName == "h1")
                {
                    elem.width = 280.0f;
                    elem.height = 30.0f;
                    elem.fontSize = 18.0f;
                    elem.textColor = IM_COL32(127, 214, 255, 255);
                }
                else if (tagName == "img")
                {
                    elem.flexGrid.displayMode = "inline-block";
                    elem.width = 64.0f;
                    elem.height = 64.0f;
                    elem.bgColor = IM_COL32(40, 50, 65, 220);
                    elem.borderRadius = 4.0f;
                }
                else if (tagName == "select" || tagName == "input")
                {
                    elem.flexGrid.displayMode = "inline-block";
                    elem.width = 160.0f;
                    elem.height = 28.0f;
                    elem.bgColor = IM_COL32(30, 40, 55, 230);
                    elem.borderRadius = 4.0f;
                }
                else
                {
                    elem.flexGrid.displayMode = "block";
                    elem.width = 200.0f;
                    elem.height = 22.0f;
                    elem.bgColor = IM_COL32(0, 0, 0, 0);
                }

                m_elements.push_back(std::move(elem));

                if (!selfClosing && tagName != "img" && tagName != "input")
                {
                    tagStack.push_back(TagContext{m_elements.back().id, tagName});
                }
            }

            parseRcssAndApplyStyles();
            layoutElementsHierarchically();
        }

        void HtmlUiEditorComposite::parseRcssAndApplyStyles()
        {
            auto applyCssChunk = [this](std::string_view rawChunk, bool markAsUserOverride) {
                const std::string cleanCss = stripCssComments(rawChunk);
                size_t pos = 0;
                while (pos < cleanCss.size())
                {
                    const size_t openBrace = cleanCss.find('{', pos);
                    if (openBrace == std::string::npos)
                    {
                        break;
                    }
                    const size_t closeBrace = cleanCss.find('}', openBrace);
                    if (closeBrace == std::string::npos)
                    {
                        break;
                    }

                    const std::string selectorGroup = trimWhitespace(
                        std::string_view(cleanCss).substr(pos, openBrace - pos));
                    const std::string_view body = std::string_view(cleanCss).substr(
                        openBrace + 1, closeBrace - openBrace - 1);
                    pos = closeBrace + 1;

                    if (selectorGroup.empty())
                    {
                        continue;
                    }

                    const auto declarations = parseDeclarationBlock(body);
                    if (declarations.empty())
                    {
                        continue;
                    }

                    // 处理 body 全局默认字体与颜色继承
                    if (selectorGroup == "body")
                    {
                        for (const auto& [prop, val] : declarations)
                        {
                            if (prop == "color")
                            {
                                const ImU32 bodyCol = parseCssColorValue(val);
                                for (auto& elem : m_elements)
                                {
                                    elem.textColor = bodyCol;
                                }
                            }
                            else if (prop == "font-size")
                            {
                                const float fs = parseCssLength(val);
                                if (fs > 4.0f)
                                {
                                    for (auto& elem : m_elements)
                                    {
                                        elem.fontSize = fs;
                                    }
                                }
                            }
                        }
                        continue;
                    }

                    std::istringstream selStream(selectorGroup);
                    std::string singleSel;
                    while (std::getline(selStream, singleSel, ','))
                    {
                        for (auto& elem : m_elements)
                        {
                            if (singleCompoundSelectorMatches(singleSel, elem))
                            {
                                applyDeclarationsToElement(elem, declarations);
                                if (markAsUserOverride)
                                {
                                    elem.layoutModifiedByUser = true;
                                }
                            }
                        }
                    }
                }
            };

            if (!m_rcssSource.empty())
            {
                const size_t markerPos = m_rcssSource.find(kOverridesBeginMarker);
                if (markerPos == std::string::npos)
                {
                    applyCssChunk(m_rcssSource, false);
                }
                else
                {
                    applyCssChunk(std::string_view(m_rcssSource).substr(0, markerPos), false);
                    applyCssChunk(std::string_view(m_rcssSource).substr(markerPos + kOverridesBeginMarker.size()), true);
                }
            }

            // 行内 style="..." 优先级高于外部样式表
            for (auto& elem : m_elements)
            {
                if (!elem.styleAttr.empty())
                {
                    const auto inlineDecls = parseDeclarationBlock(elem.styleAttr);
                    applyDeclarationsToElement(elem, inlineDecls);
                }
            }
        }

        void HtmlUiEditorComposite::applyDeclarationsToElement(
            HtmlUiElement& elem,
            const std::vector<std::pair<std::string, std::string>>& declarations)
        {
            for (const auto& [prop, val] : declarations)
            {
                if (prop == "position")
                {
                    elem.positionMode = trimWhitespace(val);
                }
                else if (prop == "display")
                {
                    elem.flexGrid.displayMode = trimWhitespace(val);
                }
                else if (prop == "flex-direction")
                {
                    elem.flexGrid.flexDirection = trimWhitespace(val);
                }
                else if (prop == "justify-content")
                {
                    elem.flexGrid.justifyContent = trimWhitespace(val);
                }
                else if (prop == "align-items")
                {
                    elem.flexGrid.alignItems = trimWhitespace(val);
                }
                else if (prop == "gap")
                {
                    elem.flexGrid.gap = parseCssLength(val);
                }
                else if (prop == "text-align")
                {
                    elem.textAlign = trimWhitespace(val);
                }
                else if (prop == "font-size")
                {
                    const float fs = parseCssLength(val);
                    if (fs > 4.0f)
                    {
                        elem.fontSize = fs;
                    }
                }
                else if (prop == "left")
                {
                    if (val == "auto")
                    {
                        elem.hasExplicitLeft = false;
                    }
                    else
                    {
                        elem.cssLeft = parseCssLength(val);
                        elem.hasExplicitLeft = true;
                    }
                }
                else if (prop == "top")
                {
                    if (val == "auto")
                    {
                        elem.hasExplicitTop = false;
                    }
                    else
                    {
                        elem.cssTop = parseCssLength(val);
                        elem.hasExplicitTop = true;
                    }
                }
                else if (prop == "right")
                {
                    if (val == "auto")
                    {
                        elem.hasExplicitRight = false;
                    }
                    else
                    {
                        elem.cssRight = parseCssLength(val);
                        elem.hasExplicitRight = true;
                    }
                }
                else if (prop == "bottom")
                {
                    if (val == "auto")
                    {
                        elem.hasExplicitBottom = false;
                    }
                    else
                    {
                        elem.cssBottom = parseCssLength(val);
                        elem.hasExplicitBottom = true;
                    }
                }
                else if (prop == "width")
                {
                    if (val != "auto")
                    {
                        elem.width = std::max(4.0f, parseCssLength(val));
                        elem.hasExplicitWidth = true;
                    }
                }
                else if (prop == "height")
                {
                    if (val != "auto")
                    {
                        elem.height = std::max(4.0f, parseCssLength(val));
                        elem.hasExplicitHeight = true;
                    }
                }
                else if (prop == "background-color")
                {
                    elem.bgColor = parseCssColorValue(val);
                }
                else if (prop == "color")
                {
                    elem.textColor = parseCssColorValue(val);
                }
                else if (prop == "border-color")
                {
                    elem.borderColor = parseCssColorValue(val);
                }
                else if (prop == "border-radius")
                {
                    elem.borderRadius = parseCssLength(val);
                }
                else if (prop == "border-width")
                {
                    parseCssBoxShorthand(
                        val,
                        elem.boxModel.borderTop,
                        elem.boxModel.borderRight,
                        elem.boxModel.borderBottom,
                        elem.boxModel.borderLeft);
                }
                else if (prop == "padding")
                {
                    parseCssBoxShorthand(
                        val,
                        elem.boxModel.paddingTop,
                        elem.boxModel.paddingRight,
                        elem.boxModel.paddingBottom,
                        elem.boxModel.paddingLeft);
                }
                else if (prop == "padding-top") elem.boxModel.paddingTop = parseCssLength(val);
                else if (prop == "padding-right") elem.boxModel.paddingRight = parseCssLength(val);
                else if (prop == "padding-bottom") elem.boxModel.paddingBottom = parseCssLength(val);
                else if (prop == "padding-left") elem.boxModel.paddingLeft = parseCssLength(val);
                else if (prop == "margin")
                {
                    parseCssBoxShorthand(
                        val,
                        elem.boxModel.marginTop,
                        elem.boxModel.marginRight,
                        elem.boxModel.marginBottom,
                        elem.boxModel.marginLeft);
                }
                else if (prop == "margin-top") elem.boxModel.marginTop = parseCssLength(val);
                else if (prop == "margin-right") elem.boxModel.marginRight = parseCssLength(val);
                else if (prop == "margin-bottom") elem.boxModel.marginBottom = parseCssLength(val);
                else if (prop == "margin-left") elem.boxModel.marginLeft = parseCssLength(val);
            }
        }

        void HtmlUiEditorComposite::layoutContainerChildren(HtmlUiElement& parent)
        {
            if (parent.childIds.empty())
            {
                return;
            }

            const float contentLeft = parent.x + parent.boxModel.borderLeft + parent.boxModel.paddingLeft;
            const float contentTop = parent.y + parent.boxModel.borderTop + parent.boxModel.paddingTop;
            const float contentWidth = std::max(
                24.0f,
                parent.width - parent.boxModel.borderLeft - parent.boxModel.borderRight -
                    parent.boxModel.paddingLeft - parent.boxModel.paddingRight);
            const float contentHeight = std::max(
                16.0f,
                parent.height - parent.boxModel.borderTop - parent.boxModel.borderBottom -
                    parent.boxModel.paddingTop - parent.boxModel.paddingBottom);

            float cursorX = contentLeft;
            float cursorY = contentTop;
            float currentRowMaxH = 0.0f;
            std::vector<uint32_t> currentInlineRow;

            auto flushInlineRowAlignment = [&](float rowEndX) {
                if (currentInlineRow.empty())
                {
                    return;
                }
                const float usedW = rowEndX - contentLeft;
                float shiftX = 0.0f;
                if (parent.textAlign == "center" || parent.flexGrid.justifyContent == "center")
                {
                    shiftX = std::max(0.0f, (contentWidth - usedW) * 0.5f);
                }
                else if (parent.textAlign == "right")
                {
                    shiftX = std::max(0.0f, contentWidth - usedW);
                }

                for (uint32_t rowCid : currentInlineRow)
                {
                    if (HtmlUiElement* rc = findElementById(rowCid))
                    {
                        if (shiftX > 0.0f)
                        {
                            rc->x += shiftX;
                        }
                        // 同行 inline-block 垂直居中对齐
                        if (currentRowMaxH > rc->height)
                        {
                            rc->y += (currentRowMaxH - rc->height) * 0.5f;
                        }
                    }
                }
                cursorY += currentRowMaxH + 4.0f;
                cursorX = contentLeft;
                currentRowMaxH = 0.0f;
                currentInlineRow.clear();
            };

            const bool parentIsFlexRow = (parent.flexGrid.displayMode == "flex" && parent.flexGrid.flexDirection == "row");

            for (uint32_t cid : parent.childIds)
            {
                HtmlUiElement* child = findElementById(cid);
                if (!child)
                {
                    continue;
                }

                // 嵌套绝对定位子元素：相对当前容器边框内侧锚定
                if (child->positionMode == "absolute")
                {
                    const float cbX = parent.x + parent.boxModel.borderLeft;
                    const float cbY = parent.y + parent.boxModel.borderTop;
                    const float cbW = std::max(16.0f, parent.width - parent.boxModel.borderLeft - parent.boxModel.borderRight);
                    const float cbH = std::max(16.0f, parent.height - parent.boxModel.borderTop - parent.boxModel.borderBottom);

                    if (child->hasExplicitLeft && child->hasExplicitRight && !child->hasExplicitWidth)
                    {
                        child->x = cbX + child->cssLeft;
                        child->width = std::max(16.0f, cbW - child->cssLeft - child->cssRight);
                    }
                    else if (child->hasExplicitRight && !child->hasExplicitLeft)
                    {
                        child->x = cbX + cbW - child->cssRight - child->width;
                    }
                    else
                    {
                        child->x = cbX + child->cssLeft;
                    }

                    if (child->hasExplicitTop && child->hasExplicitBottom && !child->hasExplicitHeight)
                    {
                        child->y = cbY + child->cssTop;
                        child->height = std::max(14.0f, cbH - child->cssTop - child->cssBottom);
                    }
                    else if (child->hasExplicitBottom && !child->hasExplicitTop)
                    {
                        child->y = cbY + cbH - child->cssBottom - child->height;
                    }
                    else
                    {
                        child->y = cbY + child->cssTop;
                    }

                    layoutContainerChildren(*child);
                    continue;
                }

                const bool isInline = (child->flexGrid.displayMode == "inline-block" ||
                                       child->flexGrid.displayMode == "inline" ||
                                       parentIsFlexRow);

                if (isInline)
                {
                    if (!child->hasExplicitWidth)
                    {
                        const float textW = estimateTextPixelWidth(child->innerText, child->fontSize);
                        child->width = std::max(
                            24.0f,
                            textW + child->boxModel.paddingLeft + child->boxModel.paddingRight +
                                child->boxModel.borderLeft + child->boxModel.borderRight + 6.0f);
                    }
                    if (!child->hasExplicitHeight)
                    {
                        child->height = std::max(
                            18.0f,
                            child->fontSize + child->boxModel.paddingTop + child->boxModel.paddingBottom +
                                child->boxModel.borderTop + child->boxModel.borderBottom + 4.0f);
                    }

                    const float itemTotalW = child->boxModel.marginLeft + child->width + child->boxModel.marginRight;
                    if (!currentInlineRow.empty() && (cursorX - contentLeft + itemTotalW > contentWidth + 1.0f))
                    {
                        flushInlineRowAlignment(cursorX);
                    }

                    child->x = cursorX + child->boxModel.marginLeft;
                    child->y = cursorY + child->boxModel.marginTop;
                    cursorX = child->x + child->width + child->boxModel.marginRight;
                    currentRowMaxH = std::max(
                        currentRowMaxH,
                        child->boxModel.marginTop + child->height + child->boxModel.marginBottom);
                    currentInlineRow.push_back(child->id);
                }
                else
                {
                    if (!currentInlineRow.empty())
                    {
                        flushInlineRowAlignment(cursorX);
                    }

                    child->x = contentLeft + child->boxModel.marginLeft;
                    child->y = cursorY + child->boxModel.marginTop;
                    if (!child->hasExplicitWidth)
                    {
                        child->width = std::max(
                            16.0f,
                            contentWidth - child->boxModel.marginLeft - child->boxModel.marginRight);
                    }
                    if (!child->hasExplicitHeight && child->childIds.empty())
                    {
                        child->height = std::max(
                            16.0f,
                            child->fontSize + child->boxModel.paddingTop + child->boxModel.paddingBottom + 4.0f);
                    }

                    layoutContainerChildren(*child);
                    cursorY = child->y + child->height + child->boxModel.marginBottom + 2.0f;
                }
            }

            if (!currentInlineRow.empty())
            {
                flushInlineRowAlignment(cursorX);
            }

            if (!parent.hasExplicitHeight)
            {
                const float neededH = (cursorY - contentTop) +
                    parent.boxModel.paddingTop + parent.boxModel.paddingBottom +
                    parent.boxModel.borderTop + parent.boxModel.borderBottom;
                if (neededH > parent.height)
                {
                    parent.height = neededH;
                }
            }
            (void)contentHeight;
        }

        void HtmlUiEditorComposite::layoutElementsHierarchically()
        {
            float rootFlowCursorY = 12.0f;
            for (auto& elem : m_elements)
            {
                if (elem.parentId != 0)
                {
                    continue;
                }

                if (elem.positionMode == "absolute" ||
                    elem.hasExplicitLeft || elem.hasExplicitRight ||
                    elem.hasExplicitTop || elem.hasExplicitBottom)
                {
                    if (elem.hasExplicitLeft && elem.hasExplicitRight && !elem.hasExplicitWidth)
                    {
                        elem.x = elem.cssLeft;
                        elem.width = std::max(40.0f, kDesignCanvasWidth - elem.cssLeft - elem.cssRight);
                    }
                    else if (elem.hasExplicitRight && !elem.hasExplicitLeft)
                    {
                        elem.x = kDesignCanvasWidth - elem.cssRight - elem.width;
                    }
                    else if (elem.hasExplicitLeft)
                    {
                        elem.x = elem.cssLeft;
                    }

                    if (elem.hasExplicitTop && elem.hasExplicitBottom && !elem.hasExplicitHeight)
                    {
                        elem.y = elem.cssTop;
                        elem.height = std::max(20.0f, kDesignCanvasHeight - elem.cssTop - elem.cssBottom);
                    }
                    else if (elem.hasExplicitBottom && !elem.hasExplicitTop)
                    {
                        elem.y = kDesignCanvasHeight - elem.cssBottom - elem.height;
                    }
                    else if (elem.hasExplicitTop)
                    {
                        elem.y = elem.cssTop;
                    }
                }
                else
                {
                    elem.x = 12.0f + elem.boxModel.marginLeft;
                    elem.y = rootFlowCursorY + elem.boxModel.marginTop;
                    if (!elem.hasExplicitWidth)
                    {
                        elem.width = kDesignCanvasWidth - 24.0f - elem.boxModel.marginLeft - elem.boxModel.marginRight;
                    }
                }

                layoutContainerChildren(elem);

                if (elem.positionMode != "absolute")
                {
                    rootFlowCursorY = std::max(rootFlowCursorY, elem.y + elem.height + elem.boxModel.marginBottom + 6.0f);
                }
            }
        }

        void HtmlUiEditorComposite::saveUiFilesToDisk()
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return;
            }

            regenerateRmlAndRcssFromElements();

            const std::string rmlRelPath = m_activeFilePath.empty() ? "Assets/UI/CityHUD.rml" : m_activeFilePath;
            std::string rcssRelPath = rmlRelPath;
            const size_t dotPos = rcssRelPath.rfind('.');
            if (dotPos != std::string::npos)
            {
                rcssRelPath = rcssRelPath.substr(0, dotPos) + ".rcss";
            }
            else
            {
                rcssRelPath += ".rcss";
            }

            const std::string rmlDiskPath = resolveUiDiskPath(rmlRelPath);
            const std::string rcssDiskPath = resolveUiDiskPath(rcssRelPath);

            const std::string parentDir = FCT::SAI::Path::parentPath(rmlDiskPath);
            if (!parentDir.empty())
            {
                (void)loader->ensureDir(parentDir);
            }

            auto rmlRes = loader->saveFile(rmlDiskPath, m_rmlSource);
            auto rcssRes = loader->saveFile(rcssDiskPath, m_rcssSource);

            if (rmlRes.ok() && rcssRes.ok())
            {
                m_isDirty = false;
                notifyRuntimeHotReload();
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "UI 资产保存并热重载",
                        "已写入 " + rmlRelPath + " / " + rcssRelPath + " 并同步刷新运行态 GameHtmlUI",
                        EditorBase::LogEntry::Level::Info,
                        2.5f);
                }
            }
            else if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "UI 保存失败",
                    "无法写入目标路径: " + rmlDiskPath,
                    EditorBase::LogEntry::Level::Error,
                    3.5f);
            }
        }

    } // namespace GameHtmlUI
} // namespace MQEngine
