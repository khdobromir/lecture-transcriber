# Проверка переносимого Linux GUI — 8 октября 2026

Реализована сборка одного AppImage с GUI, внутренним CLI, Qt/QML, CPU Whisper,
FFmpeg/ffprobe, самостоятельным yt-dlp, шрифтом и публичными CA-сертификатами.
Модели — внешние пользовательские данные; доступен импорт и загрузка из GUI.
Команды сборки и запуска: [linux-portable.md](linux-portable.md).

## Проверенный файл

`dist/Transcribe-linux-x86_64-6020c2d322e6-run-x6rokhxy.AppImage`, 200899064 байта.

```text
SHA-256: a046398bcb2dc9332ee290b6541a9708fbfb247f0fb6a526e9ffa49cd91e4084
Git HEAD: 6020c2d322e6a268ede6c869bb7247f52261f2d1
Source fingerprint: dd37b9a96c4596b231cd0a1d2ee24d4e0a8498cbb84c931e70cf58c346c3fd94
Dirty: true
Qt: 6.8.3
Required glibc: 2.39
```

Соседние `.sha256` и `.json` фиксируют хеш и 1716 файлов/ссылок payload.
Манифест извлечённого окончательного AppImage проверен полностью: состав,
хеши, права, относительные symlinks, Git HEAD. Fingerprint совпал с рабочим
деревом непосредственно перед добавлением этого отчёта. На момент сборки
выпуск/тег и commit не создавались; бинарники, модели и build logs размещались
в игнорируемых каталогах. Проверенный файл установлен отдельно в
`~/Applications/Transcribe.AppImage`; desktop entry запускает его без FUSE.

## Выполненные проверки

| Проверка | Наблюдаемый результат |
| --- | --- |
| Ubuntu 24.04, GCC 13, Qt 6.8.3, warnings as errors | 19/19 CTest, QML lint, сборка и упаковка прошли |
| Clang, Qt 6.11.2, ASan/UBSan | 19/19 CTest; `detect_leaks=1`, `halt_on_error=1`; ошибок санитайзеров нет |
| GCC 16, Qt 6.11.2, `BUILD_TESTING=OFF` | GUI/CLI собраны и установлены в отдельный prefix; help/version CLI прошли |
| clang-tidy 18, cppcheck 2.13 | Полный анализ 35 собственных TUs; после TLS-правки повторены три изменённых C++ файла; exit 0 |
| Shell и упаковка | Bash syntax, ShellCheck; четыре Python-теста небезопасных архивов/кэша, манифеста и окружения launcher прошли |
| Окончательный AppImage | Unicode-каталог и другой cwd, очищенные SDK overrides, запуск без FUSE, все встроенные tools, манифест прошли |
| GUI → встроенный CLI → Whisper | Первый импорт small, live preview, completed, история и непустые TXT/SRT/VTT с корректными timestamps прошли |
| Чистая Ubuntu 24.04 OCI и Debian 13 slim | Нет Qt/Python/FFmpeg/yt-dlp; сеть отключена, исходники и SDK не подключены; startup GUI и реальная локальная транскрипция прошли |
| Чистая Ubuntu без системных CA | HTTPS через настоящий ModelManager, TLS-проверка и SHA-256 публичного закреплённого файла прошли с CA из AppImage |
| Native Wayland и X11/XWayland | Итоговый файл оставался работающим 12 секунд, остановлен timeout с ожидаемым exit 124; fatal/QML diagnostics отсутствуют |

Для чистого HTTPS-теста подключались только итоговый AppImage и инструмент
QtTest с его библиотекой; TLS/OpenSSL, сертификаты и остальные Qt runtime
использованы из payload. Пакеты в чистую систему не устанавливались.
Офлайн-тесты подключали только AppImage и публичные входные данные/модель.

Реальный GUI smoke использует QtTest controller harness и тот же QML source;
запуск production GUI с ресурсами внутри файла проверяется отдельно. Он
подтвердил импорт, живой текст и три экспорта; не измерял качество ASR или
скорость длинной лекции. Речь — публичный 30-секундный русский пример из
`tests/fixtures/russian-speech.json`; модель — закреплённая small q5_1.

Во время проверок evidence в
`.cache/linux-package-validation/run-x6rokhxy/smoke-evidence/` включали
`package-smoke.json`, `validation.json`, QtTest log и screenshot.
CTest/QML/build logs размещались рядом. Отдельные clean/native/static/sanitizer
logs размещались в `.cache/linux-portable-inputs/`. Эти временные файлы
не входят в Git и удаляются при уборке после commit/push; итоговые результаты
и checksum проверенного приложения зафиксированы в этом отчёте.

## Саморевью

**Summary:** пользователь может перенести один файл и пользоваться GUI без
установки application runtime dependencies на поддерживаемом Linux.

**Critical issues / Major issues:** открытых дефектов в проверенном объёме нет.
Исправлены неполный набор X11/Wayland/offscreen plugins, изменение desktop/icon
при упаковке и отсутствие CA в минимальном Linux. Неполный tools bundle
отклоняется, fallback к установленному Whisper/PATH запрещён.

**Minor issues:** на текущем Arch fontconfig выдаёт предупреждения при чтении
новых системных font configuration files старой bundled библиотекой. Окно
работает; QML/fatal errors не наблюдались. Некоторые diagnostics fontconfig
видны также в QtTest harness; production launcher задаёт собственный config.

**Positive feedback:** сохранены существующий CLI, управление процессами,
отмена и completion boundary; GUI по умолчанию использует software renderer.
Загрузки проверяются SHA-256 до исполнения, архивы отклоняют traversal/links,
старые пакеты и пользовательская установка не заменяются.

**Questions for author:** нет уточнений, необходимых для этого объёма.

**Verdict:** Approve для реализованной локальной сборки и использования в
проверенной матрице. Публичная бинарная публикация требует отдельной подготовки
corresponding sources/notices по [THIRD-PARTY.md](../packaging/linux/THIRD-PARTY.md).

## Границы результата

- Поддерживаемый пакет: Linux x86_64/glibc 2.39+. Старые CPU без AVX2
  не испытывались физически; baseline CPU module включён, native ISA выключена.
- `/dev/fuse` на этой машине отсутствует: обычный mount mode не проверен.
  Использован `--appimage-extract-and-run`, установка libfuse не требуется.
- Docker daemon недоступен; Dockerfile/Compose проверены статически, локальная
  сборка выполнена в изолированном официальном Ubuntu rootfs через Bubblewrap.
  Новый GitHub workflow подготовлен, hosted CI ещё не запускался.
- Native диалоги, clipboard, открытие экспортов, live VK/cookies и GPU renderer
  не проверены этим запуском. Startup и существующие автоматические GUI suites
  их ручную приёмку не заменяют.

Проверки `wtf`: структура/изменения/ignored artifacts/конфигурация и состав
пакета проверены; признаки добавленных credentials не найдены. Web auth,
БД и migrations — N/A. Пользовательские engine/models/settings не менялись.
