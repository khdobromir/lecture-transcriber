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
| Ubuntu 24.04 desktop | Заявленные X11/Wayland, FUSE/no-FUSE, dialogs, clipboard, open exports |
| Omarchy/Hyprland | Native Wayland, масштаб 160%, Tab/Shift+Tab, минимальное окно, portal/clipboard |
| Linux и Windows | Small, medium+VAD по умолчанию, turbo; один/несколько chunks, порядок/sample bounds |
| VK/authentication | Public URL, отказ сети, cookies file/browser, cache reuse/refresh |
| Длинная запись 10–15 минут | RAM, responsiveness, отмена, границы chunks и ручная оценка срезов |

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
Это ещё не полный corresponding-source комплект: зависимости FFmpeg/yt-dlp,
AppImage runtime, build configuration Qt и SDK libraries собираются
и сопоставляются отдельно. `corresponding_sources_complete=false` сохраняется
до фактического завершения этой работы.

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
