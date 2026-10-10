# Подготовка и публикация выпуска

Работа ведётся в `dev`; аннотированный тег ставится на проверенный итоговый
`master`. Номер версии берётся из `project(... VERSION ...)` в CMake.
Ниже `X.Y.Z` означает выбранную версию, `SOURCE_SHA` — полный проверяемый SHA.
Исторические validation документы описывают прежние испытания и не заменяются.

## Подготовка и freeze

1. Завершите изменения кода, упаковки, VERSION, README, CONTRIBUTING, CHANGELOG,
   `docs/release-notes-vX.Y.Z.md` и `docs/release-validation-vX.Y.Z.md` до freeze.
2. Из чистой копии выполните GCC/Clang warnings-as-errors, CTest, ASan/UBSan,
   static analysis и QML lint; проверьте сборку без тестов, установку, help/version.
3. Проверьте сохранение settings/models/results и legacy history при upgrade.
4. Создайте Conventional Commit и candidate branch/PR. Проверьте source SHA,
   tree/fingerprint и отсутствие dirty tree. RC — обозначение кандидата, а не
   обещание, что релиз уже готов. Не переносите CI с предыдущего SHA.

## Единственный комплект кандидатов

На clean candidate вручную запустите **CI** и **Linux portable GUI** с
`real_smoke=true`, а также **Real Whisper smoke**. Дождитесь всех обязательных
jobs (в текущем CI их 10); проверяйте steps, включая отсутствие skipped real
steps. Windows MSVC/Qt, Linux Qt 6.8.3/6.11.2, sanitizers и static analysis входят
в обязательную матрицу. В отчёте сохраните ссылки на конкретные run и source SHA.

Локальная сборка release candidates:

```sh
bash scripts/package-linux.sh --container --release --real-smoke --result-file /output/candidate.json
python3 tests/check_linux_candidate.py dist/candidate.json
```

```powershell
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64 -Release -RealSmoke
```

Release mode запрещает dirty tree и SkipTests; diagnostic пакеты имеют явное
обозначение. Windows ZIP проходит smoke в staging до переноса в Destination.
Source identity снимается до configure и проверяется после финального smoke.
AppImage собирается один раз; builder, clean Ubuntu и Debian проверяют один
SHA-256. Clean containers работают без сети и без Qt/Python/FFmpeg/yt-dlp;
real mode обязательно выполняет локальный ASR на тех же public fixtures.

Запишите application/source/dependency/input/model/package hashes и toolchain.
Сохраните проверенные build inputs, особенно bytes инструментов с continuous URL,
вместе с manifests и build logs; moving URL не считается immutable источником.
Не пересобирайте файл после приёмки. Изменение кода или payload требует нового
кандидата и повторения затронутых проверок.

## Ручная приёмка

Конечные ZIP/AppImage с теми же hashes проходят следующую матрицу:

| Среда | Обязательная проверка |
| --- | --- |
| Windows 11 без SDK | Unicode unpack, model download/import, local ASR, cancel/close/retry/history, TXT/SRT/VTT |
| Omarchy/Hyprland | Native Wayland, масштаб 160%, Tab/Shift+Tab, минимальное окно, portal/clipboard |
| Linux и Windows | Small, medium+VAD по умолчанию, turbo; один/несколько chunks, порядок/sample bounds |
| VK/authentication | Public URL, отказ сети, cookies file/browser, cache reuse/refresh |
| Длинная запись 10–15 минут | RAM, responsiveness, отмена, границы chunks и ручная оценка срезов |

Решением пользователя от 10 октября 2026 ручная Ubuntu desktop приёмка больше
не обязательна для выпуска. Clean Ubuntu/Debian container checks сохраняются.
Windows 11 проверяется по [ручной инструкции](windows-11-manual-validation.md).

Каждый результат содержит OS/build, source/package/input/model SHA и evidence.
Приватные URL, cookies, записи и расшифровки остаются локальными. Публичные
CI artifacts используют только закреплённые public fixtures. Не считать
Server 2022 с SDK доказательством чистой Windows 11, offscreen — desktop приёмкой,
а короткий smoke — измерением WER. Незакрытый обязательный пункт блокирует выпуск.

## Материалы бинарной раздачи

Для фактического payload подготовьте versioned notices и corresponding source /
build materials Qt, FFmpeg, yt-dlp и зависимостей, copied Linux libraries и
AppImage runtime. Сопоставьте компоненты и версии с inventory; сохраните
проверенные исходники, патчи, build scripts и inputs. Проверьте возможность
замены shared libraries. См. `packaging/linux/THIRD-PARTY.md` и
`packaging/windows/THIRD-PARTY.md`. Source archive Transcribe не заменяет этот
комплект. До готовности этих материалов бинарники не публикуются как release assets.

`packaging/source-inputs.json` закрепляет исходники FFmpeg и yt-dlp, snapshot
BtbN build recipes и восемь Qt source modules точными revisions/SHA-256.
Исходники связаны с binary pins и Qt SDK version обоих комплектов. Упаковщики
сохраняют эти байты в build-inputs archive; обновление binary/Qt pin без
соответствующего source mapping отклоняется. `package_notices.py` копирует
Qt notices/attributions и указанные ими license/copyright files из проверенных
архивов в пакет; `Qt-source-notices/notices.json` фиксирует hashes и происхождение.
`package_standalone_notices.py` сохраняет notices и metadata из проверенного
yt-dlp PyInstaller archive без исполнения кода. Source lock также содержит
раздельные по binary SHA исходники встроенного Python и Python dependencies;
оба упаковщика используют общий `package_inputs.py select-sources` selector.
Linux дополнительно сохраняет binary/source package versions и copyright texts
в `linux-library-provenance.json` и `licenses/Linux-system`.
`package_linux_sources.py` сопоставляет эти библиотеки точным source package
versions из authenticated APT indexes, сохраняет `.dsc`/upstream/Debian archives
без распаковки и проверяет их SHA-256. `linux-source-provenance.json` связывает
сохранённые inputs с payload hashes. Container builder включает `deb-src` и
сохраняет индексы; при локальной сборке matching indexes нужны заранее.
Отсутствующая exact version, неоднозначные checksums или изменённый cache
останавливают упаковку. Источники нельзя заменять ближайшей доступной версией.
Та же процедура включает DejaVu и CA bundle: записываются hashes payload,
точные binary/source versions и hash принадлежащего пакету генератора CA bundle.
`package_sdk_sources.py` проверяет original hashes трёх ICU libraries из Qt SDK
по `linux_sdk_dependencies` в source lock. Проверенные ICU 73.2 sources входят
в Linux build inputs, notices — в `licenses/Qt-sdk-source-notices`. Обновление
SDK binaries без проверенного mapping отклоняется даже при прежней версии Qt.
`package_runtime_sources.py` отдельно проверяет AppImage runtime SHA и
`linux_appimage_runtime` в source lock. Linux архив включает sources runtime,
musl, mimalloc, zstd, zlib, libfuse, squashfuse и два exact Alpine recipe snapshots
с патчами. Оригинальные notices включаются в
`licenses/AppImage-runtime-source-notices`; `appimage-runtime-provenance.json`
связывает их с binary pin и upstream build evidence. Обязательные notices,
включая terms из README/header, должны присутствовать в verified source archive.
Это ещё не полный corresponding-source комплект: зависимости FFmpeg/yt-dlp,
toolchain/runtime details и build configuration Qt/SDK libraries собираются
и сопоставляются отдельно. `corresponding_sources_complete=false` сохраняется
до фактического завершения этой работы.

`ffmpeg_dependencies` сохраняет частичный набор 83 pinned dependency source archives:
81 для Linux и 79 для Windows. `package_ffmpeg_sources.py` проверяет SHA исходного FFmpeg
archive, исходного recipe snapshot и соответствие SCRIPT_REPO/SCRIPT_COMMIT
каждой библиотеки, включая нумерованные recipe slots. Если recipe использует тег,
`recipe_revision` сохраняет его имя, а `revision` и URL архива закрепляют полный
resolved commit; `recipe_tag_object_url` указывает на проверенный upstream tag
object. При обновлении такого input нужно заново проверить разрешение тега
в commit. Например, Windows FFmpeg использует Schannel, но его libssh/SRT всё
равно требуют OpenSSL; соответствующие sources сохраняются на обеих платформах.
MinGW/winpthreads сохраняется только для Windows; его GitHub mirror указан на
upstream странице `https://www.mingw-w64.org/source/`. Для gnulib используется
тот же GitHub mirror, что `SCRIPT_MIRROR2` в pinned libiconv recipe, и полный
`SCRIPT_COMMIT2`. Поле `repository` продолжает обозначать исходный SCRIPT_REPO.
Для libdrm сохраняются original `LICENSES/MIT.txt` и copyright-bearing core sources,
public headers/build script; для MinGW — runtime/winpthreads
COPYING, AUTHORS и disclaimers; для gnulib — COPYING и original license-notices.
Исходное дерево libiconv сохраняется отдельно на полном `SCRIPT_COMMIT`,
вместе с COPYING/COPYING.LIB и notices libcharset; его gnulib
располагается по pinned recipe. Generated inputs и toolchain materials остаются
отдельной незавершённой частью F7.
Для официальных Googlesource archives без enclosing directory поле
`strip_components=0` сохраняет пути notices целиком. По умолчанию снимается
один корневой каталог; другие значения, absolute paths и traversal отвергаются.
Gitiles назначает файлам время создания archive при каждом запросе, поэтому
у AOM/libvpx/libwebp `canonical_tar=true`: downloader формирует TAR с нулевыми
timestamps/owners и стабильным порядком записей, затем проверяет его закреплённый
`sha256`. Пин относится к сохраняемому `.tar`, а не к изменчивому upstream gzip.
Пути, типы, permissions и содержимое файлов входят в проверяемые bytes; symlinks,
devices, duplicate/unsafe entries и неизвестные PAX fields отвергаются. Оригинальные
тексты notices сохраняются без изменений. Helper общий для Linux/Windows и
публикует input только после совпадения SHA-256. Остальные downloads проверяются
по исходным bytes. Обе платформы посылают явный `Transcribe-package/1` User-Agent;
HTTP 200 с HTML challenge также отклоняется проверкой hash.
Для libiconv, soxr и opencore-amr `git_snapshot=true` сохраняет TAR прямо из
primary HTTPS Git repository. Общий `package_git_sources.py` fetches полный
закреплённый commit в временный bare repository, проверяет FETCH_HEAD и SHA-256
созданного `git archive` с явными prefix и `tar.umask=0022`. Checkout и source
scripts не выполняются. Git system/global config, inherited GIT variables,
hooks, credential helpers и redirects исключены; cache публикуется атомарно
без замены существующего input. Original Git metadata и source symlinks
остаются в pinned TAR как данные; notice collector не следует по ссылкам.
Helpers `package_git_sources.py`, `package_canonical_sources.py` и
`package_source.py` сохраняются также в `materials/` build-inputs архива.
Для LAME `svn_snapshot=true` использует неизменяемый SVN DAV baseline revision
6835. Общий `package_svn_sources.py` проверяет repository UUID, числовые revisions,
пути, per-file lengths/SHA-1 и окончательный TAR SHA-256. Raw repository bytes,
executable bits и original SVN/custom properties сохраняются; свойства доступны
в `.transcribe-svn-properties.json` внутри source TAR. Helper не разворачивает
keywords/EOL и не выполняет исходники. Checkout reconstruction должен учитывать
сохранённые properties; original FFmpeg recipe с `SCRIPT_REV` также сохраняется.
Redirects, XML DTD/entities, externals и special files отвергаются, объём/число
entries ограничены. Temporary files очищаются, готовый input публикуется без
замены прежнего cache; helper включается в build materials на обеих платформах.
SVN requests ограничены тремя попытками для HTTP 502/503/504, timeout,
connection reset, incomplete read и unexpected TLS EOF. Certificate errors,
invalid metadata и hash mismatch не повторяются.
AMF сохраняет только используемые recipe пути `amf/public/include` и original
`LICENSE.txt`: 57 headers и license, 58 original files. `git_archive_paths`
задаёт явные относительные пути без patterns/options/traversal; helper передаёт
их после `--` в `git archive` и сохраняет strict commit/TAR pins. Каждый header
также включён в original notices. Git retrieval может загружать полное дерево
objects (~345 MB для закреплённого AMF); это временный cache, а source TAR
содержит лишь 58 выбранных файлов (~640 KiB). Для выбранных source paths предел
одной Git команды — 300 секунд; обычные Git sources сохраняют предел 120 секунд.
Linux downloader повторяет только HTTP 502/503/504, максимум три попытки
с паузами 1 и 2 секунды; ошибка называет input. SHA mismatch, HTTP 404 и
небезопасный redirect не повторяются. Эти retries не меняют закреплённые bytes.
Проверяются также enable
flags настоящего bundled FFmpeg; notices из verified sources сохраняются в
`licenses/FFmpeg-dependency-source-notices`, а связи — в
`ffmpeg-source-provenance.json`. Для FFmpeg 8.1 используется recipe slot 2
nv-codec-headers (SDK 13.0). Chromaprint использует FFTW с GPL notices; upstream
имя `lgpl` не заменяет рассмотрение условий каждого компонента. Полнота набора
не заявляется: оставшиеся библиотеки, nested/generated inputs и toolchains
всё ещё требуют соответствующих материалов.

## Слияние и публикация

1. Слейте прошедший приёмку candidate через PR в `master`, Conventional Commit
   для merge: `chore(release): merge vX.Y.Z into master`. Не удаляйте `dev`.
2. Проверьте итоговый master SHA и полный обязательный CI. Если code tree изменился,
   соберите новый комплект и повторите приёмку; старое evidence автоматически не переносится.
3. Сопоставьте exact-SHA manifests, checksums, ручную матрицу и материалы раздачи.
   Все обязательные gates должны быть подтверждены текущим отчётом.
4. Убедитесь, что тег отсутствует, создайте аннотированный `vX.Y.Z` на проверенном
   master. Проверьте `git rev-parse vX.Y.Z^{commit}` и remote peeled tag.
5. Подготовьте notes из `docs/release-notes-vX.Y.Z.md` и прошедшие assets. Проверьте
   состав на private logs/media/model weights. Опубликуйте существующий проверенный
   комплект; повторная упаковка после испытаний недопустима.
6. Скачайте опубликованные assets в новый каталог, сверьте SHA-256 и выполните
   краткий запуск. Отдельно проверьте GitHub source archive и его соответствие тегу.
7. Дополните актуальный отчёт ссылками на final-SHA CI, тег, release и hashes.
   Убирайте только созданные в этой задаче временные файлы, сохраняя необходимые
   build inputs/evidence и пользовательские данные.

Текущий кандидат v0.4.0 ещё не допущен к публикации; фактическое состояние —
в [release validation](release-validation-v0.4.0.md). Не публикуйте выпуск по
одному зелёному workflow или неполной ручной матрице.
