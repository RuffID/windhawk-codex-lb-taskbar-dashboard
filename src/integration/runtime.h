#pragma once

#include "windows.h"
#include "../core/refresh_api.h"
#include "../config/settings.h"
#include "../accounts/accounts.h"
#include "../accounts/usage.h"
#include "../versions/releases.h"

namespace codex_dashboard {

struct DashboardRefreshFlagGuard {
    ~DashboardRefreshFlagGuard() {
        g_dashboardRefreshInProgress = false;
    }
};

void QueueDashboardRefresh() {
    if (g_unloading || g_dashboardUpdatesPaused) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_dashboardRefreshWorkMutex);
    if (g_unloading || g_dashboardUpdatesPaused) {
        return;
    }

    if (!g_dashboardRefreshWork) {
        g_dashboardRefreshWork = CreateThreadpoolWork(
            [](PTP_CALLBACK_INSTANCE, PVOID, PTP_WORK) {
                RefreshDashboardText();
            },
            nullptr, nullptr);
        if (!g_dashboardRefreshWork) {
            Wh_Log(L"CreateThreadpoolWork failed: %u", GetLastError());
            return;
        }
    }

    {
        std::lock_guard<std::mutex> dashboardLock(g_dashboardMutex);
        g_nextReleaseCheckTick = 0;
    }
    SubmitThreadpoolWork(g_dashboardRefreshWork);
}

void StopDashboardRefreshWork() {
    std::lock_guard<std::mutex> lock(g_dashboardRefreshWorkMutex);
    if (!g_dashboardRefreshWork) {
        return;
    }

    WaitForThreadpoolWorkCallbacks(g_dashboardRefreshWork, TRUE);
    CloseThreadpoolWork(g_dashboardRefreshWork);
    g_dashboardRefreshWork = nullptr;
}

std::wstring FormatCurrentLocalTime() {
    SYSTEMTIME time;
    GetLocalTime(&time);

    wchar_t buffer[16];
    swprintf_s(buffer, L"%02u:%02u:%02u", time.wHour, time.wMinute,
               time.wSecond);
    return buffer;
}

void RefreshDashboardText() {
    if (IsDashboardRefreshCancelled() || g_dashboardRefreshInProgress.exchange(true)) {
        return;
    }

    DashboardRefreshFlagGuard refreshGuard;

    try {
        Settings settings = GetSettingsSnapshot();
        int apiKeyRemainingPercent = -1;
        bool usageFetched = false;
        for (int attempt = 0; attempt < DASHBOARD_MAX_ATTEMPTS; attempt++) {
            if (IsDashboardRefreshCancelled()) {
                return;
            }
            bool attemptSucceeded = false;
            try {
                attemptSucceeded =
                    FetchApiKeyRemainingPercent(settings, apiKeyRemainingPercent);
            } catch (...) {
                LogException(L"ApiKeyUsageRefreshAttempt",
                             CurrentExceptionHResult());
            }

            if (attemptSucceeded) {
                usageFetched = true;
                break;
            }

            if (attempt + 1 < DASHBOARD_MAX_ATTEMPTS &&
                WaitForSingleObject(g_dashboardCancelEvent, 1000) != WAIT_TIMEOUT) {
                return;
            }
        }

        if (IsDashboardRefreshCancelled()) {
            return;
        }
        std::wstring text = FormatApiKeyRemainingText(
            usageFetched ? apiKeyRemainingPercent : -1);
        {
            std::lock_guard<std::mutex> lock(g_dashboardMutex);
            g_dashboardText = text;
            g_apiKeyRemainingPercent =
                usageFetched ? apiKeyRemainingPercent : -1;
            if (usageFetched) {
                g_lastUpdatedText = FormatCurrentLocalTime();
            } else {
                g_lastUpdatedText = L"—";
            }
        }

        PushDashboardTextToTaskbar(text);
        HWND popupWindow = g_popupWindow.load();
        if (popupWindow && IsWindow(popupWindow)) {
            PostMessage(popupWindow, WM_REFRESH_DASHBOARD_POPUP, 0, 0);
        }

        bool accountsFetched = false;
        for (int attempt = 0; attempt < DASHBOARD_MAX_ATTEMPTS; attempt++) {
            if (IsDashboardRefreshCancelled()) {
                return;
            }
            bool attemptSucceeded = false;
            try {
                attemptSucceeded = FetchDashboardAccounts(settings);
            } catch (...) {
                LogException(L"DashboardAccountsRefreshAttempt",
                             CurrentExceptionHResult());
            }

            if (attemptSucceeded) {
                accountsFetched = true;
                break;
            }

            if (attempt + 1 < DASHBOARD_MAX_ATTEMPTS &&
                WaitForSingleObject(g_dashboardCancelEvent, 1000) != WAIT_TIMEOUT) {
                return;
            }
        }

        if (IsDashboardRefreshCancelled()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_dashboardMutex);
            g_accountsRequestCompleted = true;
            g_accountsAvailable = accountsFetched;
            if (!accountsFetched) {
                g_dashboardAccounts.clear();
            }
        }

        popupWindow = g_popupWindow.load();
        if (popupWindow && IsWindow(popupWindow)) {
            PostMessage(popupWindow, WM_REFRESH_DASHBOARD_POPUP, 0, 0);
        }
        RefreshDashboardVersion(settings, accountsFetched);
        if (IsDashboardRefreshCancelled()) {
            return;
        }
        PushDashboardTextToTaskbar(text);
        popupWindow = g_popupWindow.load();
        if (popupWindow && IsWindow(popupWindow)) {
            PostMessage(popupWindow, WM_REFRESH_DASHBOARD_POPUP, 0, 0);
        }
    } catch (...) {
        LogException(L"RefreshDashboardText", CurrentExceptionHResult());
    }
}

void StopDashboardTimer() {
    if (!g_dashboardTimer) {
        return;
    }

    SetThreadpoolTimer(g_dashboardTimer, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(g_dashboardTimer, TRUE);
    CloseThreadpoolTimer(g_dashboardTimer);
    g_dashboardTimer = nullptr;
}

void StartDashboardTimer() {
    StopDashboardTimer();

    Settings settings = GetSettingsSnapshot();

    g_dashboardTimer = CreateThreadpoolTimer(
        [](PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) { RefreshDashboardText(); },
        nullptr, nullptr);

    if (!g_dashboardTimer) {
        Wh_Log(L"Create dashboard timer failed");
        return;
    }

    FILETIME dueTime;
    ULARGE_INTEGER dueTimeValue;
    dueTimeValue.QuadPart = static_cast<ULONGLONG>(-10000000LL);
    dueTime.dwLowDateTime = dueTimeValue.LowPart;
    dueTime.dwHighDateTime = dueTimeValue.HighPart;

    DWORD updateIntervalMs = settings.updateIntervalSeconds * 1000UL;
    SetThreadpoolTimer(g_dashboardTimer, &dueTime,
                       updateIntervalMs, 0);
}

void PauseDashboardUpdates() {
    g_dashboardUpdatesPaused = true;
    if (!SetEvent(g_dashboardCancelEvent)) {
        winrt::throw_last_error();
    }
    StopDashboardTimer();
    StopDashboardRefreshWork();
}

void StopInitializeTimer() {
    if (!g_initializeTimer) {
        return;
    }

    SetThreadpoolTimer(g_initializeTimer, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(g_initializeTimer, TRUE);
    CloseThreadpoolTimer(g_initializeTimer);
    g_initializeTimer = nullptr;
}

void StartInitializeTimer() {
    StopInitializeTimer();

    g_initializeAttempts = 0;
    g_taskbarRecoveryRequested = true;
    g_initializeTimer = CreateThreadpoolTimer(
        [](PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) {
            try {
                if (g_unloading || g_taskbarRecoveryInProgress.exchange(true)) {
                    return;
                }
                struct RecoveryGuard {
                    ~RecoveryGuard() {
                        g_taskbarRecoveryInProgress = false;
                    }
                } recoveryGuard;
                int attempt = ++g_initializeAttempts;
                bool recoveryRequested = g_taskbarRecoveryRequested.exchange(false);
                if (!recoveryRequested && attempt % 5 != 0) {
                    return;
                }
                if (g_initialized && g_widgetCount == 0 && attempt % 5 == 0) {
                    Wh_Log(
                        L"Widget wasn't added yet, reconnecting XAML watcher");
                    UninitializeSettingsAndTap();
                }

                TryInitializeForTaskbar();
            } catch (...) {
                LogException(L"InitializeTimer", CurrentExceptionHResult());
            }
        },
        nullptr, nullptr);

    if (!g_initializeTimer) {
        Wh_Log(L"CreateThreadpoolTimer failed");
        return;
    }

    FILETIME dueTime;
    ULARGE_INTEGER dueTimeValue;
    dueTimeValue.QuadPart = static_cast<ULONGLONG>(-10000000LL);
    dueTime.dwLowDateTime = dueTimeValue.LowPart;
    dueTime.dwHighDateTime = dueTimeValue.HighPart;

    SetThreadpoolTimer(g_initializeTimer, &dueTime, 1000, 0);
}

}  // namespace codex_dashboard
