# network

http.h содержит транспорт WinHTTP: WinHttpHandle, EventHandle, AsyncHttpState,
callback, HttpResponse, извлечение dashboard cookie и HttpRequest.

Запрос получает Settings snapshot, cookie и bearer явно. Сохраняй единый deadline
для всех этапов запроса, отмену через g_dashboardCancelEvent и время жизни async
context/read buffer до финального callback закрытия дескриптора. Не увеличивай
исходные ограничения повторов/таймаутов и не сохраняй секреты в логах.
Авторизация dashboard принадлежит accounts/session.h; этот модуль её не выполняет.
GitHub-запросы в versions/releases.h передают пустые cookie и bearer.
HttpRequest по явному collectRateLimitHeaders считывает X-RateLimit-Remaining,
X-RateLimit-Reset и Retry-After в HttpResponse; отсутствующий заголовок даёт пустую
строку, неожиданные ошибки чтения не скрываются. Статус публикуется сразу после
получения заголовков, даже если чтение тела позже завершилось ошибкой.

## Актуальность

При изменении кода, тестов или конфигурации этой папки в той же задаче обновляй
этот AGENTS.md: описание файлов, зависимостей и правил должно соответствовать
результату. При изменении структуры актуализируй также AGENTS.md родительской
папки. Это правило для работы над проектом, а не автоматическое слежение за файлами.
