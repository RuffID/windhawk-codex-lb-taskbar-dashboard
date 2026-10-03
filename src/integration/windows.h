#pragma once

#include "xaml_tap.h"
#include "../core/logging.h"
#include "../ui/taskbar/widgets.h"

namespace codex_dashboard {

using RunFromWindowThreadProc_t = void(WINAPI*)(PVOID parameter);

bool RunFromWindowThread(HWND window,
                         RunFromWindowThreadProc_t proc,
                         PVOID procParam) {
    static const UINT message =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct RunParam {
        RunFromWindowThreadProc_t proc;
        PVOID procParam;
    };

    DWORD threadId = GetWindowThreadProcessId(window, nullptr);
    if (!threadId) {
        return false;
    }

    if (threadId == GetCurrentThreadId()) {
        try {
            proc(procParam);
        } catch (...) {
            LogException(L"RunFromWindowThreadDirect",
                         CurrentExceptionHResult());
            return false;
        }

        return true;
    }

    HHOOK hook = SetWindowsHookEx(
        WH_CALLWNDPROC,
        [](int code, WPARAM, LPARAM lParam) -> LRESULT {
            if (code == HC_ACTION) {
                const auto* cwp = reinterpret_cast<const CWPSTRUCT*>(lParam);
                if (cwp->message == message) {
                    auto* param = reinterpret_cast<RunParam*>(cwp->lParam);
                    try {
                        param->proc(param->procParam);
                    } catch (...) {
                        LogException(L"RunFromWindowThreadHook",
                                     CurrentExceptionHResult());
                    }
                }
            }

            return CallNextHookEx(nullptr, code, 0, lParam);
        },
        nullptr, threadId);
    if (!hook) {
        return false;
    }

    RunParam param{proc, procParam};
    SendMessage(window, message, 0, reinterpret_cast<LPARAM>(&param));
    UnhookWindowsHookEx(hook);

    return true;
}

void OnWindowCreated(HWND window, HWND parent, LPCWSTR className) try {
    const bool textualClassName =
        (reinterpret_cast<ULONG_PTR>(className) & ~static_cast<ULONG_PTR>(0xffff)) !=
        0;

    WCHAR actualClassName[64];
    if (parent &&
        GetClassName(window, actualClassName, ARRAYSIZE(actualClassName)) &&
        _wcsicmp(actualClassName,
                 L"Windows.UI.Composition.DesktopWindowContentBridge") == 0 &&
        GetClassName(parent, actualClassName, ARRAYSIZE(actualClassName)) &&
        (_wcsicmp(actualClassName, L"Shell_TrayWnd") == 0 ||
         _wcsicmp(actualClassName, L"Shell_SecondaryTrayWnd") == 0)) {
        InitializeForCurrentThread();
        InitializeSettingsAndTap();
        g_taskbarRecoveryRequested = true;
        return;
    }

    if (textualClassName &&
        _wcsicmp(className, L"XamlExplorerHostIslandWindow") == 0) {
        InitializeForCurrentThread();
        InitializeSettingsAndTap();
        g_taskbarRecoveryRequested = true;
    }
} catch (...) {
    LogException(L"OnWindowCreated", CurrentExceptionHResult());
}

using CreateWindowExW_t = decltype(&CreateWindowExW);
CreateWindowExW_t CreateWindowExW_Original;

HWND WINAPI CreateWindowExW_Hook(DWORD exStyle,
                                 LPCWSTR className,
                                 LPCWSTR windowName,
                                 DWORD style,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 HWND parent,
                                 HMENU menu,
                                 HINSTANCE instance,
                                 PVOID param) {
    HWND window = CreateWindowExW_Original(exStyle, className, windowName, style,
                                           x, y, width, height, parent, menu,
                                           instance, param);
    if (window) {
        OnWindowCreated(window, parent, className);
    }

    return window;
}

using CreateWindowInBand_t = HWND(WINAPI*)(DWORD exStyle,
                                           LPCWSTR className,
                                           LPCWSTR windowName,
                                           DWORD style,
                                           int x,
                                           int y,
                                           int width,
                                           int height,
                                           HWND parent,
                                           HMENU menu,
                                           HINSTANCE instance,
                                           PVOID param,
                                           DWORD band);
CreateWindowInBand_t CreateWindowInBand_Original;

HWND WINAPI CreateWindowInBand_Hook(DWORD exStyle,
                                    LPCWSTR className,
                                    LPCWSTR windowName,
                                    DWORD style,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    HWND parent,
                                    HMENU menu,
                                    HINSTANCE instance,
                                    PVOID param,
                                    DWORD band) {
    HWND window = CreateWindowInBand_Original(
        exStyle, className, windowName, style, x, y, width, height, parent, menu,
        instance, param, band);
    if (window) {
        OnWindowCreated(window, parent, className);
    }

    return window;
}

using CreateWindowInBandEx_t = HWND(WINAPI*)(DWORD exStyle,
                                             LPCWSTR className,
                                             LPCWSTR windowName,
                                             DWORD style,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             HWND parent,
                                             HMENU menu,
                                             HINSTANCE instance,
                                             PVOID param,
                                             DWORD band,
                                             DWORD typeFlags);
CreateWindowInBandEx_t CreateWindowInBandEx_Original;

HWND WINAPI CreateWindowInBandEx_Hook(DWORD exStyle,
                                      LPCWSTR className,
                                      LPCWSTR windowName,
                                      DWORD style,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      HWND parent,
                                      HMENU menu,
                                      HINSTANCE instance,
                                      PVOID param,
                                      DWORD band,
                                      DWORD typeFlags) {
    HWND window = CreateWindowInBandEx_Original(
        exStyle, className, windowName, style, x, y, width, height, parent, menu,
        instance, param, band, typeFlags);
    if (window) {
        OnWindowCreated(window, parent, className);
    }

    return window;
}

std::vector<HWND> GetXamlHostWindows() {
    std::vector<HWND> windows;

    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            DWORD processId = 0;
            WCHAR className[64];
            if (GetWindowThreadProcessId(window, &processId) &&
                processId == GetCurrentProcessId() &&
                GetClassName(window, className, ARRAYSIZE(className)) &&
                (_wcsicmp(className, L"Shell_TrayWnd") == 0 ||
                 _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0)) {
                EnumChildWindows(
                    window,
                    [](HWND child, LPARAM childParameter) -> BOOL {
                        WCHAR childClass[64];
                        DWORD childProcessId = 0;
                        if (GetWindowThreadProcessId(child, &childProcessId) &&
                            childProcessId == GetCurrentProcessId() &&
                            GetClassName(child, childClass,
                                         ARRAYSIZE(childClass)) &&
                            (_wcsicmp(childClass,
                                      L"XamlExplorerHostIslandWindow") == 0 ||
                             _wcsicmp(childClass,
                                      L"Windows.UI.Composition.DesktopWindowContentBridge") == 0)) {
                            auto* result =
                                reinterpret_cast<std::vector<HWND>*>(childParameter);
                            result->push_back(child);
                        }
                        return TRUE;
                    },
                    parameter);
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));

    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            auto* result = reinterpret_cast<std::vector<HWND>*>(parameter);

            DWORD processId = 0;
            WCHAR className[64];
            if (GetWindowThreadProcessId(window, &processId) &&
                processId == GetCurrentProcessId() &&
                GetClassName(window, className, ARRAYSIZE(className)) &&
                (_wcsicmp(className, L"XamlExplorerHostIslandWindow") == 0 ||
                 _wcsicmp(className,
                          L"Windows.UI.Composition.DesktopWindowContentBridge") ==
                     0)) {
                if (std::find(result->begin(), result->end(), window) ==
                    result->end()) {
                    result->push_back(window);
                }
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));

    return windows;
}

bool TryInitializeForTaskbar() {
    auto windows = GetXamlHostWindows();

    bool initialized = false;
    std::vector<DWORD> visitedThreads;
    for (HWND window : windows) {
        DWORD threadId = GetWindowThreadProcessId(window, nullptr);
        if (!threadId ||
            std::find(visitedThreads.begin(), visitedThreads.end(), threadId) !=
                visitedThreads.end()) {
            continue;
        }
        if (RunFromWindowThread(
                window, [](PVOID) { InitializeForCurrentThread(); }, nullptr)) {
            initialized = true;
            visitedThreads.push_back(threadId);
        }
    }

    if (initialized) {
        InitializeSettingsAndTap();
    }

    return initialized;
}

void PushDashboardTextToTaskbar(const std::wstring& text) {
    auto windows = GetXamlHostWindows();
    for (HWND window : windows) {
        RunFromWindowThread(window, UpdateWidgetsTextFromThread,
                            const_cast<std::wstring*>(&text));
    }
}

}  // namespace codex_dashboard
