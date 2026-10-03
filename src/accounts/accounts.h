#pragma once

#include "session.h"
#include "../core/time.h"

namespace codex_dashboard {

bool IsDashboardAccountVisibleStatus(const std::wstring& status) {
    return status == L"active" || status == L"rate_limited" ||
           status == L"quota_exceeded" || status == L"reauth_required";
}

bool FormatDashboardAccounts(const std::wstring& body) {
    wdj::JsonObject root = nullptr;
    if (!wdj::JsonObject::TryParse(body, root)) {
        Wh_Log(L"Accounts JSON parse failed, body: %s",
               TruncateForLog(body).c_str());
        return false;
    }

    wdj::JsonArray accounts =
        root.GetNamedArray(L"accounts", wdj::JsonArray{nullptr});
    if (!accounts) {
        Wh_Log(L"Accounts JSON has no accounts array, body: %s",
               TruncateForLog(body).c_str());
        return false;
    }

    std::vector<DashboardAccount> entries;
    for (uint32_t i = 0; i < accounts.Size(); i++) {
        wdj::JsonObject account = accounts.GetObjectAt(i);

        std::wstring email;
        TryGetJsonString(account, L"email", email);

        std::wstring alias;
        TryGetJsonString(account, L"alias", alias);
        alias = TrimString(alias);

        std::wstring status;
        if (!TryGetJsonString(account, L"status", status) ||
            !IsDashboardAccountVisibleStatus(status)) {
            continue;
        }
        bool reauthRequired = status == L"reauth_required";

        wdj::JsonObject usage = nullptr;
        if (!TryGetJsonObject(account, L"usage", usage) && !reauthRequired) {
            Wh_Log(L"Skipping account without usage object");
            continue;
        }

        double secondaryPercentRaw = 0;
        std::wstring resetAtSecondary;
        double monthlyPercentRaw = 0;
        std::wstring resetAtMonthly;
        double monthlyWindowMinutes = 0;
        bool hasMonthlyPercent =
            TryGetJsonNumber(usage, L"monthlyRemainingPercent", monthlyPercentRaw);
        bool hasMonthlyReset =
            TryGetJsonString(account, L"resetAtMonthly", resetAtMonthly);
        bool hasMonthlyWindow =
            TryGetJsonNumber(account, L"windowMinutesMonthly", monthlyWindowMinutes);
        bool secondaryIsMonthly =
            hasMonthlyPercent || hasMonthlyReset || hasMonthlyWindow;
        bool hasSecondaryPercent =
            secondaryIsMonthly
                ? hasMonthlyPercent
                : TryGetJsonNumber(usage, L"secondaryRemainingPercent",
                                   secondaryPercentRaw);
        bool hasSecondaryReset =
            secondaryIsMonthly
                ? hasMonthlyReset
                : TryGetJsonString(account, L"resetAtSecondary", resetAtSecondary);
        if (secondaryIsMonthly) {
            secondaryPercentRaw = monthlyPercentRaw;
            resetAtSecondary = resetAtMonthly;
        }
        if ((!hasSecondaryPercent || !hasSecondaryReset) &&
            !reauthRequired && !secondaryIsMonthly) {
            Wh_Log(L"Skipping account without weekly limit fields");
            continue;
        }

        int secondaryPercent =
            hasSecondaryPercent
                ? static_cast<int>(std::round(secondaryPercentRaw))
                : -1;
        std::wstring secondaryResetText =
            FormatRemainingCompact(resetAtSecondary);
        std::wstring secondaryDaysText = FormatRemainingDays(resetAtSecondary);
        if ((secondaryResetText.empty() || secondaryDaysText.empty()) &&
            !reauthRequired && !secondaryIsMonthly) {
            Wh_Log(L"Skipping account with invalid weekly reset timestamp");
            continue;
        }

        double primaryPercentRaw = 0;
        bool hasPrimary = false;
        int primaryPercent = -1;
        if (TryGetJsonNumber(usage, L"primaryRemainingPercent",
                             primaryPercentRaw)) {
            primaryPercent = static_cast<int>(std::round(primaryPercentRaw));
            hasPrimary = true;
        }

        std::wstring resetAtPrimary;
        std::wstring primaryResetText = L"n/a";
        if (TryGetJsonString(account, L"resetAtPrimary", resetAtPrimary)) {
            std::wstring formattedReset =
                FormatRemainingCompact(resetAtPrimary);
            if (!formattedReset.empty()) {
                primaryResetText = formattedReset;
                hasPrimary = true;
            }
        }

        double primaryWindowMinutes = 0;
        if (TryGetJsonNumber(account, L"windowMinutesPrimary",
                             primaryWindowMinutes)) {
            hasPrimary = true;
        }

        if (alias.empty()) {
            alias = L"Account";
        }

        double resetCreditsRaw = 0;
        int availableResetCredits = -1;
        std::wstring resetCreditNearestExpiresAt;
        if (!reauthRequired &&
            TryGetJsonNumber(account, L"availableResetCredits", resetCreditsRaw) &&
            std::isfinite(resetCreditsRaw) && resetCreditsRaw >= 0 &&
            resetCreditsRaw <= INT_MAX && std::floor(resetCreditsRaw) == resetCreditsRaw) {
            availableResetCredits = static_cast<int>(resetCreditsRaw);
            if (availableResetCredits > 0) {
                TryGetJsonString(account, L"resetCreditNearestExpiresAt",
                                 resetCreditNearestExpiresAt);
            }
        }

        entries.push_back(DashboardAccount{
            alias, email, hasPrimary, primaryPercent, secondaryPercent,
            primaryResetText, secondaryResetText, secondaryDaysText,
            reauthRequired, secondaryIsMonthly, availableResetCredits,
            resetCreditNearestExpiresAt});
    }

    if (entries.empty()) {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardAccounts.clear();
        return true;
    }

    std::sort(entries.begin(), entries.end(),
              [](const DashboardAccount& left,
                 const DashboardAccount& right) {
                  int leftPercent = left.secondaryPercent < 0
                                        ? 101
                                        : left.secondaryPercent;
                  int rightPercent = right.secondaryPercent < 0
                                         ? 101
                                         : right.secondaryPercent;
                  return leftPercent < rightPercent;
              });

    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardAccounts = entries;
    }

    return true;
}

bool FetchDashboardAccounts(const Settings& settings) {
    std::wstring cookie;
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        cookie = g_dashboardCookie;
    }

    if (!EnsureDashboardSession(settings, cookie)) {
        return false;
    }

    HttpResponse response;
    if (!HttpRequest(settings, L"GET", L"/api/accounts", cookie, {}, response)) {
        return false;
    }

    if (response.status == 401 || response.status == 403) {
        if (!LoginDashboard(settings, cookie)) {
            return false;
        }

        response = {};
        if (!HttpRequest(settings, L"GET", L"/api/accounts", cookie, {}, response)) {
            return false;
        }
    }

    if (response.status < 200 || response.status >= 300 ||
        !FormatDashboardAccounts(response.body)) {
        Wh_Log(L"Dashboard accounts request failed, status: %u, body: %s",
               response.status, TruncateForLog(response.body).c_str());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardCookie = cookie;
    }

    return true;
}

}  // namespace codex_dashboard
