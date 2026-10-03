#pragma once

#include "runtime.h"
#include "../config/settings.h"

namespace codex_dashboard {

BOOL InitializeMod() try {
    Wh_Log(L">");

    g_dashboardCancelEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_dashboardCancelEvent) {
        winrt::throw_last_error();
    }
    LoadSettings();

    WindhawkUtils::SetFunctionHook(CreateWindowExW, CreateWindowExW_Hook,
                                   &CreateWindowExW_Original);

    HMODULE user32 = LoadLibraryEx(L"user32.dll", nullptr,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user32) {
        auto createWindowInBand =
            reinterpret_cast<CreateWindowInBand_t>(
                GetProcAddress(user32, "CreateWindowInBand"));
        if (createWindowInBand) {
            WindhawkUtils::SetFunctionHook(createWindowInBand,
                                           CreateWindowInBand_Hook,
                                           &CreateWindowInBand_Original);
        }

        auto createWindowInBandEx =
            reinterpret_cast<CreateWindowInBandEx_t>(
                GetProcAddress(user32, "CreateWindowInBandEx"));
        if (createWindowInBandEx) {
            WindhawkUtils::SetFunctionHook(createWindowInBandEx,
                                           CreateWindowInBandEx_Hook,
                                           &CreateWindowInBandEx_Original);
        }
    }

    return TRUE;
} catch (...) {
    LogException(L"Wh_ModInit", CurrentExceptionHResult());
    if (g_dashboardCancelEvent) {
        CloseHandle(g_dashboardCancelEvent);
        g_dashboardCancelEvent = nullptr;
    }
    return FALSE;
}

void ApplySettingsChanged() try {
    PauseDashboardUpdates();
    LoadSettings();

    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardCookie.clear();
        g_dashboardText = L"Загрузка...";
        g_lastUpdatedText = L"—";
        g_apiKeyRemainingPercent = -1;
        g_accountsRequestCompleted = false;
        g_accountsAvailable = false;
        g_dashboardAccounts.clear();
        g_dashboardVersion = {};
        g_nextReleaseCheckTick = 0;
    }

    if (!ResetEvent(g_dashboardCancelEvent)) {
        winrt::throw_last_error();
    }
    g_dashboardUpdatesPaused = false;
    StartDashboardTimer();
    PushDashboardTextToTaskbar(L"Загрузка...");
} catch (...) {
    LogException(L"Wh_ModSettingsChanged", CurrentExceptionHResult());
}

void AfterInitializeMod() try {
    Wh_Log(L">");

    g_unloading = false;
    g_dashboardUpdatesPaused = false;
    StartDashboardTimer();

    TryInitializeForTaskbar();
    StartInitializeTimer();
} catch (...) {
    LogException(L"Wh_ModAfterInit", CurrentExceptionHResult());
}

void UninitializeMod() try {
    Wh_Log(L">");

    g_unloading = true;
    PauseDashboardUpdates();
    StopInitializeTimer();
    UninitializeSettingsAndTap();

    for (HWND window : GetXamlHostWindows()) {
        RunFromWindowThread(
            window, [](PVOID) { UninitializeForCurrentThread(); }, nullptr);
    }
    CloseHandle(g_dashboardCancelEvent);
    g_dashboardCancelEvent = nullptr;
} catch (...) {
    LogException(L"Wh_ModUninit", CurrentExceptionHResult());
}

}  // namespace codex_dashboard
