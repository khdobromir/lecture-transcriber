# Участие в разработке

Перед изменениями прочитайте README и проверьте, нет ли похожего issue.
Для ошибки нужны шаги воспроизведения, команда, версия программы, дистрибутив,
модель и относящийся к проблеме фрагмент вывода. Не прикладывайте cookies,
токены, приватные URL, записи и полные расшифровки.

## Локальные проверки

Разработка ведётся в `dev`. В Arch/Omarchy дополнительно к зависимостям README
нужны `shellcheck` и Python 3:

```bash
sudo pacman -S --needed shellcheck python
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-Werror
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
bash -n install.sh scripts/download-model.sh scripts/models.sh
shellcheck -x install.sh scripts/download-model.sh scripts/models.sh
```

Для Clang добавьте `-DCMAKE_CXX_COMPILER=clang++` и выберите отдельный каталог
сборки. CTest использует собранный бинарник, а не компилирует его другим GCC.
Тесты используют C++23 и Python 3 Interpreter; CMake требует его при
`BUILD_TESTING=ON` (по умолчанию). Для запуска переносимого пакета отдельный
Python не нужен.
Тесты установщика запускайте без sudo: они используют отдельный временный HOME,
настоящие хеши и файловые операции, подменяют сеть и сборочные инструменты.
Модели и доступ к аккаунту VK для автоматических тестов не нужны.

Набор CTest зависит от платформы, GUI и наличия FFmpeg; актуальный список:
`ctest --test-dir build -N`. В Linux CLI configuration с FFmpeg доступны 17 suites,
в GUI — ещё пять (включая real-smoke harness с отдельными prerequisites).
Метки: `unit`, `integration`, `installation`, `preflight`; GUI имеет также свои метки.
Unit-тесты не запускают FFmpeg или Whisper; pipeline
использует настоящий FFmpeg и управляемую подмену Whisper. Для выборочного запуска:

```bash
ctest --test-dir build -L unit --output-on-failure
TEST_CASE_FILTER=reliability_ ctest --test-dir build -R '^pipeline$' --repeat until-fail:20 --output-on-failure
```

`TEST_CASE_FILTER` выбирает сценарии по подстроке имени. Если совпадений нет,
suite завершается ошибкой, чтобы опечатка не давала ложный успешный результат.

Статический анализ требует `clang-tidy`, `cppcheck` с поддержкой C++23 и
`shellcheck`, установленных разработчиком. Отдельная сборка с Clang и compilation
database позволяет запускать те же проверки, что и CI:

```bash
cmake -S . -B build/analysis -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
bash scripts/check-static.sh build/analysis
```

Для версионированного исполняемого файла задайте, например,
`CLANG_TIDY=clang-tidy-18`. Анализируется весь собственный C++-код, включая тесты;
автоматические исправления отключены. Подавления допустимы только адресно, с
объяснением причины. Скрипт не устанавливает инструменты.

После обычной установки можно отдельно проверить реальный движок:

```bash
build/smoke_real --binary build/transcribe
```

Для этой проверки нужны установленная модель medium и VAD, а также встроенный
пример whisper.cpp `samples/jfk.wav`. Сборка только команды через CMake
не устанавливает движок и модели. Утилита ищет их в `TRANSCRIBE_HOME` либо
`~/.local/share/transcribe`. Если они находятся в другом каталоге, укажите его:

```bash
build/smoke_real --binary build/transcribe --app-home "$HOME/путь-к-установке"
```

Этот каталог должен содержать `whisper.cpp` и `models`; утилита не переносит
и не скачивает файлы. Программа обрабатывает пятисекундный
фрагмент во временном каталоге и проверяет экспорт. Это проверка процессов,
а не качества распознавания; английский пример обрабатывается с языком `ru`.

`--artifacts /абсолютный/путь/к/новому-каталогу` сохраняет рабочие файлы и
результаты технического примера, включая диагностику после сбоя. Родительский
каталог должен существовать; уже существующий каталог не перезаписывается.
Без этого параметра используется удаляемый временный каталог.

Обязательный CI проверяет Ubuntu 24.04 (GCC/Clang 18), Debian 13 (GCC) и Arch,
Windows MSVC CLI/Qt ZIP, Qt 6.8.3/6.11.2 Linux, ASan/UBSan,
а также clang-tidy, cppcheck и ShellCheck. Ubuntu GCC дополнительно повторяет
сценарии `reliability_` двадцать раз. Обычные проверки доступны по PR, push в
`dev`/`master` и ручному запуску.

Workflow **Real Whisper smoke** запускается вручную либо по понедельникам
в 03:17 UTC. Расписание работает после включения workflow в default branch.
Он с нуля собирает закреплённый Whisper, скачивает medium и VAD с проверкой
SHA-256 во временный каталог runner и не запускает `install.sh`. При ошибке
журналы и результаты публичного примера хранятся как artifacts семь дней;
готовые модели и бинарники не кешируются и не загружаются как artifacts.
Результаты локальной проверки этих изменений и её ограничения — в
[отчёте проверки тестов и CI](docs/test-validation.md).

GUI и упаковка: [Windows/Qt](docs/windows-gui.md), [AppImage](docs/linux-portable.md).
Для приёмки на exact SHA вручную запускаются **CI** и **Linux portable GUI** с
`real_smoke=true`, а также **Real Whisper smoke**. В portable workflow AppImage
собирается один раз; builder и offline Ubuntu/Debian проверяют один checksum.
Зелёный run с пропущенным real step не закрывает реальную приёмку.

Сравнение скорости, памяти и границ дробления описано в README. Выполненные
локальные проверки `dev` — в [отчёте](docs/dev-validation.md).
Проверки подготовки v0.2.1 — в [отчёте выпуска](docs/release-validation-v0.2.1.md).

## Pull requests

Для всех новых коммитов, включая коммиты слияния, используйте Conventional
Commits: `<type>[optional scope][!]: <description>`. Например,
`fix(process): stop whisper-cli on cancellation` или
`chore(release): prepare v0.2.1`.

Опишите проблему, конечное поведение и выполненные проверки. Для исправления
ошибки добавьте воспроизводящий её тест. Сохраняйте совместимость параметров
CLI и форматов результатов либо явно документируйте изменение.
Не добавляйте в PR модели, медиа, сборки или личные результаты.

Версия задаётся только в `project(... VERSION ...)` в CMake. Изменения версии,
зависимостей и пользовательского поведения отражайте в changelog.
Порядок подготовки и публикации — в [инструкции выпуска](docs/releasing.md).
