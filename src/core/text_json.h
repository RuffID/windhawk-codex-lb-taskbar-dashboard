#pragma once

#include "platform.h"
#include "logging.h"

namespace codex_dashboard {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                   static_cast<int>(value.size()), nullptr, 0,
                                   nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                        static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                   static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

bool IsTrimChar(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

std::wstring TrimString(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && IsTrimChar(value[start])) {
        start++;
    }

    size_t end = value.size();
    while (end > start && IsTrimChar(value[end - 1])) {
        end--;
    }

    return value.substr(start, end - start);
}

std::wstring GetEnvironmentVariableString(PCWSTR name) {
    DWORD size = GetEnvironmentVariable(name, nullptr, 0);
    if (size == 0) {
        return {};
    }

    std::wstring value(size, L'\0');
    DWORD copied = GetEnvironmentVariable(name, value.data(), size);
    if (copied == 0 || copied >= size) {
        return {};
    }

    value.resize(copied);
    return value;
}

std::wstring TruncateForLog(const std::wstring& value, size_t maxLength = 400) {
    if (value.size() <= maxLength) {
        return value;
    }

    return value.substr(0, maxLength) + L"...";
}

bool TryGetJsonValue(const wdj::JsonObject& object,
                     PCWSTR name,
                     wdj::IJsonValue& value) {
    if (!object || !object.HasKey(name)) {
        return false;
    }

    try {
        value = object.Lookup(name);
        return true;
    } catch (...) {
        HRESULT hr = CurrentExceptionHResult();
        Wh_Log(L"JSON field read failed for %s: %08X", name, hr);
        AppendLogFileLine(L"TryGetJsonValue", hr);
        return false;
    }
}

bool TryGetJsonString(const wdj::JsonObject& object,
                      PCWSTR name,
                      std::wstring& value) {
    wdj::IJsonValue jsonValue = nullptr;
    if (!TryGetJsonValue(object, name, jsonValue) ||
        jsonValue.ValueType() != wdj::JsonValueType::String) {
        return false;
    }

    value = jsonValue.GetString().c_str();
    return true;
}

bool TryGetJsonNumber(const wdj::JsonObject& object,
                      PCWSTR name,
                      double& value) {
    wdj::IJsonValue jsonValue = nullptr;
    if (!TryGetJsonValue(object, name, jsonValue) ||
        jsonValue.ValueType() != wdj::JsonValueType::Number) {
        return false;
    }

    value = jsonValue.GetNumber();
    return true;
}

bool TryGetJsonBoolean(const wdj::JsonObject& object,
                       PCWSTR name,
                       bool& value) {
    wdj::IJsonValue jsonValue = nullptr;
    if (!TryGetJsonValue(object, name, jsonValue) ||
        jsonValue.ValueType() != wdj::JsonValueType::Boolean) {
        return false;
    }

    value = jsonValue.GetBoolean();
    return true;
}

bool TryGetJsonObject(const wdj::JsonObject& object,
                      PCWSTR name,
                      wdj::JsonObject& value) {
    wdj::IJsonValue jsonValue = nullptr;
    if (!TryGetJsonValue(object, name, jsonValue) ||
        jsonValue.ValueType() != wdj::JsonValueType::Object) {
        return false;
    }

    value = jsonValue.GetObject();
    return true;
}

std::wstring FormatDashboardAddress(const std::wstring& url) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) ||
        components.dwHostNameLength == 0) {
        return L"Некорректный URL";
    }

    std::wstring address(components.lpszHostName, components.dwHostNameLength);
    if (address.find(L':') != std::wstring::npos && address.front() != L'[') {
        address = L"[" + address + L"]";
    }
    if (components.nPort != INTERNET_DEFAULT_HTTPS_PORT) {
        address += L":" + std::to_wstring(components.nPort);
    }
    return address;
}

}  // namespace codex_dashboard
