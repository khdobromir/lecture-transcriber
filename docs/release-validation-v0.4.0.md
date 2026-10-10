# v0.4.0: приёмка кандидата

Дата: 9 октября 2026 года. Статус: **в работе; выпуск не допущен**.
Исходная точка: dev `6dbd8c59f69e59021637f7d3004c092b88a2989c`.
Ни результаты этого baseline, ни dirty локальная сборка не подтверждают final SHA.
Exact candidate source SHA/fingerprint и package SHA фиксируются упаковщиками;
ссылки на final-SHA CI и окончательные package hashes ещё должны быть добавлены.

## Изменения и автоматизация

| Требование плана | Реализация | Подтверждение / остаток |
| --- | --- | --- |
| F1: Windows trusted backend search | Патч pinned loader: executable directory, запрет GGML env, ограниченные dependency flags | Windows native package job на b379811 прошёл; final SHA требует повтора |
| F2: strict portable tools | Windows marker; bundled engine приоритетнее data-home; preflight ffmpeg и URL tools | Windows native package job на b379811 прошёл; final SHA требует повтора |
| F3: Python в real workflow | python3 добавлен без выключения BUILD_TESTING | Fresh-container real ASR на 1edc4f3 прошёл; ссылка ниже |
| F4: один AppImage | Единственная сборка; candidate.json с path/hash; offline containers получают тот же audio/model | Hosted run на 1edc4f3 прошёл; скачанный AppImage и build inputs сверены; native final smoke открыт |
| F6: provenance | Общий SHA/dirty/fingerprint/version guard до сборки и после smoke; release запрещает dirty/SkipTests | Mutation и concurrent commit unit test прошёл; Windows package job на b379811 прошёл |
| F6: staging | Windows проверяет ZIP до переноса; имена обоих пакетов versioned и уникальны | Windows package job на b379811 прошёл; final SHA требует повтора |
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

## Проверенные artifacts и последующие изменения

[Linux portable на 1edc4f3](https://github.com/khdobromir/lecture-transcriber/actions/runs/37980000870)
завершился успешно: builder real ASR, offline Ubuntu/Debian и сохранение artifacts.
Скачан `Transcribe-0.4.0-linux-x86_64-release-1edc4f300f26-run-s1saiuit.AppImage`;
SHA-256 `c39f7f53318fdc404e5f73802516056b1e2c2a2dcd64f5de748c199ffec87825`
совпадает с candidate.json и sidecar. Build inputs SHA-256
`fe8b9adc7db86bd8c565a47f29b29f8792a9c32b9c9f8bf1894271cb0a7adf7c`
также сверён. Manifest: version 0.4.0, release/tested/real_smoke=true.
Этот пакет предшествует последующим изменениям исходников и не является final RC.

[Windows job на b379811](https://github.com/khdobromir/lecture-transcriber/actions/runs/37981271206/job/113992581355)
завершился успешно, включая упаковку, native tests и настоящий small ASR.
Скачанные журналы подтверждают gui_controller 62/62, gui_cli_integration 7/7,
QML 7/7 и scale150/200 по 4/4; real GUI: 3 passed, 0 failed, 1 skipped
(portableHttpsDownload без portable certificate bundle). Чистая Windows 11
без SDK по-прежнему недоступна; эта ручная приёмка остаётся открытой.

Linux packaging теперь формирует `linux-library-provenance.json`: payload и
original hashes, binary/source package versions, скопированные copyright-файлы
и common licenses. Неизвестные библиотеки отмечаются unresolved; third-party
SDK libraries не считаются автоматически покрытыми исходниками Qt.
Сводка также сохраняется в build inputs. Тесты проверяют usrmerge, неоднозначного
владельца и отсутствующий copyright. Qt notice collector дополнительно сохраняет
полные тексты из каталогов LICENSES; regression сначала воспроизвёл пропуск,
затем прошёл. Реальная сборка нового payload в hosted builder ещё необходима.
Полнота corresponding sources остаётся false до завершения F7.

## Проверки 10 октября: Linux notices и standalone dependencies

На `024e6b54e9b351f491f00a208017d04803456a62` прошли
[CI 10/10](https://github.com/khdobromir/lecture-transcriber/actions/runs/37983033395),
[Linux portable с real ASR/offline containers](https://github.com/khdobromir/lecture-transcriber/actions/runs/37983037263)
и [отдельный Real Whisper smoke](https://github.com/khdobromir/lecture-transcriber/actions/runs/37983041111).
AppImage и build inputs скачаны и сверены: соответственно
`a01e92fecd87ec6100283ed925384db784c98e6e8dad4fec9ee7ef6aef4d573b` и
`de6b4acb3210994aa7c0299f0824573dd4c02c8154c6f676a7cf4d2302eb1eac`.
Provenance содержит 116 библиотек, в том числе 41 точную версию system source
packages. Unresolved: три Whisper/ggml libraries и три ICU 73 libraries из SDK;
исходники Whisper сохранены отдельно, ICU и system source archives ещё необходимы.

Последующий код упаковки сохраняет оригинальные встроенные notices yt-dlp:
проверенный Linux standalone дал 9 notice/metadata files и 159 archive entries,
Windows — 5 файлов и 151 entry. Python code objects не читаются через marshal
и не исполняются. Реальный Windows архив маркирует data files как BINARY;
это отличие воспроизведено regression и учтено при чтении.
Binary SHA mismatch, traversal и duplicate entries отклоняются до записи.

Закреплены 20 дополнительных source archives Python и runtime dependencies
yt-dlp. Shared selector выбирает Windows Python 3.10.11/websockets 16.1.1
и Linux Python 3.14.7/websockets 17.0.1; Linux SecretStorage/cryptography/Jeepney
не включаются в Windows source set. Версии Python подтверждены runtime/PE
metadata; source versions проверены в patchlevel.h, SHA — по digest из
официальных Sigstore metadata (проверка криптографической подписи не заявляется).
PyPI source bytes сверены с release-specific SHA-256. Linux/Windows build-input
archives с выбранными runtime sources созданы и перечитаны с проверкой hashes.
Для новых изменений требуется новый hosted packaging run; F7 и ручные gates
остаются открытыми. Полный комплект также требует FFmpeg dependencies,
native dependencies standalone runtime, ICU, system packages и AppImage runtime.

## Проверенный комплект e436846 и следующий шаг F7

Для `e4368463d6b3f7a9fc8d60499dff350cc3868fe0` прошли
[CI 10/10](https://github.com/khdobromir/lecture-transcriber/actions/runs/38038945825),
[Linux portable](https://github.com/khdobromir/lecture-transcriber/actions/runs/38038947954)
и [Real Whisper](https://github.com/khdobromir/lecture-transcriber/actions/runs/38038949629).
Скачанные package sidecars и каждый файл build-input archives сверены локально:

| Deliverable | SHA-256 |
| --- | --- |
| AppImage | `c52e6c4e6ea6f7af9666efc1f7dd5c05e4d49b202678c23d360f3ac18a7f1d4f` |
| Linux build inputs | `181d49c1bb6072441b39ba351ee9cebd90d0bb743b965e76727727fb56f6b278` |
| Windows ZIP | `a9d4b5b724f868d7275860df7999af8b02a891f70fc22ece40789adbcbe18509` |
| Windows build inputs | `dcc2389528a07b54f81f84652112d28cd8e37ffea91c3fc3a9fef45cc019725b` |

Windows evidence подтверждает native probes, Unicode paths, startup и real GUI→CLI
speech на hosted runner; `manual_clean_windows_11` остаётся `unverified`.
Linux provenance по-прежнему содержит 116 libraries / 41 system source versions;
три ICU SDK libraries остаются unresolved, Whisper sources сохранены отдельно.

Частичная native проверка предыдущего `024e6b5` AppImage выполнена на
Omarchy/Hyprland 0.56.2: обычный FUSE startup, native Wayland, видимый русский UI,
file dialog/navigation/cancel, Tab/Shift+Tab и Ctrl+Q с exit 0.
Масштаб монитора был 133%, а не требуемые 160%; minimum window, clipboard,
open exports и model matrix не проверены. Наблюдались fontconfig warnings при
чтении host configuration. Это agent-supervised проверка с осмотром изображений
собственного окна, не полная ручная приёмка final artifact.

Следующее изменение упаковки сохраняет exact Ubuntu source archives для
скопированных system libraries, проверяет SHA-256 из APT metadata и архивирует
payload→source mappings. Четыре локальных теста проверяют exact selection,
conflicting indexes, unsafe filenames, shared-package deduplication, cache reuse
и отказ при изменённых байтах. Actual APT integration требует нового hosted run.
`corresponding_sources_complete=false`: F7 и все открытые ручные gates сохраняются.

## Проверенный комплект 8283ed4 и дальнейшее сопоставление F7

Для `8283ed4ad966b3813934c89a21b605735140f0dc` прошли
[CI 10/10](https://github.com/khdobromir/lecture-transcriber/actions/runs/38040568453),
[Linux portable с real ASR и offline Ubuntu/Debian](https://github.com/khdobromir/lecture-transcriber/actions/runs/38040570228)
и [Real Whisper](https://github.com/khdobromir/lecture-transcriber/actions/runs/38040571888).
AppImage SHA-256: `c219da2a133a36aeeb640865ecd01cb55b988fa037e9eae9b01f3287e0c57f89`;
build inputs: `6c4c5d752258716e66e1859d5ef5e91f7c27b36a0695d4528c69ab821ab3aaec`.
Локально перечитаны и сверены все 179 retained entries, включая 139 архивов
для 41 exact Ubuntu source package, сопоставленные с 68 system libraries.
Это результат 8283ed4; subsequent changes требуют новых artifacts и CI.

Дополнительный technical CLI smoke предыдущего AppImage `e436846` использовал
закреплённый 30-секундный русский public fixture и существующие medium/turbo/VAD
models только для чтения. До/после проверены model hashes; данные и default-model
испытания изолированы. Успешны все четыре сценария:

| Recognition + VAD | Chunks / jobs / threads | Wall time, seconds |
| --- | --- | --- |
| Default medium | 1 / 1 / 2 | 41.59 |
| Default medium | 3 / 1 / 2 | 83.21 |
| Turbo | 1 / 1 / 2 | 42.64 |
| Turbo | 3 / 1 / 2 | 118.25 |

Проверены completed/exit 0, фактическая загрузка Silero и VAD reduction во всех
Whisper logs, ненулевые TXT/SRT/VTT, ordering и timestamps в пределах 30 секунд.
Это не проверка WER, длинной лекции, GUI или final artifact. Начальный запуск
локального harness с закрытым stdin штатно отменился с 141; повторный harness
сохранял machine control pipe открытым до завершения, как GUI. App code не менялся.

В следующем изменении `linux_sdk_dependencies` закрепляет ICU 73.2 source archive
(`818a80712ed3caacd9b652305e01afc7fa167e6f2e94996da44b90c2ab604ce1`)
и original SDK hashes трёх библиотек. Версия установлена статически по строке,
на которую ссылается `u_getVersion_73`; код библиотеки не исполнялся.
Source bytes сверены с официальным SHA-512; upstream `LICENSE` и `license.html`
скопированы с hashes. Секции `.text`, `.rodata`, `.data`, `.comment` (где присутствуют)
совпали с официальным Qt prebuilt ICU для RHEL 8.6. Полное совпадение файлов
и воспроизводимость SDK по этому сравнению не заявляются.
Actual collector применён к provenance 8283ed4: ICU source/notices mapping
подтверждён, retained source archive перечитан с проверкой SHA-256.

DejaVu font и generated CA bundle также добавлены в exact Ubuntu source mapping:
копии должны совпадать с builder inputs, а font/generator — принадлежать ожидаемым
пакетам. Десять Linux packaging tests и четыре source-input tests проходят локально.
Новый actual Ubuntu packaging run ещё необходим. `corresponding_sources_complete`
остаётся false: native/FFmpeg dependencies, SDK build materials и AppImage runtime
ещё требуют работы. Ручная матрица и final-SHA gates остаются открытыми.

## Самопроверка изменений кандидата

Смысл изменения: ограничить Windows tools/backend доверенным комплектом и
связать проверки переносимых пакетов с одним source/artifact identity до RC.

| Code review | Результат |
| --- | --- |
| Summary | Реализация Windows isolation, portable artifact identity и provenance подготовлена для hosted CI |
| Critical issues | Подтверждённых критических дефектов в проверенной части не найдено; final SHA/artifact приёмка открыта |
| Major issues | P1: нет чистой Windows 11/Ubuntu desktop приёмки и полных corresponding sources; публикация заблокирована этими gates |
| Minor issues | Полный static analysis и pinned Qt matrix ещё должны подтвердиться в CI |
| Positive feedback | Native DLL имеет positive control; PE imports проверяются до исполнения; archives проверяют retained bytes; публикация не заменяет предыдущий AppImage |
| Questions for author | Доступность Windows 11/Ubuntu desktop уточнена: сейчас сред нет |
| Verdict | Comment: кандидат для продолжения CI/приёмки; разрешением на релиз этот отчёт не является |

WTF audit: tracked build/cache/media/model/secret artifacts не обнаружены;
новые материалы перечислены явно. БД, ORM, web routes и web auth — N/A.
Изменения не обновляют установленное приложение, модели, настройки или результаты.
