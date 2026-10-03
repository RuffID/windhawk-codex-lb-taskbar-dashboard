# integration

- xaml_tap.h: VisualTreeWatcher, его g_visualTreeWatcher, WindhawkTAP,
  COM factory/exports, инъекция XAML diagnostics и инициализация UI-потока.
- windows.h: RunFromWindowThread, хуки CreateWindowExW/CreateWindowInBand/Ex,
  обнаружение XAML-host окон и публикация текста на taskbar-потоках.
- runtime.h: очередь manual refresh, scheduled refresh, отмена/повторы,
  dashboard timer и восстановление taskbar.
- lifecycle.h: InitializeMod, AfterInitializeMod, ApplySettingsChanged,
  UninitializeMod; вызываются короткими глобальными entrypoints корневого мода.

COM exports остаются глобальными, остальные функции — в codex_dashboard.
Сохраняй порядок запросов и публикации данных, пять попыток usage, отмену retry waits,
завершение scheduled/manual задач перед применением настроек и очистку при unloading.
При выгрузке закрываются popup, события/revokers, timers/work, watcher и cancel event.
Не запускай lifecycle или Explorer для проверки без явного разрешения пользователя.

## Актуальность

При изменении кода, тестов или конфигурации этой папки в той же задаче обновляй
этот AGENTS.md: описание файлов, зависимостей и правил должно соответствовать
результату. При изменении структуры актуализируй также AGENTS.md родительской
папки. Это правило для работы над проектом, а не автоматическое слежение за файлами.
