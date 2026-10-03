#pragma once

#include "../network/http.h"
#include "../core/text_json.h"

namespace codex_dashboard {

bool FetchApiKeyRemainingPercent(const Settings& settings, int& percent) {
    std::wstring apiKey =
        TrimString(GetEnvironmentVariableString(settings.apiKeyEnvName.c_str()));
    if (apiKey.empty()) {
        Wh_Log(L"API key environment variable is empty: %s",
               settings.apiKeyEnvName.c_str());
        return false;
    }

    HttpResponse response;
    if (!HttpRequest(settings, L"GET", L"/v1/usage", {}, {}, response, apiKey)) {
        return false;
    }

    if (response.status < 200 || response.status >= 300) {
        Wh_Log(L"API key usage request failed, status: %u, body: %s",
               response.status, TruncateForLog(response.body).c_str());
        return false;
    }

    wdj::JsonObject root = nullptr;
    if (!wdj::JsonObject::TryParse(response.body, root)) {
        Wh_Log(L"API key usage JSON parse failed, body: %s",
               TruncateForLog(response.body).c_str());
        return false;
    }

    wdj::JsonObject accountPoolUsage = nullptr;
    double secondaryPercent = 0;
    if (!TryGetJsonObject(root, L"account_pool_usage", accountPoolUsage) ||
        !TryGetJsonNumber(accountPoolUsage, L"secondary", secondaryPercent)) {
        Wh_Log(L"API key usage response has no weekly account pool percentage");
        return false;
    }

    percent = std::clamp(static_cast<int>(std::round(secondaryPercent)), 0, 100);
    return true;
}

std::wstring FormatApiKeyRemainingText(int percent) {
    if (percent < 0) {
        return DASHBOARD_OFFLINE_TEXT;
    }

    wchar_t text[16];
    swprintf_s(text, L"%d%%", percent);
    return text;
}

}  // namespace codex_dashboard
