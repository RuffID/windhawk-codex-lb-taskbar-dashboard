#pragma once

#include "../rendering/popup.h"
#include "../../config/settings.h"
#include "../../core/logging.h"
#include "../../core/text_json.h"
#include "../../core/refresh_api.h"
#include "../../accounts/usage.h"
#include "../../versions/semver.h"

namespace codex_dashboard {

void TogglePopupWindow();

void HidePopupWindow() {
    if (g_popupMouseHook) {
        UnhookWindowsHookEx(g_popupMouseHook);
        g_popupMouseHook = nullptr;
    }

    HWND popupWindow = g_popupWindow.load();
    if (popupWindow) {
        ShowWindow(popupWindow, SW_HIDE);
    }
}

LRESULT CALLBACK PopupMouseHookProc(int code,
                                    WPARAM wParam,
                                    LPARAM lParam) try {
    HWND popupWindow = g_popupWindow.load();
    if (code == HC_ACTION && popupWindow && IsWindowVisible(popupWindow) &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
         wParam == WM_MBUTTONDOWN)) {
        auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        RECT popupRect;
        GetWindowRect(popupWindow, &popupRect);
        if (!PtInRect(&popupRect, mouse->pt)) {
            g_popupClosedByOutsideClickTick.store(GetTickCount());
            HidePopupWindow();
        }
    }

    return CallNextHookEx(g_popupMouseHook, code, wParam, lParam);
} catch (...) {
    LogException(L"PopupMouseHookProc", CurrentExceptionHResult());
    return CallNextHookEx(g_popupMouseHook, code, wParam, lParam);
}

void InstallPopupMouseHook() {
    if (!g_popupMouseHook) {
        g_popupMouseHook =
            SetWindowsHookEx(WH_MOUSE_LL, PopupMouseHookProc,
                             GetCurrentModuleHandle(), 0);
    }
}

LRESULT CALLBACK PopupWindowProc(HWND window,
                                 UINT message,
                                 WPARAM wParam,
                                 LPARAM lParam) try {
    switch (message) {
        case WM_TOGGLE_DASHBOARD_POPUP:
            TogglePopupWindow();
            return 0;

        case WM_REFRESH_DASHBOARD_POPUP:
            InvalidateRect(window, nullptr, TRUE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC paintDc = BeginPaint(window, &ps);
            PaintGuard paintGuard{window, &ps};

            RECT physicalRect{};
            GetClientRect(window, &physicalRect);
            RECT rect = GetPopupClientRect(window);

            if (rect.right <= rect.left || rect.bottom <= rect.top) {
                return 0;
            }
            PopupCanvas canvas;
            UINT dpi = GetDpiForWindow(window);
            canvas.Create(paintDc, physicalRect.right, physicalRect.bottom, dpi);
            HDC dc = canvas.dc;
            SetPopupDcScale(dc, dpi);
            DrawPopupBackground(dc, rect);

            SetBkMode(dc, TRANSPARENT);

            std::vector<DashboardAccount> accounts;
            std::wstring lastUpdatedText;
            DashboardVersion dashboardVersion;
            int apiKeyRemainingPercent = -1;
            bool accountsRequestCompleted = false;
            bool accountsAvailable = false;
            {
                std::lock_guard<std::mutex> lock(g_dashboardMutex);
                accounts = g_dashboardAccounts;
                lastUpdatedText = g_lastUpdatedText;
                dashboardVersion = g_dashboardVersion;
                apiKeyRemainingPercent = g_apiKeyRemainingPercent;
                accountsRequestCompleted = g_accountsRequestCompleted;
                accountsAvailable = g_accountsAvailable;
            }

            g_popupScrollY = std::clamp(
                g_popupScrollY, 0, GetPopupMaxScroll(accounts.size(),
                                                     static_cast<int>(
                                                         rect.bottom -
                                                         rect.top)));

            HFONT titleFont = CreatePopupFont(POPUP_TITLE_FONT_HEIGHT, FW_SEMIBOLD);
            HFONT bodyFont = CreatePopupFont(POPUP_BODY_FONT_HEIGHT, FW_NORMAL);
            HFONT smallFont = CreatePopupFont(POPUP_SMALL_FONT_HEIGHT, FW_NORMAL);
            GdiObjectGuard titleFontGuard{titleFont};
            GdiObjectGuard bodyFontGuard{bodyFont};
            GdiObjectGuard smallFontGuard{smallFont};

            RECT refreshButtonRect = GetPopupRefreshButtonRect(rect);
            DrawRefreshButton(dc, smallFont, refreshButtonRect);

            RECT remainingLabelRect{
                POPUP_PADDING, 40, POPUP_PADDING + 72, 58};
            DrawPopupText(dc, smallFont, RGB(190, 190, 190), L"Осталось:",
                          remainingLabelRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            std::wstring remainingText = apiKeyRemainingPercent >= 0
                                             ? FormatApiKeyRemainingText(
                                                   apiKeyRemainingPercent)
                                             : L"н/д";
            RECT remainingValueRect{
                remainingLabelRect.right, remainingLabelRect.top,
                remainingLabelRect.right + 44, remainingLabelRect.bottom};
            DrawPopupText(dc, smallFont,
                          GetPercentColorRef(apiKeyRemainingPercent),
                          remainingText, remainingValueRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS);

            std::wstring versionText = dashboardVersion.current.empty()
                                           ? L"Текущая: н/д"
                                           : L"Текущая: " + dashboardVersion.current;
            bool updateAvailable =
                dashboardVersion.releaseCheckStatus == ReleaseCheckStatus::Succeeded &&
                IsNewerReleaseVersion(dashboardVersion.latest,
                                      dashboardVersion.current);
            std::wstring updateText;
            if (updateAvailable) {
                updateText = L"↑ " + dashboardVersion.latest;
            } else if (dashboardVersion.releaseCheckStatus !=
                       ReleaseCheckStatus::Succeeded) {
                updateText = dashboardVersion.releaseCheckStatus == ReleaseCheckStatus::Pending
                                 ? L"проверка..."
                                 : L"проверка н/д";
            }
            SIZE versionSize{}, updateSize{};
            HGDIOBJ oldFont = SelectObject(dc, smallFont);
            GetTextExtentPoint32(dc, versionText.c_str(),
                                 static_cast<int>(versionText.size()),
                                 &versionSize);
            GetTextExtentPoint32(dc, updateText.c_str(),
                                 static_cast<int>(updateText.size()), &updateSize);
            SelectObject(dc, oldFont);
            int statusRight = refreshButtonRect.left - 12;
            RECT updateRect{statusRight, refreshButtonRect.top, statusRight,
                            refreshButtonRect.bottom};
            if (!updateText.empty()) {
                updateRect.left -= std::min<LONG>(175, updateSize.cx + 12);
                if (updateAvailable) {
                    HBRUSH updateBrush = CreateSolidBrush(RGB(80, 57, 9));
                    HPEN updatePen = CreatePen(PS_SOLID, 1, RGB(246, 196, 83));
                    HGDIOBJ oldBrush = SelectObject(dc, updateBrush);
                    HGDIOBJ oldPen = SelectObject(dc, updatePen);
                    RoundRect(dc, updateRect.left, updateRect.top, updateRect.right,
                              updateRect.bottom, 6, 6);
                    SelectObject(dc, oldPen);
                    SelectObject(dc, oldBrush);
                    DeleteObject(updatePen);
                    DeleteObject(updateBrush);
                }
                RECT updateTextRect = updateRect;
                InflateRect(&updateTextRect, -6, 0);
                DrawPopupText(dc, smallFont,
                              updateAvailable ? RGB(255, 220, 100) : RGB(178, 193, 213),
                              updateText, updateTextRect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                statusRight = updateRect.left - 10;
            }
            RECT versionRect{
                statusRight - std::min<LONG>(190, versionSize.cx),
                refreshButtonRect.top, statusRight,
                refreshButtonRect.bottom};
            DrawPopupText(dc, smallFont, RGB(198, 209, 224), versionText,
                          versionRect,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            std::wstring dashboardAddress =
                FormatDashboardAddress(GetSettingsSnapshot().dashboardUrl);
            RECT addressRect{
                POPUP_PADDING, refreshButtonRect.top,
                versionRect.left - 12, refreshButtonRect.bottom};
            DrawPopupText(dc, smallFont, RGB(198, 209, 224), dashboardAddress,
                          addressRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS);

            RECT modVersionRect{remainingValueRect.right + 12, 40,
                                refreshButtonRect.left - 12, 58};
            DrawPopupText(dc, smallFont, RGB(178, 193, 213),
                          L"Мод v" WH_MOD_VERSION, modVersionRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT updatedTextRect{
                refreshButtonRect.right + 8, refreshButtonRect.top,
                rect.right - POPUP_PADDING, refreshButtonRect.bottom};
            DrawPopupText(dc, smallFont, RGB(198, 209, 224), lastUpdatedText,
                          updatedTextRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                              DT_END_ELLIPSIS);

            if (accounts.empty()) {
                RECT emptyRect{14, POPUP_HEADER_HEIGHT, rect.right - 14,
                               rect.bottom - 14};
                PCWSTR emptyText =
                    accountsRequestCompleted && !accountsAvailable
                        ? L"Не удалось получить информацию об аккаунтах.."
                        : L"Данные аккаунтов пока недоступны";
                DrawPopupText(dc, bodyFont, RGB(200, 200, 200),
                              emptyText, emptyRect,
                              DT_LEFT | DT_TOP | DT_WORDBREAK);
            } else {
                POINT clipBounds[2]{{rect.left, POPUP_HEADER_HEIGHT},
                                     {rect.right, rect.bottom}};
                LPtoDP(dc, clipBounds, 2);
                HRGN clip = CreateRectRgn(clipBounds[0].x, clipBounds[0].y,
                                          clipBounds[1].x, clipBounds[1].y);
                GdiObjectGuard clipGuard{clip};
                SelectClipRgn(dc, clip);

                int availableWidth =
                    rect.right - rect.left - POPUP_PADDING * 3 -
                    POPUP_CARD_GAP - POPUP_SCROLLBAR_WIDTH;
                int cardWidth = availableWidth / POPUP_COLUMNS;
                for (size_t i = 0; i < accounts.size(); i++) {
                    int row = static_cast<int>(i) / POPUP_COLUMNS;
                    int column = static_cast<int>(i) % POPUP_COLUMNS;
                    int x = POPUP_PADDING +
                            column * (cardWidth + POPUP_CARD_GAP);
                    int y = POPUP_HEADER_HEIGHT + POPUP_PADDING -
                            g_popupScrollY +
                            row * POPUP_ACCOUNT_HEIGHT;
                    RECT accountRect{x, y, x + cardWidth,
                                     y + POPUP_ACCOUNT_HEIGHT - 8};
                    if (accountRect.bottom >= rect.top &&
                        accountRect.top <= rect.bottom) {
                        DrawPopupAccount(dc, accounts[i], accountRect,
                                         titleFont, bodyFont, smallFont);
                    }
                }

                SelectClipRgn(dc, nullptr);
            }

            DrawPopupScrollbar(dc, rect, accounts.size(), g_popupScrollY);
            DrawPopupFrame(dc, rect);
            canvas.Present(window);

            return 0;
        }

        case WM_MOUSEWHEEL: {
            RECT rect = GetPopupClientRect(window);
            int maxScroll = 0;
            {
                std::lock_guard<std::mutex> lock(g_dashboardMutex);
                maxScroll = GetPopupMaxScroll(g_dashboardAccounts.size(),
                                              static_cast<int>(rect.bottom -
                                                               rect.top));
            }

            if (maxScroll == 0) {
                g_popupScrollY = 0;
                return 0;
            }

            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int nextScrollY =
                std::clamp(g_popupScrollY - delta / WHEEL_DELTA * 34, 0,
                           maxScroll);
            if (nextScrollY != g_popupScrollY) {
                g_popupScrollY = nextScrollY;
                InvalidateRect(window, nullptr, TRUE);
            }

            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT point = GetPopupMousePoint(window, lParam);
            RECT rect = GetPopupClientRect(window);
            RECT refreshButtonRect = GetPopupRefreshButtonRect(rect);
            bool hovered = PtInRect(&refreshButtonRect, point) != FALSE;
            if (hovered != g_refreshButtonHovered) {
                g_refreshButtonHovered = hovered;
                if (!hovered) {
                    g_refreshButtonPressed = false;
                }

                InvalidatePopupRect(window, refreshButtonRect);
            }

            if (hovered) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                TRACKMOUSEEVENT trackMouseEvent{};
                trackMouseEvent.cbSize = sizeof(trackMouseEvent);
                trackMouseEvent.dwFlags = TME_LEAVE;
                trackMouseEvent.hwndTrack = window;
                TrackMouseEvent(&trackMouseEvent);
                return 0;
            }

            break;
        }

        case WM_MOUSELEAVE:
            if (g_refreshButtonHovered || g_refreshButtonPressed) {
                g_refreshButtonHovered = false;
                g_refreshButtonPressed = false;
                RECT rect = GetPopupClientRect(window);
                RECT refreshButtonRect = GetPopupRefreshButtonRect(rect);
                InvalidatePopupRect(window, refreshButtonRect);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            POINT point = GetPopupMousePoint(window, lParam);
            RECT rect = GetPopupClientRect(window);
            RECT refreshButtonRect = GetPopupRefreshButtonRect(rect);
            if (PtInRect(&refreshButtonRect, point)) {
                g_refreshButtonPressed = true;
                InvalidatePopupRect(window, refreshButtonRect);
                SetCapture(window);
                return 0;
            }

            break;
        }

        case WM_LBUTTONUP: {
            POINT point = GetPopupMousePoint(window, lParam);
            RECT rect = GetPopupClientRect(window);
            RECT refreshButtonRect = GetPopupRefreshButtonRect(rect);
            bool wasPressed = g_refreshButtonPressed;
            g_refreshButtonPressed = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }

            bool hovered = PtInRect(&refreshButtonRect, point) != FALSE;
            g_refreshButtonHovered = hovered;
            InvalidatePopupRect(window, refreshButtonRect);
            if (wasPressed && hovered) {
                QueueDashboardRefresh();
                return 0;
            }

            break;
        }

        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }

        case WM_CLOSE:
            HidePopupWindow();
            return 0;

        case WM_NCDESTROY:
            if (window == g_popupWindow.load()) {
                g_popupWindow.store(nullptr);
                g_popupThreadId = 0;
            }
            break;
    }

    return DefWindowProc(window, message, wParam, lParam);
} catch (...) {
    LogException(L"PopupWindowProc", CurrentExceptionHResult());
    return DefWindowProc(window, message, wParam, lParam);
}

void RegisterPopupWindowClass() {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASS windowClass{};
    windowClass.lpfnWndProc = PopupWindowProc;
    windowClass.hInstance = GetCurrentModuleHandle();
    windowClass.lpszClassName = L"CodexLbDashboardPopup";
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClass(&windowClass);
    registered = true;
}

void TogglePopupWindow() {
    constexpr int POPUP_WIDTH = 720;
    constexpr int POPUP_HEIGHT = 340;
    constexpr int POPUP_GAP = 14;

    std::lock_guard<std::mutex> popupLock(g_popupCreationMutex);

    DWORD currentThreadId = GetCurrentThreadId();
    HWND popupWindow = g_popupWindow.load();
    DWORD popupThreadId = g_popupThreadId.load();
    if (popupWindow && popupThreadId && popupThreadId != currentThreadId) {
        if (!PostMessage(popupWindow, WM_TOGGLE_DASHBOARD_POPUP, 0, 0)) {
            Wh_Log(L"Failed to route popup toggle: %u", GetLastError());
        }
        return;
    }

    if (popupWindow && IsWindowVisible(popupWindow)) {
        HidePopupWindow();
        return;
    }

    DWORD closedByOutsideClickTick = g_popupClosedByOutsideClickTick.load();
    if (closedByOutsideClickTick &&
        GetTickCount() - closedByOutsideClickTick < 500) {
        g_popupClosedByOutsideClickTick.store(0);
        return;
    }

    RegisterPopupWindowClass();

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        winrt::throw_last_error();
    }
    UINT dpi = GetDpiForWindow(WindowFromPoint(cursor));
    if (!dpi) {
        winrt::throw_hresult(E_FAIL);
    }
    int popupWidth = ScalePopupLength(POPUP_WIDTH, dpi);
    int popupHeight = ScalePopupLength(POPUP_HEIGHT, dpi);

    if (!popupWindow) {
        popupWindow = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            L"CodexLbDashboardPopup", L"", WS_POPUP, cursor.x, cursor.y, popupWidth,
            popupHeight, nullptr, nullptr, GetCurrentModuleHandle(), nullptr);
        if (!popupWindow) {
            Wh_Log(L"Create popup window failed: %u", GetLastError());
            return;
        }

        g_popupWindow.store(popupWindow);
        g_popupThreadId = currentThreadId;
    }

    MONITORINFO monitorInfo{.cbSize = sizeof(monitorInfo)};
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfo(monitor, &monitorInfo);

    int x = cursor.x - popupWidth / 2;
    int y = cursor.y - popupHeight - ScalePopupLength(POPUP_GAP, dpi);

    x = std::max(static_cast<int>(monitorInfo.rcWork.left),
                 std::min(x, static_cast<int>(monitorInfo.rcWork.right) -
                                  popupWidth));
    y = std::max(static_cast<int>(monitorInfo.rcWork.top),
                 std::min(y, static_cast<int>(monitorInfo.rcWork.bottom) -
                                  popupHeight));

    g_popupScrollY = 0;
    SetWindowPos(popupWindow, HWND_TOPMOST, x, y, popupWidth, popupHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InstallPopupMouseHook();
    InvalidateRect(popupWindow, nullptr, TRUE);
}

}  // namespace codex_dashboard
