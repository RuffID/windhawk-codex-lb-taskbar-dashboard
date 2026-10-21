#pragma once

#include "semver.h"
#include "../core/state.h"

namespace codex_dashboard {

struct ReleaseCacheData {
    ULONGLONG nextCheckUtc = 0;
    ReleaseCheckStatus status = ReleaseCheckStatus::Pending;
    std::wstring latest;
};

ULONGLONG ReleaseNowUtc() {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    return (ticks.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

bool ReserveReleaseCheck(ReleaseCacheData& cache, ULONGLONG now) {
    if (now < cache.nextCheckUtc) {
        return false;
    }
    cache.nextCheckUtc = now + RELEASE_CHECK_INTERVAL_SECONDS;
    cache.status = ReleaseCheckStatus::Pending;
    cache.latest.clear();
    return true;
}

struct ReleaseCacheHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ReleaseCacheHandle() = default;
    ReleaseCacheHandle(const ReleaseCacheHandle&) = delete;
    ReleaseCacheHandle& operator=(const ReleaseCacheHandle&) = delete;
    ~ReleaseCacheHandle() {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

std::wstring GetReleaseCachePath() {
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!size) {
        winrt::throw_last_error();
    }
    std::wstring path(size, L'\0');
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path.data(), size);
    if (!length || length >= size) {
        winrt::throw_hresult(E_FAIL);
    }
    path.resize(length);
    path += L"\\CodexLbTaskbarDashboard";
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        winrt::throw_last_error();
    }
    return path + L"\\github-releases-v1.json";
}

std::wstring GetReleaseCacheMutexName(std::wstring path) {
    // A global mutex also covers multiple sessions of the same Windows user.
    CharLowerBuffW(path.data(), static_cast<DWORD>(path.size()));
    ULONGLONG hash = 14695981039346656037ULL;
    for (wchar_t ch : path) {
        hash = (hash ^ static_cast<ULONGLONG>(ch)) * 1099511628211ULL;
    }
    return L"Global\\CodexLbTaskbarDashboard.GitHubReleases.v1." + std::to_wstring(hash);
}

struct SharedReleaseCache {
    ReleaseCacheHandle mutex;
    bool locked = false;
    std::wstring path;

    ~SharedReleaseCache() {
        if (locked) {
            ReleaseMutex(mutex.value);
        }
    }

    bool Open(const std::wstring& cachePath, HANDLE cancelEvent) {
        path = cachePath;
        std::wstring name = GetReleaseCacheMutexName(path);
        mutex.value = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex.value) {
            winrt::throw_last_error();
        }
        HANDLE events[]{cancelEvent, mutex.value};
        DWORD result = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, 15000);
        if (result == WAIT_OBJECT_0) {
            return false;
        }
        locked = result == WAIT_OBJECT_0 + 1 || result == WAIT_ABANDONED_0 + 1;
        if (!locked) {
            if (result == WAIT_FAILED) {
                winrt::throw_last_error();
            }
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        }
        return true;
    }

    ReleaseCacheData Read() const {
        ReleaseCacheHandle file;
        file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                return {};
            }
            winrt::throw_last_error();
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.value, &size)) {
            winrt::throw_last_error();
        }
        if (size.QuadPart <= 0 || size.QuadPart > 65536) {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) {
            winrt::throw_last_error();
        }
        if (read != bytes.size()) {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }
        auto json = wdj::JsonObject::Parse(Utf8ToWide(bytes));
        double next = json.GetNamedNumber(L"nextCheckUtc");
        double status = json.GetNamedNumber(L"status");
        if (json.GetNamedNumber(L"schema") != 1 || !std::isfinite(next) || next < 0 ||
            next > 253402300799.0 || std::floor(next) != next ||
            !std::isfinite(status) || status < 0 || status > 3 || std::floor(status) != status) {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }
        ReleaseCacheData data{static_cast<ULONGLONG>(next),
                              static_cast<ReleaseCheckStatus>(static_cast<int>(status)),
                              std::wstring(json.GetNamedString(L"latest"))};
        ReleaseVersion version;
        if ((data.status == ReleaseCheckStatus::Succeeded && !ParseReleaseVersion(data.latest, version)) ||
            (data.status != ReleaseCheckStatus::Succeeded && !data.latest.empty())) {
            winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }
        return data;
    }

    void Write(const ReleaseCacheData& data) const {
        wdj::JsonObject json;
        json.SetNamedValue(L"schema", wdj::JsonValue::CreateNumberValue(1));
        json.SetNamedValue(L"nextCheckUtc", wdj::JsonValue::CreateNumberValue(static_cast<double>(data.nextCheckUtc)));
        json.SetNamedValue(L"status", wdj::JsonValue::CreateNumberValue(static_cast<int>(data.status)));
        json.SetNamedValue(L"latest", wdj::JsonValue::CreateStringValue(data.latest));
        std::string bytes = WideToUtf8(std::wstring(json.Stringify()));
        std::wstring temporary = path + L".tmp";
        {
            ReleaseCacheHandle file;
            file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file.value == INVALID_HANDLE_VALUE) {
                winrt::throw_last_error();
            }
            DWORD written = 0;
            if (!WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) {
                winrt::throw_last_error();
            }
            if (written != bytes.size()) {
                winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
            }
            if (!FlushFileBuffers(file.value)) {
                winrt::throw_last_error();
            }
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            winrt::throw_last_error();
        }
    }
};

}  // namespace codex_dashboard
