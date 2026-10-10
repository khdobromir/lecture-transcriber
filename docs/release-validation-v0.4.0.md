# v0.4.0: приёмка кандидата

Начало: 9 октября 2026 года; обновлён 10 октября. Статус: **в работе; выпуск не допущен**.
Исходная точка: dev `6dbd8c59f69e59021637f7d3004c092b88a2989c`.
Ни результаты этого baseline, ни dirty локальная сборка не подтверждают final SHA.
Exact candidate source SHA/fingerprint и package SHA фиксируются упаковщиками;
ссылки на final-SHA CI и окончательные package hashes ещё должны быть добавлены.

## Изменения и автоматизация

| Требование плана | Реализация | Подтверждение / остаток |
| --- | --- | --- |
| F1: Windows trusted backend search | Патч pinned loader: executable directory, запрет GGML env, ограниченные dependency flags | Native package regression в CI 8bd77f8 прошёл; final SHA требует повтора |
| F2: strict portable tools | Windows marker; bundled engine приоритетнее data-home; preflight ffmpeg и URL tools | Native package regression в CI 8bd77f8 прошёл; final SHA требует повтора |
| F3: Python в real workflow | python3 добавлен без выключения BUILD_TESTING | Fresh-container Real Whisper на 8bd77f8 прошёл; ссылка ниже |
| F4: один AppImage | Единственная сборка; candidate.json с path/hash; offline containers получают тот же audio/model | Hosted 8bd77f8 прошёл; 5e1c83b build inputs независимо сверены; native final smoke открыт |
| F6: provenance | Общий SHA/dirty/fingerprint/version guard до сборки и после smoke; release запрещает dirty/SkipTests | Mutation/concurrent commit tests и CI 8bd77f8 прошли |
| F6: staging | Windows проверяет ZIP до переноса; имена обоих пакетов versioned и уникальны | CI 8bd77f8 прошёл; final SHA требует повтора |
| F7: материалы сторонних компонентов | Verified build inputs, exact Ubuntu sources, Qt/ICU, standalone sources/notices, runtime sources/recipes; добавляется partial FFmpeg dependency set | Полные corresponding source/build materials и сопоставление фактическому payload НЕ завершены |
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

## Проверенный комплект 6322ad9

Коммит `6322ad940cf04d3a50ec274993bbbe5177755da3` прошёл
[CI 10/10](https://github.com/khdobromir/lecture-transcriber/actions/runs/38042791488),
[Linux portable с real ASR](https://github.com/khdobromir/lecture-transcriber/actions/runs/38042793593)
и [Real Whisper](https://github.com/khdobromir/lecture-transcriber/actions/runs/38042795963).
Скачанный AppImage SHA-256: `ac6127c8063c0491a01b3df92c68309c9cecaa1d2c27c532b9cc476b1c93f405`;
build inputs: `4b1cffe7a8a6b347f9aac82327b3eb3003451981df5de92db4621db7ef73e89c`.
Тот же image hash записан в успешных offline Ubuntu/Debian evidence.
Скачанный AppImage распакован локально; все 2 107 payload entries
совпали с полным manifest, включая notices и symlinks.

Перечитаны и сверены все 185 retained entries, включая 144 source archives
43 exact Ubuntu packages, сопоставленные с 68 libraries и двумя data files.
ICU mapping подтверждён для трёх SDK libraries; два original notices включены
в manifest. Font связан с `fonts-dejavu=2.37-8`, CA bundle — с
`ca-certificates=20260601~24.04.1`. В unresolved library inventory остались только
три собственные Whisper/ggml libraries, для которых pinned source уже сохраняется
отдельно. Это не означает полноту F7: native/FFmpeg dependencies и SDK build
configuration всё ещё открыты. Следующее изменение runtime требует нового
hosted run и новых artifacts; результаты 6322ad9 на него не переносятся.

## Источники AppImage runtime после 6322ad9

Для pinned runtime SHA-256 `156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074`
исследован [официальный x86_64 build](https://github.com/AppImage/type2-runtime/actions/runs/36463736478)
коммита `8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa`. Debug companion связан с
runtime через `.gnu_debuglink` CRC32 `09c79829`, его опубликованный SHA-256
также сверён. Статически прочитаны zlib 1.3.2, zstd 1.5.6, mimalloc `mi_version=217`
и musl 1.2.5; библиотеки/debug code не исполнялись.

У upstream build log и APK metadata совпали версии musl 1.2.5-r11,
mimalloc2 2.1.7-r0, zstd 1.5.6-r2 и zlib 1.3.2-r0. Исходники проверены по
SHA-512 соответствующих APKBUILD; libfuse 3.15.0 и squashfuse 0.5.2 — по
SHA-256 upstream runtime recipe. Содержимое 20 recipe/patch files в двух
retained Alpine snapshots сверено по Git blob hashes с точными source commits.
APK читались только как данные; криптографическая проверка APK signature
и пересборка runtime этим исследованием не заявляются.

Локально actual collector сохранил 13 notices для семи source components;
девять source/recipe archives упакованы и перечитаны с проверкой hashes.
Изменённый runtime pin, несовместимый dependency mapping, повреждённый runtime,
missing notice и unsafe notice path отклоняются тестами. Все 12 Linux packaging
tests проходят. Новая hosted сборка после этого изменения ещё обязательна.
Полнота F7 остаётся false, включая оставшиеся runtime/toolchain details,
native FFmpeg/standalone dependencies и Qt SDK build configuration.

## Hosted проверки 5e1c83b

Коммит `5e1c83bb275fea8fe451f43997d257815e4692a1` прошёл
[CI 10/10](https://github.com/khdobromir/lecture-transcriber/actions/runs/38044496095),
[Linux portable с real ASR и offline Ubuntu/Debian](https://github.com/khdobromir/lecture-transcriber/actions/runs/38044497989)
и [Real Whisper](https://github.com/khdobromir/lecture-transcriber/actions/runs/38044499646).
Его artifact скачан: AppImage SHA-256
`066048254fb8ebbd5d231f74c865c5356d4f2ecda5f8dd63bea9265e6929399f`,
build inputs SHA-256
`13d9bfe1190d6a703a750571cb10c18436ff6924b26038e5418fca83bf96ea84`.
Независимо перечитаны все 194 файла build-inputs archive и сверены их SHA-256,
включая девять runtime source/recipe archives. Проверены manifest source,
clean/tested/real-smoke flags и source/artifact hash в offline Ubuntu/Debian
evidence. Повторная распаковка и сверка каждого payload file этого AppImage
пока не выполнялись; это не результат native desktop приёмки.

## Исходники и notices части зависимостей FFmpeg после 5e1c83b

`ffmpeg_dependencies` закрепляет 24 source archives включённых библиотек и
зависимостей для Linux/Windows. Проверяются hash исходного FFmpeg input,
hash исходного BtbN recipe snapshot, SCRIPT_REPO/SCRIPT_COMMIT и требуемые
configure flags bundled FFmpeg. В частности, ветка FFVER=801 выбирает
nv-codec-headers recipe slot 2 (SDK 13.0); OpenCL headers и ICD loader имеют
отдельные source records. Chromaprint recipe включает FFTW: original GPL
notices сохранены; upstream название варианта `lgpl` не описывает все terms.

Actual collector обработал verified Linux FFmpeg из pinned input archive;
configuration прочитана настоящим `-buildconf`. Windows input archive и
сохранённая статически прочитанная configuration также проверены локально;
исполнение нового helper на Windows остаётся hosted проверкой.
Для каждой платформы сохранено 59 notices для 24 components. Все 24 source
archives упакованы и перечитаны с проверкой SHA-256. Прошли шесть packaging
CTest suites, включая восемь shared source/identity и 12 Linux packaging tests.
Новые проверки отклоняют stale/unknown mappings, неправильный recipe commit,
unsafe recipe path/slot, corrupt input/recipes, missing notices и выключенные
configure flags. До реализации два новых теста падали; после неё проходят.

Набор остаётся частичным: остальные native libraries, nested/generated inputs,
toolchains и Qt SDK build configuration ещё требуют материалов. Возможности
FFmpeg и binary pins не менялись. `corresponding_sources_complete=false`.
После этого изменения необходимы новые hosted CI/portable artifacts; результаты
5e1c83b не считаются проверкой нового head. Ручные платформенные gates открыты.

## Hosted проверки 8bd77f8 и расширение FFmpeg materials

Коммит `8bd77f807a68cc8273fbdab62de6f7ecd5e7846c` прошёл
[CI 10/10](https://github.com/khdobromir/lecture-transcriber/actions/runs/38045641969),
[Linux portable с real ASR и offline Ubuntu/Debian](https://github.com/khdobromir/lecture-transcriber/actions/runs/38045643983)
и [Real Whisper](https://github.com/khdobromir/lecture-transcriber/actions/runs/38045645981).
Windows package job и Linux Qt 6.8.3 real candidate step выполнены успешно.
Это подтверждает hosted работу collector для первого набора 24 dependencies;
Artifacts этого SHA ещё не скачаны для независимой локальной проверки.

Следующее расширение сохраняет 72 immutable source archives: 71 для Linux,
69 для Windows. Добавлены, среди прочего, SDL, OpenH264, SVT-AV1, rav1e,
libjxl/LCMS2, libplacebo/shaderc, libssh/SRT/OpenSSL, FreeType/Fontconfig,
PulseAudio/XCB, LV2 chain и зависимости шрифтов. Все новые архивы скачаны по
full commit SHA из upstream repositories, прочитаны как данные и проверены
действующим collector против pinned recipe snapshot и configuration обоих
FFmpeg inputs. Original LICENCE/README/header notices libunibreak сохранены
явно; никакой сторонний build/autogen code не запускался.

OpenSSL `openssl-3.6.4` разрешён через annotated tag object
`360ffdb6d82f298d8d22c838dc2b7bf61ece056d` в commit
`d3c1b1169b3569ff3069e5b399f47b2b28e03d79`; Vulkan Headers `v1.4.363` —
через tag object `7feeb71f59b8c1531ef5f7bb4b36e1aac46a0acb` в commit
`6802bb4733b63ed5efd3adb308a6c885ef180ea1`. Проверены ответы upstream Git API;
проверка криптографических подписей тегов не заявляется. Исходные tag names
сохраняются для сверки recipe, source URL/hash закрепляют immutable archives.
OpenSSL включён также в Windows materials по libssh/SRT configure flags,
хотя непосредственно FFmpeg использует Schannel.

Локально collector сохранил 253 notices для Linux и 250 для Windows.
Все 72 source archives упакованы и перечитаны с SHA-256 validation.
Packaging CTest suites прошли 6/6; shared source tests проверяют правильный
recipe tag и отказ при несовпадении, сохраняя full commit в provenance.
Новый tag case сначала падал, после изменения collector прошёл.

Набор остаётся частичным. В частности, shaderc DEPS, libjxl highway,
PCRE2 sljit, libbluray udfread, Rust crates и генерируемые входы ещё не
закрыты родительскими archives. Rav1e recipe выполняет `cargo update cc`,
поэтому исходный Cargo.lock сам по себе не доказывает final build dependency
versions. Остальные FFmpeg libraries, standalone native dependencies,
toolchain/runtime details и Qt SDK build configuration также требуют работы.
`corresponding_sources_complete=false`; capabilities и binary pins прежние.
Новые sources/helper требуют hosted проверки нового SHA. Пользователь подтвердил,
что clean Windows 11 без SDK и Ubuntu 24.04 desktop сейчас недоступны;
соответствующие manual gates остаются открытыми.

## FFmpeg sources из архивов без корневого каталога

После набора `47d9ca326cd42458620240b785b276c6048afe0c` добавлены официальные
Googlesource snapshots AOM, libvpx и libwebp по полным recipe commit SHA.
Новый набор содержит 75 archives: 74 для Linux, 72 для Windows. Его collector
проверен на обоих original FFmpeg input archives и их ранее прочитанных
configuration: сохранено 274 original notices для Linux и 271 для Windows.
Три новых архива сохранены и перечитаны с SHA-256 проверкой; 72 предыдущих
были упакованы и проверены в предыдущем этапе. Upstream code не исполнялся.

`package_notices.py` теперь принимает явный `strip_components=0` для архивов
без enclosing directory. Root LICENSE/COPYING, AUTHORS, PATENTS и nested
notices сохраняются без изменения путей; default prefix для Qt/остальных
архивов остаётся один. Другие prefix values, включая bool/string, отвергаются.
Тесты проверяют сохранение root/nested notices, отказ на absolute, traversal
и Windows-style paths, а также отсутствие копирования обычного source code.
Новый test сначала падал на root LICENSE, затем прошёл. Shared source tests
прошли 9/9; fresh packaging CTest — 6/6.

Для предыдущего `47d9ca3` запущены
[CI с real_smoke=true](https://github.com/khdobromir/lecture-transcriber/actions/runs/38057481814)
и [Linux portable с real_smoke=true](https://github.com/khdobromir/lecture-transcriber/actions/runs/38057483645);
на момент подготовки этой записи они ещё выполнялись, а
[Real Whisper](https://github.com/khdobromir/lecture-transcriber/actions/runs/38057485342)
уже прошёл. Для нового source SHA необходим отдельный hosted прогон.
Полнота F7 и final manual gates по-прежнему не подтверждены.

## Самопроверка изменений кандидата

Смысл изменения: ограничить Windows tools/backend доверенным комплектом и
связать проверки переносимых пакетов с одним source/artifact identity до RC.

| Code review | Результат |
| --- | --- |
| Summary | Windows isolation, portable identity и provenance дополнены 75 pinned FFmpeg source archives, recipe tags и сбором notices из архивов без корневого каталога |
| Critical issues | Подтверждённых критических дефектов в проверенной части не найдено; final SHA/artifact приёмка открыта |
| Major issues | P1: нет чистой Windows 11/Ubuntu desktop приёмки и полных corresponding sources; публикация заблокирована этими gates |
| Minor issues | Static analysis и pinned Qt matrix прошли на 8bd77f8; после расширения materials необходим новый exact-SHA CI |
| Positive feedback | Native DLL имеет positive control; PE imports проверяются до исполнения; archives проверяют retained bytes; публикация не заменяет предыдущий AppImage |
| Questions for author | Доступность Windows 11/Ubuntu desktop уточнена: сейчас сред нет |
| Verdict | Comment: кандидат для продолжения CI/приёмки; разрешением на релиз этот отчёт не является |

WTF audit: tracked build/cache/media/model/secret artifacts не обнаружены;
новые материалы перечислены явно. БД, ORM, web routes и web auth — N/A.
Изменения не обновляют установленное приложение, модели, настройки или результаты.
