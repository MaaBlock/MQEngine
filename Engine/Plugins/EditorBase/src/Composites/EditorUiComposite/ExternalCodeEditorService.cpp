/**
 * @file ExternalCodeEditorService.cpp
 * @brief 外部代码编辑器自动扫描探测、命令行协议组装、无黑框进程拉起与配置持久化实现
 */
#include "ExternalCodeEditorService.h"

#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/config/ConfigDocument.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/resource.h>

#include <algorithm>
#include <array>
#include <cctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MQEngine
{
    namespace EditorBase
    {
        namespace
        {
            constexpr const char* kLocalPreferencesRelPath = "Saved/CodeEditorPreferences.toml";

            constexpr auto kSupportedCodeExtensions = std::to_array<std::string_view>({
                ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh", ".hxx", ".inl", ".ixx", ".cppm",
                ".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs",
                ".cmake", ".toml", ".mq", ".json", ".yaml", ".yml", ".ini", ".cfg",
                ".hlsl", ".glsl", ".vert", ".frag", ".comp", ".shader", ".rml", ".rcss",
                ".lua", ".py"
            });

            std::string toLowerAscii(std::string_view text)
            {
                std::string result(text);
                std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                return result;
            }

            bool containsCaseInsensitive(std::string_view haystack, std::string_view needle)
            {
                const std::string lowerHay = toLowerAscii(haystack);
                const std::string lowerNeedle = toLowerAscii(needle);
                return lowerHay.find(lowerNeedle) != std::string::npos;
            }

            std::vector<uint32_t> extractVersionNumbers(std::string_view text)
            {
                std::vector<uint32_t> numbers;
                size_t idx = 0;
                while (idx < text.size())
                {
                    if (!std::isdigit(static_cast<unsigned char>(text[idx])))
                    {
                        ++idx;
                        continue;
                    }
                    size_t endIdx = idx;
                    while (endIdx < text.size() && std::isdigit(static_cast<unsigned char>(text[endIdx])))
                    {
                        ++endIdx;
                    }
                    uint32_t val = 0;
                    const auto [ptr, ec] = std::from_chars(text.data() + idx, text.data() + endIdx, val);
                    if (ec == std::errc{} && ptr == text.data() + endIdx)
                    {
                        numbers.push_back(val);
                    }
                    idx = endIdx;
                }
                return numbers;
            }

            bool isNewerVersionPath(std::string_view lhs, std::string_view rhs)
            {
                const auto vL = extractVersionNumbers(lhs);
                const auto vR = extractVersionNumbers(rhs);
                const size_t count = std::max(vL.size(), vR.size());
                for (size_t i = 0; i < count; ++i)
                {
                    const uint32_t a = (i < vL.size()) ? vL[i] : 0u;
                    const uint32_t b = (i < vR.size()) ? vR[i] : 0u;
                    if (a != b)
                    {
                        return a > b;
                    }
                }
                return lhs > rhs;
            }

            std::string extractVersionHintFromPath(std::string_view exePath)
            {
                std::string parent = SAI::Path::parentPath(exePath);
                if (toLowerAscii(SAI::Path::fileName(parent)) == "bin")
                {
                    parent = SAI::Path::parentPath(parent);
                }
                const std::string folderName = SAI::Path::fileName(parent);
                for (size_t i = 0; i < folderName.size(); ++i)
                {
                    if (std::isdigit(static_cast<unsigned char>(folderName[i])))
                    {
                        return folderName.substr(i);
                    }
                }
                if (toLowerAscii(SAI::Path::fileName(parent)) == "ide")
                {
                    const std::string vsRoot = SAI::Path::parentPath(SAI::Path::parentPath(parent));
                    const std::string edition = SAI::Path::fileName(vsRoot);
                    const std::string ver = SAI::Path::fileName(SAI::Path::parentPath(vsRoot));
                    if (!ver.empty())
                    {
                        return edition.empty() ? ver : (ver + " " + edition);
                    }
                }
                return {};
            }

            void appendUniqueDirCaseInsensitive(std::vector<std::string>& dirs, std::string_view rawDir)
            {
                if (rawDir.empty())
                {
                    return;
                }
                std::string cleaned = SAI::Path::lexicallyNormal(rawDir);
                while (cleaned.size() > 3 && (cleaned.back() == '/' || cleaned.back() == '\\'))
                {
                    cleaned.pop_back();
                }
                if (!SAI::FileSystem::isDirectory(cleaned))
                {
                    return;
                }
                const std::string key = toLowerAscii(cleaned);
                for (const auto& existing : dirs)
                {
                    if (toLowerAscii(existing) == key)
                    {
                        return;
                    }
                }
                dirs.push_back(std::move(cleaned));
            }

#ifdef _WIN32
            std::wstring utf8ToWide(std::string_view str)
            {
                if (str.empty())
                {
                    return {};
                }
                const int needed = MultiByteToWideChar(
                    CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
                if (needed <= 0)
                {
                    return {};
                }
                std::wstring wide(static_cast<size_t>(needed), L'\0');
                MultiByteToWideChar(
                    CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wide.data(), needed);
                return wide;
            }

            std::string wideToUtf8(std::wstring_view wide)
            {
                if (wide.empty())
                {
                    return {};
                }
                const int needed = WideCharToMultiByte(
                    CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
                if (needed <= 0)
                {
                    return {};
                }
                std::string utf8(static_cast<size_t>(needed), '\0');
                WideCharToMultiByte(
                    CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), needed, nullptr, nullptr);
                return utf8;
            }

            std::string getEnvVarUtf8(const wchar_t* varName)
            {
                const DWORD needed = GetEnvironmentVariableW(varName, nullptr, 0);
                if (needed == 0)
                {
                    return {};
                }
                std::wstring buf(static_cast<size_t>(needed), L'\0');
                const DWORD written = GetEnvironmentVariableW(varName, buf.data(), needed);
                if (written == 0 || written >= needed)
                {
                    return {};
                }
                buf.resize(static_cast<size_t>(written));
                return wideToUtf8(buf);
            }

            struct RegistryInstallRecord
            {
                std::string displayName;
                std::string installLocation;
                std::string displayVersion;
            };

            std::string queryRegStringValue(HKEY key, const wchar_t* valueName)
            {
                DWORD dataType = 0;
                DWORD byteSize = 0;
                if (RegQueryValueExW(key, valueName, nullptr, &dataType, nullptr, &byteSize) != ERROR_SUCCESS)
                {
                    return {};
                }
                if ((dataType != REG_SZ && dataType != REG_EXPAND_SZ) || byteSize < sizeof(wchar_t))
                {
                    return {};
                }
                std::wstring buf(byteSize / sizeof(wchar_t), L'\0');
                if (RegQueryValueExW(
                        key, valueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &byteSize) != ERROR_SUCCESS)
                {
                    return {};
                }
                while (!buf.empty() && buf.back() == L'\0')
                {
                    buf.pop_back();
                }
                return wideToUtf8(buf);
            }

            void scanUninstallRegistryHive(HKEY rootHive, const wchar_t* subKeyPath, std::vector<RegistryInstallRecord>& outRecords)
            {
                HKEY uninstallKey = nullptr;
                if (RegOpenKeyExW(rootHive, subKeyPath, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS)
                {
                    return;
                }

                wchar_t childName[256] = {};
                for (DWORD index = 0;; ++index)
                {
                    DWORD nameLen = static_cast<DWORD>( std::size(childName) );
                    const LONG enumRes = RegEnumKeyExW(
                        uninstallKey, index, childName, &nameLen, nullptr, nullptr, nullptr, nullptr);
                    if (enumRes != ERROR_SUCCESS)
                    {
                        break;
                    }

                    HKEY appKey = nullptr;
                    if (RegOpenKeyExW(uninstallKey, childName, 0, KEY_READ, &appKey) != ERROR_SUCCESS)
                    {
                        continue;
                    }

                    std::string displayName = queryRegStringValue(appKey, L"DisplayName");
                    if (!displayName.empty() &&
                        (containsCaseInsensitive(displayName, "clion") ||
                         containsCaseInsensitive(displayName, "rider") ||
                         containsCaseInsensitive(displayName, "visual studio")))
                    {
                        std::string installLoc = queryRegStringValue(appKey, L"InstallLocation");
                        if (!installLoc.empty())
                        {
                            outRecords.push_back(RegistryInstallRecord{
                                .displayName = std::move(displayName),
                                .installLocation = SAI::Path::lexicallyNormal(installLoc),
                                .displayVersion = queryRegStringValue(appKey, L"DisplayVersion")
                            });
                        }
                    }
                    RegCloseKey(appKey);
                }

                RegCloseKey(uninstallKey);
            }

            struct HostProbeEnvironment
            {
                std::string localAppData;
                std::vector<std::string> programRoots;
                std::vector<std::string> pathDirs;
                std::vector<RegistryInstallRecord> registryInstalls;
            };

            void collectFixedDriveProgramRoots(std::vector<std::string>& programRoots)
            {
                const DWORD driveMask = GetLogicalDrives();
                for (int bit = 0; bit < 26; ++bit)
                {
                    if ((driveMask & (1u << bit)) == 0)
                    {
                        continue;
                    }
                    wchar_t driveRoot[4] = {static_cast<wchar_t>(L'A' + bit), L':', L'\\', L'\0'};
                    if (GetDriveTypeW(driveRoot) != DRIVE_FIXED)
                    {
                        continue;
                    }
                    const std::string prefix(1, static_cast<char>('A' + bit));
                    appendUniqueDirCaseInsensitive(programRoots, prefix + ":/Program Files");
                    appendUniqueDirCaseInsensitive(programRoots, prefix + ":/Program Files (x86)");
                    appendUniqueDirCaseInsensitive(programRoots, prefix + ":/JetBrains");
                    appendUniqueDirCaseInsensitive(programRoots, prefix + ":/Programs");
                    appendUniqueDirCaseInsensitive(programRoots, prefix + ":/Tools");
                    appendUniqueDirCaseInsensitive(programRoots, prefix + ":/Software");
                }
            }

            void collectPathDirectories(std::vector<std::string>& pathDirs)
            {
                const std::string pathEnv = getEnvVarUtf8(L"PATH");
                size_t start = 0;
                while (start <= pathEnv.size())
                {
                    size_t end = pathEnv.find(';', start);
                    if (end == std::string::npos)
                    {
                        end = pathEnv.size();
                    }
                    std::string token = pathEnv.substr(start, end - start);
                    while (!token.empty() && (token.front() == '"' || token.front() == ' '))
                    {
                        token.erase(token.begin());
                    }
                    while (!token.empty() && (token.back() == '"' || token.back() == ' '))
                    {
                        token.pop_back();
                    }
                    appendUniqueDirCaseInsensitive(pathDirs, token);
                    if (end == pathEnv.size())
                    {
                        break;
                    }
                    start = end + 1;
                }
            }

            HostProbeEnvironment buildHostProbeEnvironment()
            {
                HostProbeEnvironment env;
                env.localAppData = SAI::Path::lexicallyNormal(getEnvVarUtf8(L"LOCALAPPDATA"));
                appendUniqueDirCaseInsensitive(env.programRoots, getEnvVarUtf8(L"ProgramW6432"));
                appendUniqueDirCaseInsensitive(env.programRoots, getEnvVarUtf8(L"PROGRAMFILES"));
                appendUniqueDirCaseInsensitive(env.programRoots, getEnvVarUtf8(L"PROGRAMFILES(X86)"));
                collectFixedDriveProgramRoots(env.programRoots);
                collectPathDirectories(env.pathDirs);

                constexpr const wchar_t* kUninstallPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
                constexpr const wchar_t* kUninstallWowPath = L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
                scanUninstallRegistryHive(HKEY_CURRENT_USER, kUninstallPath, env.registryInstalls);
                scanUninstallRegistryHive(HKEY_LOCAL_MACHINE, kUninstallPath, env.registryInstalls);
                scanUninstallRegistryHive(HKEY_LOCAL_MACHINE, kUninstallWowPath, env.registryInstalls);
                return env;
            }

            std::string pickNewestExistingExecutable(std::vector<std::string>& candidates)
            {
                std::vector<std::string> valid;
                valid.reserve(candidates.size());
                for (const auto& path : candidates)
                {
                    if (!path.empty() && SAI::FileSystem::isFile(path))
                    {
                        valid.push_back(SAI::Path::normalize(path));
                    }
                }
                if (valid.empty())
                {
                    return {};
                }
                std::sort(valid.begin(), valid.end(), isNewerVersionPath);
                return valid.front();
            }

            std::string probeJetBrainsToolboxV1(
                std::string_view localAppData,
                std::initializer_list<std::string_view> appFolderNames,
                std::string_view exeRelativePath)
            {
                if (localAppData.empty())
                {
                    return {};
                }
                std::vector<std::string> candidates;
                for (std::string_view appName : appFolderNames)
                {
                    const std::string appDir = SAI::Path::join(localAppData, "JetBrains/Toolbox/apps", appName);
                    for (const auto& channelDir : SAI::FileSystem::listSubDirectories(appDir))
                    {
                        for (const auto& buildDir : SAI::FileSystem::listSubDirectories(channelDir))
                        {
                            if (SAI::Path::fileName(buildDir).find(".plugins") != std::string::npos)
                            {
                                continue;
                            }
                            candidates.push_back(SAI::Path::join(buildDir, exeRelativePath));
                        }
                    }
                }
                return pickNewestExistingExecutable(candidates);
            }

            std::string probeJetBrainsStandalone(
                const HostProbeEnvironment& env,
                std::string_view productKeyword,
                std::string_view exeRelativePath)
            {
                std::vector<std::string> candidates;
                auto scanParentDir = [&](std::string_view parentDir) {
                    if (!SAI::FileSystem::isDirectory(parentDir))
                    {
                        return;
                    }
                    for (const auto& subDir : SAI::FileSystem::listSubDirectories(parentDir))
                    {
                        if (containsCaseInsensitive(SAI::Path::fileName(subDir), productKeyword))
                        {
                            candidates.push_back(SAI::Path::join(subDir, exeRelativePath));
                        }
                    }
                };

                for (const auto& root : env.programRoots)
                {
                    scanParentDir(SAI::Path::join(root, "JetBrains"));
                    scanParentDir(root);
                }

                for (const auto& reg : env.registryInstalls)
                {
                    if (containsCaseInsensitive(reg.displayName, productKeyword))
                    {
                        candidates.push_back(SAI::Path::join(reg.installLocation, exeRelativePath));
                        candidates.push_back(SAI::Path::join(reg.installLocation, SAI::Path::fileName(exeRelativePath)));
                    }
                }

                return pickNewestExistingExecutable(candidates);
            }

            std::string probeFromPathDirs(
                const std::vector<std::string>& pathDirs,
                std::initializer_list<std::string_view> exeNames)
            {
                for (std::string_view exeName : exeNames)
                {
                    for (const auto& dir : pathDirs)
                    {
                        const std::string candidate = SAI::Path::join(dir, exeName);
                        if (SAI::FileSystem::isFile(candidate))
                        {
                            return SAI::Path::normalize(candidate);
                        }
                    }
                }
                return {};
            }

            DetectedCodeEditorInfo detectJetBrainsProduct(
                const HostProbeEnvironment& env,
                CodeEditorKind kind,
                const char* displayName,
                std::string_view keyword,
                std::initializer_list<std::string_view> toolboxV2Folders,
                std::initializer_list<std::string_view> toolboxV1Folders,
                std::string_view binExeName,
                std::initializer_list<std::string_view> scriptAndPathNames)
            {
                DetectedCodeEditorInfo info;
                info.kind = kind;
                info.displayName = displayName;
                const std::string relBinExe = SAI::Path::join("bin", binExeName);

                if (!env.localAppData.empty())
                {
                    for (std::string_view folder : toolboxV2Folders)
                    {
                        const std::string cand = SAI::Path::join(env.localAppData, "Programs", folder, relBinExe);
                        if (SAI::FileSystem::isFile(cand))
                        {
                            info.executablePath = SAI::Path::normalize(cand);
                            break;
                        }
                    }
                }

                if (info.executablePath.empty())
                {
                    info.executablePath = probeJetBrainsToolboxV1(env.localAppData, toolboxV1Folders, relBinExe);
                }
                if (info.executablePath.empty())
                {
                    info.executablePath = probeJetBrainsStandalone(env, keyword, relBinExe);
                }
                if (info.executablePath.empty() && !env.localAppData.empty())
                {
                    for (std::string_view scriptName : scriptAndPathNames)
                    {
                        const std::string scriptCand = SAI::Path::join(env.localAppData, "JetBrains/Toolbox/scripts", scriptName);
                        if (SAI::FileSystem::isFile(scriptCand))
                        {
                            info.executablePath = SAI::Path::normalize(scriptCand);
                            break;
                        }
                    }
                }
                if (info.executablePath.empty())
                {
                    info.executablePath = probeFromPathDirs(env.pathDirs, scriptAndPathNames);
                }

                info.available = !info.executablePath.empty();
                if (info.available)
                {
                    info.versionHint = extractVersionHintFromPath(info.executablePath);
                }
                return info;
            }

            DetectedCodeEditorInfo detectVSCodeOnWindows(const HostProbeEnvironment& env)
            {
                DetectedCodeEditorInfo info;
                info.kind = CodeEditorKind::VSCode;
                info.displayName = "VS Code";

                std::vector<std::string> directCandidates;
                if (!env.localAppData.empty())
                {
                    directCandidates.push_back(SAI::Path::join(env.localAppData, "Programs/Microsoft VS Code/Code.exe"));
                    directCandidates.push_back(SAI::Path::join(env.localAppData, "Programs/Microsoft VS Code Insiders/Code - Insiders.exe"));
                }
                for (const auto& root : env.programRoots)
                {
                    directCandidates.push_back(SAI::Path::join(root, "Microsoft VS Code/Code.exe"));
                    directCandidates.push_back(SAI::Path::join(root, "Microsoft VS Code Insiders/Code - Insiders.exe"));
                }
                for (const auto& reg : env.registryInstalls)
                {
                    if (containsCaseInsensitive(reg.displayName, "visual studio code"))
                    {
                        directCandidates.push_back(SAI::Path::join(reg.installLocation, "Code.exe"));
                    }
                }

                for (const auto& cand : directCandidates)
                {
                    if (SAI::FileSystem::isFile(cand))
                    {
                        info.executablePath = SAI::Path::normalize(cand);
                        break;
                    }
                }

                if (info.executablePath.empty())
                {
                    const std::string pathMatch = probeFromPathDirs(env.pathDirs, {"Code.exe", "code.cmd"});
                    if (!pathMatch.empty())
                    {
                        const std::string parentUp = SAI::Path::parentPath(SAI::Path::parentPath(pathMatch));
                        const std::string guiExe = SAI::Path::join(parentUp, "Code.exe");
                        info.executablePath = SAI::FileSystem::isFile(guiExe) ? SAI::Path::normalize(guiExe) : pathMatch;
                    }
                }

                info.available = !info.executablePath.empty();
                return info;
            }

            DetectedCodeEditorInfo detectVisualStudioOnWindows(const HostProbeEnvironment& env)
            {
                DetectedCodeEditorInfo info;
                info.kind = CodeEditorKind::VisualStudio;
                info.displayName = "Visual Studio";

                std::vector<std::string> candidates;
                constexpr std::array<std::string_view, 4> kEditions = {
                    "Enterprise", "Professional", "Community", "Preview"
                };

                for (const auto& root : env.programRoots)
                {
                    const std::string vsBase = SAI::Path::join(root, "Microsoft Visual Studio");
                    if (!SAI::FileSystem::isDirectory(vsBase))
                    {
                        continue;
                    }
                    auto verDirs = SAI::FileSystem::listSubDirectories(vsBase);
                    std::sort(verDirs.begin(), verDirs.end(), isNewerVersionPath);
                    for (const auto& verDir : verDirs)
                    {
                        for (std::string_view edition : kEditions)
                        {
                            const std::string devenv = SAI::Path::join(verDir, edition, "Common7/IDE/devenv.exe");
                            if (SAI::FileSystem::isFile(devenv))
                            {
                                candidates.push_back(SAI::Path::normalize(devenv));
                            }
                        }
                    }
                }

                if (!candidates.empty())
                {
                    info.executablePath = candidates.front();
                }
                else
                {
                    info.executablePath = probeFromPathDirs(env.pathDirs, {"devenv.exe"});
                }

                info.available = !info.executablePath.empty();
                if (info.available)
                {
                    info.versionHint = extractVersionHintFromPath(info.executablePath);
                }
                return info;
            }

            std::string toWindowsCliPath(std::string_view pathStr)
            {
                std::string norm = SAI::Path::normalize(pathStr);
                std::replace(norm.begin(), norm.end(), '/', '\\');
                while (norm.size() > 3 && norm.back() == '\\')
                {
                    norm.pop_back();
                }
                return norm;
            }

            std::wstring quoteWin32Argument(std::wstring_view arg, bool forceQuotes)
            {
                const bool needsQuotes = forceQuotes || arg.empty() ||
                    arg.find_first_of(L" \t\"") != std::wstring_view::npos;
                if (!needsQuotes)
                {
                    return std::wstring(arg);
                }

                std::wstring quoted;
                quoted.push_back(L'"');
                size_t backslashes = 0;
                for (wchar_t ch : arg)
                {
                    if (ch == L'\\')
                    {
                        ++backslashes;
                    }
                    else if (ch == L'"')
                    {
                        quoted.append(backslashes * 2 + 1, L'\\');
                        quoted.push_back(L'"');
                        backslashes = 0;
                    }
                    else
                    {
                        quoted.append(backslashes, L'\\');
                        quoted.push_back(ch);
                        backslashes = 0;
                    }
                }
                quoted.append(backslashes * 2, L'\\');
                quoted.push_back(L'"');
                return quoted;
            }

            struct CliArgToken
            {
                std::string text;
                bool quote = false;
            };

            Status spawnEditorProcessWin32(
                std::string_view exePath,
                const std::vector<CliArgToken>& args,
                std::string_view rawTailArgs,
                std::string_view workingDir)
            {
                const std::string nativeExe = toWindowsCliPath(exePath);
                const std::wstring exeWide = utf8ToWide(nativeExe);
                if (exeWide.empty())
                {
                    return InvalidArgumentError("代码编辑器可执行文件路径无效");
                }

                std::wstring cmdLine = quoteWin32Argument(exeWide, true);
                for (const auto& arg : args)
                {
                    cmdLine.push_back(L' ');
                    cmdLine += quoteWin32Argument(utf8ToWide(arg.text), arg.quote);
                }
                if (!rawTailArgs.empty())
                {
                    cmdLine.push_back(L' ');
                    cmdLine += utf8ToWide(rawTailArgs);
                }

                const std::string extLower = SAI::Path::extensionLower(nativeExe);
                const bool isBatchScript = (extLower == ".cmd" || extLower == ".bat");

                std::wstring appNameWide = exeWide;
                if (isBatchScript)
                {
                    std::string comspec = getEnvVarUtf8(L"COMSPEC");
                    if (comspec.empty())
                    {
                        comspec = "C:\\Windows\\System32\\cmd.exe";
                    }
                    appNameWide = utf8ToWide(comspec);
                    cmdLine = quoteWin32Argument(appNameWide, true) + L" /d /s /c \"" + cmdLine + L"\"";
                }

                const std::string resolvedWorkDir = workingDir.empty()
                    ? SAI::Path::parentPath(exePath)
                    : std::string(workingDir);
                const std::wstring workDirWide = SAI::FileSystem::isDirectory(resolvedWorkDir)
                    ? utf8ToWide(toWindowsCliPath(resolvedWorkDir))
                    : std::wstring{};

                std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
                mutableCmd.push_back(L'\0');

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};

                const BOOL created = CreateProcessW(
                    appNameWide.c_str(),
                    mutableCmd.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    workDirWide.empty() ? nullptr : workDirWide.c_str(),
                    &si,
                    &pi);

                if (!created)
                {
                    return InternalError(StrCat(
                        "启动外部代码编辑器失败 [", exePath, "]，Win32 错误码: ", GetLastError()));
                }

                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return OkStatus();
            }
#endif

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
                if (auto* diskLoader = DiskResourceLoader::InstancePtr())
                {
                    return diskLoader;
                }
                return nullptr;
            }

            std::string getGlobalPreferencesPath()
            {
                const std::string exeDir = SAI::System::getExecutableDirectory();
                if (exeDir.empty())
                {
                    return kLocalPreferencesRelPath;
                }
                return SAI::Path::join(exeDir, kLocalPreferencesRelPath);
            }

            void replaceAllPlaceholders(std::string& target, std::string_view token, std::string_view value)
            {
                size_t pos = 0;
                while ((pos = target.find(token, pos)) != std::string::npos)
                {
                    target.replace(pos, token.size(), value);
                    pos += value.size();
                }
            }

            std::string resolveCLionTarget(std::string_view projectDir)
            {
                if (SAI::FileSystem::isFile(projectDir))
                {
                    return SAI::Path::parentPath(projectDir);
                }
                return std::string(projectDir);
            }

            std::string resolveRiderTarget(std::string_view projectDir)
            {
                if (SAI::FileSystem::isFile(projectDir))
                {
                    return std::string(projectDir);
                }
                auto slnFiles = SAI::FileSystem::listFiles(projectDir, ".sln");
                if (!slnFiles.empty())
                {
                    return slnFiles.front();
                }
                auto slnxFiles = SAI::FileSystem::listFiles(projectDir, ".slnx");
                if (!slnxFiles.empty())
                {
                    return slnxFiles.front();
                }
                const std::string cmakeLists = SAI::Path::join(projectDir, "CMakeLists.txt");
                if (SAI::FileSystem::isFile(cmakeLists))
                {
                    return cmakeLists;
                }
                return std::string(projectDir);
            }

            std::string resolveVisualStudioTarget(std::string_view projectDir)
            {
                auto slnFiles = SAI::FileSystem::listFiles(projectDir, ".sln");
                if (!slnFiles.empty())
                {
                    return slnFiles.front();
                }
                return std::string(projectDir);
            }

            StatusOr<std::string> resolveExistingSourceFile(std::string_view rawFilePath, std::string_view projectDir)
            {
                if (rawFilePath.empty())
                {
                    return InvalidArgumentError("源码文件路径为空");
                }
                if (SAI::Path::isAbsolute(rawFilePath) && SAI::FileSystem::isFile(rawFilePath))
                {
                    return SAI::Path::normalize(rawFilePath);
                }
                if (!projectDir.empty())
                {
                    const std::string fromProj = SAI::Path::resolve(projectDir, rawFilePath);
                    if (SAI::FileSystem::isFile(fromProj))
                    {
                        return fromProj;
                    }
                }
                const std::string fromCwd = SAI::Path::resolve(SAI::System::getCurrentDirectory(), rawFilePath);
                if (SAI::FileSystem::isFile(fromCwd))
                {
                    return fromCwd;
                }
                if (SAI::FileSystem::isFile(rawFilePath))
                {
                    return SAI::Path::normalize(rawFilePath);
                }
                return NotFoundError(StrCat("未找到目标源码文件: ", rawFilePath));
            }

            std::string resolveOwningProjectForFile(std::string_view resolvedFilePath, std::string_view explicitProjectDir)
            {
                if (!explicitProjectDir.empty() && SAI::FileSystem::isDirectory(explicitProjectDir))
                {
                    return SAI::Path::normalize(explicitProjectDir);
                }
                const std::string activeProj = ExternalCodeEditorService::resolveActiveProjectDir();
                if (!activeProj.empty() && !SAI::Path::relative(resolvedFilePath, activeProj).empty())
                {
                    return activeProj;
                }
                const std::string fileDir = SAI::Path::parentPath(resolvedFilePath);
                const std::string upwardProj = SAI::FileSystem::findDirectoryUpwardsWhere(
                    fileDir,
                    [](const std::string& dir) {
                        return SAI::FileSystem::isFile(SAI::Path::join(dir, "project.mq")) ||
                               SAI::FileSystem::isFile(SAI::Path::join(dir, "CMakeLists.txt"));
                    },
                    8);
                return upwardProj.empty() ? activeProj : upwardProj;
            }

            bool parseTrailingLineAndColumn(std::string_view tail, int& outLine, int& outColumn)
            {
                outLine = 0;
                outColumn = 0;
                if (tail.empty())
                {
                    return true;
                }
                if (tail.front() == '(')
                {
                    size_t pos = 1;
                    size_t endNum = pos;
                    while (endNum < tail.size() && std::isdigit(static_cast<unsigned char>(tail[endNum])))
                    {
                        ++endNum;
                    }
                    if (endNum == pos)
                    {
                        return false;
                    }
                    (void)std::from_chars(tail.data() + pos, tail.data() + endNum, outLine);
                    if (endNum < tail.size() && tail[endNum] == ',')
                    {
                        size_t colStart = endNum + 1;
                        size_t colEnd = colStart;
                        while (colEnd < tail.size() && std::isdigit(static_cast<unsigned char>(tail[colEnd])))
                        {
                            ++colEnd;
                        }
                        if (colEnd > colStart)
                        {
                            (void)std::from_chars(tail.data() + colStart, tail.data() + colEnd, outColumn);
                        }
                    }
                    return true;
                }
                if (tail.front() == ':')
                {
                    size_t pos = 1;
                    size_t endNum = pos;
                    while (endNum < tail.size() && std::isdigit(static_cast<unsigned char>(tail[endNum])))
                    {
                        ++endNum;
                    }
                    if (endNum > pos)
                    {
                        (void)std::from_chars(tail.data() + pos, tail.data() + endNum, outLine);
                        if (endNum < tail.size() && tail[endNum] == ':')
                        {
                            size_t colStart = endNum + 1;
                            size_t colEnd = colStart;
                            while (colEnd < tail.size() && std::isdigit(static_cast<unsigned char>(tail[colEnd])))
                            {
                                ++colEnd;
                            }
                            if (colEnd > colStart)
                            {
                                (void)std::from_chars(tail.data() + colStart, tail.data() + colEnd, outColumn);
                            }
                        }
                    }
                    return true;
                }
                return true;
            }

            size_t findPathStartBackward(std::string_view text, size_t endPos)
            {
                for (size_t pos = endPos; pos > 0; --pos)
                {
                    const size_t idx = pos - 1;
                    if (idx + 2 < endPos &&
                        std::isalpha(static_cast<unsigned char>(text[idx])) &&
                        text[idx + 1] == ':' &&
                        (text[idx + 2] == '/' || text[idx + 2] == '\\'))
                    {
                        if (idx == 0 || !std::isalnum(static_cast<unsigned char>(text[idx - 1])))
                        {
                            return idx;
                        }
                    }
                    const char ch = text[idx];
                    if (ch == '"' || ch == '\'' || ch == '[' || ch == '<' || ch == '|' || ch == '\t' || ch == '\n' || ch == '\r')
                    {
                        return pos;
                    }
                }
                return 0;
            }
        } // namespace

        ExternalCodeEditorService::ExternalCodeEditorService()
        {
            refreshDetectedEditors();
        }

        void ExternalCodeEditorService::refreshDetectedEditors()
        {
            ZoneScoped();
            m_detectedEditors.clear();
#ifdef _WIN32
            const HostProbeEnvironment env = buildHostProbeEnvironment();
            m_detectedEditors.push_back(detectJetBrainsProduct(
                env,
                CodeEditorKind::CLion,
                "CLion",
                "clion",
                {"CLion", "CLion Nova"},
                {"CLion", "CLion-Nova"},
                "clion64.exe",
                {"clion64.exe", "clion.exe", "clion.cmd", "clion.bat"}));

            m_detectedEditors.push_back(detectJetBrainsProduct(
                env,
                CodeEditorKind::Rider,
                "JetBrains Rider",
                "rider",
                {"Rider", "JetBrains Rider"},
                {"Rider"},
                "rider64.exe",
                {"rider64.exe", "rider.exe", "rider.cmd", "rider.bat"}));

            m_detectedEditors.push_back(detectVSCodeOnWindows(env));
            m_detectedEditors.push_back(detectVisualStudioOnWindows(env));
#else
            m_detectedEditors.push_back(detectSingleEditor(CodeEditorKind::CLion));
            m_detectedEditors.push_back(detectSingleEditor(CodeEditorKind::Rider));
            m_detectedEditors.push_back(detectSingleEditor(CodeEditorKind::VSCode));
            m_detectedEditors.push_back(detectSingleEditor(CodeEditorKind::VisualStudio));
#endif
        }

        DetectedCodeEditorInfo ExternalCodeEditorService::detectSingleEditor(CodeEditorKind kind) const
        {
            DetectedCodeEditorInfo info;
            info.kind = kind;
            info.displayName = getPresetDisplayName(kind, true);
            return info;
        }

        Status ExternalCodeEditorService::loadPreferences(ResourceLoader* loader)
        {
            ZoneScoped();
            ResourceLoader* activeLoader = resolveResourceLoader(loader);
            if (!activeLoader)
            {
                return FailedPreconditionError("ResourceLoader 未就绪");
            }

            std::string targetPath = kLocalPreferencesRelPath;
            if (!activeLoader->exists(targetPath))
            {
                const std::string globalPath = getGlobalPreferencesPath();
                if (activeLoader->exists(globalPath))
                {
                    targetPath = globalPath;
                }
                else
                {
                    return NotFoundError("尚未生成外部代码编辑器配置文件");
                }
            }

            auto loadedOr = ConfigDocument::loadStructFromFile<ExternalCodeEditorPreferences>(activeLoader, targetPath);
            CHECK_STATUS(loadedOr);
            m_preferences = loadedOr.value();
            if (static_cast<uint8_t>(m_preferences.preferredKind) > static_cast<uint8_t>(CodeEditorKind::Custom))
            {
                m_preferences.preferredKind = CodeEditorKind::Auto;
            }
            return OkStatus();
        }

        Status ExternalCodeEditorService::savePreferences(ResourceLoader* loader) const
        {
            ZoneScoped();
            ResourceLoader* activeLoader = resolveResourceLoader(loader);
            if (!activeLoader)
            {
                return FailedPreconditionError("ResourceLoader 未就绪");
            }

            (void)activeLoader->ensureDir("Saved");
            Status st = ConfigDocument::saveStructToFile(
                activeLoader, kLocalPreferencesRelPath, m_preferences, ConfigFormat::Toml);

            const std::string globalPath = getGlobalPreferencesPath();
            if (globalPath != kLocalPreferencesRelPath)
            {
                (void)ConfigDocument::saveStructToFile(
                    activeLoader, globalPath, m_preferences, ConfigFormat::Toml);
            }
            return st;
        }

        void ExternalCodeEditorService::setPreferences(const ExternalCodeEditorPreferences& preferences)
        {
            m_preferences = preferences;
        }

        const DetectedCodeEditorInfo* ExternalCodeEditorService::findDetectedEditor(CodeEditorKind kind) const noexcept
        {
            for (const auto& info : m_detectedEditors)
            {
                if (info.kind == kind)
                {
                    return &info;
                }
            }
            return nullptr;
        }

        CodeEditorKind ExternalCodeEditorService::resolveEffectiveKind() const noexcept
        {
            if (m_preferences.preferredKind != CodeEditorKind::Auto)
            {
                return m_preferences.preferredKind;
            }

            constexpr std::array<CodeEditorKind, 4> kAutoPriority = {
                CodeEditorKind::CLion,
                CodeEditorKind::Rider,
                CodeEditorKind::VSCode,
                CodeEditorKind::VisualStudio
            };

            for (CodeEditorKind candidate : kAutoPriority)
            {
                const auto* info = findDetectedEditor(candidate);
                if (info && info->available)
                {
                    return candidate;
                }
            }

            if (!m_preferences.customExecutablePath.empty() &&
                SAI::FileSystem::isFile(m_preferences.customExecutablePath))
            {
                return CodeEditorKind::Custom;
            }
            return CodeEditorKind::CLion;
        }

        std::string ExternalCodeEditorService::getEffectiveDisplayName(bool english) const
        {
            const CodeEditorKind effective = resolveEffectiveKind();
            if (effective == CodeEditorKind::Custom)
            {
                if (!m_preferences.customExecutablePath.empty())
                {
                    const std::string stem = SAI::Path::stem(m_preferences.customExecutablePath);
                    if (!stem.empty())
                    {
                        return stem;
                    }
                }
                return english ? "Custom Editor" : "自定义代码编辑器";
            }
            const auto* info = findDetectedEditor(effective);
            if (info && info->available)
            {
                return info->displayName;
            }
            if (m_preferences.preferredKind == CodeEditorKind::Auto)
            {
                return english ? "Code Editor" : "代码编辑器";
            }
            return getPresetDisplayName(effective, english);
        }

        std::string ExternalCodeEditorService::getEffectiveExecutablePath() const
        {
            const CodeEditorKind effective = resolveEffectiveKind();
            if (effective == CodeEditorKind::Custom)
            {
                return m_preferences.customExecutablePath;
            }
            const auto* info = findDetectedEditor(effective);
            return (info && info->available) ? info->executablePath : std::string{};
        }

        bool ExternalCodeEditorService::hasAvailableEditor() const noexcept
        {
            const std::string exePath = getEffectiveExecutablePath();
            return !exePath.empty() && SAI::FileSystem::isFile(exePath);
        }

        Status ExternalCodeEditorService::resolveLaunchTargetEditor(
            CodeEditorKind& outKind,
            std::string& outExePath) const
        {
            outKind = resolveEffectiveKind();
            outExePath = getEffectiveExecutablePath();
            if (outExePath.empty() || !SAI::FileSystem::isFile(outExePath))
            {
                return NotFoundError("未找到可用的外部代码编辑器，请在 [编辑 -> 偏好设置 -> 外部代码编辑器] 中检测或指定可执行路径");
            }
            return OkStatus();
        }

        StatusOr<IdeProjectSyncResult> ExternalCodeEditorService::syncProjectIdeFiles(
            const std::string& projectDir,
            ResourceLoader* loader) const
        {
            ZoneScoped();
            const std::string targetProj = projectDir.empty()
                ? resolveActiveProjectDir()
                : SAI::Path::normalize(projectDir);
            auto syncOr = ProjectIdeConfigGenerator::syncProjectIdeFiles(targetProj, loader);
            if (syncOr.ok())
            {
                m_syncedProjectDirs.insert(syncOr->projectDir);
            }
            return syncOr;
        }

        Status ExternalCodeEditorService::openProject(const std::string& projectDir) const
        {
            ZoneScoped();
            CodeEditorKind kind = CodeEditorKind::CLion;
            std::string exePath;
            CHECK_STATUS(resolveLaunchTargetEditor(kind, exePath));

            const std::string targetProj = projectDir.empty() ? resolveActiveProjectDir() : SAI::Path::normalize(projectDir);
            if (targetProj.empty() || !SAI::FileSystem::exists(targetProj))
            {
                return NotFoundError(StrCat("工程路径不存在: ", targetProj));
            }

            if (ProjectIdeConfigGenerator::isEngineCppProject(targetProj))
            {
                (void)syncProjectIdeFiles(targetProj);
            }

#ifdef _WIN32
            std::vector<CliArgToken> args;
            std::string rawCustomArgs;

            switch (kind)
            {
            case CodeEditorKind::CLion:
                args.push_back({toWindowsCliPath(resolveCLionTarget(targetProj)), true});
                break;
            case CodeEditorKind::Rider:
                args.push_back({toWindowsCliPath(resolveRiderTarget(targetProj)), true});
                break;
            case CodeEditorKind::VSCode:
                args.push_back({toWindowsCliPath(targetProj), true});
                break;
            case CodeEditorKind::VisualStudio:
                args.push_back({toWindowsCliPath(resolveVisualStudioTarget(targetProj)), true});
                break;
            case CodeEditorKind::Custom:
            case CodeEditorKind::Auto:
                rawCustomArgs = m_preferences.customOpenProjectArgs.empty()
                    ? "\"{project}\""
                    : m_preferences.customOpenProjectArgs;
                replaceAllPlaceholders(rawCustomArgs, "{project}", toWindowsCliPath(targetProj));
                break;
            }

            const std::string workDir = SAI::FileSystem::isDirectory(targetProj)
                ? targetProj
                : SAI::Path::parentPath(targetProj);
            return spawnEditorProcessWin32(exePath, args, rawCustomArgs, workDir);
#else
            (void)targetProj;
            return UnimplementedError("当前平台暂未实现外部代码编辑器启动");
#endif
        }

        Status ExternalCodeEditorService::openFile(
            const std::string& filePath,
            int line,
            int column,
            const std::string& projectDir) const
        {
            ZoneScoped();
            CodeEditorKind kind = CodeEditorKind::CLion;
            std::string exePath;
            CHECK_STATUS(resolveLaunchTargetEditor(kind, exePath));

            const std::string baseProjHint = projectDir.empty() ? resolveActiveProjectDir() : projectDir;
            auto resolvedFileOr = resolveExistingSourceFile(filePath, baseProjHint);
            CHECK_STATUS(resolvedFileOr);
            const std::string resolvedFile = resolvedFileOr.value();
            const std::string owningProject = resolveOwningProjectForFile(resolvedFile, projectDir);
            if (!owningProject.empty() &&
                !m_syncedProjectDirs.contains(owningProject) &&
                ProjectIdeConfigGenerator::isEngineCppProject(owningProject))
            {
                (void)syncProjectIdeFiles(owningProject);
            }

#ifdef _WIN32
            const std::string nativeFile = toWindowsCliPath(resolvedFile);
            const std::string nativeProj = owningProject.empty() ? std::string{} : toWindowsCliPath(owningProject);
            const int effectiveLine = (line > 0) ? line : 0;
            const int effectiveCol = (column > 0) ? column : 1;

            std::vector<CliArgToken> args;
            std::string rawCustomArgs;

            switch (kind)
            {
            case CodeEditorKind::CLion:
            case CodeEditorKind::Rider:
                if (!nativeProj.empty())
                {
                    args.push_back({nativeProj, true});
                }
                if (effectiveLine > 0)
                {
                    args.push_back({"--line", false});
                    args.push_back({std::to_string(effectiveLine), false});
                    args.push_back({"--column", false});
                    args.push_back({std::to_string(effectiveCol), false});
                }
                args.push_back({nativeFile, true});
                break;

            case CodeEditorKind::VSCode:
                if (!nativeProj.empty())
                {
                    args.push_back({nativeProj, true});
                }
                args.push_back({"--goto", false});
                if (effectiveLine > 0)
                {
                    args.push_back({StrCat(nativeFile, ":", effectiveLine, ":", effectiveCol), true});
                }
                else
                {
                    args.push_back({nativeFile, true});
                }
                break;

            case CodeEditorKind::VisualStudio:
                args.push_back({"/Edit", false});
                args.push_back({nativeFile, true});
                if (effectiveLine > 0)
                {
                    args.push_back({"/Command", false});
                    args.push_back({StrCat("Edit.Goto ", effectiveLine), true});
                }
                break;

            case CodeEditorKind::Custom:
            case CodeEditorKind::Auto:
                rawCustomArgs = m_preferences.customOpenFileArgs.empty()
                    ? "\"{file}\""
                    : m_preferences.customOpenFileArgs;
                replaceAllPlaceholders(rawCustomArgs, "{project}", nativeProj);
                replaceAllPlaceholders(rawCustomArgs, "{file}", nativeFile);
                replaceAllPlaceholders(rawCustomArgs, "{line}", std::to_string(effectiveLine > 0 ? effectiveLine : 1));
                replaceAllPlaceholders(rawCustomArgs, "{col}", std::to_string(effectiveCol));
                break;
            }

            const std::string workDir = !owningProject.empty() ? owningProject : SAI::Path::parentPath(resolvedFile);
            return spawnEditorProcessWin32(exePath, args, rawCustomArgs, workDir);
#else
            (void)line;
            (void)column;
            return UnimplementedError("当前平台暂未实现外部代码编辑器启动");
#endif
        }

        const char* ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind kind, bool english) noexcept
        {
            switch (kind)
            {
            case CodeEditorKind::Auto:
                return english ? "Auto Detect (Recommended)" : "自动优选本机 IDE (Auto)";
            case CodeEditorKind::CLion:
                return "CLion";
            case CodeEditorKind::Rider:
                return "JetBrains Rider";
            case CodeEditorKind::VSCode:
                return "VS Code";
            case CodeEditorKind::VisualStudio:
                return "Visual Studio";
            case CodeEditorKind::Custom:
                return english ? "Custom Executable..." : "自定义可执行文件 (Custom)...";
            }
            return "CLion";
        }

        bool ExternalCodeEditorService::isSourceCodeFile(std::string_view filePath) noexcept
        {
            if (filePath.empty())
            {
                return false;
            }
            const std::string fileName = toLowerAscii(SAI::Path::fileName(filePath));
            if (fileName == "cmakelists.txt")
            {
                return true;
            }
            const std::string ext = SAI::Path::extensionLower(filePath);
            if (ext.empty())
            {
                return false;
            }
            for (std::string_view candidate : kSupportedCodeExtensions)
            {
                if (ext == candidate)
                {
                    return true;
                }
            }
            return false;
        }

        std::string ExternalCodeEditorService::resolveActiveProjectDir()
        {
            if (auto* res = Resource::Resource::InstancePtr())
            {
                if (const auto* cfg = res->getConfig())
                {
                    if (cfg->isProjectOpen() && !cfg->getProjectDirectory().empty() &&
                        SAI::FileSystem::isDirectory(cfg->getProjectDirectory()))
                    {
                        return SAI::Path::normalize(cfg->getProjectDirectory());
                    }
                }

                const std::string& projDir = res->getProjectDir();
                if (!projDir.empty() && SAI::FileSystem::isDirectory(projDir))
                {
                    if (SAI::FileSystem::isFile(SAI::Path::join(projDir, "project.mq")) ||
                        SAI::FileSystem::isFile(SAI::Path::join(projDir, "CMakeLists.txt")))
                    {
                        return SAI::Path::normalize(projDir);
                    }
                    const std::string upward = SAI::FileSystem::findDirectoryUpwardsWhere(
                        projDir,
                        [](const std::string& dir) {
                            return SAI::FileSystem::isFile(SAI::Path::join(dir, "project.mq")) ||
                                   SAI::FileSystem::isFile(SAI::Path::join(dir, "CMakeLists.txt"));
                        },
                        6);
                    if (!upward.empty())
                    {
                        return upward;
                    }
                    return SAI::Path::normalize(projDir);
                }
            }

            const std::string cwd = SAI::System::getCurrentDirectory();
            const std::string upwardFromCwd = SAI::FileSystem::findDirectoryUpwardsWhere(
                cwd,
                [](const std::string& dir) {
                    return SAI::FileSystem::isFile(SAI::Path::join(dir, "project.mq")) ||
                           SAI::FileSystem::isFile(SAI::Path::join(dir, "CMakeLists.txt"));
                },
                6);
            return upwardFromCwd.empty() ? cwd : upwardFromCwd;
        }

        bool ExternalCodeEditorService::tryExtractSourceLocation(
            std::string_view logText,
            SourceFileLocation& outLocation)
        {
            const std::string lowerText = toLowerAscii(logText);
            const std::string activeProj = resolveActiveProjectDir();

            auto tryCandidateSpan = [&](size_t suffixPos, size_t suffixLen) -> bool {
                const size_t pathEnd = suffixPos + suffixLen;
                if (pathEnd < logText.size())
                {
                    const char nextCh = logText[pathEnd];
                    if (std::isalnum(static_cast<unsigned char>(nextCh)) || nextCh == '_' || nextCh == '.')
                    {
                        return false;
                    }
                }

                size_t pathStart = findPathStartBackward(logText, suffixPos);
                while (pathStart < pathEnd && (logText[pathStart] == ' ' || logText[pathStart] == '('))
                {
                    ++pathStart;
                }
                if (pathStart >= pathEnd)
                {
                    return false;
                }

                std::string rawCandidate(logText.substr(pathStart, pathEnd - pathStart));
                auto resolvedOr = resolveExistingSourceFile(rawCandidate, activeProj);
                if (!resolvedOr.ok())
                {
                    const size_t lastSpace = rawCandidate.rfind(' ');
                    if (lastSpace == std::string::npos)
                    {
                        return false;
                    }
                    resolvedOr = resolveExistingSourceFile(rawCandidate.substr(lastSpace + 1), activeProj);
                    if (!resolvedOr.ok())
                    {
                        return false;
                    }
                }

                int line = 0;
                int column = 0;
                (void)parseTrailingLineAndColumn(logText.substr(pathEnd), line, column);
                outLocation.filePath = resolvedOr.value();
                outLocation.line = line;
                outLocation.column = column;
                return true;
            };

            size_t cmakePos = lowerText.find("cmakelists.txt");
            while (cmakePos != std::string::npos)
            {
                if (tryCandidateSpan(cmakePos, 14))
                {
                    return true;
                }
                cmakePos = lowerText.find("cmakelists.txt", cmakePos + 14);
            }

            for (std::string_view ext : kSupportedCodeExtensions)
            {
                size_t pos = lowerText.find(ext);
                while (pos != std::string::npos)
                {
                    if (tryCandidateSpan(pos, ext.size()))
                    {
                        return true;
                    }
                    pos = lowerText.find(ext, pos + ext.size());
                }
            }
            return false;
        }

    } // namespace EditorBase
} // namespace MQEngine
