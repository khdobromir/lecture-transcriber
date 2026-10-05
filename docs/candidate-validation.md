# Проверка установленного кандидата стабилизации

Этот документ описывает воспроизводимую проверку и её ограничения. Он не
назначает следующую версию и не объявляет готовность к выпуску. Исторические
release notes и отчёт v0.3.0 сохранены. Общий перечень:
[stabilization-scenarios.md](stabilization-scenarios.md).

## Получение evidence для точного SHA

Проверки PR используют merge tree. После последнего изменения исходников,
зависимостей или упаковки запускается `CI` через workflow_dispatch на ветке
кандидата с `real_smoke=true`. Отчёт привязывается к этому source SHA, а не к
предыдущему зелёному запуску. `package-manifest.json` содержит source SHA и
dirty flag; `.validation.json` рядом с ZIP содержит SHA-256 **самого ZIP**,
не digest контейнера GitHub artifact.

Linux Qt 6.8.3 job устанавливает GUI/CLI в отдельный Unicode prefix, выполняет
startup smoke, собирает закреплённый Whisper и запускает `test_gui_real` с
установленным CLI. Windows package job сначала проверяет распакованный
итоговый ZIP с минимальным PATH, затем candidate-only harness запускает CLI
из этого ZIP. Harness использует Qt SDK; этот сценарий не доказывает работу
на чистом Windows-компьютере без SDK.

Artifacts содержат QtTest/CTest diagnostics, toolchain configuration,
`prerequisites.json`, `validation.json`, screenshot приложения и проверенный
ZIP с отдельными checksum/validation files. Модели и исходное аудио в Git
и diagnostic artifacts не включаются. `tests/fixtures/russian-speech.json`
содержит URL/SHA-256 публичного OGG, 30-секундный интервал, лицензию и атрибуцию.
SHA-256 фактического WAV после декодирования фиксируется для каждой ОС; вход
и модель должны быть проверены перед использованием. Побайтовое равенство
ASR-текста между CPU не является критерием.

## Дополнительная реальная проверка

Реальный Whisper выявил две ситуации, отсутствовавшие в mock fixtures:
20 ms quantization timestamps и предсказанный конец последнего сегмента
после конца аудиочасти. `export_timestamp_bounds` проверяет реальные sample
bounds, ограничение пересекающегося сегмента, одно contextual warning при
существенном ограничении и отказ для целиком внешнего сегмента. Финальные
экспорты остаются в пределах исходных samples; ошибка сохраняет рабочие файлы.

Локальная разработческая проверка с Qt 6.11.2/GCC 16/FFmpeg 9 подтвердила
installed GUI startup и реальный QML → CLI → Whisper с medium и small,
первый model import, preview, completed и три экспорта. Это evidence рабочего дерева; финальный
SHA обязан повторить затронутые проверки. Native Wayland/Hyprland проверка
окна, Tab/Shift+Tab, minimum size и screenshot прошла без QML warnings.
Минимальный Qt 6.8.3 также прошёл 18 CTest suites, QML lint, cppcheck и
clang-tidy 18 для новых сценариев; installed prefix и реальная цепочка с
первым импортом small model проверены отдельно. Это также проверка рабочего дерева.

CLI `install.sh` допускает отдельный `TRANSCRIBE_BIN_DIR`: это позволяет
проверить обновление копии v0.3.0 без изменения HOME или пользовательского CLI.
Обычный путь установки остаётся `~/.local/bin`. Installation regression
проверяет независимый prefix и сохранение прежнего пользовательского файла.
Реальный изолированный upgrade копии v0.3.0 с `install.sh` и subsequent
GUI → обновлённый CLI → pinned small model прошёл; пользовательский CLI не менялся.
100/150/200% scale, light/dark, minimum window, длинный input и keyboard
проверены автоматически; native Wayland и X11/XWayland запуски прошли.

## Матрица допуска

| Проверка | Обязательное evidence |
|---|---|
| Четыре review defects | Native Windows/Linux regressions, PR #5–#7 |
| Protocol/process/cancel | Общие vectors, GUI streaming и управляемое дерево, PR #8 |
| GUI state/ownership/retry | Переходы, поколения, teardown, late commit, PR #9 |
| GUI → настоящий CLI | Success/failure/cancel/cache/history на обеих ОС, PR #10 |
| Реальное распознавание | Source/model/input SHA, Qt/Whisper/FFmpeg versions, preview + bounded TXT/SRT/VTT |
| Итоговый ZIP | Manifest + все files, CRT/DPI/asInvoker, clean PATH, Unicode unpack, ZIP SHA |
| Linux install | System Qt и официальный SDK, отдельный prefix/cwd, entry/icon, legacy upgrade |
| Interface | 100/150/200% DPI, light/dark, minimum size, long messages, keyboard, dialogs/clipboard/open exports |
| Финальный CI | Все обязательные jobs на точном SHA workflow_dispatch |

Отсутствие результата обозначается «не проверено». Native Windows CI использует
Windows Server 2022 runner; чистая Windows 11 VM/машина недоступна. Ручная полная
цепочка ZIP без Qt/Visual Studio/Python/Git, native dialogs/clipboard/open exports
и пользовательские DPI остаётся отдельным блокирующим условием выпуска.
Проверки Wayland automation не заменяют оценку native file dialogs/portal и
полный ручной UI smoke. VK/cookies/network являются отдельными сценариями и не
объявляются пройденными по mock cache tests. Большие Windows processor groups и
специальные cgroup/affinity конфигурации не проверены.
