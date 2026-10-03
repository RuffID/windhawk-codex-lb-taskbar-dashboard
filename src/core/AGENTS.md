# core

Общие функции и единый владелец данных, используемых несколькими областями.

- platform.h: подключения Win32/WinRT/STL, namespace aliases и дескриптор текущей DLL.
- state.h: Settings, DashboardAccount, DashboardVersion, WidgetState,
  TaskbarRootState, общие константы и исходные глобальные объекты/блокировки.
- logging.h: Windhawk log и файловый журнал с исходным резервным путём.
- text_json.h: UTF-8/UTF-16, trim, чтение environment, JSON helpers и адрес dashboard.
- time.h: UTC/FILETIME, форматирование сроков; Reset становится красным при <= 7 дней,
  неизвестный срок даёт пустой countdown, истёкший срок — 0m.
- refresh_api.h: объявления QueueDashboardRefresh, RefreshDashboardText и
  StopDashboardRefreshWork для связи UI с реализацией integration/runtime.h.

Общее состояние определено только в state.h, включаемом один раз на TU.
Не создавай его копии и не добавляй inline для сокрытия нарушений владения.
COM watcher имеет отдельного владельца в integration/xaml_tap.h.

## Актуальность

При изменении кода, тестов или конфигурации этой папки в той же задаче обновляй
этот AGENTS.md: описание файлов, зависимостей и правил должно соответствовать
результату. При изменении структуры актуализируй также AGENTS.md родительской
папки. Это правило для работы над проектом, а не автоматическое слежение за файлами.
