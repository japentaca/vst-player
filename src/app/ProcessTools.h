#pragma once

#include <string>

namespace vstplayer::processtools
{

// Small Win32 helpers for the self-tests (plain std::wstring, no JUCE).

// Full path of the first running process whose executable name matches (case-insensitive),
// empty if none.
std::wstring findProcessPath (const std::wstring& exeName);

// Terminates every process with that executable name and waits for them to exit.
// Returns how many were terminated.
int killProcesses (const std::wstring& exeName, unsigned int timeoutMs = 10000);

// Starts a program minimized and without activating it (does not wait).
bool launchMinimized (const std::wstring& exePath);

} // namespace vstplayer::processtools
