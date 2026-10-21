#pragma once

#include "platform.h"

namespace codex_dashboard {

// Single owner of shared settings, published data and runtime/UI resources.
// Definitions are included once in the mod's single translation unit.

std::atomic<bool> g_initialized;
thread_local bool g_reconcilingWidgets;
std::atomic<bool> g_taskbarRecoveryRequested = true;
std::atomic<bool> g_taskbarRecoveryInProgress;
PTP_TIMER g_initializeTimer;
std::atomic<int> g_initializeAttempts;
std::atomic<int> g_widgetCount;
std::atomic<bool> g_dashboardRefreshInProgress;
std::atomic<bool> g_dashboardUpdatesPaused = true;
HANDLE g_dashboardCancelEvent;
std::atomic<bool> g_unloading;
std::atomic<HWND> g_popupWindow;
PTP_TIMER g_dashboardTimer;
PTP_WORK g_dashboardRefreshWork;
std::mutex g_dashboardMutex;
std::mutex g_dashboardRefreshWorkMutex;
std::wstring g_dashboardCookie;
std::wstring g_dashboardText = L"Загрузка...";
std::wstring g_lastUpdatedText = L"—";
int g_apiKeyRemainingPercent = -1;
bool g_accountsRequestCompleted;
bool g_accountsAvailable;
HHOOK g_popupMouseHook;
int g_popupScrollY;
std::atomic<DWORD> g_popupThreadId;
std::atomic<DWORD> g_popupClosedByOutsideClickTick;
bool g_refreshButtonHovered;
bool g_refreshButtonPressed;
RECT g_popupGithubRect{};
RECT g_popupModGithubRect{};
RECT g_popupReleaseRect{};
std::wstring g_popupReleaseUrl;
std::wstring g_popupPressedUrl;
std::mutex g_popupCreationMutex;
std::mutex g_widgetsMutex;

constexpr PCWSTR DASHBOARD_OFFLINE_TEXT = L"Сервер недоступен";
constexpr PCWSTR CODEX_LB_GITHUB_URL = L"https://github.com/Soju06/codex-lb";
constexpr PCWSTR MOD_GITHUB_URL = L"https://github.com/RuffID/windhawk-codex-lb-taskbar-dashboard";
constexpr PCWSTR LOG_FILE_PATH = L"C:\\codex-lb-taskbar-dashboard.log";
constexpr PCWSTR FALLBACK_LOG_DIR_NAME = L"CodexLbTaskbarDashboard";
constexpr PCWSTR FALLBACK_LOG_FILE_NAME = L"dashboard.log";
constexpr DWORD MIN_SETTING_SECONDS = 30;
constexpr DWORD MAX_SETTING_SECONDS = 2147483;
constexpr int DASHBOARD_MAX_ATTEMPTS = 5;
constexpr ULONGLONG RELEASE_CHECK_INTERVAL_SECONDS = 120;
constexpr ULONGLONG RELEASE_RATE_LIMIT_FALLBACK_SECONDS = 3600;
constexpr UINT WM_TOGGLE_DASHBOARD_POPUP = WM_APP + 1;
constexpr UINT WM_REFRESH_DASHBOARD_POPUP = WM_APP + 2;

struct Settings {
    std::wstring dashboardUrl = L"https://sonmp.ru:53950";
    std::wstring usernameEnvName = L"CODEX_LB_DASHBOARD_USERNAME";
    std::wstring passwordEnvName = L"CODEX_LB_DASHBOARD_PASSWORD";
    std::wstring apiKeyEnvName = L"CODEX_LB_API_KEY";
    DWORD updateIntervalSeconds = 60;
    DWORD requestTimeoutSeconds = 30;
};

Settings g_settings;
std::mutex g_settingsMutex;

struct DashboardAccount {
    std::wstring alias;
    std::wstring email;
    bool hasPrimary = false;
    int primaryPercent = 0;
    int secondaryPercent = 0;
    std::wstring primaryResetText;
    std::wstring secondaryResetText;
    std::wstring secondaryDaysText;
    bool reauthRequired = false;
    bool secondaryIsMonthly = false;
    int availableResetCredits = -1;
    std::wstring resetCreditNearestExpiresAt;
};

std::vector<DashboardAccount> g_dashboardAccounts;

enum class ReleaseCheckStatus { Pending, Succeeded, Failed, RateLimited };

struct DashboardVersion {
    std::wstring current;
    std::wstring latest;
    ReleaseCheckStatus releaseCheckStatus = ReleaseCheckStatus::Pending;
    bool latestUpdateAvailable = false;
};

DashboardVersion g_dashboardVersion;

struct WidgetState {
    DWORD threadId = 0;
    winrt::weak_ref<wuxc::Panel> root;
    winrt::weak_ref<wux::FrameworkElement> tray;
    winrt::weak_ref<wux::FrameworkElement> widget;
    winrt::weak_ref<wux::XamlRoot> xamlRoot;
    wux::FrameworkElement::SizeChanged_revoker traySizeChangedRevoker;
    wux::UIElement::Tapped_revoker widgetTappedRevoker;
    wux::UIElement::PointerEntered_revoker widgetPointerEnteredRevoker;
    wux::UIElement::PointerExited_revoker widgetPointerExitedRevoker;
};

std::vector<WidgetState> g_widgets;

struct TaskbarRootState {
    DWORD threadId = 0;
    winrt::weak_ref<wux::XamlRoot> xamlRoot;
};

std::vector<TaskbarRootState> g_taskbarRoots;

}  // namespace codex_dashboard
