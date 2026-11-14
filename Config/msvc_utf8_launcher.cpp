#include <windows.h>
#include <cstring>
#include <string>
#include <string_view>

namespace
{
    // GBK 编码下的 "注意: 包含文件:"
    constexpr unsigned char kGbkShowIncludesPrefix[] = {
        0xD7, 0xA2, 0xD2, 0xE2, ':', ' ', 0xB0, 0xFC, 0xBA, 0xAC, 0xCE, 0xC4, 0xBC, 0xFE, ':'
    };

    // UTF-8 编码下的 "注意: 包含文件:"
    constexpr unsigned char kUtf8ShowIncludesPrefix[] = {
        0xE6, 0xB3, 0xA8, 0xE6, 0x84, 0x8F, ':', ' ',
        0xE5, 0x8C, 0x85, 0xE5, 0x90, 0xAB, 0xE6, 0x96, 0x87, 0xE4, 0xBB, 0xB6, ':'
    };

    bool startsWithBytes(std::string_view line, const unsigned char* prefix, size_t prefixLen)
    {
        if (line.size() < prefixLen)
        {
            return false;
        }
        return std::memcmp(line.data(), prefix, prefixLen) == 0;
    }

    bool containsBytes(std::string_view hay, const unsigned char* needle, size_t needleLen)
    {
        if (hay.size() < needleLen)
        {
            return false;
        }
        for (size_t i = 0; i <= hay.size() - needleLen; ++i)
        {
            if (std::memcmp(hay.data() + i, needle, needleLen) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // 检查当前构建目录的 CMakeFiles/rules.ninja 中 msvc_deps_prefix 是否为 GBK 编码
    bool shouldUseGbkShowIncludes()
    {
        HANDLE hFile = CreateFileW(
            L"CMakeFiles\\rules.ninja",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        char buf[1024];
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr);
        CloseHandle(hFile);
        if (!ok || bytesRead == 0)
        {
            return false;
        }
        std::string_view header(buf, bytesRead);
        return containsBytes(header, kGbkShowIncludesPrefix, sizeof(kGbkShowIncludesPrefix));
    }

    bool isPureAscii(std::string_view str)
    {
        for (unsigned char c : str)
        {
            if (c >= 0x80)
            {
                return false;
            }
        }
        return true;
    }

    bool isValidUtf8(std::string_view str)
    {
        if (str.empty())
        {
            return true;
        }
        return MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), static_cast<int>(str.size()), nullptr, 0) > 0;
    }

    std::string convertCodePage(std::string_view input, UINT fromCp, UINT toCp)
    {
        if (input.empty())
        {
            return {};
        }
        int wideLen = MultiByteToWideChar(
            fromCp, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
        if (wideLen <= 0)
        {
            return std::string(input);
        }
        std::wstring wideStr(static_cast<size_t>(wideLen), L'\0');
        MultiByteToWideChar(
            fromCp, 0, input.data(), static_cast<int>(input.size()), wideStr.data(), wideLen);

        int outLen = WideCharToMultiByte(
            toCp, 0, wideStr.data(), wideLen, nullptr, 0, nullptr, nullptr);
        if (outLen <= 0)
        {
            return std::string(input);
        }
        std::string outStr(static_cast<size_t>(outLen), '\0');
        WideCharToMultiByte(
            toCp, 0, wideStr.data(), wideLen, outStr.data(), outLen, nullptr, nullptr);
        return outStr;
    }

    void writeRaw(HANDLE hOut, std::string_view data)
    {
        if (data.empty() || hOut == INVALID_HANDLE_VALUE || hOut == nullptr)
        {
            return;
        }
        DWORD written = 0;
        WriteFile(hOut, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    }

    void processLine(HANDLE hOut, std::string_view line, bool useGbkShowIncludes)
    {
        if (line.empty())
        {
            return;
        }
        if (startsWithBytes(line, kGbkShowIncludesPrefix, sizeof(kGbkShowIncludesPrefix)))
        {
            if (useGbkShowIncludes)
            {
                writeRaw(hOut, line);
            }
            else
            {
                std::string utf8Line = convertCodePage(line, CP_ACP, CP_UTF8);
                writeRaw(hOut, utf8Line);
            }
            return;
        }
        if (startsWithBytes(line, kUtf8ShowIncludesPrefix, sizeof(kUtf8ShowIncludesPrefix)))
        {
            if (useGbkShowIncludes)
            {
                std::string gbkLine = convertCodePage(line, CP_UTF8, CP_ACP);
                writeRaw(hOut, gbkLine);
            }
            else
            {
                writeRaw(hOut, line);
            }
            return;
        }
        if (isPureAscii(line) || isValidUtf8(line))
        {
            writeRaw(hOut, line);
            return;
        }
        std::string utf8Line = convertCodePage(line, CP_ACP, CP_UTF8);
        writeRaw(hOut, utf8Line);
    }

    wchar_t* extractChildCommandLine(wchar_t* fullCmd)
    {
        wchar_t* p = fullCmd;
        while (*p == L' ' || *p == L'\t')
        {
            ++p;
        }
        if (*p == L'"')
        {
            ++p;
            while (*p && *p != L'"')
            {
                ++p;
            }
            if (*p == L'"')
            {
                ++p;
            }
        }
        else
        {
            while (*p && *p != L' ' && *p != L'\t')
            {
                ++p;
            }
        }
        while (*p == L' ' || *p == L'\t')
        {
            ++p;
        }
        return p;
    }
} // namespace

int wmain()
{
    wchar_t* childCmd = extractChildCommandLine(GetCommandLineW());
    if (!childCmd || !*childCmd)
    {
        return 0;
    }

    const bool useGbkShowIncludes = shouldUseGbkShowIncludes();

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    {
        return 1;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd(childCmd);
    BOOL ok = CreateProcessW(
        nullptr,
        mutableCmd.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi);
    CloseHandle(hWrite);

    if (!ok)
    {
        CloseHandle(hRead);
        return 1;
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    std::string pending;
    char buf[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0)
    {
        pending.append(buf, bytesRead);
        size_t start = 0;
        for (;;)
        {
            size_t nl = pending.find('\n', start);
            if (nl == std::string::npos)
            {
                break;
            }
            processLine(
                hOut,
                std::string_view(pending.data() + start, nl - start + 1),
                useGbkShowIncludes);
            start = nl + 1;
        }
        if (start > 0)
        {
            pending.erase(0, start);
        }
    }
    if (!pending.empty())
    {
        processLine(hOut, pending, useGbkShowIncludes);
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}
