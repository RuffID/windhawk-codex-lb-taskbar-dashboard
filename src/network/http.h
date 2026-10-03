#pragma once

#include "../core/state.h"
#include "../core/text_json.h"

namespace codex_dashboard {

struct WinHttpHandle {
    HINTERNET handle = nullptr;
    HANDLE closingEvent = nullptr;

    WinHttpHandle() = default;

    explicit WinHttpHandle(HINTERNET handle) : handle(handle) {}

    ~WinHttpHandle() {
        if (handle) {
            WinHttpCloseHandle(handle);
            if (closingEvent) {
                // The async context and read buffer must outlive the final callback.
                WaitForSingleObject(closingEvent, INFINITE);
            }
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    explicit operator bool() const {
        return handle != nullptr;
    }
};

struct EventHandle {
    HANDLE handle = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    EventHandle() {
        if (!handle) {
            winrt::throw_last_error();
        }
    }

    ~EventHandle() {
        CloseHandle(handle);
    }

    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
};

bool IsDashboardRefreshCancelled() {
    return g_unloading || g_dashboardUpdatesPaused ||
           WaitForSingleObject(g_dashboardCancelEvent, 0) != WAIT_TIMEOUT;
}

struct AsyncHttpState {
    EventHandle completed;
    EventHandle closed;
    std::atomic<DWORD> status;
    std::atomic<DWORD> error;
    std::atomic<DWORD> bytesRead;
    char readBuffer[8192];

    bool Prepare() {
        if (IsDashboardRefreshCancelled()) {
            return false;
        }
        status = 0;
        error = 0;
        bytesRead = 0;
        if (!ResetEvent(completed.handle)) {
            winrt::throw_last_error();
        }
        return true;
    }

    bool Wait(DWORD expectedStatus, ULONGLONG deadline) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            Wh_Log(L"WinHTTP request deadline exceeded");
            return false;
        }
        HANDLE events[]{g_dashboardCancelEvent, completed.handle};
        DWORD result = WaitForMultipleObjects(
            ARRAYSIZE(events), events, FALSE, static_cast<DWORD>(deadline - now));
        if (result == WAIT_OBJECT_0) {
            return false;
        }
        if (result != WAIT_OBJECT_0 + 1) {
            Wh_Log(L"WinHTTP wait failed or timed out: %u", result);
            return false;
        }
        if (status != expectedStatus) {
            Wh_Log(L"WinHTTP async operation failed: status=%u error=%u",
                   status.load(), error.load());
            return false;
        }
        return !IsDashboardRefreshCancelled();
    }
};

void CALLBACK HttpStatusCallback(HINTERNET,
                                DWORD_PTR context,
                                DWORD status,
                                LPVOID information,
                                DWORD informationLength) noexcept {
    auto* state = reinterpret_cast<AsyncHttpState*>(context);
    if (!state) {
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        SetEvent(state->closed.handle);
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        state->error = static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError;
    } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        state->bytesRead = informationLength;
    } else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
               status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
        return;
    }
    state->status = status;
    SetEvent(state->completed.handle);
}

struct HttpResponse {
    DWORD status = 0;
    std::wstring body;
    std::wstring setCookie;
};

std::wstring ExtractDashboardCookie(const std::wstring& setCookieHeaders) {
    constexpr PCWSTR COOKIE_NAME = L"codex_lb_dashboard_session=";
    const std::wstring headers = setCookieHeaders;
    size_t cookieStart = headers.find(COOKIE_NAME);
    if (cookieStart == std::wstring::npos) {
        return {};
    }

    size_t cookieEnd = headers.find_first_of(L";\r\n", cookieStart);
    if (cookieEnd == std::wstring::npos) {
        cookieEnd = headers.size();
    }

    return headers.substr(cookieStart, cookieEnd - cookieStart);
}

bool HttpRequest(const Settings& settings,
                 PCWSTR method,
                 PCWSTR path,
                 const std::wstring& cookie,
                 const std::string& body,
                 HttpResponse& response,
                 const std::wstring& bearerToken = {}) {
    if (IsDashboardRefreshCancelled()) {
        return false;
    }
    std::wstring url = settings.dashboardUrl;
    while (!url.empty() && url.back() == L'/') {
        url.pop_back();
    }
    url += path;
    ULONGLONG deadline = GetTickCount64() +
                         static_cast<ULONGLONG>(settings.requestTimeoutSeconds) * 1000;

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) {
        Wh_Log(L"WinHttpCrackUrl failed: %u", GetLastError());
        return false;
    }

    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring requestPath(components.lpszUrlPath,
                             components.dwUrlPathLength);
    if (components.dwExtraInfoLength) {
        requestPath.append(components.lpszExtraInfo,
                           components.dwExtraInfoLength);
    }

    WinHttpHandle session(WinHttpOpen(L"TaskbarCodexLbWidget/0.2",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session) {
        Wh_Log(L"WinHttpOpen failed: %u", GetLastError());
        return false;
    }

    int timeoutMs =
        static_cast<int>(settings.requestTimeoutSeconds * 1000UL);
    if (!WinHttpSetTimeouts(session.handle, timeoutMs, timeoutMs, timeoutMs,
                           timeoutMs)) {
        Wh_Log(L"WinHttpSetTimeouts failed: %u", GetLastError());
        return false;
    }

    WinHttpHandle connect(
        WinHttpConnect(session.handle, host.c_str(), components.nPort, 0));
    if (!connect) {
        Wh_Log(L"WinHttpConnect failed: %u", GetLastError());
        return false;
    }

    DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS
                      ? WINHTTP_FLAG_SECURE
                      : 0;
    AsyncHttpState asyncState;
    WinHttpHandle request(WinHttpOpenRequest(
        connect.handle, method, requestPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        Wh_Log(L"WinHttpOpenRequest failed: %u", GetLastError());
        return false;
    }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&asyncState);
    if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_CONTEXT_VALUE,
                          &context, sizeof(context))) {
        Wh_Log(L"WinHttpSetOption context failed: %u", GetLastError());
        return false;
    }
    if (WinHttpSetStatusCallback(
            request.handle, HttpStatusCallback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
                WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
                WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
                WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
                WINHTTP_CALLBACK_FLAG_HANDLES,
            0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        Wh_Log(L"WinHttpSetStatusCallback failed: %u", GetLastError());
        return false;
    }
    request.closingEvent = asyncState.closed.handle;

    std::wstring headers;
    if (!cookie.empty()) {
        headers += L"Cookie: ";
        headers += cookie;
        headers += L"\r\n";
    }

    if (!bearerToken.empty()) {
        headers += L"Authorization: Bearer ";
        headers += bearerToken;
        headers += L"\r\n";
    }

    if (!body.empty()) {
        headers += L"Content-Type: application/json\r\n";
    }

    if (!asyncState.Prepare()) {
        return false;
    }
    BOOL sent = WinHttpSendRequest(
        request.handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                        : headers.c_str(),
        headers.empty() ? 0 : static_cast<DWORD>(-1),
        body.empty() ? WINHTTP_NO_REQUEST_DATA
                     : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), context);
    if (!sent) {
        Wh_Log(L"WinHttp request failed: %u", GetLastError());
        return false;
    }
    if (!asyncState.Wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline) ||
        !asyncState.Prepare()) {
        return false;
    }
    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
        Wh_Log(L"WinHttpReceiveResponse failed: %u", GetLastError());
        return false;
    }
    if (!asyncState.Wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline)) {
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle,
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status,
                             &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        Wh_Log(L"WinHttpQueryHeaders status failed: %u", GetLastError());
        return false;
    }

    DWORD cookieHeaderSize = 0;
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_SET_COOKIE,
                             WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                             &cookieHeaderSize, WINHTTP_NO_HEADER_INDEX) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        std::wstring cookieHeaders(cookieHeaderSize / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_SET_COOKIE,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                cookieHeaders.data(), &cookieHeaderSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            cookieHeaders.resize(cookieHeaderSize / sizeof(wchar_t));
            response.setCookie = ExtractDashboardCookie(cookieHeaders);
        }
    }

    std::string responseBytes;
    for (;;) {
        if (!asyncState.Prepare()) {
            return false;
        }
        if (!WinHttpReadData(request.handle, asyncState.readBuffer,
                             sizeof(asyncState.readBuffer), nullptr)) {
            Wh_Log(L"WinHttpReadData failed: %u", GetLastError());
            return false;
        }
        if (!asyncState.Wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline)) {
            return false;
        }
        DWORD read = asyncState.bytesRead;
        if (read == 0) {
            break;
        }
        responseBytes.append(asyncState.readBuffer, read);
    }

    response.status = status;
    response.body = Utf8ToWide(responseBytes);
    return true;
}

}  // namespace codex_dashboard
