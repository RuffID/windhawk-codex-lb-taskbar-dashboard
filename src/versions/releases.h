#pragma once

#include "semver.h"
#include "../network/http.h"
#include "../core/state.h"

namespace codex_dashboard {

bool FindLatestPublishedRelease(const std::wstring& body, std::wstring& latest) {
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
            found = true;
        }
    }
    if (found) {
        latest = newest.text;
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
        if (GetTickCount64() < g_nextReleaseCheckTick) {
            return;
        }
        g_nextReleaseCheckTick = GetTickCount64() + RELEASE_CHECK_FAILURE_INTERVAL_MS;
    }

    std::wstring latest;
    bool succeeded = false;
    try {
        Settings githubSettings = settings;
        githubSettings.dashboardUrl = L"https://api.github.com";
        githubSettings.requestTimeoutSeconds = 10;
        HttpResponse response;
        succeeded = HttpRequest(githubSettings, L"GET",
                                L"/repos/Soju06/codex-lb/releases?per_page=100", {}, {}, response) &&
                    response.status >= 200 && response.status < 300 &&
                    FindLatestPublishedRelease(response.body, latest);
        if (!succeeded && !IsDashboardRefreshCancelled()) {
            Wh_Log(L"GitHub release check failed, status: %u", response.status);
        }
    } catch (...) {
        LogException(L"GitHubReleaseRefresh", CurrentExceptionHResult());
    }
    if (IsDashboardRefreshCancelled()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_dashboardMutex);
    g_dashboardVersion.latest = succeeded ? latest : L"";
    g_dashboardVersion.releaseCheckStatus =
        succeeded ? ReleaseCheckStatus::Succeeded : ReleaseCheckStatus::Failed;
    g_nextReleaseCheckTick = GetTickCount64() +
                            (succeeded ? RELEASE_CHECK_INTERVAL_MS
                                       : RELEASE_CHECK_FAILURE_INTERVAL_MS);
}

}  // namespace codex_dashboard
