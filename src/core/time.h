#pragma once

#include "platform.h"

namespace codex_dashboard {

bool ParseIsoUtcFileTime(const std::wstring& isoUtc, FILETIME& fileTime) {
    if (isoUtc.size() < 20 || isoUtc[4] != L'-' || isoUtc[7] != L'-' ||
        isoUtc[10] != L'T' || isoUtc[13] != L':' || isoUtc[16] != L':') {
        return false;
    }

    auto parseDecimal = [&isoUtc](size_t start, size_t length, int& value) {
        value = 0;
        for (size_t i = start; i < start + length; i++) {
            wchar_t ch = isoUtc[i];
            if (ch < L'0' || ch > L'9') {
                return false;
            }

            value = value * 10 + (ch - L'0');
        }

        return true;
    };

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!parseDecimal(0, 4, year) || !parseDecimal(5, 2, month) ||
        !parseDecimal(8, 2, day) || !parseDecimal(11, 2, hour) ||
        !parseDecimal(14, 2, minute) || !parseDecimal(17, 2, second)) {
        return false;
    }

    size_t suffix = 19;
    if (suffix < isoUtc.size() && isoUtc[suffix] == L'.') {
        suffix++;
        size_t fractionStart = suffix;
        while (suffix < isoUtc.size() && isoUtc[suffix] >= L'0' &&
               isoUtc[suffix] <= L'9') {
            suffix++;
        }

        if (suffix == fractionStart) {
            return false;
        }
    }

    if (suffix + 1 != isoUtc.size() || isoUtc[suffix] != L'Z') {
        return false;
    }

    SYSTEMTIME resetTime{};
    resetTime.wYear = static_cast<WORD>(year);
    resetTime.wMonth = static_cast<WORD>(month);
    resetTime.wDay = static_cast<WORD>(day);
    resetTime.wHour = static_cast<WORD>(hour);
    resetTime.wMinute = static_cast<WORD>(minute);
    resetTime.wSecond = static_cast<WORD>(second);

    return SystemTimeToFileTime(&resetTime, &fileTime) != FALSE;
}

bool TryGetRemainingTicks(const std::wstring& isoUtc,
                          ULONGLONG& remainingTicks) {
    FILETIME resetFileTime{};
    if (!ParseIsoUtcFileTime(isoUtc, resetFileTime)) {
        return false;
    }

    remainingTicks = 0;

    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);

    ULARGE_INTEGER reset{};
    reset.LowPart = resetFileTime.dwLowDateTime;
    reset.HighPart = resetFileTime.dwHighDateTime;

    ULARGE_INTEGER now{};
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;

    if (reset.QuadPart <= now.QuadPart) {
        return true;
    }

    remainingTicks = reset.QuadPart - now.QuadPart;
    return true;
}

bool TryGetRemainingMinutes(const std::wstring& isoUtc,
                            ULONGLONG& remainingMinutes) {
    ULONGLONG remainingTicks = 0;
    if (!TryGetRemainingTicks(isoUtc, remainingTicks)) {
        return false;
    }
    remainingMinutes = remainingTicks / (10000000ULL * 60ULL);
    return true;
}

std::wstring FormatResetCreditExpiry(const std::wstring& isoUtc,
                                    bool& expiringSoon) {
    expiringSoon = false;
    ULONGLONG remainingTicks = 0;
    if (!TryGetRemainingTicks(isoUtc, remainingTicks)) {
        return {};
    }
    expiringSoon = remainingTicks <= 7ULL * 24ULL * 60ULL * 60ULL * 10000000ULL;
    ULONGLONG minutes = remainingTicks / (60ULL * 10000000ULL);
    if (minutes >= 24ULL * 60ULL) {
        return std::to_wstring(minutes / (24ULL * 60ULL)) + L"d";
    }
    if (minutes >= 60ULL) {
        return std::to_wstring(minutes / 60ULL) + L"h";
    }
    if (minutes > 0) {
        return std::to_wstring(minutes) + L"m";
    }
    return remainingTicks > 0 ? L"<1m" : L"0m";
}

std::wstring FormatRemainingDays(const std::wstring& isoUtc) {
    ULONGLONG totalMinutes = 0;
    if (!TryGetRemainingMinutes(isoUtc, totalMinutes)) {
        return {};
    }

    if (totalMinutes == 0) {
        return L"0дн.";
    }

    ULONGLONG days = totalMinutes / (60ULL * 24ULL);

    wchar_t buffer[32];
    swprintf_s(buffer, L"%lluдн.", days);
    return buffer;
}

std::wstring FormatRemainingCompact(const std::wstring& isoUtc) {
    ULONGLONG totalMinutes = 0;
    if (!TryGetRemainingMinutes(isoUtc, totalMinutes)) {
        return {};
    }

    if (totalMinutes == 0) {
        return L"0m";
    }

    ULONGLONG days = totalMinutes / (60ULL * 24ULL);
    ULONGLONG hours = (totalMinutes / 60ULL) % 24ULL;
    ULONGLONG minutes = totalMinutes % 60ULL;

    wchar_t buffer[32];
    if (days > 0) {
        swprintf_s(buffer, L"%llud %lluh", days, hours);
    } else if (hours > 0) {
        swprintf_s(buffer, L"%lluh %llum", hours, minutes);
    } else {
        swprintf_s(buffer, L"%llum", minutes);
    }

    return buffer;
}

}  // namespace codex_dashboard
