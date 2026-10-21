#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <exception>

#include "src/integration/lifecycle.h"

using namespace codex_dashboard;

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class LocalHttpServer {
public:
    enum class Mode { Reply, StallHeaders, StallBody, SlowResponse };

    explicit LocalHttpServer(Mode mode) : m_mode(mode) {
        m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(m_listener != INVALID_SOCKET, "socket failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Require(bind(m_listener, reinterpret_cast<sockaddr*>(&address),
                     sizeof(address)) == 0, "bind failed");
        int length = sizeof(address);
        Require(getsockname(m_listener, reinterpret_cast<sockaddr*>(&address),
                            &length) == 0, "getsockname failed");
        m_port = ntohs(address.sin_port);
        Require(listen(m_listener, 1) == 0, "listen failed");
        m_thread = std::thread([this] { Serve(); });
    }

    ~LocalHttpServer() {
        SetEvent(m_stop.handle);
        shutdown(m_listener, SD_BOTH);
        closesocket(m_listener);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    std::wstring Url() const {
        return L"http://127.0.0.1:" + std::to_wstring(m_port);
    }

    void WaitUntilRequested() {
        Require(WaitForSingleObject(m_ready.handle, 5000) == WAIT_OBJECT_0,
                "local request did not arrive");
    }

    std::string Request() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_request;
    }

private:
    void Serve() {
        SOCKET client = accept(m_listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            return;
        }
        DWORD timeout = 3000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::string request;
        char buffer[2048];
        while (request.find("\r\n\r\n") == std::string::npos) {
            int count = recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) {
                closesocket(client);
                return;
            }
            request.append(buffer, count);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_request = request;
        }
        SetEvent(m_ready.handle);
        if (m_mode == Mode::StallHeaders) {
            WaitForSingleObject(m_stop.handle, INFINITE);
        } else {
            std::string body = "{\"account_pool_usage\":{\"secondary\":54}}";
            if (m_mode == Mode::SlowResponse &&
                WaitForSingleObject(m_stop.handle, 650) != WAIT_TIMEOUT) {
                closesocket(client);
                return;
            }
            std::string headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                  "Connection: close\r\nContent-Length: " +
                                  std::to_string(body.size()) + "\r\n\r\n";
            send(client, headers.data(), static_cast<int>(headers.size()), 0);
            if (m_mode == Mode::StallBody) {
                send(client, body.data(), 1, 0);
                WaitForSingleObject(m_stop.handle, INFINITE);
            } else if (m_mode != Mode::SlowResponse ||
                       WaitForSingleObject(m_stop.handle, 650) == WAIT_TIMEOUT) {
                send(client, body.data(), static_cast<int>(body.size()), 0);
            }
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    Mode m_mode;
    SOCKET m_listener = INVALID_SOCKET;
    unsigned short m_port = 0;
    EventHandle m_stop;
    EventHandle m_ready;
    std::thread m_thread;
    std::mutex m_mutex;
    std::string m_request;
};

void ResumeRefreshes() {
    Require(ResetEvent(g_dashboardCancelEvent) != FALSE, "reset cancellation failed");
    g_dashboardUpdatesPaused = false;
    g_unloading = false;
}

void TestQuotaAccounts() {
    Require(FormatDashboardAccounts(LR"({"accounts":[
        {"alias":"Monthly","email":"monthly@example.test","status":"active",
         "usage":{"monthlyRemainingPercent":42},"windowMinutesMonthly":43200},
        {"alias":"Weekly","email":"weekly@example.test","status":"active",
         "usage":{"secondaryRemainingPercent":60},"resetAtSecondary":"2099-01-01T00:00:00Z"},
        {"alias":"Reauth","email":"reauth@example.test","status":"reauth_required","usage":null}
    ]})"), "account payload failed");
    Require(g_dashboardAccounts.size() == 3, "monthly or reauth account was hidden");
    Require(g_dashboardAccounts[0].secondaryIsMonthly &&
                g_dashboardAccounts[0].secondaryPercent == 42 &&
                g_dashboardAccounts[0].secondaryResetText.empty(),
            "monthly quota or missing reset was misread");
    Require(!g_dashboardAccounts[1].secondaryIsMonthly &&
                g_dashboardAccounts[1].secondaryPercent == 60,
            "weekly quota changed");
    Require(g_dashboardAccounts[2].reauthRequired &&
                g_dashboardAccounts[2].secondaryPercent == -1,
            "reauth badge state was lost");
    std::cout << "PASS monthly, weekly and reauth account payloads\n";
}

void TestUrlAndSnapshot() {
    ResumeRefreshes();
    LocalHttpServer server(LocalHttpServer::Mode::Reply);
    Settings settings;
    settings.dashboardUrl = server.Url() + L"/";
    settings.apiKeyEnvName = L"CODEX_LB_MOD_REGRESSION_API_KEY";
    Require(SetEnvironmentVariable(settings.apiKeyEnvName.c_str(), L"fake-test-key") != FALSE,
            "test key setup failed");
    g_settings.dashboardUrl = L"http://127.0.0.1:1";
    int percent = -1;
    Require(FetchApiKeyRemainingPercent(settings, percent) && percent == 54,
            "HTTP request ignored settings snapshot or trailing slash");
    Require(server.Request().starts_with("GET /v1/usage HTTP/1.1\r\n"),
            "request path contains a doubled slash");
    SetEnvironmentVariable(settings.apiKeyEnvName.c_str(), nullptr);
    Require(FormatDashboardAddress(L"https://example.test:443/") == L"example.test" &&
                FormatDashboardAddress(L"https://example.test:8443/") == L"example.test:8443",
            "dashboard host or port display changed");
    std::cout << "PASS HTTP URL, settings snapshot and host display\n";
}

void TestCancellation(LocalHttpServer::Mode mode) {
    ResumeRefreshes();
    LocalHttpServer server(mode);
    Settings settings;
    settings.dashboardUrl = server.Url();
    bool succeeded = true;
    std::thread request([&] {
        HttpResponse response;
        succeeded = HttpRequest(settings, L"GET", L"/stall", {}, {}, response);
    });
    server.WaitUntilRequested();
    Sleep(100);
    ULONGLONG started = GetTickCount64();
    SetEvent(g_dashboardCancelEvent);
    request.join();
    Require(!succeeded && GetTickCount64() - started < 2000,
            "cancellation waited for network timeout");
    std::cout << "PASS cancellation during "
              << (mode == LocalHttpServer::Mode::StallBody ? "body" : "headers") << "\n";
}

void TestTotalDeadline() {
    ResumeRefreshes();
    LocalHttpServer server(LocalHttpServer::Mode::SlowResponse);
    Settings settings;
    settings.dashboardUrl = server.Url();
    settings.requestTimeoutSeconds = 1;
    HttpResponse response;
    ULONGLONG started = GetTickCount64();
    Require(!HttpRequest(settings, L"GET", L"/slow", {}, {}, response) &&
                GetTickCount64() - started < 1800,
            "request timeout was restarted between headers and body");
    std::cout << "PASS total HTTP deadline\n";
}

void TestManualRefreshPause() {
    ResumeRefreshes();
    LocalHttpServer server(LocalHttpServer::Mode::StallHeaders);
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        g_settings.dashboardUrl = server.Url();
        g_settings.apiKeyEnvName = L"CODEX_LB_MOD_REGRESSION_API_KEY";
    }
    SetEnvironmentVariable(L"CODEX_LB_MOD_REGRESSION_API_KEY", L"fake-test-key");
    g_dashboardText = L"unchanged";
    QueueDashboardRefresh();
    server.WaitUntilRequested();
    ULONGLONG started = GetTickCount64();
    PauseDashboardUpdates();
    Require(GetTickCount64() - started < 2000 &&
                !g_dashboardRefreshInProgress && !g_dashboardRefreshWork &&
                g_dashboardText == L"unchanged",
            "manual refresh survived pause or published cancelled data");
    QueueDashboardRefresh();
    Require(!g_dashboardRefreshWork, "paused mod accepted new manual work");
    SetEnvironmentVariable(L"CODEX_LB_MOD_REGRESSION_API_KEY", nullptr);
    std::cout << "PASS manual refresh pause, cancellation and publication\n";
}

std::wstring FutureIsoUtc(ULONGLONG seconds) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER value{};
    value.LowPart = now.dwLowDateTime;
    value.HighPart = now.dwHighDateTime;
    value.QuadPart += seconds * 10000000ULL;
    FILETIME future{value.LowPart, value.HighPart};
    SYSTEMTIME time{};
    Require(FileTimeToSystemTime(&future, &time) != FALSE, "UTC conversion failed");
    wchar_t buffer[32];
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear,
               time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

void TestResetCredits() {
    Require(FormatDashboardAccounts(LR"({"accounts":[
        {"alias":"Unknown","status":"active","usage":{"monthlyRemainingPercent":10}},
        {"alias":"Zero","status":"active","usage":{"monthlyRemainingPercent":20},
         "availableResetCredits":0,"resetCreditNearestExpiresAt":"2099-01-01T00:00:00Z"},
        {"alias":"CountOnly","status":"active","usage":{"monthlyRemainingPercent":30},
         "availableResetCredits":2},
        {"alias":"Expiry","status":"active","usage":{"monthlyRemainingPercent":40},
         "availableResetCredits":3,"resetCreditNearestExpiresAt":"2099-01-01T00:00:00Z"},
        {"alias":"Fraction","status":"active","usage":{"monthlyRemainingPercent":50},
         "availableResetCredits":1.5},
        {"alias":"Reauth","status":"reauth_required","usage":null,
         "availableResetCredits":9,"resetCreditNearestExpiresAt":"2099-01-01T00:00:00Z"}
    ]})"), "reset payload failed");
    Require(g_dashboardAccounts.size() == 6, "reset accounts were hidden");
    Require(g_dashboardAccounts[0].availableResetCredits == -1, "unknown count became known");
    Require(g_dashboardAccounts[1].availableResetCredits == 0 &&
                g_dashboardAccounts[1].resetCreditNearestExpiresAt.empty(),
            "zero credits retained an expiry");
    Require(g_dashboardAccounts[2].availableResetCredits == 2 &&
                g_dashboardAccounts[2].resetCreditNearestExpiresAt.empty(),
            "unknown expiry hid the count");
    Require(g_dashboardAccounts[3].availableResetCredits == 3 &&
                !g_dashboardAccounts[3].resetCreditNearestExpiresAt.empty(),
            "known credit expiry was lost");
    Require(g_dashboardAccounts[4].availableResetCredits == -1, "fractional count was accepted");
    Require(g_dashboardAccounts[5].reauthRequired &&
                g_dashboardAccounts[5].availableResetCredits == -1 &&
                g_dashboardAccounts[5].resetCreditNearestExpiresAt.empty(),
            "reauth account retained reset badge data");
    bool red = true;
    Require(FormatResetCreditExpiry(L"", red).empty() && !red, "unknown expiry changed");
    Require(!FormatResetCreditExpiry(FutureIsoUtc(7 * 24 * 60 * 60), red).empty() && red,
            "seven-day expiry is not red");
    Require(!FormatResetCreditExpiry(FutureIsoUtc(7 * 24 * 60 * 60 + 60), red).empty() && !red,
            "expiry above seven days is red");
    Require(FormatResetCreditExpiry(L"2000-01-01T00:00:00Z", red) == L"0m" && red,
            "expired credit changed");
    std::cout << "PASS reset credits, missing expiry and seven-day threshold\n";
}

void TestVersions() {
    Require(IsNewerReleaseVersion(L"1.2.3-beta.10", L"1.2.3-beta.9"),
            "beta versions were compared lexically");
    Require(IsNewerReleaseVersion(L"1.2.3", L"1.2.3-beta.10") &&
                !IsNewerReleaseVersion(L"1.2.3-beta.10", L"1.2.3"),
            "stable/prerelease precedence changed");
    Require(!IsNewerReleaseVersion(L"v1.2.3+build.2", L"1.2.3+build.1"),
            "build metadata affected precedence");
    ReleaseVersion invalid;
    Require(!ParseReleaseVersion(L"01.2.3", invalid) &&
                !ParseReleaseVersion(L"1.2.3-beta.01", invalid),
            "leading zero versions were accepted");
    Require(IsNewerReleaseVersion(L"999999999999999999999.0.0", L"9.0.0"),
            "numeric version comparison overflowed");
    std::wstring latest;
    Require(FindLatestPublishedRelease(LR"([
        {"draft":false,"tag_name":"v1.2.3-beta.9","prerelease":true},
        {"draft":false,"tag_name":"v1.2.3-beta.10","prerelease":true},
        {"draft":true,"tag_name":"v9.0.0"},
        {"draft":false,"tag_name":"not-a-version"}
    ])", latest) && latest == L"v1.2.3-beta.10", "release selection lost the GitHub tag");
    Require(!FindLatestPublishedRelease(L"[]", latest) &&
                !FindLatestPublishedRelease(L"{}", latest),
            "invalid or empty release checks became successful");
    DashboardVersion version;
    Require(ParseDashboardVersionStatus(
                LR"({"currentVersion":"1.2.3","updateAvailable":false})",
                version.current, version.latestUpdateAvailable),
            "runtime version payload failed");
    version.latest = L"9.0.0";
    version.releaseCheckStatus = ReleaseCheckStatus::Succeeded;
    Require(!ShouldShowLatestUpdateIndicator(version, 50), "GitHub affected taskbar indicator");
    version.latestUpdateAvailable = true;
    version.releaseCheckStatus = ReleaseCheckStatus::Failed;
    Require(ShouldShowLatestUpdateIndicator(version, 50) &&
                !ShouldShowLatestUpdateIndicator(version, -1),
            "runtime latest indicator or offline visibility changed");
    Require(!ParseDashboardVersionStatus(
                LR"({"currentVersion":"1.2.3"})", version.current,
                version.latestUpdateAvailable) &&
                version.current.empty() && !version.latestUpdateAvailable,
            "malformed runtime payload retained old data");
    std::cout << "PASS SemVer, beta releases, failures and independent latest indicator\n";
}

void TestGitHubDiagnostics() {
    Require(GetGitHubErrorMessage(
                LR"({"message":"API rate limit exceeded","documentation_url":"https://docs.github.com","other":"not logged"})") ==
                L"API rate limit exceeded",
            "GitHub diagnostic lost message or included unrelated response fields");
    Require(GetGitHubErrorMessage(LR"({"message":"Denied\r\nnext\tline"})") == L"Denied  next line",
            "GitHub response injected log lines");
    Require(GetGitHubErrorMessage(L"<html>Forbidden</html>") == L"<non-JSON response>" &&
                GetGitHubErrorMessage(L"") == L"<empty response>" &&
                GetGitHubErrorMessage(LR"({"message":403})") == L"<missing>",
            "missing or malformed GitHub error details were misrepresented");
    Require(FormatGitHubLogValue(L"0", 64) == L"0" &&
                FormatGitHubLogValue(L"1792600000", 64) == L"1792600000" &&
                FormatGitHubLogValue(L"120", 64) == L"120" &&
                FormatGitHubLogValue(L"", 64) == L"<missing>" &&
                FormatGitHubLogValue(std::wstring(300, L'x')).size() == 259,
            "rate-limit diagnostics lost zero values or failed to bound output");
    std::cout << "PASS GitHub diagnostic message extraction, missing fields and bounded log output\n";
}

void TestPopupReleaseStatus() {
    DashboardVersion version;
    Require(GetPopupReleaseBadge(version).releaseUrl.empty(),
            "pending check exposed a release link");
    version.current = L"1.25.0-beta.9";
    version.latest = L"v1.25.0-beta.10";
    version.releaseCheckStatus = ReleaseCheckStatus::Succeeded;
    auto badge = GetPopupReleaseBadge(version);
    Require(badge.text == L"Доступно: v1.25.0-beta.10" &&
                badge.releaseUrl == L"https://github.com/Soju06/codex-lb/releases/tag/v1.25.0-beta.10" &&
                badge.borderColor == RGB(106, 212, 124) &&
                badge.textColor == RGB(198, 209, 224),
            "new beta did not expose its exact green release link");
    version.current = L"1.25.0-beta.10";
    badge = GetPopupReleaseBadge(version);
    Require(badge.text == L"Нет новых версий" && badge.releaseUrl.empty() &&
                badge.borderColor == RGB(46, 62, 84) &&
                badge.textColor == RGB(198, 209, 224),
            "equal installed version did not report up-to-date");
    version.current = L"1.25.0";
    Require(GetPopupReleaseBadge(version).text == L"Нет новых версий",
            "older beta was offered over installed stable release");
    version.current.clear();
    Require(GetPopupReleaseBadge(version).text != L"Нет новых версий" &&
                GetPopupReleaseBadge(version).releaseUrl.empty(),
            "unknown installed version was reported up-to-date");
    version.releaseCheckStatus = ReleaseCheckStatus::Failed;
    badge = GetPopupReleaseBadge(version);
    Require(badge.text == L"Не удалось получить новые версии с GitHub" &&
                badge.borderColor == RGB(242, 113, 113) &&
                badge.textColor == RGB(198, 209, 224) && badge.releaseUrl.empty(),
            "failed check retained a stale release link or success state");
    version.releaseCheckStatus = ReleaseCheckStatus::RateLimited;
    badge = GetPopupReleaseBadge(version);
    Require(badge.text == L"Превышено число запросов в GitHub" &&
                badge.borderColor == RGB(242, 113, 113) &&
                badge.textColor == RGB(198, 209, 224) && badge.releaseUrl.empty(),
            "rate-limit status was lost or colored its text");
    std::cout << "PASS popup release states and exact tag links\n";
}

void TestGitHubRateLimitSchedule() {
    ReleaseCacheData cache;
    Require(ReserveReleaseCheck(cache, 1000) && cache.nextCheckUtc == 1120 &&
                !ReserveReleaseCheck(cache, 1000) && !ReserveReleaseCheck(cache, 1119) &&
                ReserveReleaseCheck(cache, 1120),
            "repeated/manual checks bypassed the two-minute interval");
    HttpResponse response;
    response.status = 403;
    response.body = LR"({"message":"Resource not accessible"})";
    Require(!IsGitHubRateLimited(response) && GetGitHubNextCheckUtc(response, 1000) == 1120,
            "ordinary forbidden response was misclassified as a rate limit");
    response.rateLimitRemaining = L"0";
    response.rateLimitReset = L"4600";
    Require(IsGitHubRateLimited(response) && GetGitHubNextCheckUtc(response, 1000) == 4601,
            "primary rate limit did not honor the reset timestamp");
    response.status = 200;
    Require(!IsGitHubRateLimited(response) && GetGitHubNextCheckUtc(response, 1000) == 4601,
            "successful final quota request ignored reset or became an error");
    response.status = 403;
    response.retryAfter = L"7200";
    Require(GetGitHubNextCheckUtc(response, 1000) == 8200,
            "Retry-After was ignored when later than reset");
    response = {};
    response.status = 403;
    response.body = LR"({"message":"You have exceeded a secondary RATE LIMIT."})";
    Require(IsGitHubRateLimited(response) && GetGitHubNextCheckUtc(response, 1000) == 4600,
            "secondary rate limit without headers was not deferred");
    response.status = 429;
    response.body.clear();
    response.retryAfter = L"Thu, 01 Jan 1970 01:00:00 GMT";
    Require(GetGitHubNextCheckUtc(response, 1000) == 3601,
            "HTTP-date Retry-After was not respected");
    response.retryAfter = L"1";
    Require(GetGitHubNextCheckUtc(response, 1000) == 1120,
            "short Retry-After bypassed the minimum interval");
    response.retryAfter = L"99999999999999999999999999";
    response.rateLimitReset = L"invalid";
    Require(GetGitHubNextCheckUtc(response, 1000) == 4600,
            "malformed rate-limit timestamps overflowed or disabled the backoff");
    response.status = 503;
    response.retryAfter = L"300";
    Require(!IsGitHubRateLimited(response) && GetGitHubNextCheckUtc(response, 1000) == 1300,
            "server Retry-After was ignored or mislabeled as rate limiting");
    std::cout << "PASS GitHub throttling, primary/secondary limits and retry deadlines\n";
}

void TestSharedReleaseCache() {
    wchar_t temporaryPath[MAX_PATH];
    DWORD length = GetTempPathW(ARRAYSIZE(temporaryPath), temporaryPath);
    Require(length > 0 && length < ARRAYSIZE(temporaryPath), "test temp path failed");
    GUID id{};
    winrt::check_hresult(CoCreateGuid(&id));
    wchar_t idText[40];
    Require(StringFromGUID2(id, idText, ARRAYSIZE(idText)) > 0, "test GUID failed");
    std::wstring path = std::wstring(temporaryPath) + L"codex-lb-cache-" + idText + L".json";
    struct Cleanup {
        std::wstring path;
        ~Cleanup() { DeleteFileW(path.c_str()); DeleteFileW((path + L".tmp").c_str()); }
    } cleanup{path};
    EventHandle start, cancel;
    std::atomic<int> requestCount = 0;
    std::array<ReleaseCacheData, 2> results;
    std::array<std::exception_ptr, 2> errors;
    auto worker = [&](size_t index) {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            Require(WaitForSingleObject(start.handle, 5000) == WAIT_OBJECT_0, "test start timed out");
            SharedReleaseCache shared;
            Require(shared.Open(path, cancel.handle), "test lock was cancelled");
            auto data = shared.Read();
            if (ReserveReleaseCheck(data, 1000)) {
                shared.Write(data);
                requestCount++;
                data.status = ReleaseCheckStatus::RateLimited;
                data.nextCheckUtc = 5000;
                shared.Write(data);
            }
            results[index] = data;
            winrt::uninit_apartment();
        } catch (...) {
            errors[index] = std::current_exception();
        }
    };
    std::thread first(worker, 0), second(worker, 1);
    SetEvent(start.handle);
    first.join();
    second.join();
    for (const auto& error : errors) {
        if (error) { std::rethrow_exception(error); }
    }
    Require(requestCount == 1 && results[0].status == ReleaseCheckStatus::RateLimited &&
                results[1].status == ReleaseCheckStatus::RateLimited &&
                results[0].nextCheckUtc == 5000 && results[1].nextCheckUtc == 5000,
            "independent cache clients duplicated a request or lost the shared rate limit");
    {
        SharedReleaseCache shared;
        Require(shared.Open(path, cancel.handle), "reopened cache was cancelled");
        auto data = shared.Read();
        Require(!ReserveReleaseCheck(data, 4999) && ReserveReleaseCheck(data, 5000),
                "cache reopen bypassed the persisted cooldown");
        shared.Write(data);
    }
    {
        SharedReleaseCache shared;
        Require(shared.Open(path, cancel.handle), "reservation reopen was cancelled");
        auto data = shared.Read();
        Require(!ReserveReleaseCheck(data, 5000) && data.nextCheckUtc == 5120,
                "unfinished request lost its persisted reservation");
        data.status = ReleaseCheckStatus::Succeeded;
        data.latest = L"v1.25.0-beta.10";
        shared.Write(data);
        auto reloaded = shared.Read();
        Require(reloaded.latest == L"v1.25.0-beta.10" &&
                    reloaded.status == ReleaseCheckStatus::Succeeded,
                "cached success lost its exact release tag");
    }
    SetEvent(cancel.handle);
    SharedReleaseCache cancelled;
    Require(!cancelled.Open(path, cancel.handle), "cache ignored refresh cancellation");
    std::cout << "PASS shared release cache contention, persistence, reservation and cancellation\n";
}

void TestRenderingMathAndLogin() {
    Require(PremultiplyPopupPixel(0) == 0, "transparent pixel changed");
    Require((PremultiplyPopupPixel(PopupDibColor(POPUP_BACKGROUND_COLOR)) >> 24) == 224 &&
                (PremultiplyPopupPixel(PopupDibColor(POPUP_HEADER_COLOR)) >> 24) == 232 &&
                (PremultiplyPopupPixel(PopupDibColor(POPUP_CARD_COLOR)) >> 24) == 240,
            "surface transparency changed");
    const DWORD background = PremultiplyPopupPixel(PopupDibColor(POPUP_BACKGROUND_COLOR));
    Require(BlendPopupTextPixel(background, RGB(242, 113, 113), 0) == background,
            "zero coverage changed background");
    Require(BlendPopupTextPixel(background, RGB(242, 113, 113), 255) == 0xFFF27171,
            "glyph interior lost opaque status color");
    Require((BlendPopupTextPixel(background, RGB(242, 113, 113), 128) >> 24) == 240,
            "antialiased edge alpha changed");
    Require(ScalePopupLength(100, 96) == 100 && ScalePopupLength(100, 144) == 150 &&
                ScalePopupLength(100, 192) == 200, "DPI scaling changed");
    Require(GetPopupMaxScroll(4, 340) == 0 && GetPopupMaxScroll(6, 340) > 0,
            "four-account fit or scrolling changed");
    const std::wstring username = L"user\"\\\t";
    const std::wstring password = L"pass\n\r";
    wdj::JsonObject payload = wdj::JsonObject::Parse(GetDashboardLoginPayload(username, password));
    Require(payload.GetNamedString(L"username") == username &&
                payload.GetNamedString(L"password") == password,
            "login escaping changed");
    Require(FormatDashboardAddress(L"https://example.test:443/") == L"example.test" &&
                FormatDashboardAddress(L"https://example.test:8443/") == L"example.test:8443",
            "dashboard host/port display changed");
    std::cout << "PASS alpha blending, DPI, scrolling, host display and login escaping\n";
}

void TestPopupLinksAndIcon() {
    g_popupGithubRect = RECT{100, 15, 116, 31};
    g_popupModGithubRect = RECT{200, 41, 216, 57};
    g_popupReleaseRect = RECT{300, 38, 420, 60};
    g_popupReleaseUrl = L"https://github.com/Soju06/codex-lb/releases/tag/v1.25.0-beta.10";
    Require(GetPopupLinkAt(POINT{108, 23}) == L"https://github.com/Soju06/codex-lb/releases" &&
                GetPopupLinkAt(POINT{208, 49}) == L"https://github.com/RuffID/windhawk-codex-lb-taskbar-dashboard" &&
                GetPopupLinkAt(POINT{190, 49}).empty() &&
                GetPopupLinkAt(POINT{216, 49}).empty() &&
                GetPopupLinkAt(POINT{320, 48}) == g_popupReleaseUrl &&
                GetPopupLinkAt(POINT{250, 48}).empty() &&
                GetPopupLinkAt(POINT{116, 23}).empty(),
            "popup link hit targets overlap text or lose tag URL");
    g_popupReleaseUrl.clear();
    Require(GetPopupLinkAt(POINT{320, 48}).empty(),
            "non-update status retained a release link");
    g_popupGithubRect = {};
    g_popupModGithubRect = {};
    g_popupReleaseRect = {};

    for (UINT dpi : {96U, 144U, 192U}) {
        PopupCanvas canvas;
        canvas.Create(nullptr, 64, 64, dpi);
        const DWORD background = PremultiplyPopupPixel(PopupDibColor(POPUP_HEADER_COLOR));
        std::fill_n(canvas.pixels, 64 * 64, background);
        SetPopupDcScale(canvas.dc, dpi);
        DrawPopupGithubIcon(canvas, RECT{4, 4, 20, 20});
        bool opaque = false, antialiased = false;
        for (int i = 0; i < 64 * 64; i++) {
            DWORD alpha = canvas.pixels[i] >> 24;
            opaque = opaque || alpha == 255;
            antialiased = antialiased || (alpha > 232 && alpha < 255);
        }
        Require(opaque && antialiased && canvas.pixels[0] == background &&
                    canvas.pixels[63 * 64 + 63] == background,
                "GitHub icon lost its fill, antialiasing or drawing bounds at DPI");
    }
    std::cout << "PASS popup link targets and GitHub icon alpha at 96/144/192 DPI\n";
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--offline") {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            TestQuotaAccounts();
            TestResetCredits();
            TestVersions();
            TestGitHubDiagnostics();
            TestGitHubRateLimitSchedule();
            TestSharedReleaseCache();
            TestPopupReleaseStatus();
            TestRenderingMathAndLogin();
            TestPopupLinksAndIcon();
            winrt::uninit_apartment();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << error.what() << '\n';
        } catch (...) {
            std::cerr << "FAIL HRESULT=" << std::hex << winrt::to_hresult().value << '\n';
        }
        return 1;
    }
    if (argc != 1) {
        std::cerr << "Usage: regression.exe [--offline]\n";
        return 1;
    }

    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        return 1;
    }
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        g_dashboardCancelEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        Require(g_dashboardCancelEvent != nullptr, "cancellation event failed");
        TestQuotaAccounts();
        TestUrlAndSnapshot();
        TestCancellation(LocalHttpServer::Mode::StallHeaders);
        TestCancellation(LocalHttpServer::Mode::StallBody);
        TestTotalDeadline();
        TestManualRefreshPause();
        CloseHandle(g_dashboardCancelEvent);
        g_dashboardCancelEvent = nullptr;
        winrt::uninit_apartment();
        WSACleanup();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
    } catch (...) {
        std::cerr << "FAIL HRESULT=" << std::hex << winrt::to_hresult().value << '\n';
    }
    SetEvent(g_dashboardCancelEvent);
    StopDashboardTimer();
    StopDashboardRefreshWork();
    if (g_dashboardCancelEvent) {
        CloseHandle(g_dashboardCancelEvent);
    }
    WSACleanup();
    return 1;
}
