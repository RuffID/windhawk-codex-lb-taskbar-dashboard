#pragma once

#include "state.h"

namespace codex_dashboard {

HANDLE OpenLogFile() noexcept {
    HANDLE file = CreateFile(LOG_FILE_PATH, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE |
                                 FILE_SHARE_DELETE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        return file;
    }

    WCHAR localAppData[MAX_PATH];
    DWORD localAppDataLength =
        GetEnvironmentVariable(L"LOCALAPPDATA", localAppData,
                               ARRAYSIZE(localAppData));
    if (localAppDataLength == 0 || localAppDataLength >= ARRAYSIZE(localAppData)) {
        return INVALID_HANDLE_VALUE;
    }

    WCHAR directory[MAX_PATH];
    if (swprintf_s(directory, L"%s\\%s", localAppData,
                   FALLBACK_LOG_DIR_NAME) <= 0) {
        return INVALID_HANDLE_VALUE;
    }

    CreateDirectory(directory, nullptr);

    WCHAR fallbackPath[MAX_PATH];
    if (swprintf_s(fallbackPath, L"%s\\%s", directory,
                   FALLBACK_LOG_FILE_NAME) <= 0) {
        return INVALID_HANDLE_VALUE;
    }

    return CreateFile(fallbackPath, FILE_APPEND_DATA,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void AppendLogFileLine(PCWSTR area, HRESULT hr) noexcept {
    SYSTEMTIME time;
    GetLocalTime(&time);

    wchar_t line[512];
    int lineLength = swprintf_s(
        line,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%u tid=%u %s hr=%08X\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, GetCurrentProcessId(),
        GetCurrentThreadId(), area ? area : L"unknown", hr);
    if (lineLength <= 0) {
        return;
    }

    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, line, lineLength, nullptr,
                                         0, nullptr, nullptr);
    if (utf8Length <= 0) {
        return;
    }

    char utf8Line[1024];
    if (utf8Length > static_cast<int>(sizeof(utf8Line))) {
        return;
    }

    if (!WideCharToMultiByte(CP_UTF8, 0, line, lineLength, utf8Line,
                             utf8Length, nullptr, nullptr)) {
        return;
    }

    HANDLE file = OpenLogFile();
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, utf8Line, static_cast<DWORD>(utf8Length), &written,
              nullptr);
    CloseHandle(file);
}

void LogException(PCWSTR area, HRESULT hr) noexcept {
    Wh_Log(L"%s exception: %08X", area ? area : L"unknown", hr);
    AppendLogFileLine(area, hr);
}

HRESULT CurrentExceptionHResult() noexcept {
    try {
        return winrt::to_hresult();
    } catch (...) {
        return E_FAIL;
    }
}

}  // namespace codex_dashboard
