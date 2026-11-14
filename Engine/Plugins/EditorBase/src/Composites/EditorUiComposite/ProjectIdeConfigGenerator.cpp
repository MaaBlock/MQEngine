/**
 * @file ProjectIdeConfigGenerator.cpp
 * @brief 工程 CMakePresets.json、CLion .idea/cmake.xml、.run 启动项与 .gitignore 生成实现
 */
#include "ProjectIdeConfigGenerator.h"

#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/resource.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace MQEngine
{
    namespace EditorBase
    {
        namespace
        {
            constexpr const char* kPresetDebugName = "windows-ninja-debug-x64";
            constexpr const char* kPresetReleaseName = "windows-ninja-release-x64";
            constexpr const char* kBuildPresetDebugName = "build-ninja-debug-win";
            constexpr const char* kBuildPresetReleaseName = "build-ninja-release-win";
            constexpr const char* kClionActiveConfigName = "windows-ninja-debug-x64 - build-ninja-debug-win";

            struct ParsedProjectCMakeInfo
            {
                std::string projectName;
                std::vector<std::string> targets;
                std::string editorCompositeTarget;
                std::string runtimeCompositeTarget;
                std::string nativeModuleTarget;
            };

            std::string normalizeForwardSlash(std::string_view rawPath)
            {
                if (rawPath.empty())
                {
                    return {};
                }
                std::string path = SAI::Path::normalize(rawPath);
                std::replace(path.begin(), path.end(), '\\', '/');
                while (path.size() > 1 && path.back() == '/')
                {
                    if (path.size() == 3 && path[1] == ':')
                    {
                        break;
                    }
                    path.pop_back();
                }
                return path;
            }

            std::string toLowerAscii(std::string_view text)
            {
                std::string lower(text);
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                return lower;
            }

            std::string trimAscii(std::string_view text)
            {
                size_t start = 0;
                while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
                {
                    ++start;
                }
                size_t end = text.size();
                while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
                {
                    --end;
                }
                return std::string(text.substr(start, end - start));
            }

            bool endsWithAscii(std::string_view value, std::string_view suffix)
            {
                return value.size() >= suffix.size() &&
                       value.substr(value.size() - suffix.size()) == suffix;
            }

            bool isIdentChar(char ch) noexcept
            {
                const unsigned char uch = static_cast<unsigned char>(ch);
                return std::isalnum(uch) != 0 || ch == '_' || ch == '-' || ch == '.';
            }

            ResourceLoader* resolveResourceLoader(ResourceLoader* explicitLoader)
            {
                if (explicitLoader)
                {
                    return explicitLoader;
                }
                if (auto* res = Resource::Resource::InstancePtr())
                {
                    if (auto* loader = res->getResourceLoader())
                    {
                        return loader;
                    }
                }
                return DiskResourceLoader::InstancePtr();
            }

            bool isEngineSourceRootDir(const std::string& dir)
            {
                if (dir.empty())
                {
                    return false;
                }
                return SAI::FileSystem::isFile(SAI::Path::join(dir, "CMakeLists.txt")) &&
                       SAI::FileSystem::isFile(SAI::Path::join(dir, "Config/MQPluginSDK.cmake")) &&
                       SAI::FileSystem::isFile(SAI::Path::join(dir, "Config/copy_headers.cmake")) &&
                       SAI::FileSystem::isFile(SAI::Path::join(dir, "Engine/Core/src/EnginePCH.h"));
            }

            bool isEngineBuildDir(const std::string& dir)
            {
                if (dir.empty() || !SAI::FileSystem::isFile(SAI::Path::join(dir, "CMakeCache.txt")))
                {
                    return false;
                }
                constexpr auto kEngineLibCandidates = std::to_array<std::string_view>({
                    "Engine/Core/Engine.lib",
                    "Engine/Core/Debug/Engine.lib",
                    "Engine/Core/Release/Engine.lib",
                    "Engine/Core/libEngine.so",
                    "Engine/Core/libEngine.a"
                });
                for (std::string_view rel : kEngineLibCandidates)
                {
                    if (SAI::FileSystem::isFile(SAI::Path::join(dir, rel)))
                    {
                        return true;
                    }
                }
                return false;
            }

            std::string readCMakeCacheVar(const std::string& cacheContent, std::string_view keyPrefix)
            {
                size_t lineStart = 0;
                while (lineStart < cacheContent.size())
                {
                    size_t lineEnd = cacheContent.find('\n', lineStart);
                    if (lineEnd == std::string::npos)
                    {
                        lineEnd = cacheContent.size();
                    }
                    std::string_view line(cacheContent.data() + lineStart, lineEnd - lineStart);
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.remove_suffix(1);
                    }
                    if (line.size() > keyPrefix.size() &&
                        line.compare(0, keyPrefix.size(), keyPrefix) == 0)
                    {
                        const size_t eqPos = line.find('=', keyPrefix.size());
                        if (eqPos != std::string_view::npos)
                        {
                            return trimAscii(line.substr(eqPos + 1));
                        }
                    }
                    lineStart = lineEnd + 1;
                }
                return {};
            }

            bool isReleaseBuildDir(const std::string& buildDir, ResourceLoader* loader)
            {
                if (loader)
                {
                    const std::string cachePath = SAI::Path::join(buildDir, "CMakeCache.txt");
                    if (loader->hasFile(cachePath))
                    {
                        auto cacheOr = loader->loadFile(cachePath);
                        if (cacheOr.ok())
                        {
                            const std::string buildType =
                                toLowerAscii(readCMakeCacheVar(cacheOr.value(), "CMAKE_BUILD_TYPE:"));
                            if (!buildType.empty())
                            {
                                return buildType == "release" || buildType == "relwithdebinfo" ||
                                       buildType == "minsizerel";
                            }
                        }
                    }
                }
                return toLowerAscii(buildDir).find("release") != std::string::npos;
            }

            void resolveDebugAndReleaseBuildDirs(
                const std::string& engineRootDir,
                const std::string& activeBuildDir,
                ResourceLoader* loader,
                std::string& outDebugDir,
                std::string& outReleaseDir)
            {
                outDebugDir.clear();
                outReleaseDir.clear();

                const bool activeIsRelease = !activeBuildDir.empty() && isReleaseBuildDir(activeBuildDir, loader);
                if (!activeBuildDir.empty())
                {
                    if (activeIsRelease)
                    {
                        outReleaseDir = activeBuildDir;
                    }
                    else
                    {
                        outDebugDir = activeBuildDir;
                    }
                }

                if (outDebugDir.empty() && !engineRootDir.empty())
                {
                    const std::array<std::string, 3> debugCandidates = {
                        normalizeForwardSlash(SAI::Path::join(engineRootDir, "Output/build-windows-debug")),
                        normalizeForwardSlash(SAI::Path::join(engineRootDir, "Output/build-vs2022-debug")),
                        normalizeForwardSlash(SAI::Path::join(engineRootDir, "Output/build-linux-debug"))
                    };
                    for (const std::string& cand : debugCandidates)
                    {
                        if (isEngineBuildDir(cand))
                        {
                            outDebugDir = cand;
                            break;
                        }
                    }
                    if (outDebugDir.empty())
                    {
                        outDebugDir = debugCandidates.front();
                    }
                }

                if (outReleaseDir.empty() && !engineRootDir.empty())
                {
                    const std::array<std::string, 3> releaseCandidates = {
                        normalizeForwardSlash(SAI::Path::join(engineRootDir, "Output/build-windows-release")),
                        normalizeForwardSlash(SAI::Path::join(engineRootDir, "Output/build-vs2022-release")),
                        normalizeForwardSlash(SAI::Path::join(engineRootDir, "Output/build-linux-release"))
                    };
                    for (const std::string& cand : releaseCandidates)
                    {
                        if (isEngineBuildDir(cand))
                        {
                            outReleaseDir = cand;
                            break;
                        }
                    }
                    if (outReleaseDir.empty())
                    {
                        outReleaseDir = releaseCandidates.front();
                    }
                }
            }

            void resolveRuntimeHostExecutables(
                const std::string& debugBuildDir,
                const std::string& activeBuildDir,
                std::string& outEditorExe,
                std::string& outGameExe)
            {
                const std::string exeDir = normalizeForwardSlash(SAI::System::getExecutableDirectory());
                std::vector<std::string> baseBuildDirs;
                if (!debugBuildDir.empty())
                {
                    baseBuildDirs.push_back(debugBuildDir);
                }
                if (!activeBuildDir.empty() && activeBuildDir != debugBuildDir)
                {
                    baseBuildDirs.push_back(activeBuildDir);
                }

                outEditorExe.clear();
                for (const std::string& dir : baseBuildDirs)
                {
                    const std::string cand = normalizeForwardSlash(SAI::Path::join(dir, "Applications/Editor/Editor.exe"));
                    if (SAI::FileSystem::isFile(cand))
                    {
                        outEditorExe = cand;
                        break;
                    }
                }
                if (outEditorExe.empty() && !exeDir.empty())
                {
                    const std::string selfEditor = normalizeForwardSlash(SAI::Path::join(exeDir, "Editor.exe"));
                    if (SAI::FileSystem::isFile(selfEditor))
                    {
                        outEditorExe = selfEditor;
                    }
                }
                if (outEditorExe.empty() && !baseBuildDirs.empty())
                {
                    outEditorExe = normalizeForwardSlash(
                        SAI::Path::join(baseBuildDirs.front(), "Applications/Editor/Editor.exe"));
                }

                outGameExe.clear();
                for (const std::string& dir : baseBuildDirs)
                {
                    const std::string cand = normalizeForwardSlash(SAI::Path::join(dir, "Applications/Game/Game.exe"));
                    if (SAI::FileSystem::isFile(cand))
                    {
                        outGameExe = cand;
                        break;
                    }
                }
                if (outGameExe.empty() && !exeDir.empty())
                {
                    const std::string siblingGame = normalizeForwardSlash(SAI::Path::join(exeDir, "../Game/Game.exe"));
                    if (SAI::FileSystem::isFile(siblingGame))
                    {
                        outGameExe = siblingGame;
                    }
                }
                if (outGameExe.empty() && !baseBuildDirs.empty())
                {
                    outGameExe = normalizeForwardSlash(
                        SAI::Path::join(baseBuildDirs.front(), "Applications/Game/Game.exe"));
                }
            }

            std::string stripCMakeComments(std::string_view text)
            {
                std::string stripped;
                stripped.reserve(text.size());
                size_t lineStart = 0;
                while (lineStart < text.size())
                {
                    size_t lineEnd = text.find('\n', lineStart);
                    if (lineEnd == std::string::npos)
                    {
                        lineEnd = text.size();
                    }
                    bool inQuotes = false;
                    size_t commentPos = lineEnd;
                    for (size_t i = lineStart; i < lineEnd; ++i)
                    {
                        const char ch = text[i];
                        if (ch == '"' && (i == lineStart || text[i - 1] != '\\'))
                        {
                            inQuotes = !inQuotes;
                        }
                        else if (ch == '#' && !inQuotes)
                        {
                            commentPos = i;
                            break;
                        }
                    }
                    stripped.append(text.substr(lineStart, commentPos - lineStart));
                    stripped.push_back('\n');
                    lineStart = lineEnd + 1;
                }
                return stripped;
            }

            std::string extractFirstCallArg(std::string_view cleanedLower, std::string_view rawCleaned, size_t& searchPos, std::string_view fnName)
            {
                while (searchPos < cleanedLower.size())
                {
                    const size_t hit = cleanedLower.find(fnName, searchPos);
                    if (hit == std::string::npos)
                    {
                        searchPos = cleanedLower.size();
                        return {};
                    }
                    searchPos = hit + fnName.size();
                    if (hit > 0 && isIdentChar(cleanedLower[hit - 1]))
                    {
                        continue;
                    }
                    size_t cursor = searchPos;
                    while (cursor < rawCleaned.size() && std::isspace(static_cast<unsigned char>(rawCleaned[cursor])))
                    {
                        ++cursor;
                    }
                    if (cursor >= rawCleaned.size() || rawCleaned[cursor] != '(')
                    {
                        continue;
                    }
                    ++cursor;
                    while (cursor < rawCleaned.size() && std::isspace(static_cast<unsigned char>(rawCleaned[cursor])))
                    {
                        ++cursor;
                    }
                    if (cursor >= rawCleaned.size())
                    {
                        searchPos = cleanedLower.size();
                        return {};
                    }
                    if (rawCleaned[cursor] == '"')
                    {
                        ++cursor;
                        const size_t closeQuote = rawCleaned.find('"', cursor);
                        if (closeQuote == std::string::npos)
                        {
                            searchPos = cleanedLower.size();
                            return {};
                        }
                        searchPos = closeQuote + 1;
                        return std::string(rawCleaned.substr(cursor, closeQuote - cursor));
                    }
                    size_t tokenEnd = cursor;
                    while (tokenEnd < rawCleaned.size() &&
                           !std::isspace(static_cast<unsigned char>(rawCleaned[tokenEnd])) &&
                           rawCleaned[tokenEnd] != ')')
                    {
                        ++tokenEnd;
                    }
                    searchPos = tokenEnd;
                    return std::string(rawCleaned.substr(cursor, tokenEnd - cursor));
                }
                return {};
            }

            bool isValidDeclaredTargetName(std::string_view name)
            {
                if (name.empty() || name.find('$') != std::string_view::npos || name.find(';') != std::string_view::npos)
                {
                    return false;
                }
                const std::string upper = [&]() {
                    std::string u(name);
                    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) {
                        return static_cast<char>(std::toupper(c));
                    });
                    return u;
                }();
                constexpr auto kKeywords = std::to_array<std::string_view>({
                    "STATIC", "SHARED", "MODULE", "OBJECT", "INTERFACE", "UNKNOWN",
                    "ALIAS", "IMPORTED", "GLOBAL", "WIN32", "MACOSX_BUNDLE",
                    "EXCLUDE_FROM_ALL", "NAME", "PLUGIN", "DEPENDS", "LIBS"
                });
                for (std::string_view kw : kKeywords)
                {
                    if (upper == kw)
                    {
                        return false;
                    }
                }
                return true;
            }

            void collectTargetsFromCMakeText(
                std::string_view rawCMakeText,
                std::vector<std::string>& outTargets,
                std::unordered_set<std::string>& seenTargets,
                std::vector<std::string>* outSubdirs,
                std::string* outProjectName)
            {
                const std::string cleaned = stripCMakeComments(rawCMakeText);
                const std::string cleanedLower = toLowerAscii(cleaned);

                if (outProjectName && outProjectName->empty())
                {
                    size_t pos = 0;
                    const std::string candidate = extractFirstCallArg(cleanedLower, cleaned, pos, "project");
                    if (!candidate.empty() && candidate.find('$') == std::string::npos)
                    {
                        *outProjectName = candidate;
                    }
                }

                constexpr auto kTargetFns = std::to_array<std::string_view>({
                    "add_library", "add_executable", "mq_add_composite"
                });
                for (std::string_view fn : kTargetFns)
                {
                    size_t pos = 0;
                    while (pos < cleanedLower.size())
                    {
                        const std::string target = extractFirstCallArg(cleanedLower, cleaned, pos, fn);
                        if (target.empty() && pos >= cleanedLower.size())
                        {
                            break;
                        }
                        if (isValidDeclaredTargetName(target) && seenTargets.insert(target).second)
                        {
                            outTargets.push_back(target);
                        }
                    }
                }

                if (outSubdirs)
                {
                    size_t pos = 0;
                    while (pos < cleanedLower.size())
                    {
                        const std::string sub = extractFirstCallArg(cleanedLower, cleaned, pos, "add_subdirectory");
                        if (sub.empty() && pos >= cleanedLower.size())
                        {
                            break;
                        }
                        if (!sub.empty() && sub.find('$') == std::string::npos &&
                            sub.find("..") == std::string::npos && !SAI::Path::isAbsolute(sub))
                        {
                            outSubdirs->push_back(sub);
                        }
                    }
                }
            }

            ParsedProjectCMakeInfo parseProjectCMakeInfo(const std::string& projectDir, ResourceLoader* loader)
            {
                ParsedProjectCMakeInfo info;
                std::unordered_set<std::string> seenTargets;
                std::vector<std::string> candidateCMakeFiles;
                candidateCMakeFiles.push_back(normalizeForwardSlash(SAI::Path::join(projectDir, "CMakeLists.txt")));

                std::vector<std::string> declaredSubdirs;
                if (loader)
                {
                    auto topOr = loader->loadFile(candidateCMakeFiles.front());
                    if (topOr.ok())
                    {
                        collectTargetsFromCMakeText(
                            topOr.value(),
                            info.targets,
                            seenTargets,
                            &declaredSubdirs,
                            &info.projectName);
                    }
                }

                std::unordered_set<std::string> visitedDirs;
                visitedDirs.insert(candidateCMakeFiles.front());

                auto appendSubdirCMake = [&](const std::string& subDirPath) {
                    const std::string cmakePath = normalizeForwardSlash(SAI::Path::join(subDirPath, "CMakeLists.txt"));
                    if (visitedDirs.insert(cmakePath).second && loader && loader->hasFile(cmakePath))
                    {
                        auto subOr = loader->loadFile(cmakePath);
                        if (subOr.ok())
                        {
                            collectTargetsFromCMakeText(subOr.value(), info.targets, seenTargets, nullptr, nullptr);
                        }
                    }
                };

                for (const std::string& relSub : declaredSubdirs)
                {
                    appendSubdirCMake(SAI::Path::join(projectDir, relSub));
                }

                const std::string pluginsDir = SAI::Path::join(projectDir, "Plugins");
                if (SAI::FileSystem::isDirectory(pluginsDir))
                {
                    for (const std::string& pluginDir : SAI::FileSystem::listSubDirectories(pluginsDir))
                    {
                        appendSubdirCMake(pluginDir);
                    }
                }

                if (info.projectName.empty())
                {
                    info.projectName = SAI::Path::fileName(projectDir);
                }

                for (const std::string& target : info.targets)
                {
                    if (info.editorCompositeTarget.empty() && endsWithAscii(target, "EditorComposite"))
                    {
                        info.editorCompositeTarget = target;
                    }
                    else if (info.runtimeCompositeTarget.empty() && endsWithAscii(target, "RuntimeComposite"))
                    {
                        info.runtimeCompositeTarget = target;
                    }
                    else if (info.nativeModuleTarget.empty() &&
                             (endsWithAscii(target, "Native") || endsWithAscii(target, "NativeModule")))
                    {
                        info.nativeModuleTarget = target;
                    }
                }
                return info;
            }

            void appendIndent(std::string& out, int indent)
            {
                out.append(static_cast<size_t>(indent) * 2, ' ');
            }

            void prettyPrintJsonValue(const boost::json::value& val, std::string& out, int indent)
            {
                if (val.is_object())
                {
                    const auto& obj = val.as_object();
                    if (obj.empty())
                    {
                        out += "{}";
                        return;
                    }
                    out += "{\n";
                    size_t idx = 0;
                    for (const auto& kv : obj)
                    {
                        appendIndent(out, indent + 1);
                        out += boost::json::serialize(boost::json::value(std::string(kv.key())));
                        out += ": ";
                        prettyPrintJsonValue(kv.value(), out, indent + 1);
                        if (++idx < obj.size())
                        {
                            out += ",";
                        }
                        out += "\n";
                    }
                    appendIndent(out, indent);
                    out += "}";
                    return;
                }

                if (val.is_array())
                {
                    const auto& arr = val.as_array();
                    if (arr.empty())
                    {
                        out += "[]";
                        return;
                    }
                    out += "[\n";
                    for (size_t i = 0; i < arr.size(); ++i)
                    {
                        appendIndent(out, indent + 1);
                        prettyPrintJsonValue(arr[i], out, indent + 1);
                        if (i + 1 < arr.size())
                        {
                            out += ",";
                        }
                        out += "\n";
                    }
                    appendIndent(out, indent);
                    out += "]";
                    return;
                }

                out += boost::json::serialize(val);
            }

            boost::json::object buildConfigurePreset(
                std::string_view presetName,
                std::string_view displayName,
                std::string_view description,
                std::string_view buildType,
                std::string_view engineRoot,
                std::string_view engineBuildDir)
            {
                boost::json::object condition;
                condition["type"] = "equals";
                condition["lhs"] = "${hostSystemName}";
                condition["rhs"] = "Windows";

                boost::json::object arch;
                arch["value"] = "x64";
                arch["strategy"] = "external";

                boost::json::object toolset;
                toolset["value"] = "host=x64";
                toolset["strategy"] = "external";

                boost::json::object cacheVars;
                cacheVars["CMAKE_BUILD_TYPE"] = std::string(buildType);
                cacheVars["CMAKE_C_COMPILER"] = "cl.exe";
                cacheVars["CMAKE_CXX_COMPILER"] = "cl.exe";
                cacheVars["CMAKE_LINKER"] = "link.exe";
                cacheVars["CMAKE_AR"] = "lib.exe";
                cacheVars["MQENGINE_ROOT"] = std::string(engineRoot);
                cacheVars["MQENGINE_BUILD_DIR"] = std::string(engineBuildDir);

                boost::json::object preset;
                preset["name"] = std::string(presetName);
                preset["displayName"] = std::string(displayName);
                preset["description"] = std::string(description);
                preset["generator"] = "Ninja";
                preset["binaryDir"] = "${sourceDir}/Saved/Build/${presetName}";
                preset["condition"] = std::move(condition);
                preset["architecture"] = std::move(arch);
                preset["toolset"] = std::move(toolset);
                preset["cacheVariables"] = std::move(cacheVars);
                return preset;
            }

            boost::json::object buildBuildPreset(
                std::string_view presetName,
                std::string_view displayName,
                std::string_view configurePreset,
                const std::vector<std::string>& targets)
            {
                boost::json::object preset;
                preset["name"] = std::string(presetName);
                preset["displayName"] = std::string(displayName);
                preset["configurePreset"] = std::string(configurePreset);
                if (!targets.empty())
                {
                    boost::json::array targetsArr;
                    for (const std::string& target : targets)
                    {
                        targetsArr.emplace_back(target);
                    }
                    preset["targets"] = std::move(targetsArr);
                }
                return preset;
            }

            void upsertPresetByName(boost::json::array& presets, const boost::json::object& desired)
            {
                const auto* namePtr = desired.if_contains("name");
                const std::string desiredName = (namePtr && namePtr->is_string())
                    ? std::string(namePtr->as_string().c_str())
                    : std::string{};
                for (auto& item : presets)
                {
                    if (!item.is_object())
                    {
                        continue;
                    }
                    auto* objName = item.as_object().if_contains("name");
                    if (objName && objName->is_string() && objName->as_string().c_str() == desiredName)
                    {
                        item = desired;
                        return;
                    }
                }
                presets.emplace_back(desired);
            }

            std::string normalizeLineEndingsToLf(std::string_view text)
            {
                std::string normalized;
                normalized.reserve(text.size());
                for (size_t i = 0; i < text.size(); ++i)
                {
                    if (text[i] == '\r')
                    {
                        continue;
                    }
                    normalized.push_back(text[i]);
                }
                return normalized;
            }

            Status saveTextIfChanged(
                ResourceLoader* loader,
                const std::string& filePath,
                const std::string& content,
                bool& outWritten)
            {
                outWritten = false;
                if (loader->hasFile(filePath))
                {
                    auto existingOr = loader->loadFile(filePath);
                    if (existingOr.ok() &&
                        normalizeLineEndingsToLf(existingOr.value()) == normalizeLineEndingsToLf(content))
                    {
                        return OkStatus();
                    }
                }
                CHECK_STATUS(loader->saveFile(filePath, content));
                outWritten = true;
                return OkStatus();
            }

            Status syncCMakePresetsFile(
                ResourceLoader* loader,
                const std::string& projectDir,
                const ParsedProjectCMakeInfo& projectInfo,
                const std::string& engineRootDir,
                const std::string& debugBuildDir,
                const std::string& releaseBuildDir,
                bool& outWritten)
            {
                const std::string presetsPath = SAI::Path::join(projectDir, "CMakePresets.json");
                boost::json::object root;

                if (loader->hasFile(presetsPath))
                {
                    auto existingOr = loader->loadFile(presetsPath);
                    if (existingOr.ok())
                    {
                        boost::system::error_code ec;
                        boost::json::value parsed = boost::json::parse(existingOr.value(), ec);
                        if (!ec && parsed.is_object())
                        {
                            root = parsed.as_object();
                        }
                    }
                }

                if (!root.contains("version") || !root["version"].is_int64() || root["version"].as_int64() < 3)
                {
                    root["version"] = 3;
                }
                if (!root.contains("cmakeMinimumRequired") || !root["cmakeMinimumRequired"].is_object())
                {
                    boost::json::object minVer;
                    minVer["major"] = 3;
                    minVer["minor"] = 22;
                    minVer["patch"] = 1;
                    root["cmakeMinimumRequired"] = std::move(minVer);
                }

                boost::json::array configurePresets;
                if (auto* existingCfg = root.if_contains("configurePresets"); existingCfg && existingCfg->is_array())
                {
                    configurePresets = existingCfg->as_array();
                }

                const std::string debugDesc = projectInfo.projectName +
                    " standalone IDE build using Ninja and MSVC x64 (Debug), linked against prebuilt MQEngine";
                const std::string releaseDesc = projectInfo.projectName +
                    " standalone IDE build using Ninja and MSVC x64 (Release), requires prebuilt Release MQEngine";

                upsertPresetByName(
                    configurePresets,
                    buildConfigurePreset(kPresetDebugName, "Windows x64 Ninja Debug", debugDesc, "Debug", engineRootDir, debugBuildDir));
                upsertPresetByName(
                    configurePresets,
                    buildConfigurePreset(kPresetReleaseName, "Windows x64 Ninja Release", releaseDesc, "Release", engineRootDir, releaseBuildDir));
                root["configurePresets"] = std::move(configurePresets);

                boost::json::array buildPresets;
                if (auto* existingBuild = root.if_contains("buildPresets"); existingBuild && existingBuild->is_array())
                {
                    buildPresets = existingBuild->as_array();
                }

                upsertPresetByName(
                    buildPresets,
                    buildBuildPreset(
                        kBuildPresetDebugName,
                        "Build " + projectInfo.projectName + " (Debug)",
                        kPresetDebugName,
                        projectInfo.targets));
                upsertPresetByName(
                    buildPresets,
                    buildBuildPreset(
                        kBuildPresetReleaseName,
                        "Build " + projectInfo.projectName + " (Release)",
                        kPresetReleaseName,
                        projectInfo.targets));
                root["buildPresets"] = std::move(buildPresets);

                std::string jsonText;
                prettyPrintJsonValue(boost::json::value(std::move(root)), jsonText, 0);
                jsonText.push_back('\n');
                return saveTextIfChanged(loader, presetsPath, jsonText, outWritten);
            }

            std::string escapeXmlAttribute(std::string_view value)
            {
                std::string escaped;
                escaped.reserve(value.size() + 8);
                for (char ch : value)
                {
                    switch (ch)
                    {
                    case '&':
                        escaped += "&amp;";
                        break;
                    case '"':
                        escaped += "&quot;";
                        break;
                    case '<':
                        escaped += "&lt;";
                        break;
                    case '>':
                        escaped += "&gt;";
                        break;
                    default:
                        escaped.push_back(ch);
                        break;
                    }
                }
                return escaped;
            }

            Status patchExistingWorkspaceXmlIfNeeded(
                ResourceLoader* loader,
                const std::string& projectDir,
                bool& outUpdated)
            {
                outUpdated = false;
                const std::string wsPath = SAI::Path::join(projectDir, ".idea/workspace.xml");
                if (!loader->hasFile(wsPath))
                {
                    return OkStatus();
                }

                auto contentOr = loader->loadFile(wsPath);
                CHECK_STATUS(contentOr);
                std::string xml = contentOr.value();

                const std::string marker = "<component name=\"CMakeSettings\"";
                const size_t compStart = xml.find(marker);
                if (compStart == std::string::npos)
                {
                    return OkStatus();
                }
                const size_t compEnd = xml.find("</component>", compStart);
                if (compEnd == std::string::npos)
                {
                    return OkStatus();
                }

                std::string block = xml.substr(compStart, compEnd - compStart);
                bool modified = false;

                const size_t openTagEnd = block.find('>');
                if (openTagEnd != std::string::npos)
                {
                    std::string openTag = block.substr(0, openTagEnd);
                    if (const size_t autoPos = openTag.find("AUTO_RELOAD=\"true\""); autoPos != std::string::npos)
                    {
                        openTag.replace(autoPos, 18, "AUTO_RELOAD=\"false\"");
                        block.replace(0, openTagEnd, openTag);
                        modified = true;
                    }
                    else if (openTag.find("AUTO_RELOAD=") == std::string::npos)
                    {
                        openTag += " AUTO_RELOAD=\"false\"";
                        block.replace(0, openTagEnd, openTag);
                        modified = true;
                    }
                }

                size_t searchPos = 0;
                while (true)
                {
                    const size_t cfgPos = block.find("<configuration ", searchPos);
                    if (cfgPos == std::string::npos)
                    {
                        break;
                    }
                    const size_t cfgEnd = block.find('>', cfgPos);
                    if (cfgEnd == std::string::npos)
                    {
                        break;
                    }
                    std::string cfgTag = block.substr(cfgPos, cfgEnd - cfgPos);
                    if (cfgTag.find("PROFILE_NAME=\"Debug\"") != std::string::npos)
                    {
                        if (const size_t enPos = cfgTag.find("ENABLED=\"true\""); enPos != std::string::npos)
                        {
                            cfgTag.replace(enPos, 14, "ENABLED=\"false\"");
                            block.replace(cfgPos, cfgEnd - cfgPos, cfgTag);
                            modified = true;
                        }
                    }
                    searchPos = cfgPos + cfgTag.size();
                }

                if (!modified)
                {
                    return OkStatus();
                }
                xml.replace(compStart, compEnd - compStart, block);
                CHECK_STATUS(loader->saveFile(wsPath, xml));
                outUpdated = true;
                return OkStatus();
            }

            Status syncIdeaCMakeFiles(
                ResourceLoader* loader,
                const std::string& projectDir,
                bool& outCmakeXmlWritten,
                bool& outWorkspaceUpdated)
            {
                const std::string cmakeXmlContent =
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<project version=\"4\">\n"
                    "  <component name=\"CMakeSharedSettings\" AUTO_RELOAD=\"false\">\n"
                    "    <configurations>\n"
                    "      <configuration PROFILE_NAME=\"Debug\" ENABLED=\"false\" CONFIG_NAME=\"Debug\" />\n"
                    "      <configuration PROFILE_NAME=\"windows-ninja-debug-x64 - build-ninja-debug-win\" ENABLED=\"true\" FROM_PRESET=\"true\" GENERATION_DIR=\"$PROJECT_DIR$/Saved/Build/windows-ninja-debug-x64\" />\n"
                    "      <configuration PROFILE_NAME=\"windows-ninja-debug-x64\" PROFILE_DISPLAY_NAME=\"Windows x64 Ninja Debug\" ENABLED=\"false\" FROM_PRESET=\"true\" GENERATION_DIR=\"$PROJECT_DIR$/Saved/Build/windows-ninja-debug-x64\" />\n"
                    "      <configuration PROFILE_NAME=\"windows-ninja-release-x64\" PROFILE_DISPLAY_NAME=\"Windows x64 Ninja Release\" ENABLED=\"false\" FROM_PRESET=\"true\" GENERATION_DIR=\"$PROJECT_DIR$/Saved/Build/windows-ninja-release-x64\" />\n"
                    "      <configuration PROFILE_NAME=\"windows-ninja-release-x64 - build-ninja-release-win\" ENABLED=\"false\" FROM_PRESET=\"true\" GENERATION_DIR=\"$PROJECT_DIR$/Saved/Build/windows-ninja-release-x64\" />\n"
                    "    </configurations>\n"
                    "  </component>\n"
                    "</project>\n";

                const std::string cmakeXmlPath = SAI::Path::join(projectDir, ".idea/cmake.xml");
                CHECK_STATUS(saveTextIfChanged(loader, cmakeXmlPath, cmakeXmlContent, outCmakeXmlWritten));
                return patchExistingWorkspaceXmlIfNeeded(loader, projectDir, outWorkspaceUpdated);
            }

            std::string buildCMakeRunXml(
                std::string_view configName,
                std::string_view /*projectDir*/,
                std::string_view projectName,
                std::string_view targetName,
                std::string_view runExecutablePath)
            {
                constexpr std::string_view rawParams = "--project=\"$PROJECT_DIR$\"";
                std::string xml =
                    "<component name=\"ProjectRunConfigurationManager\">\n"
                    "  <configuration default=\"false\" name=\"";
                xml += escapeXmlAttribute(configName);
                xml += "\" type=\"CMakeRunConfiguration\" factoryName=\"Application\" PROGRAM_PARAMS=\"";
                xml += escapeXmlAttribute(rawParams);
                xml += "\" REDIRECT_INPUT=\"false\" ELEVATE=\"false\" USE_EXTERNAL_CONSOLE=\"false\" EMULATE_TERMINAL=\"false\" WORKING_DIR=\"file://$PROJECT_DIR$\" PASS_PARENT_ENVS_2=\"true\" ";
                if (!targetName.empty())
                {
                    xml += "PROJECT_NAME=\"";
                    xml += escapeXmlAttribute(projectName);
                    xml += "\" TARGET_NAME=\"";
                    xml += escapeXmlAttribute(targetName);
                    xml += "\" ";
                }
                else
                {
                    xml += "EXPLICIT_BUILD_TARGET_NAME=\"all\" ";
                }
                xml += "CONFIG_NAME=\"";
                xml += escapeXmlAttribute(kClionActiveConfigName);
                xml += "\" RUN_PATH=\"";
                xml += escapeXmlAttribute(runExecutablePath);
                xml += "\">\n"
                       "    <method v=\"2\">\n"
                       "      <option name=\"com.jetbrains.cidr.execution.CidrBuildBeforeRunTaskProvider$BuildBeforeRunTask\" enabled=\"true\" />\n"
                       "    </method>\n"
                       "  </configuration>\n"
                       "</component>\n";
                return xml;
            }

            Status syncRunConfigurations(
                ResourceLoader* loader,
                const std::string& projectDir,
                const ParsedProjectCMakeInfo& projectInfo,
                const std::string& editorExe,
                const std::string& gameExe,
                bool& outWritten)
            {
                const std::string fallbackTarget = projectInfo.targets.empty()
                    ? std::string{}
                    : projectInfo.targets.front();
                const std::string editorTarget = !projectInfo.editorCompositeTarget.empty()
                    ? projectInfo.editorCompositeTarget
                    : (!projectInfo.nativeModuleTarget.empty()
                           ? projectInfo.nativeModuleTarget
                           : (!projectInfo.runtimeCompositeTarget.empty() ? projectInfo.runtimeCompositeTarget : fallbackTarget));
                const std::string runtimeTarget = !projectInfo.runtimeCompositeTarget.empty()
                    ? projectInfo.runtimeCompositeTarget
                    : (!projectInfo.nativeModuleTarget.empty() ? projectInfo.nativeModuleTarget : fallbackTarget);

                bool wroteEditor = false;
                bool wroteGame = false;
                CHECK_STATUS(saveTextIfChanged(
                    loader,
                    SAI::Path::join(projectDir, ".run/Run in MQEditor.run.xml"),
                    buildCMakeRunXml("Run in MQEditor", projectDir, projectInfo.projectName, editorTarget, editorExe),
                    wroteEditor));
                CHECK_STATUS(saveTextIfChanged(
                    loader,
                    SAI::Path::join(projectDir, ".run/Run Standalone Game.run.xml"),
                    buildCMakeRunXml("Run Standalone Game", projectDir, projectInfo.projectName, runtimeTarget, gameExe),
                    wroteGame));
                outWritten = wroteEditor || wroteGame;
                return OkStatus();
            }

            bool hasIgnoreLine(std::string_view content, std::string_view pattern)
            {
                size_t lineStart = 0;
                while (lineStart < content.size())
                {
                    size_t lineEnd = content.find('\n', lineStart);
                    if (lineEnd == std::string::npos)
                    {
                        lineEnd = content.size();
                    }
                    const std::string trimmed = trimAscii(
                        std::string_view(content.data() + lineStart, lineEnd - lineStart));
                    if (trimmed == pattern)
                    {
                        return true;
                    }
                    lineStart = lineEnd + 1;
                }
                return false;
            }

            Status syncGitignore(ResourceLoader* loader, const std::string& projectDir, bool& outWritten)
            {
                outWritten = false;
                const std::string gitignorePath = SAI::Path::join(projectDir, ".gitignore");
                constexpr const char* kDefaultGitignore =
                    "# 编译输出与工程临时目录\n"
                    "Saved/\n"
                    "Dist/\n"
                    "cmake-build-*/\n"
                    "build*/\n"
                    "out/\n\n"
                    "# IDE 用户本地会话缓存（保留 .idea/cmake.xml 与 .run/ 共享配置）\n"
                    ".idea/workspace.xml\n"
                    ".idea/**/workspace.xml\n"
                    ".vs/\n"
                    "CMakeUserPresets.json\n";

                if (!loader->hasFile(gitignorePath))
                {
                    return saveTextIfChanged(loader, gitignorePath, kDefaultGitignore, outWritten);
                }

                auto existingOr = loader->loadFile(gitignorePath);
                CHECK_STATUS(existingOr);
                std::string updated = existingOr.value();

                constexpr auto kRequiredRules = std::to_array<std::string_view>({
                    "Saved/",
                    "Dist/",
                    "cmake-build-*/",
                    "build*/",
                    "out/",
                    ".idea/workspace.xml",
                    ".idea/**/workspace.xml",
                    ".vs/",
                    "CMakeUserPresets.json"
                });

                std::vector<std::string_view> missingRules;
                for (std::string_view rule : kRequiredRules)
                {
                    if (!hasIgnoreLine(updated, rule))
                    {
                        missingRules.push_back(rule);
                    }
                }
                if (missingRules.empty())
                {
                    return OkStatus();
                }

                if (!updated.empty() && updated.back() != '\n')
                {
                    updated.push_back('\n');
                }
                updated += "\n# MQEngine IDE 与构建输出自动忽略规则\n";
                for (std::string_view rule : missingRules)
                {
                    updated.append(rule);
                    updated.push_back('\n');
                }
                return saveTextIfChanged(loader, gitignorePath, updated, outWritten);
            }
        } // namespace

        std::string ProjectIdeConfigGenerator::resolveEngineRootDir()
        {
            const std::string buildDir = resolveEngineBuildDir("");
            ResourceLoader* loader = resolveResourceLoader(nullptr);
            if (!buildDir.empty() && loader)
            {
                const std::string cachePath = SAI::Path::join(buildDir, "CMakeCache.txt");
                if (loader->hasFile(cachePath))
                {
                    auto cacheOr = loader->loadFile(cachePath);
                    if (cacheOr.ok())
                    {
                        const std::string homeDir = normalizeForwardSlash(
                            readCMakeCacheVar(cacheOr.value(), "CMAKE_HOME_DIRECTORY:"));
                        if (isEngineSourceRootDir(homeDir))
                        {
                            return homeDir;
                        }
                    }
                }
            }

            const std::string exeDir = SAI::System::getExecutableDirectory();
            if (!exeDir.empty())
            {
                const std::string found = normalizeForwardSlash(
                    SAI::FileSystem::findDirectoryUpwardsWhere(exeDir, isEngineSourceRootDir, 8));
                if (!found.empty())
                {
                    return found;
                }
            }

            if (auto* res = Resource::Resource::InstancePtr())
            {
                const std::string& cfgEngineDir = res->getEngineDir();
                if (!cfgEngineDir.empty())
                {
                    const std::string found = normalizeForwardSlash(
                        SAI::FileSystem::findDirectoryUpwardsWhere(cfgEngineDir, isEngineSourceRootDir, 8));
                    if (!found.empty())
                    {
                        return found;
                    }
                }
            }

            const std::string cwd = SAI::System::getCurrentDirectory();
            const std::string foundFromCwd = normalizeForwardSlash(
                SAI::FileSystem::findDirectoryUpwardsWhere(cwd, isEngineSourceRootDir, 8));
            if (!foundFromCwd.empty())
            {
                return foundFromCwd;
            }

            const std::string nestedEngine = normalizeForwardSlash(SAI::Path::join(cwd, "MQEngine"));
            return isEngineSourceRootDir(nestedEngine) ? nestedEngine : std::string{};
        }

        std::string ProjectIdeConfigGenerator::resolveEngineBuildDir(const std::string& engineRootDir)
        {
            const std::string exeDir = SAI::System::getExecutableDirectory();
            if (!exeDir.empty())
            {
                const std::string fromExe = normalizeForwardSlash(
                    SAI::FileSystem::findDirectoryUpwardsWhere(exeDir, isEngineBuildDir, 6));
                if (!fromExe.empty())
                {
                    return fromExe;
                }
            }

            if (auto* res = Resource::Resource::InstancePtr())
            {
                const std::string& cfgEngineDir = res->getEngineDir();
                if (!cfgEngineDir.empty())
                {
                    const std::string fromCfg = normalizeForwardSlash(
                        SAI::FileSystem::findDirectoryUpwardsWhere(cfgEngineDir, isEngineBuildDir, 6));
                    if (!fromCfg.empty())
                    {
                        return fromCfg;
                    }
                }
            }

            if (!engineRootDir.empty())
            {
                constexpr auto kBuildSubdirs = std::to_array<std::string_view>({
                    "Output/build-windows-debug",
                    "Output/build-vs2022-debug",
                    "Output/build-windows-release",
                    "Output/build-linux-debug"
                });
                for (std::string_view rel : kBuildSubdirs)
                {
                    const std::string cand = normalizeForwardSlash(SAI::Path::join(engineRootDir, rel));
                    if (isEngineBuildDir(cand))
                    {
                        return cand;
                    }
                }
                return normalizeForwardSlash(SAI::Path::join(engineRootDir, "Output/build-windows-debug"));
            }

            return {};
        }

        bool ProjectIdeConfigGenerator::isEngineCppProject(const std::string& projectDir, ResourceLoader* explicitLoader)
        {
            const std::string normDir = normalizeForwardSlash(projectDir);
            if (normDir.empty() || !SAI::FileSystem::isDirectory(normDir))
            {
                return false;
            }

            ResourceLoader* loader = resolveResourceLoader(explicitLoader);
            const std::string projectMq = SAI::Path::join(normDir, "project.mq");
            const std::string cmakeLists = SAI::Path::join(normDir, "CMakeLists.txt");
            if (loader)
            {
                return loader->hasFile(projectMq) && loader->hasFile(cmakeLists);
            }
            return SAI::FileSystem::isFile(projectMq) && SAI::FileSystem::isFile(cmakeLists);
        }

        StatusOr<IdeProjectSyncResult> ProjectIdeConfigGenerator::syncProjectIdeFiles(
            const std::string& projectDir,
            ResourceLoader* explicitLoader)
        {
            ZoneScoped();
            ResourceLoader* loader = resolveResourceLoader(explicitLoader);
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader 尚未初始化，无法生成工程 IDE 构建配置");
            }

            const std::string normProjDir = normalizeForwardSlash(projectDir);
            if (normProjDir.empty() || !SAI::FileSystem::isDirectory(normProjDir))
            {
                return NotFoundError(StrCat("工程目录不存在: ", projectDir));
            }
            if (!isEngineCppProject(normProjDir, loader))
            {
                return FailedPreconditionError(
                    StrCat("目标目录不同时包含 project.mq 与顶层 CMakeLists.txt，跳过 IDE 预设生成: ", normProjDir));
            }

            const std::string engineRootDir = resolveEngineRootDir();
            if (engineRootDir.empty())
            {
                return NotFoundError("未能定位 MQEngine 源码根目录（需包含 Config/MQPluginSDK.cmake）");
            }
            const std::string activeBuildDir = resolveEngineBuildDir(engineRootDir);

            std::string debugBuildDir;
            std::string releaseBuildDir;
            resolveDebugAndReleaseBuildDirs(engineRootDir, activeBuildDir, loader, debugBuildDir, releaseBuildDir);

            std::string editorExe;
            std::string gameExe;
            resolveRuntimeHostExecutables(debugBuildDir, activeBuildDir, editorExe, gameExe);

            const ParsedProjectCMakeInfo projectInfo = parseProjectCMakeInfo(normProjDir, loader);

            IdeProjectSyncResult result;
            result.projectDir = normProjDir;
            result.projectName = projectInfo.projectName;
            result.engineRootDir = engineRootDir;
            result.engineBuildDir = debugBuildDir;
            result.targets = projectInfo.targets;
            result.editorCompositeTarget = projectInfo.editorCompositeTarget;
            result.runtimeCompositeTarget = projectInfo.runtimeCompositeTarget;

            CHECK_STATUS(syncCMakePresetsFile(
                loader,
                normProjDir,
                projectInfo,
                engineRootDir,
                debugBuildDir,
                releaseBuildDir,
                result.generatedCMakePresets));
            CHECK_STATUS(syncIdeaCMakeFiles(
                loader,
                normProjDir,
                result.generatedIdeaCMake,
                result.updatedWorkspaceXml));
            CHECK_STATUS(syncRunConfigurations(
                loader,
                normProjDir,
                projectInfo,
                editorExe,
                gameExe,
                result.generatedRunConfigs));
            CHECK_STATUS(syncGitignore(loader, normProjDir, result.generatedGitignore));

            return result;
        }
    } // namespace EditorBase
} // namespace MQEngine
