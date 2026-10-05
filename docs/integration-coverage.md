# Интеграционное покрытие кандидата

`gui_cli_integration` запускает Backend с настоящим `transcribe`. Подменяются
только FFmpeg, whisper-cli и yt-dlp переносимым native tool fixture. Исходный WAV
фиксирован, пути содержат пробелы, кириллицу и emoji. Workers объявляют ready/PID
после вывода частичного текста; release file удерживает обработку на границе.
Отмена и гонки синхронизируются этими событиями.

| Сценарий | Linux | Windows |
|---|---|---|
| GUI → CLI → live preview → completed + TXT/SRT/VTT | Каждый PR | Каждый PR |
| 2 chunks/jobs, порядок и сдвиг времени SRT/VTT | Каждый PR | Каждый PR |
| URL temporary → final result и cache reuse | Mock сеть, каждый PR | Mock сеть, каждый PR |
| Cancel после partial, нет оставшихся PID, retry | Каждый PR | Каждый PR |
| Tool failure code 17, прежний result остаётся готовым, retry | Каждый PR | Каждый PR |
| История после перезапуска с отдельным preview | Каждый PR | Каждый PR |
| Настоящий FFmpeg 1/2/17/256 + Unicode/sample bounds | Каждый PR | Pinned FFmpeg package job, каждый PR |
| Протокол/range/model recovery/states | Общие C++/Qt vectors | Общие C++/Qt vectors |
| POSIX signals/installation/supervisor SIGKILL | Отдельные Linux suites | Windows Job Objects/process suite |

Равное число CTest suites не требуется; Linux-specific installation/signals
сохраняются отдельно. Настоящие Whisper/model/audio, установленный ZIP/prefix,
VK/cookies и ручной UI smoke проверяются отдельно в отчёте кандидата.

CI сохраняет GCC/Clang Linux и native MSVC с warnings-as-errors. GUI обязательно
проверяется с Qt 6.8.3 на обеих ОС; дополнительная Linux-конфигурация использует
[выпущенную Qt 6.11.2](https://www.qt.io/blog/qt-6.11.2-released).
Сборка GUI формирует compilation database для clang-tidy/cppcheck. Оба анализатора
выбирают только TUs из project src/tests/gui, исключая generated/vendor trees.
Для clang-tidy 18 Qt includes переводятся из `-isystem` в `-I` только в
копии compilation database анализатора: [LLVM #62985](https://github.com/llvm/llvm-project/issues/62985)
ошибочно учитывает overloaded Qt delete дважды. Компиляция и остальные SDK
не меняются; проверка NewDelete остаётся включённой. Исключения на строках
QML registration macros относятся только к SDK enum marker и обязательной
static plugin registration; пользовательские enums/initializers анализируются.
Cppcheck использует Qt library definitions, реальную MOC revision из SDK headers
и MOC helper markers из SDK, определение static QML plugin registration macro; diagnostics GUI не отключены.

Отдельный Linux job инструментирует проект ASan/UBSan и запускает проектные
сценарии с LSan включённым. Qt SDK не пересобирается с sanitizers. Диагностики SDK
требуют отдельного разбора; глобальных suppressions нет. Локально LSan недоступен
из-за ptrace, что отражено в отчёте; local ASan/UBSan остаются активны.

Job logs и CTest diagnostics сохраняются как CI artifacts, в том числе при
ошибке. Compilation database/CMakeCache фиксируют параметры toolchain и SDK.
Проверки PR относятся к merge tree; финальный workflow_dispatch должен проверить
точный source SHA кандидата и упаковку, созданную из этого состояния.
