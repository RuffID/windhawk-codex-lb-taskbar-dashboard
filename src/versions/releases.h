#pragma once

#include "semver.h"
#include "release_cache.h"
#include "../network/http.h"
#include "../core/state.h"

namespace codex_dashboard {

std::wstring FormatGitHubLogValue(std::wstring value, size_t maxLength = 256) {
    if (value.empty()) {
        return L"<missing>";
    }
    bool truncated = value.size() > maxLength;
    if (truncated) {
        value.resize(maxLength);
    }
    for (wchar_t& ch : value) {
        if (ch < L' ' || ch == 127) {
            ch = L' ';
        }
    }
    if (truncated) {
        value += L"...";
    }
    return value;
}

std::wstring GetGitHubErrorMessage(const std::wstring& body) {
    if (body.empty()) {
        return L"<empty response>";
    }
    wdj::JsonObject json = nullptr;
    if (!wdj::JsonObject::TryParse(body, json)) {
        return L"<non-JSON response>";
    }
    std::wstring message;
    TryGetJsonString(json, L"message", message);
    return FormatGitHubLogValue(message);
}

bool FindLatestPublishedRelease(const std::wstring& body, std::wstring& latest) {
    latest.clear();
    wdj::JsonArray releases = nullptr;
    if (!wdj::JsonArray::TryParse(body, releases)) {
        return false;
    }
    ReleaseVersion newest;
    bool found = false;
    for (uint32_t i = 0; i < releases.Size(); i++) {
        auto item = releases.GetAt(i);
        if (item.ValueType() != wdj::JsonValueType::Object) {
            return false;
        }
        auto release = item.GetObject();
        bool draft = false;
        if (!TryGetJsonBoolean(release, L"draft", draft)) {
            return false;
        }
        if (draft) {
            continue;
        }
        std::wstring tag;
        ReleaseVersion candidate;
        if (!TryGetJsonString(release, L"tag_name", tag) ||
            !ParseReleaseVersion(tag, candidate)) {
            continue;
        }
        if (!found || CompareReleaseVersions(candidate, newest) > 0) {
            newest = std::move(candidate);
            latest = TrimString(tag);
            found = true;
        }
    }
    return found;
}

bool ParseDashboardVersionStatus(const std::wstring& body,
                                 std::wstring& current,
                                 bool& latestUpdateAvailable) {
    current.clear();
    latestUpdateAvailable = false;
    wdj::JsonObject json = nullptr;
    std::wstring parsedCurrent;
    bool parsedUpdateAvailable = false;
    if (!wdj::JsonObject::TryParse(body, json) ||
        !TryGetJsonString(json, L"currentVersion", parsedCurrent) ||
        !TryGetJsonBoolean(json, L"updateAvailable", parsedUpdateAvailable)) {
        return false;
    }
    parsedCurrent = TrimString(parsedCurrent);
    if (parsedCurrent.empty()) {
        return false;
    }
    current = parsedCurrent;
    latestUpdateAvailable = parsedUpdateAvailable;
    return true;
}

bool TryParseReleaseSeconds(const std::wstring& value, ULONGLONG& result) {
    std::wstring text = TrimString(value);
    if (text.empty()) {
        return false;
    }
    ULONGLONG parsed = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9' || parsed > (ULLONG_MAX - (ch - L'0')) / 10) {
            return false;
        }
        parsed = parsed * 10 + (ch - L'0');
    }
    result = parsed;
    return true;
}

bool IsGitHubRateLimited(const HttpResponse& response) {
    if (response.status == 429) {
        return true;
    }
    if (response.status != 403) {
        return false;
    }
    if (TrimString(response.rateLimitRemaining) == L"0" || !TrimString(response.retryAfter).empty()) {
        return true;
    }
    std::wstring message = GetGitHubErrorMessage(response.body);
    std::transform(message.begin(), message.end(), message.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + L'a' - L'A') : ch;
    });
    return message.find(L"rate limit") != std::wstring::npos ||
           message.find(L"abuse detection") != std::wstring::npos;
}

ULONGLONG GetGitHubNextCheckUtc(const HttpResponse& response, ULONGLONG now) {
    ULONGLONG next = now + RELEASE_CHECK_INTERVAL_SECONDS;
    bool limited = IsGitHubRateLimited(response);
    constexpr ULONGLONG maxTimestamp = 253402300799ULL;
    bool hasDeadline = false;
    ULONGLONG reset = 0;
    if ((limited || TrimString(response.rateLimitRemaining) == L"0") &&
        TryParseReleaseSeconds(response.rateLimitReset, reset) && reset > now && reset < maxTimestamp) {
        next = std::max(next, reset + 1);
        hasDeadline = true;
    }
    ULONGLONG retry = 0;
    if (TryParseReleaseSeconds(response.retryAfter, retry) && retry <= maxTimestamp - now) {
        next = std::max(next, now + retry);
        hasDeadline = true;
    } else if (response.retryAfter.find(L',') != std::wstring::npos &&
               response.retryAfter.ends_with(L" GMT")) {
        SYSTEMTIME time{};
        FILETIME fileTime{};
        if (WinHttpTimeToSystemTime(response.retryAfter.c_str(), &time) &&
            SystemTimeToFileTime(&time, &fileTime)) {
            ULARGE_INTEGER ticks{};
            ticks.LowPart = fileTime.dwLowDateTime;
            ticks.HighPart = fileTime.dwHighDateTime;
            if (ticks.QuadPart >= 116444736000000000ULL) {
                ULONGLONG deadline = (ticks.QuadPart - 116444736000000000ULL) / 10000000ULL;
                if (deadline > now) {
                    next = std::max(next, deadline + 1);
                    hasDeadline = true;
                }
            }
        }
    }
    return hasDeadline || !limited ? next : now + RELEASE_RATE_LIMIT_FALLBACK_SECONDS;
}

void RefreshGitHubRelease(const Settings& settings) {
    ReleaseCacheData data;
    try {
        SharedReleaseCache cache;
        if (!cache.Open(GetReleaseCachePath(), g_dashboardCancelEvent)) {
            return;
        }
        data = cache.Read();
        if (ReserveReleaseCheck(data, ReleaseNowUtc())) {
            // Persist before sending: cancellation, unloading or a crash cannot bypass the interval.
            cache.Write(data);
            Settings githubSettings = settings;
            githubSettings.dashboardUrl = L"https://api.github.com";
            githubSettings.requestTimeoutSeconds = 10;
            HttpResponse response;
            bool succeeded = false;
            try {
                succeeded = HttpRequest(githubSettings, L"GET",
                                        L"/repos/Soju06/codex-lb/releases?per_page=100", {}, {}, response,
                                        {}, true) &&
                            response.status >= 200 && response.status < 300 &&
                            FindLatestPublishedRelease(response.body, data.latest);
            } catch (...) {
                LogException(L"GitHubReleaseRequest", CurrentExceptionHResult());
            }
            data.status = succeeded ? ReleaseCheckStatus::Succeeded
                                    : IsGitHubRateLimited(response) ? ReleaseCheckStatus::RateLimited
                                                                  : ReleaseCheckStatus::Failed;
            if (!succeeded) {
                data.latest.clear();
            }
            data.nextCheckUtc = GetGitHubNextCheckUtc(response, ReleaseNowUtc());
            cache.Write(data);
            if (!succeeded && !IsDashboardRefreshCancelled()) {
                Wh_Log(L"GitHub release check failed, status: %u", response.status);
                Wh_Log(L"GitHub response message: %s", GetGitHubErrorMessage(response.body).c_str());
                Wh_Log(L"GitHub rate limits: X-RateLimit-Remaining=%s; X-RateLimit-Reset=%s; Retry-After=%s",
                       FormatGitHubLogValue(response.rateLimitRemaining, 64).c_str(),
                       FormatGitHubLogValue(response.rateLimitReset, 64).c_str(),
                       FormatGitHubLogValue(response.retryAfter, 64).c_str());
                Wh_Log(L"GitHub next check UTC (Unix seconds): %llu", data.nextCheckUtc);
            }
        }
    } catch (...) {
        // A broken shared cache must not turn into an uncoordinated request.
        LogException(L"GitHubReleaseCache", CurrentExceptionHResult());
        data.status = ReleaseCheckStatus::Failed;
        data.latest.clear();
    }
    if (IsDashboardRefreshCancelled()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_dashboardMutex);
    g_dashboardVersion.latest = data.latest;
    g_dashboardVersion.releaseCheckStatus = data.status;
}

void RefreshDashboardVersion(const Settings& settings, bool accountsFetched) {
    std::wstring current;
    bool latestUpdateAvailable = false;
    if (accountsFetched) {
        try {
            std::wstring cookie;
            {
                std::lock_guard<std::mutex> lock(g_dashboardMutex);
                cookie = g_dashboardCookie;
            }
            HttpResponse response;
            if (HttpRequest(settings, L"GET", L"/api/runtime/version", cookie, {}, response) &&
                response.status >= 200 && response.status < 300) {
                ParseDashboardVersionStatus(response.body, current,
                                            latestUpdateAvailable);
            }
            if (current.empty() && !IsDashboardRefreshCancelled()) {
                Wh_Log(L"Dashboard version request failed, status: %u", response.status);
            }
        } catch (...) {
            LogException(L"DashboardVersionRefresh", CurrentExceptionHResult());
        }
    }
    if (IsDashboardRefreshCancelled()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardVersion.current = current;
        g_dashboardVersion.latestUpdateAvailable = latestUpdateAvailable;
    }
    RefreshGitHubRelease(settings);
}

}  // namespace codex_dashboard
