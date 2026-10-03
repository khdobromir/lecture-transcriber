# Проверка каталогов, прогресса и кэша — 3 октября 2026

Проверено рабочее дерево `dev` поверх `2d51055`. Версия CMake остаётся 0.2.1;
изменения описаны в разделе разработки CHANGELOG и пока не опубликованы.

## Результаты

| Проверка | Результат |
|---|---|
| Arch, GCC 16.2.1 Debug, `-Werror` | Все шесть CTest suites прошли |
| Ubuntu 24.04, GCC 13.3.0 Release, `-Werror` | Сборка и все шесть suites прошли |
| Ubuntu 24.04, Clang 18.1.3 Release, `-Werror` | Сборка и все шесть suites прошли |
| Debian 13, GCC 14.2.0 Release, `-Werror` | Сборка и все шесть suites прошли |
| Clang 23.1.1, ASan/UBSan | Все шесть suites прошли с `ASAN_OPTIONS=detect_leaks=0` |
| Повтор сценариев `cache_` | 20 успешных повторов |
| Повтор сценариев `reliability_` | 20 успешных повторов |
| clang-tidy 18.1.3 / cppcheck 2.13.0 | Все 16 собственных translation units прошли обязательные проверки |
| Bash syntax / ShellCheck 0.9.0 и 0.10.0 | Установщик и все shell-скрипты прошли |
| Реальный Whisper | 5 секунд JFK, medium-q5_0, Silero VAD 6.2.0, `ru`; экспорты и статус проверены |
| Реальный yt-dlp и кэш без сети | Технический WAV загружен с локального HTTP-сервера; после остановки сервера повторная транскрипция успешна |

Полный запуск содержит **158 сценариев**: 73 pipeline, 19 installation,
11 smoke_preflight, 22 unit_cli, 24 unit_audio и 9 unit_features.
CI автоматически включает новый suite и дополнен 20 повторами сценариев кэша.

Проверены имена с кириллицей, `%`, управляющими символами и разделителями;
коллизии и переименование без перезаписи; размещение по умолчанию в HOME;
ETA с управляемыми часами и неравные длительности частей; прогресс через
псевдотерминал, `TERM=dumb`, перенаправление stdout и отключение прогресса.
Проверки кэша покрывают URL без сети, псевдонимы, контексты авторизации,
обновление, неудачное обновление, LRU, снижение лимита, повреждение, symlink,
конкурентные загрузки, live-потоки, отмену скачивания и ожидания блокировки.
Сохранены регрессии SIGINT/SIGPIPE, падения после частичного вывода и объединения.

## Изоляция и ограничения

Docker-сокет недоступен. Ubuntu/Debian проверены через bubblewrap с отдельными
user/PID namespaces, UID 1000, read-only проектом и rootfs, writable рабочими
каталогами под `/tmp` и отдельным `/tmp` внутри окружения. Официальные OCI-слои
проверены по SHA-256:

- Ubuntu 24.04 amd64 manifest:
  `sha256:f610ab94648195aa356059f5b41d6085c9d4d903c072430cdd1af7bdb646106b`.
- Debian 13-slim amd64 manifest:
  `sha256:7792b1f7702a86946cd518db72b6a407302c3e9bc1635634368b878189e8221c`.

Пакеты скачаны apt с `--download-only` и распакованы `dpkg-deb --extract` только
в rootfs под `/tmp`. Package install hooks не запускались. Для отсутствующих
после распаковки alternatives BLAS/LAPACK задан `LD_LIBRARY_PATH` исключительно
в окружении проверки. Основная система, глобальные настройки и пользовательская
установка transcribe не изменялись. Первые попытки с read-only `/tmp` и без
путей BLAS/LAPACK выявили ошибки окружения; результаты таблицы относятся к
полным повторным проверкам после исправления тестового окружения.

GitHub Actions в этой сессии не запускались. Проверены локальные команды job;
изменение workflow не опубликовано. LeakSanitizer отключён из-за известных
ограничений ptrace в sandbox; утечки этим прогоном не проверены.
В описанном выше прогоне реальные сетевые сервисы и конкретные видео VK
не тестировались.
Последующая [полная проверка длинной лекции и ресурсов](lecture-validation-2026-10-03.md)
подтвердила VK-загрузку, распознавание, offline-кэш и выявила колебания ETA.
Короткий WAV проверяет интеграцию, а не качество распознавания или точность
ETA на длинных лекциях.

## Повторная локальная проверка

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
TEST_CASE_FILTER=cache_ ctest --test-dir build/debug -R '^pipeline$' --repeat until-fail:20 --output-on-failure
TEST_CASE_FILTER=reliability_ ctest --test-dir build/debug -R '^pipeline$' --repeat until-fail:20 --output-on-failure
```

Статический анализ: сборку сконфигурировать с
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, затем выполнить
`bash scripts/check-static.sh build/debug` с установленными проверочными инструментами.
