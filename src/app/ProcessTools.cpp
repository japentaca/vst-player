#include "app/ProcessTools.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <vector>

namespace vstplayer::processtools
{

namespace
{
std::vector<DWORD> findProcessIds (const std::wstring& exeName)
{
    std::vector<DWORD> ids;
    const auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
        return ids;

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof (entry);

    for (auto ok = Process32FirstW (snapshot, &entry); ok; ok = Process32NextW (snapshot, &entry))
        if (_wcsicmp (entry.szExeFile, exeName.c_str()) == 0)
            ids.push_back (entry.th32ProcessID);

    CloseHandle (snapshot);
    return ids;
}
} // namespace

std::wstring findProcessPath (const std::wstring& exeName)
{
    for (auto id : findProcessIds (exeName))
    {
        if (auto process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id))
        {
            wchar_t buffer[MAX_PATH * 2] = {};
            DWORD size = (DWORD) (sizeof (buffer) / sizeof (buffer[0]));
            const auto ok = QueryFullProcessImageNameW (process, 0, buffer, &size);
            CloseHandle (process);

            if (ok)
                return std::wstring (buffer, size);
        }
    }

    return {};
}

int killProcesses (const std::wstring& exeName, unsigned int timeoutMs)
{
    int killed = 0;

    for (auto id : findProcessIds (exeName))
    {
        if (auto process = OpenProcess (PROCESS_TERMINATE | SYNCHRONIZE, FALSE, id))
        {
            if (TerminateProcess (process, 1))
            {
                WaitForSingleObject (process, timeoutMs);
                ++killed;
            }

            CloseHandle (process);
        }
    }

    return killed;
}

bool launchMinimized (const std::wstring& exePath)
{
    STARTUPINFOW startup {};
    startup.cb = sizeof (startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWMINNOACTIVE;

    PROCESS_INFORMATION info {};
    std::wstring commandLine = L"\"" + exePath + L"\"";
    const auto slash = exePath.find_last_of (L"\\/");
    const auto directory = slash != std::wstring::npos ? exePath.substr (0, slash) : std::wstring();

    if (! CreateProcessW (exePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                          directory.empty() ? nullptr : directory.c_str(), &startup, &info))
        return false;

    CloseHandle (info.hThread);
    CloseHandle (info.hProcess);
    return true;
}

} // namespace vstplayer::processtools
