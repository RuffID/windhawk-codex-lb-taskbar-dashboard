#pragma once

#include "platform.h"

namespace codex_dashboard {

// UI submits work; integration/runtime.h owns scheduling and refresh execution.
void QueueDashboardRefresh();
void RefreshDashboardText();
void StopDashboardRefreshWork();



}  // namespace codex_dashboard
