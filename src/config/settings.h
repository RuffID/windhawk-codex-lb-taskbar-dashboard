#pragma once

#include "../core/state.h"

namespace codex_dashboard {

DWORD ClampSeconds(int value) {
    if (value < static_cast<int>(MIN_SETTING_SECONDS)) {
        return MIN_SETTING_SECONDS;
    }

    if (value > static_cast<int>(MAX_SETTING_SECONDS)) {
        return MAX_SETTING_SECONDS;
    }

    return static_cast<DWORD>(value);
}

void LoadSettings() {
    Settings settings;

    PCWSTR dashboardUrl = Wh_GetStringSetting(L"dashboardUrl");
    settings.dashboardUrl =
        dashboardUrl && *dashboardUrl ? dashboardUrl : L"https://sonmp.ru:53950";
    if (dashboardUrl) {
        Wh_FreeStringSetting(dashboardUrl);
    }

    PCWSTR usernameEnvName = Wh_GetStringSetting(L"usernameEnvName");
    settings.usernameEnvName = usernameEnvName && *usernameEnvName
                                   ? usernameEnvName
                                   : L"CODEX_LB_DASHBOARD_USERNAME";
    if (usernameEnvName) {
        Wh_FreeStringSetting(usernameEnvName);
    }

    PCWSTR passwordEnvName = Wh_GetStringSetting(L"passwordEnvName");
    settings.passwordEnvName = passwordEnvName && *passwordEnvName
                                   ? passwordEnvName
                                   : L"CODEX_LB_DASHBOARD_PASSWORD";
    if (passwordEnvName) {
        Wh_FreeStringSetting(passwordEnvName);
    }

    PCWSTR apiKeyEnvName = Wh_GetStringSetting(L"apiKeyEnvName");
    settings.apiKeyEnvName = apiKeyEnvName && *apiKeyEnvName
                                 ? apiKeyEnvName
                                 : L"CODEX_LB_API_KEY";
    if (apiKeyEnvName) {
        Wh_FreeStringSetting(apiKeyEnvName);
    }

    settings.updateIntervalSeconds =
        ClampSeconds(Wh_GetIntSetting(L"updateIntervalSeconds"));
    settings.requestTimeoutSeconds =
        ClampSeconds(Wh_GetIntSetting(L"requestTimeoutSeconds"));

    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = settings;
}

Settings GetSettingsSnapshot() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
}

}  // namespace codex_dashboard
