/**
 * @file CliEditorComposite.cpp
 * @brief 渐进式披露 CLI 与 AI 自动化编辑器核心 Composite 生命周期与调度实现
 */
#include "CliEditorComposite.h"
#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/host/logic/LogicHost.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace MQEngine
{
    namespace CliEditor
    {
        namespace
        {
            std::string trimWhitespace(std::string_view s)
            {
                size_t start = 0;
                while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
                {
                    ++start;
                }
                size_t end = s.size();
                while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
                {
                    --end;
                }
                return std::string(s.substr(start, end - start));
            }
        } // namespace

        CliEditorComposite::CliEditorComposite() = default;

        Status CliEditorComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));
            m_engine = engine;
            registerAllDomainsAndCommands();
            parseCommandLineArguments();
            return OkStatus();
        }

        Status CliEditorComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            m_engine = engine;
            return OkStatus();
        }

        void CliEditorComposite::onTick(float /*deltaTime*/)
        {
            ++m_tickFrameCounter;

            if (!m_hasExecutedStartupWorkload)
            {
                m_hasExecutedStartupWorkload = true;
                executeStartupCliWorkload();
            }

            if (m_waitRenderFramesBeforeExit > 0 && m_tickFrameCounter >= m_waitRenderFramesBeforeExit)
            {
                if (auto* logicHost = Host::LogicHost::InstancePtr())
                {
                    logicHost->requestExit();
                }
            }
        }

        void CliEditorComposite::onUnload()
        {
            m_commands.clear();
            m_domains.clear();
            m_engine = nullptr;
            Asset::Composite::onUnload();
        }

        void CliEditorComposite::registerAllDomainsAndCommands()
        {
            m_domains.clear();
            m_commands.clear();

            registerDomain({"plugin", "插件包与 16+ Composite 清单、依赖拓扑及职责边界自省（防重复造轮子）", "Asset::CompositeManager"});
            registerDomain({"scene", "ECS 场景创建/加载/保存、实体增删改查、组件读写与相机位姿控制", "Domain::Scene"});
            registerDomain({"character", "3D 角色生产管线（内置免费 15 骨骼参数化人形/机甲生成 + Tripo3D/本地 AI 3D 桥接）", "CliEditor::CharacterGen"});
            registerDomain({"render", "RenderGraph 拓扑检视、.pgraph 读写编译、C++ 代码生成与 Vulkan GPU 抓帧截图", "Domain::Renderer"});
            registerDomain({"shader", "着色器节点图 .mqshader 管理、HLSL 实时热编译与 PBR 材质球参数控制", "MQEditor::ShaderEditor"});
            registerDomain({"asset", "工程资产目录扫描、.mqmeta 侧车文件检视与 Assimp 3D 模型导入分析", "MQEditor::AssetBrowser"});
            registerDomain({"terrain", "大世界高度场地形创建、笔刷雕刻（Raise/Lower/Smooth/Flatten）、Splatmap 与植被散布", "Terrain::TerrainRuntime"});
            registerDomain({"blueprint", "可视化蓝图 .bp 节点图编辑、与 TypeScript @bp-node 双向无损互转及 VM 求值", "Blueprint::BlueprintRuntime"});
            registerDomain({"ui", "RmlUi 6.0 游戏 HTML/CSS 界面（.rml/.rcss）DOM 树检视、节点注入与样式热修改", "GameHtmlUI::RmlUi"});
            registerDomain({"replay", "ECS 环形缓冲帧录制、.mqreplay 时间轴跳转回放（Scrub）与两帧实体状态 Diff", "MQEditor::ReplayTimeline"});
            registerDomain({"ntc", "BC7 块压缩与 NTC 神经纹理潜在网格编码烘焙及 PSNR 质量评估", "MQEditor::NtcBaker"});
            registerDomain({"package", "4 阶段全自动独立游戏打包流水线（依赖裁剪 -> 资产烘焙 -> .mqpak 归档 -> Dist 产物）", "MQEditor::ProjectPackaging"});
            registerDomain({"profile", "Tracy 性能剖析器遥测统计、帧耗时分解与 GpuResourceTable 显存快照", "MQEditor::TracyProfiler"});

            registerSceneAndCharacterCommands();
            registerRenderAndShaderCommands();
            registerAssetAndWorldCommands();
        }

        void CliEditorComposite::registerDomain(CliDomainSpec domainSpec)
        {
            for (auto& existing : m_domains)
            {
                if (existing.name == domainSpec.name)
                {
                    existing = std::move(domainSpec);
                    return;
                }
            }
            m_domains.push_back(std::move(domainSpec));
        }

        void CliEditorComposite::registerCommand(CliCommandSpec commandSpec)
        {
            const std::string full = commandSpec.fullName();
            for (auto& existing : m_commands)
            {
                if (existing.fullName() == full)
                {
                    existing = std::move(commandSpec);
                    return;
                }
            }
            m_commands.push_back(std::move(commandSpec));
        }

        void CliEditorComposite::parseCommandLineArguments()
        {
            auto* res = Resource::Resource::InstancePtr();
            if (!res || !res->getCommandLineParser())
            {
                return;
            }

            bool hasMaxFramesFlag = false;
            const auto& parsedArgs = res->getCommandLineParser()->getParsedArgs();
            for (const auto& arg : parsedArgs)
            {
                if (arg.matchesKey("max-frames") || arg.matchesKey("test-frames"))
                {
                    hasMaxFramesFlag = true;
                }
                else if (arg.matchesKey("help") || arg.matchesKey("domains") || arg.matchesKey("cli-help") || arg.matchesKey("domain"))
                {
                    m_cliRequestedHelp = true;
                    if (arg.hasExplicitValue && !arg.value.empty() && arg.value != "true")
                    {
                        m_cliHelpDomain = arg.value;
                    }
                }
                else if (arg.matchesKey("schema") || arg.matchesKey("cli-schema"))
                {
                    m_cliSchemaTarget = arg.value;
                }
                else if (arg.matchesKey("exec") || arg.matchesKey("cli") || arg.matchesKey("cmd"))
                {
                    if (arg.value == "help" || arg.value == "domains")
                    {
                        m_cliRequestedHelp = true;
                    }
                    else if (arg.value.rfind("help ", 0) == 0)
                    {
                        m_cliRequestedHelp = true;
                        m_cliHelpDomain = trimWhitespace(arg.value.substr(5));
                    }
                    else if (arg.value.rfind("schema ", 0) == 0)
                    {
                        m_cliSchemaTarget = trimWhitespace(arg.value.substr(7));
                    }
                    else
                    {
                        if (!m_cliPipelineExpr.empty())
                        {
                            m_cliPipelineExpr += "; ";
                        }
                        m_cliPipelineExpr += arg.value;
                    }
                }
                else if (arg.matchesKey("script") || arg.matchesKey("cli-script"))
                {
                    m_cliScriptPath = arg.value;
                }
                else if (arg.matchesKey("json") || arg.matchesKey("cli-json"))
                {
                    m_cliJsonMode = arg.asBool(true);
                }
                else if (arg.matchesKey("out") || arg.matchesKey("cli-out"))
                {
                    m_cliOutputPath = arg.value;
                }
                else if (arg.isPositional && !arg.value.empty())
                {
                    // Note: 兼容 PowerShell 未对整段 --exec="cmd1; cmd2 k=v" 整体加引号时的位置参数拼接，并清除误捕获的 startupFilePath
                    if (!m_cliPipelineExpr.empty() && m_cliPipelineExpr.back() != ';')
                    {
                        m_cliPipelineExpr += " ";
                    }
                    m_cliPipelineExpr += arg.value;
                    if (auto* cfg = res->getConfig(); cfg && cfg->getStartupFilePath() == arg.value)
                    {
                        cfg->setStartupFilePath("");
                    }
                }
            }

            // 若未指定任何子命令且未显式传入 --max-frames，则默认展示 Level 0 顶层能力域并在第 1 帧后退出
            if (!m_cliRequestedHelp && m_cliSchemaTarget.empty() &&
                m_cliPipelineExpr.empty() && m_cliScriptPath.empty() && !hasMaxFramesFlag)
            {
                m_cliRequestedHelp = true;
                m_waitRenderFramesBeforeExit = 1;
            }
            else if (!hasMaxFramesFlag)
            {
                m_waitRenderFramesBeforeExit = std::max<uint32_t>(m_waitRenderFramesBeforeExit, 1u);
            }
        }

        void CliEditorComposite::executeStartupCliWorkload()
        {
            if (m_cliRequestedHelp)
            {
                const std::string out = m_cliHelpDomain.empty()
                    ? formatLevel0Domains(m_cliJsonMode)
                    : formatLevel1DomainCommands(m_cliHelpDomain, m_cliJsonMode);
                emitOutput(out);
            }

            if (!m_cliSchemaTarget.empty())
            {
                emitOutput(formatLevel2CommandSchema(m_cliSchemaTarget, m_cliJsonMode));
            }

            std::string combinedPipeline = m_cliPipelineExpr;
            if (!m_cliScriptPath.empty())
            {
                if (auto* res = Resource::Resource::InstancePtr(); res && res->getResourceLoader())
                {
                    auto scriptOr = res->getResourceLoader()->loadFile(m_cliScriptPath);
                    if (scriptOr.ok())
                    {
                        std::stringstream lines(*scriptOr);
                        std::string line;
                        while (std::getline(lines, line))
                        {
                            const std::string t = trimWhitespace(line);
                            if (t.empty() || t[0] == '#')
                            {
                                continue;
                            }
                            if (!combinedPipeline.empty())
                            {
                                combinedPipeline += "; ";
                            }
                            combinedPipeline += t;
                        }
                    }
                }
            }

            if (!combinedPipeline.empty())
            {
                const auto results = executePipeline(combinedPipeline);
                std::ostringstream oss;
                if (m_cliJsonMode)
                {
                    oss << "{\n  \"level\": 3,\n  \"results\": [\n";
                    for (size_t i = 0; i < results.size(); ++i)
                    {
                        const auto& r = results[i];
                        oss << "    {\"ok\": " << (r.ok ? "true" : "false")
                            << ", \"command\": \"" << escapeJson(r.command)
                            << "\", \"message\": \"" << escapeJson(r.message)
                            << "\", \"data\": " << (r.jsonPayload.empty() ? "{}" : r.jsonPayload) << "}";
                        if (i + 1 < results.size())
                        {
                            oss << ",";
                        }
                        oss << "\n";
                    }
                    oss << "  ]\n}\n";
                }
                else
                {
                    for (const auto& r : results)
                    {
                        oss << "[CliEditor " << (r.ok ? "OK" : "ERR") << "] " << r.command << " -> " << r.message << "\n";
                        if (!r.jsonPayload.empty() && r.jsonPayload != "{}")
                        {
                            oss << "  " << r.jsonPayload << "\n";
                        }
                    }
                }
                emitOutput(oss.str());
            }
        }

        std::string CliEditorComposite::formatLevel0Domains(bool asJson) const
        {
            std::ostringstream oss;
            if (asJson)
            {
                oss << "{\n  \"level\": 0,\n  \"protocol\": \"MQEngine Progressive CLI\",\n  \"domains\": [\n";
                for (size_t i = 0; i < m_domains.size(); ++i)
                {
                    const auto& d = m_domains[i];
                    size_t count = 0;
                    for (const auto& c : m_commands)
                    {
                        if (c.domain == d.name)
                        {
                            ++count;
                        }
                    }
                    oss << "    {\"domain\": \"" << escapeJson(d.name)
                        << "\", \"owner\": \"" << escapeJson(d.ownerModule)
                        << "\", \"commands\": " << count
                        << ", \"summary\": \"" << escapeJson(d.summary) << "\"}";
                    if (i + 1 < m_domains.size())
                    {
                        oss << ",";
                    }
                    oss << "\n";
                }
                oss << "  ],\n  \"next\": \"CliEditor.exe --headless --domain=<domain> [--json]\"\n}\n";
                return oss.str();
            }

            oss << "====================================================================================\n";
            oss << " MQEngine CliEditor — 4-Level Progressive Disclosure CLI (Level 0: Domains)\n";
            oss << "====================================================================================\n";
            for (const auto& d : m_domains)
            {
                size_t count = 0;
                for (const auto& c : m_commands)
                {
                    if (c.domain == d.name)
                    {
                        ++count;
                    }
                }
                oss << "  [" << d.name << "] (" << count << " cmds | " << d.ownerModule << ")\n"
                    << "      " << d.summary << "\n";
            }
            oss << "------------------------------------------------------------------------------------\n";
            oss << "Progressive Disclosure Usage:\n";
            oss << "  Level 0 (All Domains)    : CliEditor.exe --headless --help\n";
            oss << "  Level 1 (Domain Actions) : CliEditor.exe --headless --domain=<domain>\n";
            oss << "  Level 2 (Action Schema)  : CliEditor.exe --headless --schema=<domain.action>\n";
            oss << "  Level 3 (Execute/Batch)  : CliEditor.exe --headless --exec=\"<domain.action> k=v; ...\" [--json]\n";
            oss << "====================================================================================\n";
            return oss.str();
        }

        std::string CliEditorComposite::formatLevel1DomainCommands(const std::string& domain, bool asJson) const
        {
            std::vector<const CliCommandSpec*> matched;
            for (const auto& c : m_commands)
            {
                if (c.domain == domain)
                {
                    matched.push_back(&c);
                }
            }

            std::ostringstream oss;
            if (asJson)
            {
                oss << "{\n  \"level\": 1,\n  \"domain\": \"" << escapeJson(domain) << "\",\n  \"commands\": [\n";
                for (size_t i = 0; i < matched.size(); ++i)
                {
                    const auto* c = matched[i];
                    oss << "    {\"command\": \"" << escapeJson(c->fullName())
                        << "\", \"owner\": \"" << escapeJson(c->ownerComponent)
                        << "\", \"params\": " << c->params.size()
                        << ", \"summary\": \"" << escapeJson(c->summary) << "\"}";
                    if (i + 1 < matched.size())
                    {
                        oss << ",";
                    }
                    oss << "\n";
                }
                oss << "  ],\n  \"next\": \"CliEditor.exe --headless --schema=" << escapeJson(domain) << ".<action> [--json]\"\n}\n";
                return oss.str();
            }

            oss << "====================================================================================\n";
            oss << " MQEngine CliEditor — Level 1: Domain [" << domain << "] Commands\n";
            oss << "====================================================================================\n";
            if (matched.empty())
            {
                oss << "  (No commands found in domain '" << domain << "'. Run --help to list domains.)\n";
            }
            for (const auto* c : matched)
            {
                oss << "  * " << c->fullName() << "  [" << c->ownerComponent << "]\n"
                    << "      " << c->summary << "\n";
            }
            oss << "------------------------------------------------------------------------------------\n";
            oss << "Next Step (Level 2 Schema): CliEditor.exe --headless --schema=" << domain << ".<action>\n";
            oss << "====================================================================================\n";
            return oss.str();
        }

        std::string CliEditorComposite::formatLevel2CommandSchema(const std::string& fullCommandName, bool asJson) const
        {
            const CliCommandSpec* target = nullptr;
            for (const auto& c : m_commands)
            {
                if (c.fullName() == fullCommandName)
                {
                    target = &c;
                    break;
                }
            }

            if (!target)
            {
                if (asJson)
                {
                    return "{\"ok\": false, \"error\": \"Command not found: " + escapeJson(fullCommandName) + "\"}\n";
                }
                return "[CliEditor] Unknown command: " + fullCommandName + "\n";
            }

            std::ostringstream oss;
            if (asJson)
            {
                oss << "{\n  \"level\": 2,\n  \"command\": \"" << escapeJson(target->fullName())
                    << "\",\n  \"owner\": \"" << escapeJson(target->ownerComponent)
                    << "\",\n  \"summary\": \"" << escapeJson(target->summary)
                    << "\",\n  \"details\": \"" << escapeJson(target->details)
                    << "\",\n  \"example\": \"" << escapeJson(target->example)
                    << "\",\n  \"params\": [\n";
                for (size_t i = 0; i < target->params.size(); ++i)
                {
                    const auto& p = target->params[i];
                    oss << "    {\"name\": \"" << escapeJson(p.name)
                        << "\", \"type\": \"" << escapeJson(p.type)
                        << "\", \"required\": " << (p.required ? "true" : "false")
                        << ", \"default\": \"" << escapeJson(p.defaultValue)
                        << "\", \"description\": \"" << escapeJson(p.description) << "\"}";
                    if (i + 1 < target->params.size())
                    {
                        oss << ",";
                    }
                    oss << "\n";
                }
                oss << "  ]\n}\n";
                return oss.str();
            }

            oss << "====================================================================================\n";
            oss << " MQEngine CliEditor — Level 2 Schema: " << target->fullName() << "\n";
            oss << "====================================================================================\n";
            oss << "  Owner Component : " << target->ownerComponent << "\n";
            oss << "  Summary         : " << target->summary << "\n";
            if (!target->details.empty())
            {
                oss << "  Details         : " << target->details << "\n";
            }
            oss << "  Parameters      :\n";
            if (target->params.empty())
            {
                oss << "    (None)\n";
            }
            for (const auto& p : target->params)
            {
                oss << "    - " << p.name << " (" << p.type << ", "
                    << (p.required ? "REQUIRED" : ("default=" + (p.defaultValue.empty() ? "\"\"" : p.defaultValue)))
                    << "): " << p.description << "\n";
            }
            if (!target->example.empty())
            {
                oss << "  Example (Level 3):\n    " << target->example << "\n";
            }
            oss << "====================================================================================\n";
            return oss.str();
        }

        std::vector<std::string> CliEditorComposite::tokenizeCommandExpr(const std::string& expr)
        {
            std::vector<std::string> tokens;
            std::string current;
            bool inQuotes = false;
            char quoteChar = '\0';

            for (char c : expr)
            {
                if (inQuotes)
                {
                    if (c == quoteChar)
                    {
                        inQuotes = false;
                    }
                    else
                    {
                        current += c;
                    }
                }
                else
                {
                    if (c == '\'' || c == '"')
                    {
                        inQuotes = true;
                        quoteChar = c;
                    }
                    else if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                    {
                        if (!current.empty())
                        {
                            tokens.push_back(std::move(current));
                            current.clear();
                        }
                    }
                    else
                    {
                        current += c;
                    }
                }
            }
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
            }
            return tokens;
        }

        std::unordered_map<std::string, std::string> CliEditorComposite::parseKeyValueArgs(
            const std::vector<std::string>& tokens,
            size_t startIndex,
            const CliCommandSpec& spec)
        {
            std::unordered_map<std::string, std::string> args;
            for (const auto& p : spec.params)
            {
                args[p.name] = p.defaultValue;
            }

            size_t posIdx = 0;
            for (size_t i = startIndex; i < tokens.size(); ++i)
            {
                std::string tok = tokens[i];
                if (tok.rfind("--", 0) == 0)
                {
                    tok = tok.substr(2);
                }
                const auto eq = tok.find('=');
                if (eq != std::string::npos)
                {
                    args[tok.substr(0, eq)] = tok.substr(eq + 1);
                }
                else if (posIdx < spec.params.size())
                {
                    args[spec.params[posIdx].name] = tok;
                    ++posIdx;
                }
            }
            return args;
        }

        CliExecutionResult CliEditorComposite::executeCommandLine(const std::string& commandLine)
        {
            const std::string trimmed = trimWhitespace(commandLine);
            if (trimmed.empty())
            {
                return CliExecutionResult{true, "", "Empty command", "{}"};
            }
            const auto tokens = tokenizeCommandExpr(trimmed);
            if (tokens.empty())
            {
                return CliExecutionResult{true, "", "Empty command", "{}"};
            }

            std::string cmdName = tokens[0];
            size_t argStart = 1;
            if (cmdName.find('.') == std::string::npos && tokens.size() >= 2 && tokens[1].find('=') == std::string::npos)
            {
                cmdName = tokens[0] + "." + tokens[1];
                argStart = 2;
            }

            for (const auto& spec : m_commands)
            {
                if (spec.fullName() == cmdName)
                {
                    const auto kvArgs = parseKeyValueArgs(tokens, argStart, spec);
                    for (const auto& p : spec.params)
                    {
                        if (p.required && (!kvArgs.contains(p.name) || kvArgs.at(p.name).empty()))
                        {
                            return CliExecutionResult{
                                false,
                                cmdName,
                                "Missing required parameter '" + p.name + "'. Run --schema=" + cmdName,
                                "{\"error\": \"missing_required_param\", \"param\": \"" + escapeJson(p.name) + "\"}"
                            };
                        }
                    }
                    CliExecutionResult res = spec.handler(kvArgs);
                    res.command = cmdName;
                    return res;
                }
            }

            return CliExecutionResult{
                false,
                cmdName,
                "Unknown command '" + cmdName + "'. Run --help to list domains.",
                "{\"error\": \"unknown_command\"}"
            };
        }

        std::vector<CliExecutionResult> CliEditorComposite::executePipeline(const std::string& pipelineExpr)
        {
            std::vector<CliExecutionResult> results;
            std::stringstream ss(pipelineExpr);
            std::string seg;
            while (std::getline(ss, seg, ';'))
            {
                const std::string cleaned = trimWhitespace(seg);
                if (cleaned.empty() || cleaned[0] == '#')
                {
                    continue;
                }
                results.push_back(executeCommandLine(cleaned));
            }
            return results;
        }

        void CliEditorComposite::emitOutput(const std::string& text) const
        {
            std::fwrite(text.data(), 1, text.size(), stdout);
            std::fflush(stdout);

            if (!m_cliOutputPath.empty())
            {
                if (auto* res = Resource::Resource::InstancePtr(); res && res->getResourceLoader())
                {
                    (void)res->getResourceLoader()->saveFile(m_cliOutputPath, text);
                }
            }
        }

    } // namespace CliEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::CliEditor::CliEditorComposite)
