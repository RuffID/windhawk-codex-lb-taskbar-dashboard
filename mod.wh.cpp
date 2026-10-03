// ==WindhawkMod==
// @id              codex-lb-taskbar-dashboard
// @name            Codex LB Taskbar Dashboard
// @description     Shows a compact codex-lb dashboard in the Windows 11 taskbar
// @version         1.1
// @author          P.D.G.
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lgdi32 -lwinhttp -I"D:/Media/User/source/repos/windhawk-codex-lb-taskbar-dashboard"
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Codex LB Taskbar Dashboard

Shows a compact codex-lb dashboard in the Windows 11 taskbar, immediately to
the left of the system tray area.

Project: [Soju06/codex-lb](https://github.com/Soju06/codex-lb)

## What is displayed

The taskbar requests `/v1/usage` with the configured API key and displays the
remaining weekly percentage from `account_pool_usage.secondary` as one compact
value, for example `54%`.

## Colors

The percent part is colored by remaining percent:

- `61%` and higher: green.
- `31%` through `60%`: yellow.
- Less than `31%`: red.

The same thresholds should be used for any displayed codex-lb limit percentage.

## Popup

Clicking the taskbar widget opens a details popup. Clicking anywhere outside
that popup closes it. The popup uses two account cards per row, fits at least
four accounts without scrolling, and stays scrollable when there are more
accounts than fit.

The mod tracks primary and secondary taskbars separately and checks their
XAML trees every five seconds to restore a removed widget or a replaced tray.

Popup accounts are sorted by remaining percent in their long limit window:
weekly or monthly. The account with the lowest remaining percent appears first.
Each popup item shows alias, email, and every available limit window.
When the 5-hour window is absent, it is hidden and the weekly window uses the
full card width.
Accounts with a monthly limit show `Monthly` instead of `Weekly`; a missing
reset timestamp is shown as `n/a` and does not hide a monthly account.
Accounts with `status = reauth_required` remain visible and have a blue
`Нужна переавторизация` badge beside the alias. Sign in to that account again
from the codex-lb dashboard. This status means the refresh token needs repair;
the account may still serve requests until its current access token expires.
Other accounts with a known positive `availableResetCredits` show `Reset (N)`
in the top-right corner. The nearest credit's expiry appears above the count,
in days, hours, or minutes, and turns red at seven days or less. Unknown or zero
counts hide the badge; an unknown expiry hides only the countdown.
Missing limit values are shown as `n/a`; accounts without a weekly percentage
are placed after accounts with a known weekly percentage.

The popup header also shows `Осталось: <N%>`, the dashboard host (with the port
when it differs from 443), and the mod version. Account data is requested
independently through the dashboard session, so API-key usage remains available
when dashboard authentication, including 2FA, prevents account access.
The popup also requests `/api/runtime/version` for the running codex-lb version
and independently checks the 100 most recent published GitHub releases,
including prereleases. Versions are compared by SemVer, so `beta.10` is newer
than `beta.9` and a stable release is newer than a beta of the same version.
A newer version is highlighted in yellow beside the running version and URL.
The taskbar shows a blue circled exclamation mark beside the percentage only
when `/api/runtime/version` reports `updateAvailable: true`. This indicator
uses codex-lb's latest-release check, independently of the popup's beta check.
GitHub checks are cached for one hour, or fifteen minutes after a failure;
manual refresh checks again. Failed checks are shown as unavailable, not as
confirmation that the installed version is up to date. GitHub requests never
include the dashboard cookie or proxy API key.

The popup has a manual refresh button in the top-right area. The text to the
right of the button shows the local time of the last successful update. The
button has simple hover and pressed states, and the popup always draws a
scrollbar indicator on the right side.
The popup uses a layered window with a translucent dark background, rounded
corners, a blue-gray outline, and softly tinted account cards. Transparency
applies to the background surfaces; text and status colors retain opaque glyph
interiors and correctly blended antialiased edges. Popup fonts use 12-14 logical
pixels and the layout scales with the monitor's DPI.

## Network and auth

The mod requests usage with `Authorization: Bearer <API key>`. Separately, it
checks the dashboard session before logging in. If the current session cookie
still works, login is skipped. If `/api/accounts` returns `401` or `403`, the
mod logs in again and retries `/api/accounts` once.

If the API-key usage endpoint is unreachable for five attempts, the widget
shows:

`Сервер недоступен`

The next scheduled refresh tries again.

Each HTTP request has a total deadline set by the request timeout. Changing
settings or disabling the mod cancels in-flight requests and retry waits.
Settings changes finish both scheduled and manual refresh work before applying
the new settings. Every refresh uses one settings snapshot for all its requests.
Trailing slashes in the dashboard base URL are removed when building requests.

## Error log

When guarded callback or refresh code catches an exception, the mod writes an
entry to the Windhawk log and also appends a file log.

Primary log path:

`C:\codex-lb-taskbar-dashboard.log`

If Explorer cannot create or write that file, the fallback path is:

`%LOCALAPPDATA%\CodexLbTaskbarDashboard\dashboard.log`

## Dashboard login and API key setup

The mod uses your existing codex-lb dashboard username and password to load
the account cards. These are separate from the API key used for the taskbar
percentage. On a default installation the dashboard username is `admin`; if
you renamed it or use another dashboard user, enter that user's actual name.
The mod requires a username even when the dashboard's single-account login
screen only asks for a password.

Open PowerShell as your normal Windows user (administrator rights are not
required), paste the following block, and enter the values at the prompts:

```powershell
$login = Read-Host 'Dashboard username'
$password = Read-Host 'Dashboard password' -AsSecureString
$apiKey = Read-Host 'Codex LB API key' -AsSecureString

[Environment]::SetEnvironmentVariable('CODEX_LB_DASHBOARD_USERNAME', $login, 'User')
[Environment]::SetEnvironmentVariable(
    'CODEX_LB_DASHBOARD_PASSWORD',
    [System.Net.NetworkCredential]::new('', $password).Password,
    'User'
)
[Environment]::SetEnvironmentVariable(
    'CODEX_LB_API_KEY',
    [System.Net.NetworkCredential]::new('', $apiKey).Password,
    'User'
)
```

Type the values directly, without adding surrounding quotes. Quotes,
backticks, and dollar signs entered at these prompts are saved literally.
Password and API key input is hidden, but user environment variables store
the resulting values as plain text.

To replace only the saved dashboard login, run the same block without the
`$apiKey` prompt and the `CODEX_LB_API_KEY` call. These commands update the
mod's saved credentials; to change the account password itself, use
codex-lb **Settings → Access**, then update the mod's saved password.

The variable names above are the defaults. If you changed `usernameEnvName`,
`passwordEnvName`, or `apiKeyEnvName` in the mod settings, use those configured
names in the commands instead.

After setting or replacing the values, sign out of Windows and sign back in
so Explorer and the mod receive the updated environment. Toggling the mod in
an already running Explorer does not reload environment variables.
Accounts that require dashboard 2FA cannot sign in through this mod; the API-key
taskbar percentage still works independently.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- dashboardUrl: "https://sonmp.ru:53950"
  $name: Dashboard URL
  $description: Base URL of the codex-lb dashboard, including the port.
- usernameEnvName: "CODEX_LB_DASHBOARD_USERNAME"
  $name: Username environment variable
  $description: User environment variable that contains the dashboard username.
- passwordEnvName: "CODEX_LB_DASHBOARD_PASSWORD"
  $name: Password environment variable
  $description: User environment variable that contains the dashboard password.
- apiKeyEnvName: "CODEX_LB_API_KEY"
  $name: API key environment variable
  $description: User environment variable that contains the codex-lb API key used to request the remaining weekly pool limit.
- updateIntervalSeconds: 60
  $name: Update interval, seconds
  $description: How often the taskbar text is refreshed. Values are clamped to the safe range from 30 to 2147483.
- requestTimeoutSeconds: 30
  $name: Request timeout, seconds
  $description: Timeout for dashboard HTTP requests. Values are clamped to the safe range from 30 to 2147483.
*/
// ==/WindhawkModSettings==

#include "src/integration/lifecycle.h"

BOOL Wh_ModInit() {
    return codex_dashboard::InitializeMod();
}

void Wh_ModSettingsChanged() {
    codex_dashboard::ApplySettingsChanged();
}

void Wh_ModAfterInit() {
    codex_dashboard::AfterInitializeMod();
}

void Wh_ModUninit() {
    codex_dashboard::UninitializeMod();
}
