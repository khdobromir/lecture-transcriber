# Проверка подготовки v0.3.0 — 3 октября 2026

Исходная `dev` — `79fd3b572159f3fe34460cf2a55fe5aae61eda18`,
базовая `master` — `69f877665f325da6c7a2c2a8c635cf36f77be067`.
Подготовка меняет версию CMake на `0.3.0` и документацию;
изменения поведения выпуска описаны в [release notes](release-notes-v0.3.0.md).

Проверки проведены в новой копии tracked исходников с релизными изменениями,
без `.git`, skills, старых сборок, кэша и пользовательских данных. Сборки и
установка CLI во временный prefix находятся только в `/tmp`.
Системные пакеты и существующая пользовательская установка не изменялись.

## Свежие результаты

| Проверка | Результат |
|---|---|
| GCC 16.2.1 Release, `-Wall -Wextra -Wpedantic -Werror` | Build exit 0; CLI `transcribe 0.3.0` |
| Clang 23.1.1 Release, те же предупреждения | Build exit 0; CLI `transcribe 0.3.0` |
| CTest GCC | 6/6 suites, 158/158 сценариев, 20.71 с |
| CTest Clang | 6/6 suites, 158/158 сценариев, 20.84 с |
| Clang ASan + UBSan, Debug/O1 | 6/6 suites, 158/158 сценариев, 28.76 с; ошибок ASan/UBSan нет |
| `BUILD_TESTING=OFF`, временный `cmake --install` | Build/install exit 0; `--help` exit 0, `transcribe 0.3.0` |
| Реальный Whisper medium-q5_0 + Silero VAD 6.2.0 | 5 с JFK, язык ru, экспорт проверен; exit 0, 16.60 с |
| Bash syntax + ShellCheck 0.11.0 | Exit 0 для install.sh и scripts/*.sh |
| clang-tidy 23.1.1 с конфигурацией проекта | 16 единиц трансляции, exit 0; нет diagnostics в анализируемом коде |
| `git diff --check`, проверка приватных идентификаторов | Exit 0; URL/название частного тестового источника в публикуемых файлах отсутствуют |

Сценарии: 73 pipeline, 19 installation, 11 smoke_preflight, 22 unit_cli,
24 unit_audio, 9 unit_features. Pipeline использует настоящий FFmpeg и подмену
Whisper; smoke отдельно использует уже установленный движок и модели,
которые проверяет по SHA-256 манифеста. Пользовательский install.sh не запускался;
installation suite использует изолированные mock-окружения.

clang-tidy подавил 926 предупреждений из non-user code и 15 существующих NOLINT
с пояснениями; это не утверждение об отсутствии всех предупреждений стороннего
кода. Локально cppcheck отсутствует: он проверяется в отдельном job CI, без
установки нового сканера на пользовательскую систему.

## Команды

`SOURCE` — чистая копия исходников, каждый `BUILD` — новый каталог.
Release-команды выполнены отдельно с g++ и clang++:

```bash
cmake -S SOURCE -B BUILD -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-Werror -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build BUILD --parallel 2
ctest --test-dir BUILD --output-on-failure -V
BUILD/transcribe --version

cmake -S SOURCE -B BUILD-ASAN -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build BUILD-ASAN --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir BUILD-ASAN --output-on-failure -V

cmake -S SOURCE -B BUILD-NO-TESTS -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-Werror
cmake --build BUILD-NO-TESTS --parallel 2
cmake --install BUILD-NO-TESTS --prefix TEMP-PREFIX
TEMP-PREFIX/bin/transcribe --help
TEMP-PREFIX/bin/transcribe --version
BUILD/smoke_real --binary BUILD/transcribe --artifacts NEW-TEMP-DIRECTORY

cd SOURCE
bash -n install.sh scripts/*.sh
shellcheck -x install.sh scripts/*.sh
clang-tidy -p BUILD-CLANG src/*.cpp tests/*.cpp
```

## Удалённый CI и публикация

Перед слиянием отдельно проверяются head/base PR, его diff и свежие пять jobs:
Ubuntu 24.04 GCC/Clang 18, Debian 13 GCC, Arch Linux и Static analysis
(clang-tidy 18, cppcheck, ShellCheck). Ubuntu GCC дополнительно выполняет
20 повторов сценариев отмены/падения и 20 повторов сценариев кэша.
После слияния проверяется CI на точном merge SHA master;
аннотированный тег создаётся только после успеха, без force и без удаления dev.
Результаты удалённого CI связываются с PR и GitHub Release после их получения;
этот документ не объявляет заранее результаты будущих запусков.

## Границы проверки

LeakSanitizer отключён (`detect_leaks=0`): поиск утечек памяти этим прогоном
не подтверждается. ASan и UBSan активны; диагностик ошибок памяти/UB не найдено.
Короткий английский JFK с ru проверяет интеграцию и экспорт, а не WER русской речи.
Длинный запуск и потребление ресурсов измерены ранее на том же функциональном
коде, до изменения номера версии, в
[обезличенном отчёте](lecture-validation-2026-10-03.md); полный длинный прогон
при подготовке номера 0.3.0 не повторялся.

ETA имеет известные колебания между обновлениями процента. Точность распознавания,
ускорение дробления, качество возле границ частей и разные аккаунты браузера
этим выпуском не измерены. SIGKILL CLI и выход потомков из управляемых групп
процессов остаются ограничениями отмены.
