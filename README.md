# Kodex

Нативный клиент Codex для KDE: C++23, Qt Quick, Kirigami и C++ backend.
Чат, история, Plan mode, вопросы, подтверждения, модели, Fast mode и MCP работают
через **Codex app-server**. `codex exec --json` не используется.

## Сборка и запуск

Проверенная среда: Qt **6.11.2**, KDE Frameworks/Kirigami **6.30.0**,
Codex CLI **0.156.1**, GCC **16.2.1**, CMake **4.4.3**. Понадобятся Ninja,
Gettext, Qt Core/Gui/Widgets/Qml/Quick/QuickControls2/Network/Test,
KF6 CoreAddons/Config/I18n/Kirigami/Wallet/SyntaxHighlighting,
`qqc2-desktop-style`, Python 3 для тестов и Extra CMake Modules.

Если ECM отсутствует, можно установить его только для этой сборки:

```sh
./scripts/bootstrap-ecm.sh
```

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
./build/kodex
```

Необязательная установка в меню приложений:

```sh
cmake --install build --prefix "$HOME/.local"
```

`--settings` открывает настройки, `--offline` не подключается к Codex.
`--smoke-test` завершает приложение после загрузки интерфейса;
`--screenshot /tmp/kodex.png` сохраняет снимок окна для проверки оформления.

## Работа

Добавьте локальный каталог как проект и создайте задачу. Задача — thread Codex;
задачи одного проекта используют общий каталог. Существующие CLI-сессии с этим
`cwd` появляются в списке задач. Архив скрывает задачу, не удаляя файлы.
При запуске восстанавливаются последний проект и задача; черновики сохраняются
отдельно для каждой задачи. `Ctrl+N` — новая задача, `Ctrl+Enter` — отправка,
`Ctrl+,` — настройки, `Ctrl+Shift+D` — панель изменений.

Модель, reasoning effort и доступный Fast tier берутся из каталога сервера.
Компактная строка над полем ввода содержит меню модели, effort, доступа и
переключатели Plan/Fast. Изменения режима относятся к следующим ходам.
«Настройки сессии» восстанавливают разрешения, с которыми сессия была открыта.
Серверные ограничения имеют приоритет; отклонённое изменение не применяется.

Поддерживаются `/plan`, `/compact`, `/model [id]`, `/permissions`, `/fast`,
`/new`, `/resume`, `/review`, `/status`, `/mcp`, `/help`.
Выполнение `/review` проверяет незакоммиченные изменения текущего каталога.
Во время ответа отправка сообщения использует `turn/steer`; остановка —
`turn/interrupt`. При ошибке отправки текст остаётся в редакторе.

Git-панель разделяет рабочий каталог и diff ответа Codex. Коммит включает
только индекс. Push и Sync требуют уже настроенного upstream. Sync выполняет
fetch, fast-forward и push; грязное дерево или разошедшиеся ветки требуют
решения пользователя. Credentials и SSH остаются у системного Git. Для
интерактивной авторизации настройте системный credential helper/SSH agent.

## Codex, авторизация и прокси

По умолчанию используется `codex` из PATH. Можно выбрать другой executable,
включая `codex-proxy`, и отдельные аргументы. Shell для запуска Codex не нужен.
Транспорт подключается через `codex app-server proxy` к общему daemon. Этот
proxy передаёт WebSocket-поток с HTTP Upgrade, а не JSONL. Приложение проверяет
handshake, разбирает кадры, отвечает на ping и маршрутизирует JSON-RPC в обе
стороны. Разрыв соединения не приводит к повторной отправке пользовательской
команды. Закрытие Kodex не останавливает daemon или другие клиенты.

Вход ChatGPT выполняет app-server через системный браузер либо device code.
Kodex не читает `auth.json`, не запрашивает API key и не управляет токенами.
Существующая CLI-авторизация продолжает работать. Выход затрагивает общий
аккаунт Codex и требует подтверждения в интерфейсе.

В настройках прокси доступны наследование окружения, отключение и свой URL:
HTTP/HTTPS/SOCKS5/SOCKS5h. Пароль вводится отдельно и хранится в KWallet;
имя пользователя можно указать в URL. Окружение передаётся дочерним процессам,
с исключениями localhost через `NO_PROXY`. Wrapper и внешний updater могут
переопределить это окружение своими настройками.

Работающий daemon сохраняет окружение старого запуска. После сохранения новых
настроек используйте отдельную кнопку перезапуска. Она предупреждает о
прерывании активных задач всех клиентов. Перезапуск не происходит автоматически.

## Обновление и remote-control

При первом запуске обнаруживается `codex-update`, если он есть в PATH.
Можно выбрать внешний updater с аргументами либо встроенный установщик для
standalone-версии Codex. Встроенное обновление скачивает официальный
`https://chatgpt.com/codex/install.sh` во временный файл и запускает его только
после успешной HTTPS-загрузки. Для загрузки есть резервный прокси. Ошибка
самой установки требует явного повтора; частично выполненный установщик
автоматически не перезапускается. Пакетные установки не заменяются
standalone-версией. Журнал обновления очищается от URL credentials.

Remote-control включает штатный доступ к этому компьютеру. Pairing вызывает
`remoteControl/pairing/start` через app-server — штатное сопряжение Codex;
показываются код, срок действия и успешное подключение. Код остаётся только в памяти окна.
Отключение remote-control сохраняет локальный daemon. Для сопряжения нужен
телефон пользователя; подключение Kodex к другим компьютерам в v0.1 не входит.

## Проверки

```sh
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/kodex --offline --smoke-test
KODEX_LIVE_TEST=1 ./build/kodex_tests liveHandshake
# Необязательно: один короткий реальный ответ, расходует лимит аккаунта.
KODEX_LIVE_TURN=1 ./build/kodex_tests liveTurn
# Полный сценарий приложения: временный проект, файл, diff, история,
# окно Kodex и локальный коммит. Использует настоящий аккаунт Codex.
KODEX_LIVE_WORKFLOW=1 ./build/kodex_live_tests
# Реальные вопросы/план/реализация, модели, остановка, approvals и архив.
QT_QPA_PLATFORMTHEME=kde KODEX_LIVE_INTERACTIVE=1 ./build/kodex_interactive_tests
# Два отдельных процесса с настоящими KDE-палитрами Light/Dark.
./scripts/test-native-ui.sh
```

Дополнительная проверка сериализации по JSON Schema (зависимости только для тестов):

```sh
python -m pip install --target .deps/python -r tests/requirements.txt
KODEX_VALIDATE_SCHEMA=1 PYTHONPATH="$PWD/.deps/python" ./build/kodex_tests conversation
```

Обычные тесты используют изолированные настройки, fake app-server и временные
Git-репозитории с локальным bare remote. Реальный тест ответа использует
временный каталог, ephemeral thread и read-only sandbox; файловые инструменты
не запрашиваются. Обновление установленного Codex, вход/выход и pairing не
выполняются обычными тестами.

Подробная матрица реальных проверок и найденных исправлений:
[docs/verification.md](docs/verification.md).

## Границы v0.1

Протокол experimental; эталон — `protocol/codex-0.156.1.json`, полученный
командой `codex app-server generate-json-schema --experimental`. Требуется
версия с daemon/proxy и project RPC. Неподдерживаемые серверные запросы
отклоняются явно. Стандартные MCP формы и URL flows поддерживаются;
device attestation и дополнительные proprietary формы не эмулируются.
Нет редактора кода, merge/rebase UI или менеджера Git worktree.
Большой diff отображается до 2 МиБ. Секреты прокси остаются в KWallet,
настройки и черновики — в `kodexrc`; полные RPC payload не журналируются.

Интерактивный вход, настоящее сопряжение телефона и обновление рабочей
установки требуют ручной приёмки. Доступность лимитов, моделей и Fast зависит
от аккаунта и сервера.

Архитектура: `RpcClient` — транспорт, `AppController` — сессии и протокол,
`ListModel` — данные QML, `GitService` — Git, `Settings` — KConfig/KWallet,
`Maintenance` — daemon/remote-control/updates. QML отвечает за представление.
