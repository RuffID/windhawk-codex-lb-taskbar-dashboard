#pragma once

#include "../../core/state.h"
#include "../../core/time.h"

namespace codex_dashboard {

struct PaintGuard {
    HWND window = nullptr;
    PAINTSTRUCT* paint = nullptr;

    ~PaintGuard() {
        if (window && paint) {
            EndPaint(window, paint);
        }
    }
};

struct GdiObjectGuard {
    HGDIOBJ object = nullptr;

    ~GdiObjectGuard() {
        if (object) {
            DeleteObject(object);
        }
    }
};

constexpr int POPUP_ACCOUNT_HEIGHT = 124;
constexpr int POPUP_PADDING = 10;
constexpr int POPUP_CARD_GAP = 10;
constexpr int POPUP_COLUMNS = 2;
constexpr int POPUP_HEADER_HEIGHT = 64;
constexpr int POPUP_REFRESH_BUTTON_WIDTH = 88;
constexpr int POPUP_REFRESH_BUTTON_HEIGHT = 26;
constexpr int POPUP_UPDATED_TEXT_WIDTH = 90;
constexpr int POPUP_SCROLLBAR_WIDTH = 4;
constexpr int POPUP_CORNER_RADIUS = 14;
constexpr COLORREF POPUP_BACKGROUND_COLOR = RGB(14, 20, 30);
constexpr COLORREF POPUP_HEADER_COLOR = RGB(22, 31, 46);
constexpr COLORREF POPUP_CARD_COLOR = RGB(24, 32, 46);
constexpr int POPUP_TITLE_FONT_HEIGHT = 14;
constexpr int POPUP_BODY_FONT_HEIGHT = 13;
constexpr int POPUP_SMALL_FONT_HEIGHT = 12;

int ScalePopupLength(int length, UINT dpi) {
    return MulDiv(length, dpi, USER_DEFAULT_SCREEN_DPI);
}

RECT GetPopupClientRect(HWND window) {
    RECT rect{};
    if (!GetClientRect(window, &rect)) {
        winrt::throw_last_error();
    }
    UINT dpi = GetDpiForWindow(window);
    if (!dpi) {
        winrt::throw_hresult(E_FAIL);
    }
    rect.right = MulDiv(rect.right, USER_DEFAULT_SCREEN_DPI, dpi);
    rect.bottom = MulDiv(rect.bottom, USER_DEFAULT_SCREEN_DPI, dpi);
    return rect;
}

POINT GetPopupMousePoint(HWND window, LPARAM lParam) {
    UINT dpi = GetDpiForWindow(window);
    if (!dpi) {
        winrt::throw_hresult(E_FAIL);
    }
    return POINT{MulDiv(static_cast<short>(LOWORD(lParam)), USER_DEFAULT_SCREEN_DPI, dpi),
                 MulDiv(static_cast<short>(HIWORD(lParam)), USER_DEFAULT_SCREEN_DPI, dpi)};
}

void SetPopupDcScale(HDC dc, UINT dpi) {
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, USER_DEFAULT_SCREEN_DPI, USER_DEFAULT_SCREEN_DPI, nullptr);
    SetViewportExtEx(dc, dpi, dpi, nullptr);
}

void InvalidatePopupRect(HWND window, RECT rect) {
    UINT dpi = GetDpiForWindow(window);
    rect.left = ScalePopupLength(rect.left, dpi);
    rect.top = ScalePopupLength(rect.top, dpi);
    rect.right = ScalePopupLength(rect.right, dpi);
    rect.bottom = ScalePopupLength(rect.bottom, dpi);
    InvalidateRect(window, &rect, FALSE);
}

DWORD PopupDibColor(COLORREF color) {
    return (static_cast<DWORD>(GetRValue(color)) << 16) |
           (static_cast<DWORD>(GetGValue(color)) << 8) | GetBValue(color);
}

DWORD PremultiplyPopupPixel(DWORD pixel) {
    if (pixel >> 24) {
        return pixel;
    }
    DWORD color = pixel & 0x00FFFFFF;
    BYTE alpha = 255;
    if (color == 0) {
        return 0;
    }
    if (color == PopupDibColor(POPUP_BACKGROUND_COLOR)) {
        alpha = 224;
    } else if (color == PopupDibColor(POPUP_HEADER_COLOR)) {
        alpha = 232;
    } else if (color == PopupDibColor(POPUP_CARD_COLOR)) {
        alpha = 240;
    }
    DWORD red = ((color >> 16) & 0xFF) * alpha / 255;
    DWORD green = ((color >> 8) & 0xFF) * alpha / 255;
    DWORD blue = (color & 0xFF) * alpha / 255;
    return (static_cast<DWORD>(alpha) << 24) | (red << 16) | (green << 8) | blue;
}

DWORD BlendPopupTextPixel(DWORD background, COLORREF color, BYTE coverage) {
    if (!coverage) {
        return background;
    }
    background = PremultiplyPopupPixel(background);
    DWORD inverse = 255 - coverage;
    DWORD alpha = coverage + ((background >> 24) * inverse + 127) / 255;
    DWORD red = (GetRValue(color) * coverage +
                 ((background >> 16) & 0xFF) * inverse + 127) / 255;
    DWORD green = (GetGValue(color) * coverage +
                   ((background >> 8) & 0xFF) * inverse + 127) / 255;
    DWORD blue = (GetBValue(color) * coverage + (background & 0xFF) * inverse + 127) / 255;
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

struct PopupCanvas {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    DWORD* pixels = nullptr;
    int width = 0;
    int height = 0;
    int cornerRadius = POPUP_CORNER_RADIUS;

    PopupCanvas() = default;
    PopupCanvas(const PopupCanvas&) = delete;
    PopupCanvas& operator=(const PopupCanvas&) = delete;

    ~PopupCanvas() {
        if (dc && oldBitmap) {
            SelectObject(dc, oldBitmap);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
    }

    void Create(HDC referenceDc, int canvasWidth, int canvasHeight,
                UINT dpi = USER_DEFAULT_SCREEN_DPI) {
        width = canvasWidth;
        height = canvasHeight;
        cornerRadius = ScalePopupLength(POPUP_CORNER_RADIUS, dpi);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(referenceDc);
        if (!dc) {
            winrt::throw_hresult(E_OUTOFMEMORY);
        }
        void* data = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap || !data) {
            winrt::throw_hresult(E_OUTOFMEMORY);
        }
        pixels = static_cast<DWORD*>(data);
        oldBitmap = SelectObject(dc, bitmap);
        if (!oldBitmap || oldBitmap == HGDI_ERROR) {
            oldBitmap = nullptr;
            winrt::throw_hresult(E_FAIL);
        }
        std::fill_n(pixels, static_cast<size_t>(width) * height, 0);
    }

    void PrepareAlpha() {
        GdiFlush();
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int centerX = std::clamp(x, cornerRadius, width - cornerRadius - 1);
                int centerY = std::clamp(y, cornerRadius, height - cornerRadius - 1);
                int dx = x - centerX;
                int dy = y - centerY;
                size_t offset = static_cast<size_t>(y) * width + x;
                pixels[offset] = dx * dx + dy * dy >
                                         cornerRadius * cornerRadius
                                     ? 0
                                     : PremultiplyPopupPixel(pixels[offset]);
            }
        }
    }

    void Present(HWND window) {
        PrepareAlpha();
        RECT windowRect{};
        if (!GetWindowRect(window, &windowRect)) {
            winrt::throw_last_error();
        }
        POINT destination{windowRect.left, windowRect.top};
        POINT source{};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(window, nullptr, &destination, &size, dc,
                                 &source, 0, &blend, ULW_ALPHA)) {
            winrt::throw_last_error();
        }
    }
};

void DrawPopupBackground(HDC dc, const RECT& rect) {
    HBRUSH backgroundBrush = CreateSolidBrush(POPUP_BACKGROUND_COLOR);
    FillRect(dc, &rect, backgroundBrush);
    DeleteObject(backgroundBrush);
    RECT headerRect{rect.left, rect.top, rect.right,
                    rect.top + POPUP_HEADER_HEIGHT};
    HBRUSH headerBrush = CreateSolidBrush(POPUP_HEADER_COLOR);
    FillRect(dc, &headerRect, headerBrush);
    DeleteObject(headerBrush);
    HPEN dividerPen = CreatePen(PS_SOLID, 1, RGB(46, 62, 84));
    HGDIOBJ oldPen = SelectObject(dc, dividerPen);
    MoveToEx(dc, rect.left + 14, headerRect.bottom - 1, nullptr);
    LineTo(dc, rect.right - 14, headerRect.bottom - 1);
    SelectObject(dc, oldPen);
    DeleteObject(dividerPen);
}

void DrawPopupFrame(HDC dc, const RECT& rect) {
    HPEN framePen = CreatePen(PS_SOLID, 1, RGB(80, 108, 146));
    HGDIOBJ oldPen = SelectObject(dc, framePen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              POPUP_CORNER_RADIUS * 2, POPUP_CORNER_RADIUS * 2);
    HPEN innerPen = CreatePen(PS_SOLID, 1, RGB(35, 48, 67));
    SelectObject(dc, innerPen);
    RoundRect(dc, rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1,
              (POPUP_CORNER_RADIUS - 1) * 2, (POPUP_CORNER_RADIUS - 1) * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(innerPen);
    DeleteObject(framePen);
}

COLORREF GetPercentColorRef(int percent) {
    if (percent < 0) {
        return RGB(180, 180, 180);
    }

    if (percent >= 61) {
        return RGB(106, 212, 124);
    }

    if (percent >= 31) {
        return RGB(246, 196, 83);
    }

    return RGB(242, 113, 113);
}

int GetPopupMaxScroll(size_t accountCount, int popupHeight) {
    int rows = (static_cast<int>(accountCount) + POPUP_COLUMNS - 1) /
               POPUP_COLUMNS;
    int contentHeight = POPUP_PADDING * 2 +
                        rows * POPUP_ACCOUNT_HEIGHT;
    int visibleHeight = std::max(0, popupHeight - POPUP_HEADER_HEIGHT);
    return std::max(0, contentHeight - visibleHeight);
}

RECT GetPopupRefreshButtonRect(const RECT& rect) {
    int right = rect.right - POPUP_UPDATED_TEXT_WIDTH - POPUP_PADDING;
    int left = right - POPUP_REFRESH_BUTTON_WIDTH;
    int top = POPUP_PADDING;
    return RECT{left, top, right, top + POPUP_REFRESH_BUTTON_HEIGHT};
}

HFONT CreatePopupFont(int height, int weight) {
    return CreateFont(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void DrawPopupText(HDC dc,
                   HFONT font,
                   COLORREF color,
                   const std::wstring& text,
                   RECT rect,
                   UINT format) {
    if (text.empty() || rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    DIBSECTION destination{};
    if (GetObject(GetCurrentObject(dc, OBJ_BITMAP), sizeof(destination), &destination) !=
            sizeof(destination) || !destination.dsBm.bmBits ||
        destination.dsBmih.biBitCount != 32) {
        winrt::throw_hresult(E_FAIL);
    }
    POINT corners[2]{{rect.left, rect.top}, {rect.right, rect.bottom}};
    LPtoDP(dc, corners, 2);
    RECT deviceRect{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
    if (deviceRect.right <= deviceRect.left || deviceRect.bottom <= deviceRect.top) {
        return;
    }
    PopupCanvas mask;
    mask.Create(dc, deviceRect.right - deviceRect.left,
                deviceRect.bottom - deviceRect.top);
    SIZE windowExt{}, viewportExt{};
    POINT windowOrigin{}, viewportOrigin{};
    GetWindowExtEx(dc, &windowExt);
    GetViewportExtEx(dc, &viewportExt);
    GetWindowOrgEx(dc, &windowOrigin);
    GetViewportOrgEx(dc, &viewportOrigin);
    SetMapMode(mask.dc, GetMapMode(dc));
    SetWindowExtEx(mask.dc, windowExt.cx, windowExt.cy, nullptr);
    SetViewportExtEx(mask.dc, viewportExt.cx, viewportExt.cy, nullptr);
    SetWindowOrgEx(mask.dc, windowOrigin.x, windowOrigin.y, nullptr);
    SetViewportOrgEx(mask.dc, viewportOrigin.x - deviceRect.left,
                      viewportOrigin.y - deviceRect.top, nullptr);
    HGDIOBJ oldFont = SelectObject(mask.dc, font);
    SetBkMode(mask.dc, TRANSPARENT);
    SetTextColor(mask.dc, RGB(255, 255, 255));
    DrawText(mask.dc, text.c_str(), -1, &rect, format);
    SelectObject(mask.dc, oldFont);
    GdiFlush();

    RECT clip{};
    int clipType = GetClipBox(dc, &clip);
    if (clipType == ERROR) {
        winrt::throw_hresult(E_FAIL);
    }
    if (clipType == NULLREGION) {
        return;
    }
    POINT clipCorners[2]{{clip.left, clip.top}, {clip.right, clip.bottom}};
    LPtoDP(dc, clipCorners, 2);
    RECT deviceClip{clipCorners[0].x, clipCorners[0].y,
                    clipCorners[1].x, clipCorners[1].y};
    RECT bounds{0, 0, destination.dsBm.bmWidth, destination.dsBm.bmHeight};
    RECT visible{};
    if (!IntersectRect(&visible, &deviceRect, &bounds) ||
        !IntersectRect(&visible, &visible, &deviceClip)) {
        return;
    }
    auto pixels = static_cast<DWORD*>(destination.dsBm.bmBits);
    int stride = destination.dsBm.bmWidthBytes / sizeof(DWORD);
    // PopupCanvas always creates top-down DIBs. GetObject reports a
    // positive biHeight even for these bitmaps, so do not infer orientation.
    for (int y = visible.top; y < visible.bottom; y++) {
        for (int x = visible.left; x < visible.right; x++) {
            DWORD maskPixel = mask.pixels[static_cast<size_t>(y - deviceRect.top) *
                                             mask.width + x - deviceRect.left];
            BYTE coverage = static_cast<BYTE>(maskPixel & 0xFF);
            size_t offset = static_cast<size_t>(y) * stride + x;
            pixels[offset] = BlendPopupTextPixel(pixels[offset], color, coverage);
        }
    }
}

void DrawLimitBar(HDC dc,
                  int x,
                  int y,
                  int width,
                  int percent,
                  COLORREF color) {
    RECT background{x, y, x + width, y + 5};
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(37, 47, 61));
    FillRect(dc, &background, backgroundBrush);
    DeleteObject(backgroundBrush);

    if (percent < 0) {
        return;
    }

    RECT foreground{x, y,
                    x + std::clamp(width * percent / 100, 0, width), y + 5};
    HBRUSH foregroundBrush = CreateSolidBrush(color);
    FillRect(dc, &foreground, foregroundBrush);
    DeleteObject(foregroundBrush);
}

void DrawPopupScrollbar(HDC dc,
                        const RECT& rect,
                        size_t accountCount,
                        int scrollY) {
    int trackTop = POPUP_HEADER_HEIGHT + POPUP_PADDING;
    int trackBottom = rect.bottom - POPUP_PADDING;
    int trackHeight = std::max(0, trackBottom - trackTop);
    int trackLeft = rect.right - POPUP_PADDING - POPUP_SCROLLBAR_WIDTH;
    RECT track{trackLeft, trackTop, trackLeft + POPUP_SCROLLBAR_WIDTH,
               trackBottom};

    HBRUSH trackBrush = CreateSolidBrush(RGB(24, 34, 49));
    FillRect(dc, &track, trackBrush);
    DeleteObject(trackBrush);

    int maxScroll =
        GetPopupMaxScroll(accountCount,
                          static_cast<int>(rect.bottom - rect.top));
    RECT thumb = track;
    if (maxScroll > 0 && trackHeight > 0) {
        int rows = (static_cast<int>(accountCount) + POPUP_COLUMNS - 1) /
                   POPUP_COLUMNS;
        int contentHeight = POPUP_PADDING * 2 +
                            rows * POPUP_ACCOUNT_HEIGHT;
        int popupHeight = static_cast<int>(rect.bottom - rect.top);
        int visibleHeight = std::max(1, popupHeight - POPUP_HEADER_HEIGHT);
        int thumbHeight =
            std::max(28, trackHeight * visibleHeight / contentHeight);
        int travel = std::max(0, trackHeight - thumbHeight);
        int thumbTop = trackTop + travel * scrollY / maxScroll;
        thumb = RECT{trackLeft, thumbTop, trackLeft + POPUP_SCROLLBAR_WIDTH,
                     thumbTop + thumbHeight};
    }

    HBRUSH thumbBrush = CreateSolidBrush(maxScroll > 0 ? RGB(72, 96, 128)
                                                       : RGB(44, 59, 79));
    FillRect(dc, &thumb, thumbBrush);
    DeleteObject(thumbBrush);
}

void DrawRefreshButton(HDC dc, HFONT font, const RECT& buttonRect) {
    COLORREF background = RGB(27, 53, 80);
    COLORREF border = RGB(67, 103, 148);
    COLORREF text = RGB(208, 228, 255);
    if (g_refreshButtonPressed) {
        background = RGB(18, 40, 64);
        border = RGB(58, 91, 133);
        text = RGB(185, 210, 241);
    } else if (g_refreshButtonHovered) {
        background = RGB(39, 72, 108);
        border = RGB(100, 149, 210);
    }

    HBRUSH buttonBrush = CreateSolidBrush(background);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, buttonBrush);
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    RoundRect(dc, buttonRect.left, buttonRect.top, buttonRect.right,
              buttonRect.bottom, 8, 8);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(buttonBrush);

    RECT textRect = buttonRect;
    if (g_refreshButtonPressed) {
        OffsetRect(&textRect, 0, 1);
    }

    DrawPopupText(dc, font, text, L"Обновить", textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawPopupAccount(HDC dc,
                      const DashboardAccount& account,
                      const RECT& rect,
                      HFONT titleFont,
                      HFONT bodyFont,
                      HFONT smallFont) {
    HBRUSH cardBrush = CreateSolidBrush(POPUP_CARD_COLOR);
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(53, 70, 94));
    HGDIOBJ oldBrush = SelectObject(dc, cardBrush);
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(cardBrush);

    int identityRight = rect.right - 8;
    RECT aliasRect{rect.left + 8, rect.top + 7, identityRight,
                   rect.top + 27};
    if (account.reauthRequired) {
        RECT badgeRect{rect.right - 176, rect.top + 7, rect.right - 8,
                       rect.top + 29};
        HBRUSH badgeBrush = CreateSolidBrush(RGB(22, 49, 64));
        HPEN badgePen = CreatePen(PS_SOLID, 1, RGB(30, 84, 110));
        HGDIOBJ oldBadgeBrush = SelectObject(dc, badgeBrush);
        HGDIOBJ oldBadgePen = SelectObject(dc, badgePen);
        RoundRect(dc, badgeRect.left, badgeRect.top, badgeRect.right,
                  badgeRect.bottom, 8, 8);
        SelectObject(dc, oldBadgePen);
        SelectObject(dc, oldBadgeBrush);
        DeleteObject(badgePen);
        DeleteObject(badgeBrush);
        DrawPopupText(dc, smallFont, RGB(125, 211, 252),
                      L"Нужна переавторизация", badgeRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        aliasRect.right = badgeRect.left - 6;
    } else if (account.availableResetCredits > 0) {
        std::wstring resetText =
            L"Reset (" + std::to_wstring(account.availableResetCredits) + L")";
        SIZE resetSize{};
        HGDIOBJ oldFont = SelectObject(dc, smallFont);
        GetTextExtentPoint32(dc, resetText.c_str(),
                             static_cast<int>(resetText.size()), &resetSize);
        SelectObject(dc, oldFont);
        RECT badgeRect{rect.right - 8 - resetSize.cx - 12, rect.top + 20,
                       rect.right - 8, rect.top + 43};
        HBRUSH badgeBrush = CreateSolidBrush(RGB(31, 43, 59));
        HPEN badgePen = CreatePen(PS_SOLID, 1, RGB(69, 88, 114));
        HGDIOBJ oldBadgeBrush = SelectObject(dc, badgeBrush);
        HGDIOBJ oldBadgePen = SelectObject(dc, badgePen);
        RoundRect(dc, badgeRect.left, badgeRect.top, badgeRect.right,
                  badgeRect.bottom, 6, 6);
        SelectObject(dc, oldBadgePen);
        SelectObject(dc, oldBadgeBrush);
        DeleteObject(badgePen);
        DeleteObject(badgeBrush);
        DrawPopupText(dc, smallFont, RGB(220, 220, 220), resetText, badgeRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        bool expiringSoon = false;
        std::wstring expiryText = FormatResetCreditExpiry(
            account.resetCreditNearestExpiresAt, expiringSoon);
        RECT expiryRect{badgeRect.left, rect.top + 3, badgeRect.right - 6,
                        rect.top + 20};
        DrawPopupText(dc, smallFont,
                      expiringSoon ? RGB(242, 113, 113) : RGB(190, 204, 224),
                      expiryText, expiryRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        identityRight = badgeRect.left - 6;
        aliasRect.right = identityRight;
    }
    DrawPopupText(dc, titleFont, RGB(238, 238, 238), account.alias, aliasRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT emailRect{rect.left + 8, rect.top + 30, identityRight,
                   rect.top + 49};
    DrawPopupText(dc, bodyFont, RGB(198, 209, 224), account.email, emailRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    int columnWidth = account.hasPrimary
                          ? (rect.right - rect.left - 24) / 2
                          : rect.right - rect.left - 16;
    int leftX = rect.left + 8;
    int rightX = account.hasPrimary ? leftX + columnWidth + 8 : leftX;
    int labelY = rect.top + 56;
    int barY = rect.top + 78;
    int resetY = rect.top + 88;

    wchar_t primaryPercent[16];
    if (account.primaryPercent < 0) {
        wcscpy_s(primaryPercent, L"n/a");
    } else {
        swprintf_s(primaryPercent, L"%d%%", account.primaryPercent);
    }
    wchar_t secondaryPercent[16];
    if (account.secondaryPercent < 0) {
        wcscpy_s(secondaryPercent, L"n/a");
    } else {
        swprintf_s(secondaryPercent, L"%d%%", account.secondaryPercent);
    }

    if (account.hasPrimary) {
        RECT primaryLabel{leftX, labelY, leftX + columnWidth, labelY + 18};
        DrawPopupText(dc, smallFont, RGB(180, 180, 180), L"5h",
                      primaryLabel,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawPopupText(dc, smallFont, GetPercentColorRef(account.primaryPercent),
                      primaryPercent, primaryLabel,
                      DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    RECT secondaryLabel{rightX, labelY, rightX + columnWidth, labelY + 18};
    DrawPopupText(dc, smallFont, RGB(180, 180, 180),
                  account.secondaryIsMonthly ? L"Monthly" : L"Weekly",
                  secondaryLabel,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawPopupText(dc, smallFont, GetPercentColorRef(account.secondaryPercent),
                  secondaryPercent, secondaryLabel,
                  DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (account.hasPrimary) {
        DrawLimitBar(dc, leftX, barY, columnWidth, account.primaryPercent,
                     GetPercentColorRef(account.primaryPercent));
    }
    DrawLimitBar(dc, rightX, barY, columnWidth, account.secondaryPercent,
                 GetPercentColorRef(account.secondaryPercent));

    if (account.hasPrimary) {
        std::wstring primaryResetText = L"in ";
        primaryResetText += account.primaryResetText;
        RECT primaryReset{leftX, resetY, leftX + columnWidth, resetY + 18};
        DrawPopupText(dc, smallFont, RGB(178, 193, 213), primaryResetText,
                      primaryReset,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    std::wstring secondaryResetText = account.secondaryResetText.empty()
                                         ? L"n/a"
                                         : L"in " + account.secondaryResetText;
    RECT secondaryReset{rightX, resetY, rightX + columnWidth, resetY + 18};
    DrawPopupText(dc, smallFont, RGB(178, 193, 213), secondaryResetText,
                  secondaryReset,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
}

}  // namespace codex_dashboard
