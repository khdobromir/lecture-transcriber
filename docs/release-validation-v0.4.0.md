# v0.4.0: приёмка кандидата

Дата: 9 октября 2026 года. Статус: **в работе; выпуск не допущен**.
Исходная точка: dev `6dbd8c59f69e59021637f7d3004c092b88a2989c`.
Ни результаты этого baseline, ни dirty локальная сборка не подтверждают final SHA.
Exact candidate source SHA/fingerprint и package SHA фиксируются упаковщиками;
ссылки на final-SHA CI и окончательные package hashes ещё должны быть добавлены.

## Изменения и автоматизация

| Требование плана | Реализация | Подтверждение / остаток |
| --- | --- | --- |
| F1: Windows trusted backend search | Патч pinned loader: executable directory, запрет GGML env, ограниченные dependency flags | Native probe DLL, positive control, dependency probe добавлены; Windows run ожидается |
| F2: strict portable tools | Windows marker; bundled engine приоритетнее data-home; preflight ffmpeg и URL tools | machine/portable tests; Windows native run ожидается |
| F3: Python в real workflow | python3 добавлен без выключения BUILD_TESTING | Fresh-container real ASR на 1edc4f3 прошёл; ссылка ниже |
| F4: один AppImage | Единственная сборка; candidate.json с path/hash; offline containers получают тот же audio/model | Unit checksum/tampering test прошёл; hosted package run ожидается |
| F6: provenance | Общий SHA/dirty/fingerprint/version guard до сборки и после smoke; release запрещает dirty/SkipTests | Mutation и concurrent commit unit test прошёл; Windows packager run ожидается |
| F6: staging | Windows проверяет ZIP до переноса; имена обоих пакетов versioned и уникальны | Native packaging run ожидается |
| F7: материалы сторонних компонентов | Versioned build-input archives сохраняют pinned downloads, Whisper, Windows patch, FFmpeg/yt-dlp source snapshots и FFmpeg build recipes; mappings связаны с binary SHA | Полные corresponding source/build materials и сопоставление фактическому payload НЕ завершены |
| F8: документация и VERSION | 0.4.0 candidate, Python/test/packaging/support/cancellation docs и release notes | Локальные help/version; окончательные manifests ожидаются |

## Локальные проверки

Окружение: Linux x86_64, GCC 16.2.1, Clang 23.1.1, Qt 6.12.0.
Qt 6.12.0 — дополнительная локальная проверка, не замена pinned Qt 6.8.3/6.11.2 CI.
Дерево при этих проверках содержало ещё не закоммиченные изменения кандидата.

| Проверка | Команда / результат |
| --- | --- |
| GCC Debug, warnings-as-errors | `/tmp/transcribe-v040-gcc`: сборка и CTest **17/17** |
| Clang GUI ASan/UBSan | `/tmp/transcribe-v040-asan`: **22/22**, detect_leaks=1, halt_on_error=1 |
| QML lint | `cmake --build /tmp/transcribe-v040-asan --target all_qmllint`: exit 0 |
| Release GUI, BUILD_TESTING=OFF | Сборка и отдельная установка `/tmp/transcribe-v040-installed`: exit 0; `--version` = transcribe 0.4.0; `--help` работает |
| Реальный medium + Silero VAD | `smoke_real --binary /tmp/transcribe-v040-gcc/transcribe --app-home <installed-data> --artifacts /tmp/transcribe-v040-real-medium`: exit 0, 5 с JFK, ru, 14.9619 с; exports проверены |
| Pinned Windows patch | Применён к копии семи исходных файлов pinned Whisper; повторное применение отклонено source-hash guard |
| Python / shell / diff | Изменённые Python AST разобраны; Bash syntax, ShellCheck и `git diff --check`: exit 0 |

Первый sanitizer run внутри sandbox завершился ошибками LeakSanitizer/ptrace и
недоступного GUI test socket. Контрольный тест и полный повтор вне sandbox с
теми же sanitizer flags прошли. Sanitizers и поиск утечек не отключались;
системные ptrace settings не менялись. Полный log: `/tmp/transcribe-v040-asan-verified.log`.
Локальный Docker недоступен даже вне sandbox (socket permission denied);
container и native Windows packaging должны быть выполнены в GitHub Actions.
Локально cppcheck отсутствует; полный static analysis ещё не подтверждён.

## Обязательные gates до RC и публикации

Первый hosted candidate `18fc2fa1fead34c4c87319984cb3241835d8737a`
([CI](https://github.com/khdobromir/lecture-transcriber/actions/runs/37974886332))
прошёл Windows MSVC CLI, Arch, Debian, Ubuntu GCC/Clang, Qt 6.11.2 и sanitizers.
Windows ZIP job остановился до configure: source guard читал UTF-8 CMakeLists
через CP1252. Исправлено явным UTF-8; regression воспроизводит отказ без UTF-8
mode и затем проходит. Повтор на новом SHA обязателен; этот run не является
допуском окончательного кандидата.

Отдельный [Real Whisper smoke на e8260b4](https://github.com/khdobromir/lecture-transcriber/actions/runs/37978155093)
завершился успешно с настоящим ASR. [Linux portable на 18fc2fa](https://github.com/khdobromir/lecture-transcriber/actions/runs/37974993815)
прошёл единственную сборку и offline clean Ubuntu/Debian, но не upload evidence:
root-owned results с приватными правами не читались uploader. Clean containers
переведены на UID/GID runner; новый hosted run должен подтвердить исправление.

[Windows CI на e8260b4](https://github.com/khdobromir/lecture-transcriber/actions/runs/37978147639)
прошёл configure/build и CLI tests, но GUI tests не стартовали (0xc0000135):
import policy исключает SDK из PATH, а test directory ещё не содержал Qt DLLs.
Упаковщик теперь отдельно развёртывает Qt/QtTest около GUI test executables
перед CTest. В поставляемый ZIP test-only deployment не входит. Native повтор
обязателен; успешное закрытие F1/F2 пока не заявлено.

Повтор Windows на `9ca2ad1` подтвердил исправление Qt startup: GUI integration
и три QML suites прошли. Оставшийся `gui_controller/failedBackendStartCanRetry`
копировал Qt helper в новый каталог и ожидал DLL через PATH; fixture теперь
копирует app-local runtime вместе с helper. Hosted regression остаётся обязательной.

Source snapshots (FFmpeg `330caae0c1ac`, yt-dlp `3a08beaf031a`, BtbN recipes
`e88e49f62445`) скачаны, сверены с новым source manifest и проверены через
archive_inputs: 5 entries, 20 025 391 bytes. Binary/source mismatch regression
падает без guard и проходит с ним на обоих platform locks. Полнота F7 не заявлена.

| Gate | Статус / необходимое evidence |
| --- | --- |
| Окончательный clean source SHA | Открыт: commit, source identity и exact-SHA CI |
| CI 10/10 и Linux portable | Открыт: links + conclusions + real steps не skipped |
| F1/F2 native regressions | Открыт: Windows CI DLL probes, missing-tools, Unicode, process/cancel |
| Real Whisper fresh container | Успех на 1edc4f3 (ссылка ниже); повторить на окончательном SHA |
| Один AppImage SHA во всех средах | Открыт: builder + clean Ubuntu/Debian + native smoke + published download |
| Windows 11 без SDK | Открыт: среда сейчас недоступна, подтверждено пользователем |
| Ubuntu 24.04 desktop X11/Wayland | Открыт: среда сейчас недоступна, подтверждено пользователем |
| Omarchy/Hyprland | Открыт: native Wayland, 160%, keyboard, minimum window, dialogs/clipboard/portal/open exports |
| FUSE/no-FUSE | Открыт для final artifact: обычный mount и extraction launch |
| Recognition models | Открыт для пакетов Linux/Windows: small, medium+VAD, turbo, один/несколько chunks, sample bounds |
| GUI lifecycle | Открыт для пакетов: download/import, live preview, cancel/close/retry/restart history, TXT/SRT/VTT |
| Cancellation/completion invariant | Локальные suites прошли; final Windows/Linux artifacts ещё не подтверждены |
| Upgrade v0.3.0 | Локальные installer/history suites прошли; final packages/settings/models/results/legacy history ещё не подтверждены |
| VK/authentication/network/cache | Открыт: public URL, network failure, cookies file/browser, cache reuse/refresh; private evidence хранить локально |
| Длинная локальная лекция | Открыт: 10–15 мин, RAM/responsiveness/cancel/chunks и ручная оценка срезов |
| Corresponding sources/notices | Открыт: actual payload inventory, sources/patches/build materials, copied-library notices, сохранённые immutable build inputs |
| Версия/manifests/docs | Версия локально 0.4.0; final packages ожидаются |
| Release hygiene | Открыт: clean tree, без SkipTests, private logs/media/model weights |
| Master/tag/release/download | Открыт: проверенный final master, peeled annotated v0.4.0, assets download/hash/startup/source archive |

Не переносить historical green runs на кандидат и не отмечать отсутствующую
native приёмку как пройденную. При изменении исходников или payload повторять
затронутые проверки на новом SHA/комплекте. До закрытия всех gates цель остаётся активной.

## Дополнительное evidence на 1edc4f3 и Qt sources

[Fresh-container real smoke](https://github.com/khdobromir/lecture-transcriber/actions/runs/37980010698)
завершился успешно. [Windows CI](https://github.com/khdobromir/lecture-transcriber/actions/runs/37979991730)
прошёл 13/13 CTest, DLL probe/preflight checks и installedCliWithRealSpeech:
first model import, live preview, small ASR, TXT/SRT/VTT. GUI screenshot и
validation.json скачаны из native artifact. Portable HTTPS test был skipped:
certificate bundle не настроен; Windows использует системный certificate store.
Сам job failed: цикл QML diagnostics перезаписал Path `marker` значением bytes.
Исправлено имя переменной цикла. Нельзя считать final marker check пройденным
или переносить green acceptance на будущий пакет без повторного native run.

Для Qt 6.8.3 скачаны и сверены с официальными SHA-256 qtbase, qtdeclarative,
qtwayland, qtsvg, qtimageformats, qtshadertools, qttranslations и qttools.
Из них извлечены 199 notice/attribution files, включая LicenseFile/CopyrightFile
references. Qt version mismatch, missing reference, corrupt archive и path
traversal regressions проходят. Notices и sources теперь включаются в оба
packaging пути. Сопоставление всех фактических payload components и полный F7
ещё не завершены.

## Самопроверка изменений кандидата

Смысл изменения: ограничить Windows tools/backend доверенным комплектом и
связать проверки переносимых пакетов с одним source/artifact identity до RC.

| Code review | Результат |
| --- | --- |
| Summary | Реализация Windows isolation, portable artifact identity и provenance подготовлена для hosted CI |
| Critical issues | Подтверждённых критических дефектов в локально проверенной части не найдено; Windows результат ещё ожидается |
| Major issues | P1: нет чистой Windows 11/Ubuntu desktop приёмки и полных corresponding sources; публикация заблокирована этими gates |
| Minor issues | Полный static analysis и pinned Qt matrix ещё должны подтвердиться в CI |
| Positive feedback | Native DLL имеет positive control; PE imports проверяются до исполнения; archives проверяют retained bytes; публикация не заменяет предыдущий AppImage |
| Questions for author | Доступность Windows 11/Ubuntu desktop уточнена: сейчас сред нет |
| Verdict | Comment: кандидат для продолжения CI/приёмки; разрешением на релиз этот отчёт не является |

WTF audit: tracked build/cache/media/model/secret artifacts не обнаружены;
новые материалы перечислены явно. БД, ORM, web routes и web auth — N/A.
Изменения не обновляют установленное приложение, модели, настройки или результаты.
