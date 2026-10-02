# Проверка подготовки v0.2.1 — 2 октября 2026

Версия CMake и CLI — `0.2.1`. Основа подготовки — `dev` на `1317eb4`;
релизные изменения обновляют номер версии и документацию, без изменения
поведения исходного C++-кода. Проверки выполнены в отдельной копии tracked
исходников без `.git`, прежних сборок и пользовательских данных.

## Выполненные проверки

| Проверка | Результат |
|---|---|
| Release GCC 16.2.1, `-Wall -Wextra -Wpedantic -Werror` | Успешно |
| Release Clang 23.1.1, те же предупреждения и `-Werror` | Успешно |
| CTest GCC и Clang | Все пять suites прошли в каждой сборке |
| Отдельная Release-сборка с `BUILD_TESTING=OFF` | Успешно |
| `cmake --install` во временный prefix, `--help` и `--version` | Успешно; `transcribe 0.2.1` |
| `--version` GCC/Clang сборок с тестами | `transcribe 0.2.1` |
| Реальный medium-q5_0 и Silero VAD 6.2.0 | Успешный экспорт 5 с JFK, язык `ru`, 9.52 с |
| Bash syntax и ShellCheck | Успешно для установщика и всех shell-скриптов |

Полный CTest запускает 119 сценариев: 54 pipeline, 19 installation,
11 smoke_preflight, 20 unit_cli и 15 unit_audio. Обычные сценарии используют
подмену Whisper и настоящий FFmpeg; real smoke выполнен отдельно с уже
проверенными движком и моделями во временном каталоге. Установщик пользователя
не запускался; глобальные зависимости и пользовательская установка не менялись.

Команды для чистой копии (повторить для GCC и Clang с разными build directories):

```bash
cmake -S SOURCE -B BUILD -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-Werror
cmake --build BUILD --parallel 2
ctest --test-dir BUILD --output-on-failure
BUILD/transcribe --version
cmake -S SOURCE -B BUILD-NO-TESTS -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-Werror
cmake --build BUILD-NO-TESTS --parallel 2
cmake --install BUILD-NO-TESTS --prefix TEMP-PREFIX
TEMP-PREFIX/bin/transcribe --help
TEMP-PREFIX/bin/transcribe --version
```

## Удалённый CI и публикация

Базовый коммит `1317eb4` прошёл все пять jobs
[CI на GitHub](https://github.com/khdobromir/lecture-transcriber/actions/runs/37058533165):
Ubuntu GCC/Clang, Debian GCC, Arch и статический анализ, включая двадцать
повторов сценариев отмены и падения. Для релизного коммита и PR проверки
запускаются заново. После слияния отдельно проверяется итоговый SHA `master`;
тег создаётся только после его успешного CI согласно [инструкции](releasing.md).
Этот документ не объявляет заранее результат будущих CI или публикации.

Подробности статического анализа, изолированных Ubuntu/Debian проверок,
предыдущего ASan/UBSan и границ LeakSanitizer — в [отчёте](test-validation.md).

## Границы проверки

Короткий английский JFK с языком `ru` подтверждает интеграцию и экспорт,
а не качество русских лекций. Длинные записи, ускорение дробления, качество
возле границ, загрузка конкретных видео VK и cookies этим выпуском не измерялись.
Санитайзеры при подготовке версии повторно не запускались: изменения касаются
версии и документации. SIGKILL самого transcribe и потомки, покинувшие
управляемые группы процессов, остаются ограничениями отмены.
