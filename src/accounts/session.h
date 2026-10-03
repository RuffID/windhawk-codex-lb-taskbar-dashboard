#pragma once

#include "../network/http.h"
#include "../core/text_json.h"

namespace codex_dashboard {

std::wstring GetDashboardLoginPayload(const std::wstring& username,
                                      const std::wstring& password) {
    std::wstring payload;
    auto appendEscaped = [&payload](const std::wstring& value) {
        for (wchar_t ch : value) {
            switch (ch) {
                case L'\\':
                    payload += L"\\\\";
                    break;
                case L'"':
                    payload += L"\\\"";
                    break;
                case L'\b':
                    payload += L"\\b";
                    break;
                case L'\f':
                    payload += L"\\f";
                    break;
                case L'\n':
                    payload += L"\\n";
                    break;
                case L'\r':
                    payload += L"\\r";
                    break;
                case L'\t':
                    payload += L"\\t";
                    break;
                default:
                    if (ch < 0x20) {
                        wchar_t escaped[8];
                        swprintf_s(escaped, L"\\u%04X", ch);
                        payload += escaped;
                    } else {
                        payload += ch;
                    }
                    break;
            }
        }
    };

    payload = L"{\"username\":\"";
    appendEscaped(username);
    payload += L"\",\"password\":\"";
    appendEscaped(password);
    payload += L"\"}";
    return payload;
}

bool IsSessionActive(const Settings& settings, const std::wstring& cookie) {
    HttpResponse response;
    if (!HttpRequest(settings, L"GET", L"/api/dashboard-auth/session", cookie, {},
                     response)) {
        return false;
    }

    if (response.status == 401 || response.status == 403) {
        return false;
    }

    wdj::JsonObject json = nullptr;
    if (response.status < 200 || response.status >= 300 ||
        !wdj::JsonObject::TryParse(response.body, json)) {
        return false;
    }

    bool authenticated = false;
    TryGetJsonBoolean(json, L"authenticated", authenticated);

    bool passwordSessionActive = false;
    TryGetJsonBoolean(json, L"passwordSessionActive", passwordSessionActive);

    return authenticated || passwordSessionActive;
}

bool LoginDashboard(const Settings& settings, std::wstring& cookie) {
    std::wstring username =
        GetEnvironmentVariableString(settings.usernameEnvName.c_str());
    if (username.empty()) {
        Wh_Log(L"%s is empty", settings.usernameEnvName.c_str());
        return false;
    }

    std::wstring password =
        GetEnvironmentVariableString(settings.passwordEnvName.c_str());
    if (password.empty()) {
        Wh_Log(L"%s is empty", settings.passwordEnvName.c_str());
        return false;
    }

    std::string body =
        WideToUtf8(GetDashboardLoginPayload(username, password));
    HttpResponse response;
    if (!HttpRequest(settings, L"POST", L"/api/dashboard-auth/password/login", cookie,
                     body, response)) {
        return false;
    }

    if (response.status < 200 || response.status >= 300 ||
        response.setCookie.empty()) {
        Wh_Log(L"Dashboard login failed, status: %u, body: %s",
               response.status, TruncateForLog(response.body).c_str());
        return false;
    }

    cookie = response.setCookie;
    return true;
}

bool EnsureDashboardSession(const Settings& settings, std::wstring& cookie) {
    if (!cookie.empty() && IsSessionActive(settings, cookie)) {
        return true;
    }

    return LoginDashboard(settings, cookie);
}

}  // namespace codex_dashboard
