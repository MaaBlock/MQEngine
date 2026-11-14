/**
 * @file ProjectLauncherService.cpp
 * @brief 工程打开、最近工程记录、工程模板实例化与场景快捷路径服务实现
 */
#include "ProjectLauncherService.h"

#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/resource.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <combaseapi.h>
#include <shobjidl.h>
#endif

namespace MQEngine
{
    namespace EditorBase
    {
        namespace
        {
            constexpr size_t kMaxRecentProjects = 12;

            ResourceLoader* resolveLoader(ResourceLoader* explicitLoader)
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

            bool equalsIgnoreCasePath(std::string_view a, std::string_view b)
            {
                return toLowerAscii(normalizeForwardSlash(a)) == toLowerAscii(normalizeForwardSlash(b));
            }

            bool startsWithIgnoreCase(std::string_view value, std::string_view prefix)
            {
                if (value.size() < prefix.size())
                {
                    return false;
                }
                return toLowerAscii(value.substr(0, prefix.size())) == toLowerAscii(prefix);
            }

            bool endsWithIgnoreCase(std::string_view value, std::string_view suffix)
            {
                if (value.size() < suffix.size())
                {
                    return false;
                }
                return toLowerAscii(value.substr(value.size() - suffix.size())) == toLowerAscii(suffix);
            }

            std::string unquoteTomlString(std::string_view rawValue)
            {
                std::string trimmed = trimAscii(rawValue);
                if (trimmed.size() >= 2 &&
                    ((trimmed.front() == '"' && trimmed.back() == '"') ||
                     (trimmed.front() == '\'' && trimmed.back() == '\'')))
                {
                    return trimmed.substr(1, trimmed.size() - 2);
                }
                return trimmed;
            }

            std::string stripTomlInlineComment(std::string_view rawLine)
            {
                bool inDoubleQuote = false;
                bool inSingleQuote = false;
                for (size_t i = 0; i < rawLine.size(); ++i)
                {
                    const char ch = rawLine[i];
                    if (ch == '"' && !inSingleQuote && (i == 0 || rawLine[i - 1] != '\\'))
                    {
                        inDoubleQuote = !inDoubleQuote;
                    }
                    else if (ch == '\'' && !inDoubleQuote)
                    {
                        inSingleQuote = !inSingleQuote;
                    }
                    else if (ch == '#' && !inDoubleQuote && !inSingleQuote)
                    {
                        return std::string(rawLine.substr(0, i));
                    }
                }
                return std::string(rawLine);
            }

            std::string escapeTomlString(std::string_view value)
            {
                std::string out;
                out.reserve(value.size() + 4);
                for (char ch : value)
                {
                    if (ch == '\\')
                    {
                        out += "\\\\";
                    }
                    else if (ch == '"')
                    {
                        out += "\\\"";
                    }
                    else
                    {
                        out.push_back(ch);
                    }
                }
                return out;
            }

            std::string sanitizeProjectFolderName(std::string_view rawName)
            {
                std::string trimmed = trimAscii(rawName);
                std::string cleaned;
                cleaned.reserve(trimmed.size());
                for (char ch : trimmed)
                {
                    constexpr std::string_view kForbidden = "<>:\"/\\|?*";
                    if (kForbidden.find(ch) != std::string_view::npos ||
                        static_cast<unsigned char>(ch) < 32)
                    {
                        cleaned.push_back('_');
                    }
                    else
                    {
                        cleaned.push_back(ch);
                    }
                }
                while (!cleaned.empty() && (cleaned.back() == '.' || cleaned.back() == ' '))
                {
                    cleaned.pop_back();
                }
                return cleaned;
            }

            std::string sanitizeExecutableToken(std::string_view projectName)
            {
                std::string token;
                token.reserve(projectName.size());
                for (char ch : projectName)
                {
                    const unsigned char uch = static_cast<unsigned char>(ch);
                    if (std::isalnum(uch) != 0 || ch == '_' || ch == '-')
                    {
                        token.push_back(ch);
                    }
                }
                return token.empty() ? std::string("MQGame") : token;
            }

            bool isSkippedTemplateDirName(std::string_view dirName)
            {
                const std::string lower = toLowerAscii(dirName);
                if (lower == "saved" || lower == "dist" || lower == ".git" ||
                    lower == ".vs" || lower == "out" || lower == "cmakefiles" ||
                    lower == "build")
                {
                    return true;
                }
                return startsWithIgnoreCase(lower, "cmake-build-") ||
                       startsWithIgnoreCase(lower, "build-") ||
                       startsWithIgnoreCase(lower, "build_");
            }

            bool isSkippedTemplateFileName(std::string_view fileName)
            {
                return toLowerAscii(fileName) == "workspace.xml";
            }

            void replaceAllInPlace(std::string& text, std::string_view from, std::string_view to)
            {
                if (from.empty() || from == to)
                {
                    return;
                }
                size_t pos = 0;
                while ((pos = text.find(from, pos)) != std::string::npos)
                {
                    text.replace(pos, from.size(), to);
                    pos += to.size();
                }
            }

            bool parseTomlAssignment(
                std::string_view rawLine,
                std::string& outKey,
                std::string& outValue)
            {
                const std::string clean = trimAscii(stripTomlInlineComment(rawLine));
                if (clean.empty() || clean.front() == '[')
                {
                    return false;
                }
                const size_t eqPos = clean.find('=');
                if (eqPos == std::string::npos)
                {
                    return false;
                }
                outKey = trimAscii(std::string_view(clean.data(), eqPos));
                outValue = unquoteTomlString(std::string_view(clean.data() + eqPos + 1, clean.size() - eqPos - 1));
                return !outKey.empty();
            }

            std::string updateProjectMqMetadata(
                std::string_view content,
                std::string_view oldTemplateName,
                std::string_view oldTemplateId,
                std::string_view newProjectName)
            {
                std::vector<std::string> lines;
                size_t start = 0;
                while (start <= content.size())
                {
                    size_t end = content.find('\n', start);
                    if (end == std::string_view::npos)
                    {
                        lines.emplace_back(content.substr(start));
                        break;
                    }
                    std::string line(content.substr(start, end - start));
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    lines.push_back(std::move(line));
                    start = end + 1;
                    if (start == content.size())
                    {
                        break;
                    }
                }

                std::string currentSection;
                bool updatedTopLevelName = false;
                const std::string execToken = sanitizeExecutableToken(newProjectName);

                for (std::string& line : lines)
                {
                    const std::string trimmed = trimAscii(stripTomlInlineComment(line));
                    if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
                    {
                        currentSection = toLowerAscii(trimAscii(std::string_view(trimmed.data() + 1, trimmed.size() - 2)));
                        continue;
                    }

                    std::string key;
                    std::string value;
                    if (!parseTomlAssignment(line, key, value))
                    {
                        continue;
                    }

                    if (currentSection.empty() && key == "name")
                    {
                        line = StrCat("name = \"", escapeTomlString(newProjectName), "\"");
                        updatedTopLevelName = true;
                    }
                    else if (currentSection == "packaging" && key == "executableName")
                    {
                        line = StrCat("executableName = \"", escapeTomlString(execToken), "\"");
                    }
                    else if (currentSection == "packaging" && key == "windowTitle")
                    {
                        std::string nextTitle = value;
                        if (!oldTemplateName.empty())
                        {
                            replaceAllInPlace(nextTitle, oldTemplateName, newProjectName);
                        }
                        if (!oldTemplateId.empty() && oldTemplateId != oldTemplateName)
                        {
                            replaceAllInPlace(nextTitle, oldTemplateId, newProjectName);
                        }
                        if (nextTitle == value)
                        {
                            nextTitle = StrCat("MQEngine - ", newProjectName);
                        }
                        line = StrCat("windowTitle = \"", escapeTomlString(nextTitle), "\"");
                    }
                }

                std::ostringstream oss;
                if (!updatedTopLevelName)
                {
                    oss << "name = \"" << escapeTomlString(newProjectName) << "\"\n";
                }
                for (const std::string& line : lines)
                {
                    oss << line << '\n';
                }
                return oss.str();
            }

            std::string updateCMakeProjectName(std::string_view cmakeContent, std::string_view newProjectName)
            {
                std::string result(cmakeContent);
                const std::string lower = toLowerAscii(cmakeContent);
                size_t searchPos = 0;

                while (searchPos < lower.size())
                {
                    const size_t hit = lower.find("project", searchPos);
                    if (hit == std::string::npos)
                    {
                        break;
                    }
                    if (hit > 0 && (std::isalnum(static_cast<unsigned char>(lower[hit - 1])) != 0 ||
                                      lower[hit - 1] == '_'))
                    {
                        searchPos = hit + 7;
                        continue;
                    }
                    const size_t lineStart = result.rfind('\n', hit);
                    const size_t commentPos = result.find('#', lineStart == std::string::npos ? 0 : lineStart + 1);
                    if (commentPos != std::string::npos && commentPos < hit)
                    {
                        searchPos = hit + 7;
                        continue;
                    }

                    size_t cursor = hit + 7;
                    while (cursor < result.size() && std::isspace(static_cast<unsigned char>(result[cursor])))
                    {
                        ++cursor;
                    }
                    if (cursor >= result.size() || result[cursor] != '(')
                    {
                        searchPos = cursor;
                        continue;
                    }
                    ++cursor;
                    while (cursor < result.size() && std::isspace(static_cast<unsigned char>(result[cursor])))
                    {
                        ++cursor;
                    }
                    if (cursor >= result.size())
                    {
                        break;
                    }

                    size_t tokenStart = cursor;
                    size_t tokenEnd = cursor;
                    if (result[cursor] == '"')
                    {
                        const size_t closeQuote = result.find('"', cursor + 1);
                        if (closeQuote == std::string::npos)
                        {
                            break;
                        }
                        tokenEnd = closeQuote + 1;
                    }
                    else
                    {
                        while (tokenEnd < result.size() &&
                               !std::isspace(static_cast<unsigned char>(result[tokenEnd])) &&
                               result[tokenEnd] != ')')
                        {
                            ++tokenEnd;
                        }
                    }

                    if (tokenEnd > tokenStart)
                    {
                        result.replace(
                            tokenStart,
                            tokenEnd - tokenStart,
                            StrCat("\"", newProjectName, "\""));
                    }
                    break;
                }
                return result;
            }

            Status copyTemplateDirectoryRecursive(
                const std::string& srcDir,
                const std::string& dstDir,
                ResourceLoader* loader,
                size_t& outCopiedCount)
            {
                CHECK_STATUS(loader->ensureDir(dstDir));
                const std::vector<SAI::FileEntry> entries = SAI::FileSystem::listEntries(srcDir);
                for (const SAI::FileEntry& entry : entries)
                {
                    if (entry.isDirectory)
                    {
                        if (isSkippedTemplateDirName(entry.name))
                        {
                            continue;
                        }
                        CHECK_STATUS(copyTemplateDirectoryRecursive(
                            entry.path,
                            SAI::Path::join(dstDir, entry.name),
                            loader,
                            outCopiedCount));
                    }
                    else
                    {
                        if (isSkippedTemplateFileName(entry.name))
                        {
                            continue;
                        }
                        const std::string dstFile = SAI::Path::join(dstDir, entry.name);
                        CHECK_STATUS(loader->copyFile(entry.path, dstFile, true));
                        ++outCopiedCount;
                    }
                }
                return OkStatus();
            }

            void collectSceneFilesRecursive(
                const std::string& rootDir,
                const std::string& currentDir,
                int depth,
                std::string_view normalizedMainRel,
                std::vector<ProjectSceneFileEntry>& outScenes)
            {
                if (depth > 4 || !SAI::FileSystem::isDirectory(currentDir))
                {
                    return;
                }

                for (const SAI::FileEntry& entry : SAI::FileSystem::listEntries(currentDir))
                {
                    if (entry.isDirectory)
                    {
                        if (isSkippedTemplateDirName(entry.name) || entry.name == "Assets" || entry.name == "Plugins")
                        {
                            continue;
                        }
                        collectSceneFilesRecursive(rootDir, entry.path, depth + 1, normalizedMainRel, outScenes);
                    }
                    else if (endsWithIgnoreCase(entry.name, ".mqscene"))
                    {
                        const std::string fullNorm = normalizeForwardSlash(entry.path);
                        const std::string relNorm = normalizeForwardSlash(SAI::Path::relative(fullNorm, rootDir));
                        ProjectSceneFileEntry item;
                        item.displayRelPath = relNorm.empty() ? entry.name : relNorm;
                        item.fullPath = fullNorm;
                        item.isMainScene = !normalizedMainRel.empty() &&
                                           equalsIgnoreCasePath(item.displayRelPath, normalizedMainRel);
                        outScenes.push_back(std::move(item));
                    }
                }
            }

            int64_t currentEpochSeconds()
            {
                return std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            }

#ifdef _WIN32
            struct ComInitScope
            {
                bool needsUninit = false;
                HRESULT initResult = E_FAIL;

                ComInitScope()
                {
                    initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                    needsUninit = (initResult == S_OK || initResult == S_FALSE);
                }

                ~ComInitScope()
                {
                    if (needsUninit)
                    {
                        CoUninitialize();
                    }
                }
            };

            template <typename ComInterface>
            struct ComPtrGuard
            {
                ComInterface* ptr = nullptr;

                ~ComPtrGuard()
                {
                    if (ptr)
                    {
                        ptr->Release();
                    }
                }
            };

            std::wstring toWideUtf8(std::string_view utf8)
            {
                if (utf8.empty())
                {
                    return {};
                }
                const int required = MultiByteToWideChar(
                    CP_UTF8,
                    0,
                    utf8.data(),
                    static_cast<int>(utf8.size()),
                    nullptr,
                    0);
                if (required <= 0)
                {
                    return {};
                }
                std::wstring wide(static_cast<size_t>(required), L'\0');
                MultiByteToWideChar(
                    CP_UTF8,
                    0,
                    utf8.data(),
                    static_cast<int>(utf8.size()),
                    wide.data(),
                    required);
                return wide;
            }

            std::string fromWideUtf8(const wchar_t* wide)
            {
                if (!wide || !wide[0])
                {
                    return {};
                }
                const int required = WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    wide,
                    -1,
                    nullptr,
                    0,
                    nullptr,
                    nullptr);
                if (required <= 1)
                {
                    return {};
                }
                std::string utf8(static_cast<size_t>(required - 1), '\0');
                WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    wide,
                    -1,
                    utf8.data(),
                    required,
                    nullptr,
                    nullptr);
                return utf8;
            }

            std::string getEnvVar(const char* name)
            {
                if (!name)
                {
                    return {};
                }
                const DWORD needed = GetEnvironmentVariableA(name, nullptr, 0);
                if (needed == 0)
                {
                    return {};
                }
                std::string value(static_cast<size_t>(needed), '\0');
                const DWORD written = GetEnvironmentVariableA(name, value.data(), needed);
                if (written == 0)
                {
                    return {};
                }
                value.resize(written);
                return value;
            }

            std::wstring toWindowsNativePathW(std::string_view pathUtf8)
            {
                std::string norm = normalizeForwardSlash(pathUtf8);
                std::replace(norm.begin(), norm.end(), '/', '\\');
                return toWideUtf8(norm);
            }

            void applyDialogInitialDir(IFileDialog* dialog, const std::string& initialDir)
            {
                if (!dialog || initialDir.empty() || !SAI::FileSystem::isDirectory(initialDir))
                {
                    return;
                }
                const std::wstring dirW = toWindowsNativePathW(initialDir);
                ComPtrGuard<IShellItem> folderItem;
                if (SUCCEEDED(SHCreateItemFromParsingName(
                        dirW.c_str(),
                        nullptr,
                        IID_PPV_ARGS(&folderItem.ptr))) &&
                    folderItem.ptr)
                {
                    dialog->SetFolder(folderItem.ptr);
                }
            }

            StatusOr<std::string> runWindowsFileDialog(
                bool isSaveDialog,
                bool pickFolders,
                const std::string& initialDir,
                const std::string& defaultFileName,
                const wchar_t* title)
            {
                ComInitScope comScope;
                if (FAILED(comScope.initResult) && comScope.initResult != RPC_E_CHANGED_MODE)
                {
                    return FailedPreconditionError("Windows COM 初始化失败，请直接输入路径");
                }

                ComPtrGuard<IFileDialog> dialog;
                const CLSID clsid = isSaveDialog ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
                const IID iid = isSaveDialog ? IID_IFileSaveDialog : IID_IFileOpenDialog;
                const HRESULT createHr = CoCreateInstance(
                    clsid,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    iid,
                    reinterpret_cast<void**>(&dialog.ptr));
                if (FAILED(createHr) || !dialog.ptr)
                {
                    return FailedPreconditionError("无法打开系统文件选择对话框，请直接输入路径");
                }

                DWORD options = 0;
                dialog.ptr->GetOptions(&options);
                options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
                if (pickFolders)
                {
                    options |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
                }
                else if (!isSaveDialog)
                {
                    options |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
                }
                else
                {
                    options |= FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST;
                }
                dialog.ptr->SetOptions(options);

                if (title)
                {
                    dialog.ptr->SetTitle(title);
                }

                if (!pickFolders)
                {
                    constexpr COMDLG_FILTERSPEC kSceneSpecs[] = {
                        {L"MQEngine Scene (*.mqscene)", L"*.mqscene"},
                        {L"All Files (*.*)", L"*.*"}
                    };
                    dialog.ptr->SetFileTypes(2, kSceneSpecs);
                    dialog.ptr->SetFileTypeIndex(1);
                    dialog.ptr->SetDefaultExtension(L"mqscene");
                    if (isSaveDialog && !defaultFileName.empty())
                    {
                        dialog.ptr->SetFileName(toWideUtf8(defaultFileName).c_str());
                    }
                }

                applyDialogInitialDir(dialog.ptr, initialDir);

                const HWND ownerHwnd = GetActiveWindow();
                const HRESULT showHr = dialog.ptr->Show(ownerHwnd);
                if (showHr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                {
                    return CancelledError("已取消选择");
                }
                if (FAILED(showHr))
                {
                    return FailedPreconditionError("系统文件对话框打开失败，请直接输入路径");
                }

                ComPtrGuard<IShellItem> resultItem;
                if (FAILED(dialog.ptr->GetResult(&resultItem.ptr)) || !resultItem.ptr)
                {
                    return InternalError("未能获取选中的路径");
                }

                PWSTR rawPath = nullptr;
                if (FAILED(resultItem.ptr->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || !rawPath)
                {
                    return InternalError("未能解析选中路径的系统文件名");
                }
                const std::string chosen = normalizeForwardSlash(fromWideUtf8(rawPath));
                CoTaskMemFree(rawPath);
                return chosen;
            }
#endif
        } // namespace

        std::string ProjectLauncherService::resolveTemplatesRootDir()
        {
            const std::string engineRoot = ProjectIdeConfigGenerator::resolveEngineRootDir();
            if (!engineRoot.empty())
            {
                const std::string cand = normalizeForwardSlash(SAI::Path::join(engineRoot, "Templates"));
                if (SAI::FileSystem::isDirectory(cand))
                {
                    return cand;
                }
            }

            const auto hasTemplatesDir = [](const std::string& dir) {
                return SAI::FileSystem::isDirectory(SAI::Path::join(dir, "Templates"));
            };

            const std::string exeDir = SAI::System::getExecutableDirectory();
            if (!exeDir.empty())
            {
                const std::string base = SAI::FileSystem::findDirectoryUpwardsWhere(exeDir, hasTemplatesDir, 8);
                if (!base.empty())
                {
                    return normalizeForwardSlash(SAI::Path::join(base, "Templates"));
                }
            }

            const std::string cwd = SAI::System::getCurrentDirectory();
            const std::string fromCwd = SAI::FileSystem::findDirectoryUpwardsWhere(cwd, hasTemplatesDir, 8);
            if (!fromCwd.empty())
            {
                return normalizeForwardSlash(SAI::Path::join(fromCwd, "Templates"));
            }

            return {};
        }

        std::string ProjectLauncherService::resolveEditorExecutablePath()
        {
            const std::string currentExe = normalizeForwardSlash(SAI::System::getExecutablePath());
            if (!currentExe.empty() &&
                endsWithIgnoreCase(SAI::Path::fileName(currentExe), "Editor.exe") &&
                SAI::FileSystem::isFile(currentExe))
            {
                return currentExe;
            }

            const std::string exeDir = normalizeForwardSlash(SAI::System::getExecutableDirectory());
            if (!exeDir.empty())
            {
                const std::string siblingEditor = normalizeForwardSlash(SAI::Path::join(exeDir, "Editor.exe"));
                if (SAI::FileSystem::isFile(siblingEditor))
                {
                    return siblingEditor;
                }
            }

            const std::string engineRoot = ProjectIdeConfigGenerator::resolveEngineRootDir();
            const std::string buildDir = ProjectIdeConfigGenerator::resolveEngineBuildDir(engineRoot);
            if (!buildDir.empty())
            {
                const std::string builtEditor = normalizeForwardSlash(
                    SAI::Path::join(buildDir, "Applications/Editor/Editor.exe"));
                if (SAI::FileSystem::isFile(builtEditor))
                {
                    return builtEditor;
                }
            }

            return {};
        }

        std::string ProjectLauncherService::resolveDefaultProjectsParentDir()
        {
            const std::string engineRoot = ProjectIdeConfigGenerator::resolveEngineRootDir();
            if (!engineRoot.empty())
            {
                const std::string parent = normalizeForwardSlash(SAI::Path::parentPath(engineRoot));
                if (!parent.empty() && SAI::FileSystem::isDirectory(parent))
                {
                    return parent;
                }
            }
            const std::string cwd = normalizeForwardSlash(SAI::System::getCurrentDirectory());
            return cwd.empty() ? std::string("G:/MQEngine") : cwd;
        }

        std::string ProjectLauncherService::getDefaultRecentProjectsPath()
        {
#ifdef _WIN32
            const std::string localAppData = normalizeForwardSlash(getEnvVar("LOCALAPPDATA"));
            if (!localAppData.empty())
            {
                return normalizeForwardSlash(SAI::Path::join(localAppData, "MQEngine/RecentProjects.toml"));
            }
#endif
            const std::string engineRoot = ProjectIdeConfigGenerator::resolveEngineRootDir();
            const std::string buildDir = ProjectIdeConfigGenerator::resolveEngineBuildDir(engineRoot);
            if (!buildDir.empty())
            {
                return normalizeForwardSlash(SAI::Path::join(buildDir, "Applications/Editor/Saved/RecentProjects.toml"));
            }
            return "Saved/RecentProjects.toml";
        }

        bool ProjectLauncherService::isValidProjectDir(const std::string& projectDir, ResourceLoader* explicitLoader)
        {
            const std::string normDir = normalizeForwardSlash(projectDir);
            if (normDir.empty() || !SAI::FileSystem::isDirectory(normDir))
            {
                return false;
            }
            ResourceLoader* loader = resolveLoader(explicitLoader);
            const std::string projectMq = SAI::Path::join(normDir, "project.mq");
            return loader ? loader->hasFile(projectMq) : SAI::FileSystem::isFile(projectMq);
        }

        bool ProjectLauncherService::readProjectMetadata(
            const std::string& projectDir,
            std::string& outName,
            std::string& outStartupScene,
            std::string& outDescription,
            ResourceLoader* explicitLoader)
        {
            outName.clear();
            outStartupScene.clear();
            outDescription.clear();

            const std::string normDir = normalizeForwardSlash(projectDir);
            ResourceLoader* loader = resolveLoader(explicitLoader);
            const std::string projectMqPath = SAI::Path::join(normDir, "project.mq");
            if (!loader || !loader->hasFile(projectMqPath))
            {
                return false;
            }

            auto contentOr = loader->loadFile(projectMqPath);
            if (!contentOr.ok())
            {
                return false;
            }

            std::string currentSection;
            std::string fallbackInitialScene;
            const std::string& content = contentOr.value();
            size_t lineStart = 0;
            while (lineStart < content.size())
            {
                size_t lineEnd = content.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                {
                    lineEnd = content.size();
                }
                const std::string_view line(content.data() + lineStart, lineEnd - lineStart);
                lineStart = lineEnd + 1;

                const std::string trimmed = trimAscii(stripTomlInlineComment(line));
                if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
                {
                    currentSection = toLowerAscii(trimAscii(std::string_view(trimmed.data() + 1, trimmed.size() - 2)));
                    continue;
                }
                if (!currentSection.empty())
                {
                    continue;
                }

                std::string key;
                std::string val;
                if (!parseTomlAssignment(line, key, val))
                {
                    continue;
                }
                if (key == "name" && outName.empty())
                {
                    outName = val;
                }
                else if (key == "startup_scene" || key == "startupScene")
                {
                    outStartupScene = normalizeForwardSlash(val);
                }
                else if ((key == "initialSceneUuid" || key == "initialScene" || key == "initial_scene") &&
                         fallbackInitialScene.empty())
                {
                    fallbackInitialScene = normalizeForwardSlash(val);
                }
                else if (key == "description" && outDescription.empty())
                {
                    outDescription = val;
                }
            }

            if (outStartupScene.empty())
            {
                outStartupScene = fallbackInitialScene;
            }
            if (outName.empty())
            {
                outName = SAI::Path::fileName(normDir);
            }
            return true;
        }

        std::vector<ProjectTemplateInfo> ProjectLauncherService::discoverTemplates(
            ResourceLoader* explicitLoader,
            const std::string& templatesRootOverride)
        {
            ZoneScoped();
            std::vector<ProjectTemplateInfo> templates;
            ResourceLoader* loader = resolveLoader(explicitLoader);
            const std::string rootDir = templatesRootOverride.empty()
                ? resolveTemplatesRootDir()
                : normalizeForwardSlash(templatesRootOverride);
            if (rootDir.empty() || !SAI::FileSystem::isDirectory(rootDir))
            {
                return templates;
            }

            for (const std::string& subDir : SAI::FileSystem::listSubDirectories(rootDir))
            {
                const std::string normSub = normalizeForwardSlash(subDir);
                if (!isValidProjectDir(normSub, loader))
                {
                    continue;
                }

                ProjectTemplateInfo info;
                info.id = SAI::Path::fileName(normSub);
                info.templateDir = normSub;
                readProjectMetadata(normSub, info.displayName, info.startupScene, info.description, loader);
                info.hasCMakeLists = loader
                    ? loader->hasFile(SAI::Path::join(normSub, "CMakeLists.txt"))
                    : SAI::FileSystem::isFile(SAI::Path::join(normSub, "CMakeLists.txt"));
                templates.push_back(std::move(info));
            }

            auto rankOf = [](const std::string& id) {
                const std::string lower = toLowerAscii(id);
                if (lower == "blank") return 0;
                if (lower == "pbrshowcase") return 1;
                return 2;
            };
            std::sort(templates.begin(), templates.end(), [&](const ProjectTemplateInfo& a, const ProjectTemplateInfo& b) {
                const int ra = rankOf(a.id);
                const int rb = rankOf(b.id);
                if (ra != rb)
                {
                    return ra < rb;
                }
                return toLowerAscii(a.id) < toLowerAscii(b.id);
            });
            return templates;
        }

        std::vector<ProjectSceneFileEntry> ProjectLauncherService::discoverProjectScenes(
            const std::string& projectDir,
            ResourceLoader* explicitLoader)
        {
            ZoneScoped();
            std::vector<ProjectSceneFileEntry> scenes;
            const std::string normProjDir = normalizeForwardSlash(projectDir);
            if (normProjDir.empty() || !SAI::FileSystem::isDirectory(normProjDir))
            {
                return scenes;
            }

            std::string projName;
            std::string mainScene;
            std::string desc;
            readProjectMetadata(normProjDir, projName, mainScene, desc, explicitLoader);

            collectSceneFilesRecursive(normProjDir, normProjDir, 0, mainScene, scenes);
            std::sort(scenes.begin(), scenes.end(), [](const ProjectSceneFileEntry& a, const ProjectSceneFileEntry& b) {
                if (a.isMainScene != b.isMainScene)
                {
                    return a.isMainScene;
                }
                return toLowerAscii(a.displayRelPath) < toLowerAscii(b.displayRelPath);
            });
            return scenes;
        }

        std::vector<RecentProjectEntry> ProjectLauncherService::discoverWorkspaceProjects(ResourceLoader* explicitLoader)
        {
            std::vector<RecentProjectEntry> discovered;
            ResourceLoader* loader = resolveLoader(explicitLoader);

            const std::string engineRoot = ProjectIdeConfigGenerator::resolveEngineRootDir();
            if (engineRoot.empty())
            {
                return discovered;
            }

            const std::string workspaceParent = normalizeForwardSlash(SAI::Path::parentPath(engineRoot));
            if (workspaceParent.empty() || !SAI::FileSystem::isDirectory(workspaceParent))
            {
                return discovered;
            }

            for (const std::string& subDir : SAI::FileSystem::listSubDirectories(workspaceParent))
            {
                const std::string normSub = normalizeForwardSlash(subDir);
                if (equalsIgnoreCasePath(normSub, engineRoot) || !isValidProjectDir(normSub, loader))
                {
                    continue;
                }

                RecentProjectEntry entry;
                entry.projectDir = normSub;
                entry.existsOnDisk = true;
                std::string desc;
                readProjectMetadata(normSub, entry.name, entry.startupScene, desc, loader);
                discovered.push_back(std::move(entry));
            }
            return discovered;
        }

        StatusOr<std::string> ProjectLauncherService::pickProjectDirectoryDialog(const std::string& initialDir)
        {
#ifdef _WIN32
            const std::string startDir = initialDir.empty() ? resolveDefaultProjectsParentDir() : initialDir;
            return runWindowsFileDialog(
                false,
                true,
                startDir,
                {},
                L"选择 MQEngine 工程目录（需包含 project.mq）");
#else
            (void)initialDir;
            return UnimplementedError("当前平台未实现系统目录选择对话框");
#endif
        }

        StatusOr<std::string> ProjectLauncherService::pickOpenSceneFileDialog(const std::string& initialDir)
        {
#ifdef _WIN32
            return runWindowsFileDialog(
                false,
                false,
                initialDir,
                {},
                L"打开场景文件 (.mqscene)");
#else
            (void)initialDir;
            return UnimplementedError("当前平台未实现系统文件选择对话框");
#endif
        }

        StatusOr<std::string> ProjectLauncherService::pickSaveSceneFileDialog(
            const std::string& initialDir,
            const std::string& defaultFileName)
        {
#ifdef _WIN32
            return runWindowsFileDialog(
                true,
                false,
                initialDir,
                defaultFileName,
                L"场景另存为 (.mqscene)");
#else
            (void)initialDir;
            (void)defaultFileName;
            return UnimplementedError("当前平台未实现系统保存文件对话框");
#endif
        }

        Status ProjectLauncherService::launchEditorProcess(const std::string& projectDir, ResourceLoader* explicitLoader)
        {
            ZoneScoped();
            const std::string normProjDir = normalizeForwardSlash(projectDir);
            if (!isValidProjectDir(normProjDir, explicitLoader))
            {
                return InvalidArgumentError(StrCat("无效的工程目录（缺少 project.mq）: ", projectDir));
            }

            const std::string editorExe = resolveEditorExecutablePath();
            if (editorExe.empty() || !SAI::FileSystem::isFile(editorExe))
            {
                return NotFoundError("未能定位 Editor.exe，请确认已完成 Applications/Editor 编译部署");
            }

            const std::string editorExeDir = normalizeForwardSlash(SAI::Path::parentPath(editorExe));
#ifdef _WIN32
            const std::wstring exePathW = toWindowsNativePathW(editorExe);
            const std::wstring workDirW = toWindowsNativePathW(editorExeDir);
            const std::wstring cmdLineW = toWideUtf8(
                StrCat("\"", editorExe, "\" --project=\"", normProjDir, "\""));

            std::vector<wchar_t> mutableCmd(cmdLineW.begin(), cmdLineW.end());
            mutableCmd.push_back(L'\0');

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_SHOWNORMAL;

            PROCESS_INFORMATION pi{};
            // 以 Editor.exe 所在目录作为子进程工作目录，确保引擎 DLL 与 Composite 搜索路径稳定；
            // 使用 CREATE_NO_WINDOW 抑制控制台弹窗同时保留 GUI 窗口正常显示。
            const BOOL ok = CreateProcessW(
                exePathW.c_str(),
                mutableCmd.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                nullptr,
                workDirW.empty() ? nullptr : workDirW.c_str(),
                &si,
                &pi);
            if (!ok)
            {
                const DWORD err = GetLastError();
                return InternalError(StrCat("启动 Editor.exe 失败，Windows 错误码: ", err));
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return OkStatus();
#else
            return UnimplementedError("当前平台未实现 Editor 异步拉起");
#endif
        }

        Status ProjectLauncherService::loadRecentProjects(
            ResourceLoader* explicitLoader,
            const std::string& customStoragePath)
        {
            ZoneScoped();
            m_recentProjects.clear();
            ResourceLoader* loader = resolveLoader(explicitLoader);
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader 尚未初始化");
            }

            const std::string path = customStoragePath.empty()
                ? getDefaultRecentProjectsPath()
                : normalizeForwardSlash(customStoragePath);
            if (!loader->hasFile(path))
            {
                return OkStatus();
            }

            auto contentOr = loader->loadFile(path);
            CHECK_STATUS(contentOr);

            RecentProjectEntry current;
            bool inEntry = false;
            auto flushCurrent = [&]() {
                if (inEntry && !current.projectDir.empty())
                {
                    current.projectDir = normalizeForwardSlash(current.projectDir);
                    current.existsOnDisk = isValidProjectDir(current.projectDir, loader);
                    if (current.name.empty())
                    {
                        current.name = SAI::Path::fileName(current.projectDir);
                    }
                    m_recentProjects.push_back(current);
                }
                current = RecentProjectEntry{};
                inEntry = false;
            };

            const std::string& content = contentOr.value();
            size_t lineStart = 0;
            while (lineStart < content.size())
            {
                size_t lineEnd = content.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                {
                    lineEnd = content.size();
                }
                const std::string_view rawLine(content.data() + lineStart, lineEnd - lineStart);
                lineStart = lineEnd + 1;

                const std::string trimmed = trimAscii(stripTomlInlineComment(rawLine));
                if (trimmed == "[[projects]]")
                {
                    flushCurrent();
                    inEntry = true;
                    continue;
                }
                if (!inEntry)
                {
                    continue;
                }

                std::string key;
                std::string val;
                if (!parseTomlAssignment(rawLine, key, val))
                {
                    continue;
                }
                if (key == "name")
                {
                    current.name = val;
                }
                else if (key == "dir")
                {
                    current.projectDir = normalizeForwardSlash(val);
                }
                else if (key == "startup_scene")
                {
                    current.startupScene = normalizeForwardSlash(val);
                }
                else if (key == "last_opened")
                {
                    int64_t parsedSec = 0;
                    const auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), parsedSec);
                    if (ec == std::errc{} && ptr == val.data() + val.size())
                    {
                        current.lastOpenedEpochSec = parsedSec;
                    }
                }
            }
            flushCurrent();

            std::sort(m_recentProjects.begin(), m_recentProjects.end(),
                      [](const RecentProjectEntry& a, const RecentProjectEntry& b) {
                          return a.lastOpenedEpochSec > b.lastOpenedEpochSec;
                      });
            return OkStatus();
        }

        Status ProjectLauncherService::saveRecentProjects(
            ResourceLoader* explicitLoader,
            const std::string& customStoragePath) const
        {
            ZoneScoped();
            ResourceLoader* loader = resolveLoader(explicitLoader);
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader 尚未初始化");
            }

            const std::string path = customStoragePath.empty()
                ? getDefaultRecentProjectsPath()
                : normalizeForwardSlash(customStoragePath);
            const std::string parentDir = normalizeForwardSlash(SAI::Path::parentPath(path));
            if (!parentDir.empty())
            {
                CHECK_STATUS(loader->ensureDir(parentDir));
            }

            std::ostringstream oss;
            oss << "# MQEngine 最近打开工程列表\n";
            for (const RecentProjectEntry& entry : m_recentProjects)
            {
                oss << "\n[[projects]]\n";
                oss << "name = \"" << escapeTomlString(entry.name) << "\"\n";
                oss << "dir = \"" << escapeTomlString(entry.projectDir) << "\"\n";
                oss << "startup_scene = \"" << escapeTomlString(entry.startupScene) << "\"\n";
                oss << "last_opened = \"" << entry.lastOpenedEpochSec << "\"\n";
            }
            return loader->saveFile(path, oss.str());
        }

        Status ProjectLauncherService::recordRecentProject(
            const std::string& projectDir,
            ResourceLoader* explicitLoader,
            const std::string& customStoragePath)
        {
            ResourceLoader* loader = resolveLoader(explicitLoader);
            const std::string normDir = normalizeForwardSlash(projectDir);
            if (!isValidProjectDir(normDir, loader))
            {
                return InvalidArgumentError(StrCat("无效工程目录，无法加入最近工程: ", projectDir));
            }

            RecentProjectEntry entry;
            entry.projectDir = normDir;
            entry.lastOpenedEpochSec = currentEpochSeconds();
            entry.existsOnDisk = true;
            std::string desc;
            readProjectMetadata(normDir, entry.name, entry.startupScene, desc, loader);

            m_recentProjects.erase(
                std::remove_if(m_recentProjects.begin(), m_recentProjects.end(), [&](const RecentProjectEntry& existing) {
                    return equalsIgnoreCasePath(existing.projectDir, normDir);
                }),
                m_recentProjects.end());

            m_recentProjects.insert(m_recentProjects.begin(), std::move(entry));
            if (m_recentProjects.size() > kMaxRecentProjects)
            {
                m_recentProjects.resize(kMaxRecentProjects);
            }

            return saveRecentProjects(loader, customStoragePath);
        }

        void ProjectLauncherService::clearRecentProjects(
            ResourceLoader* explicitLoader,
            const std::string& customStoragePath)
        {
            m_recentProjects.clear();
            (void)saveRecentProjects(explicitLoader, customStoragePath);
        }

        void ProjectLauncherService::refreshRecentProjectsStatus(ResourceLoader* explicitLoader)
        {
            ResourceLoader* loader = resolveLoader(explicitLoader);
            for (RecentProjectEntry& entry : m_recentProjects)
            {
                entry.existsOnDisk = isValidProjectDir(entry.projectDir, loader);
            }
        }

        Status ProjectLauncherService::openProject(
            const std::string& projectDir,
            ResourceLoader* explicitLoader,
            const std::string& customStoragePath)
        {
            ZoneScoped();
            ResourceLoader* loader = resolveLoader(explicitLoader);
            const std::string normDir = normalizeForwardSlash(projectDir);
            CHECK_STATUS(launchEditorProcess(normDir, loader));
            (void)recordRecentProject(normDir, loader, customStoragePath);
            return OkStatus();
        }

        StatusOr<CreateProjectFromTemplateResult> ProjectLauncherService::createProjectFromTemplate(
            const CreateProjectFromTemplateRequest& request,
            ResourceLoader* explicitLoader,
            const std::string& customStoragePath)
        {
            ZoneScoped();
            ResourceLoader* loader = resolveLoader(explicitLoader);
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader 尚未初始化，无法创建模板工程");
            }

            const std::string cleanProjectName = sanitizeProjectFolderName(request.projectName);
            if (cleanProjectName.empty())
            {
                return InvalidArgumentError("工程名称不能为空");
            }

            const std::string parentDir = normalizeForwardSlash(request.targetParentDir);
            if (parentDir.empty())
            {
                return InvalidArgumentError("目标父目录不能为空");
            }

            const std::vector<ProjectTemplateInfo> templates =
                discoverTemplates(loader, request.templatesRootDir);
            auto foundIt = std::find_if(templates.begin(), templates.end(), [&](const ProjectTemplateInfo& t) {
                return toLowerAscii(t.id) == toLowerAscii(request.templateId);
            });
            if (foundIt == templates.end())
            {
                return NotFoundError(StrCat("未找到指定的工程模板: ", request.templateId));
            }

            const std::string targetProjectDir = normalizeForwardSlash(SAI::Path::join(parentDir, cleanProjectName));
            if (isValidProjectDir(targetProjectDir, loader))
            {
                return AlreadyExistsError(StrCat("目标目录下已存在同名工程: ", targetProjectDir));
            }

            size_t copiedFiles = 0;
            CHECK_STATUS(copyTemplateDirectoryRecursive(foundIt->templateDir, targetProjectDir, loader, copiedFiles));

            // 同步更新新工程 project.mq 中的工程名与打包元数据
            const std::string newProjectMqPath = SAI::Path::join(targetProjectDir, "project.mq");
            if (loader->hasFile(newProjectMqPath))
            {
                auto mqOr = loader->loadFile(newProjectMqPath);
                CHECK_STATUS(mqOr);
                const std::string updatedMq = updateProjectMqMetadata(
                    mqOr.value(),
                    foundIt->displayName,
                    foundIt->id,
                    cleanProjectName);
                CHECK_STATUS(loader->saveFile(newProjectMqPath, updatedMq));
            }

            // 若模板包含 CMakeLists.txt，同步更新 project(...) 工程名并生成本机 IDE 构建配置
            const std::string newCMakePath = SAI::Path::join(targetProjectDir, "CMakeLists.txt");
            if (loader->hasFile(newCMakePath))
            {
                auto cmakeOr = loader->loadFile(newCMakePath);
                CHECK_STATUS(cmakeOr);
                const std::string updatedCMake = updateCMakeProjectName(cmakeOr.value(), cleanProjectName);
                CHECK_STATUS(loader->saveFile(newCMakePath, updatedCMake));
            }

            bool ideSynced = false;
            if (request.syncIdeFiles && ProjectIdeConfigGenerator::isEngineCppProject(targetProjectDir, loader))
            {
                auto syncOr = ProjectIdeConfigGenerator::syncProjectIdeFiles(targetProjectDir, loader);
                if (syncOr.ok())
                {
                    ideSynced = true;
                }
            }

            (void)recordRecentProject(targetProjectDir, loader, customStoragePath);

            bool launched = false;
            if (request.openAfterCreate)
            {
                Status launchSt = launchEditorProcess(targetProjectDir, loader);
                if (!launchSt.ok())
                {
                    return launchSt;
                }
                launched = true;
            }

            CreateProjectFromTemplateResult result;
            result.projectDir = targetProjectDir;
            result.projectName = cleanProjectName;
            result.copiedFilesCount = copiedFiles;
            result.ideSynced = ideSynced;
            result.launchedEditor = launched;
            return result;
        }
    } // namespace EditorBase
} // namespace MQEngine
