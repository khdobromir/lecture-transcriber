# Контракт стабилизации Windows/Linux

Основа: `ec4f2d0755f9cd873bb74531fc7e311b87afb0b8`, ветка
`stabilization/windows-gui`. Совместимость CLI: v0.3.0. Номер выпуска не выбран.

Текущая задача, выбранный исторический результат, подтверждённые файлы и установка
модели имеют независимые состояния. Ошибка preflight новой задачи не меняет
сохранённый статус выбранного результата. Работники передают данные; QObject и
QML меняются только в GUI thread. Каждый запуск/чтение имеет поколение.

Инварианты:

- 100% recognize означает завершение распознавания, затем следует публикация.
- Успех задачи требует согласованных terminal event, exit code и файлов на диске.
- Отмена до commit сохраняет частичные данные; после commit сохраняется успех.
- После выхода/отмены не остаются управляемые дочерние процессы.
- Сбой связи и отдельно подтверждённый сохранённый результат различаются.
- Штатное восстановление не требует ручного удаления .part/lock/служебных файлов.
- Ошибки задачи, предупреждения и состояние модели не затирают друг друга.

Один перечень используется тестами и отчётом кандидата. Каждый пункт применяется
к Linux и Windows, если явно не указана платформенная граница.

| ID | Сценарий / регрессия | Доказательство |
|---|---|---|
| FILE-1 | TXT/result.json заменяются при открытом собственном reader | Native test с удерживаемым handle; GUI preview/history |
| FILE-2 | Временная/постоянная внешняя блокировка и отмена | Windows: освобождение через barrier, ограниченный timeout, cancel |
| SPLIT-1 | 1/2/17/256 частей, Unicode и длинные пути | Настоящий FFmpeg; точные begin/end samples и сумма |
| SPLIT-2 | Ошибка/отмена между ограниченными группами | Portable mock tools с сигналом готовности |
| MODEL-1 | 200/206/416, неверный Content-Range, повреждённый/избыточный .part | HTTP fixture; bounded restart; SHA-256 |
| MODEL-2 | Разрыв, повтор, cancel hash/import/lock, ошибка записи | Рабочая модель сохраняется, lock освобождён |
| MODEL-3 | .part другой ревизии, HTTPS redirect | Привязка к SHA; итоговый hash |
| HISTORY-1 | 2000 известных + новый, несколько roots, удалённые, равные даты | GUI controller + перезапуск QSettings |
| PROTOCOL-1 | Типы/поля/числа, unknown, повтор hello/terminal, post-terminal | Общие serializer/parser vectors |
| PROTOCOL-2 | UTF-8 по частям, много строк, длинная строка, временный result | Потоковый parser; ограниченный бюджет GUI iteration |
| TASK-1 | Success/failure/cancel, до/после commit, stdout потерян | Настоящий CLI и проверка диска |
| PROCESS-1 | Потомки, flood, retained pipes, callback throw, forced stop | Native process tests; Linux groups / Windows jobs |
| GUI-1 | Preflight/FailedToStart → retry, delayed A→B→A, destroy workers | Controller transitions / generations |
| GUI-2 | Close при задаче/model install, cancel verification | Асинхронное завершение; QML/controller |
| GUI-3 | Статусы, retry, каталог/логи/экспорты, tail preview | QML + ручной установленный GUI |
| ASR-1 | Реальная русская запись + закреплённая модель | Оба ОС; hash записи/модели, структура TXT/SRT/VTT |
| INSTALL-1 | Итоговый ZIP после распаковки / Linux cmake install | Другой cwd, Unicode path, минимальное окружение |
| INSTALL-2 | Чистая Windows 11 / Ubuntu / Omarchy Wayland | Ручной полный сценарий, без SDK/build directories |
| UI-1 | DPI 100/150/200%, палитры, минимальное окно, клавиатура/фокус | Ручной smoke; отсутствие QML warnings |
| COMPAT-1 | CLI v0.3.0, частичные результаты, legacy history, upgrade | Существующие suites + установочный smoke |
| NETWORK-1 | VK URL/cache/cookies, штатная HTTPS-цепочка моделей | Отдельно от детерминированного CI |

Порядок: file sharing → split → model/history → protocol/cancel → GUI states →
integration/CI → packaging/report. Регрессия входит в тот же PR, что исправление.
Исторические release notes/отчёты v0.3.0 сохраняются. Проверка нового SHA не
наследует статус затронутых сценариев предыдущего кандидата.

Windows preflight: output parent до 169 UTF-16 единиц, локальный input до 240;
резерв для фиксированных worker options оставляется до запуска обработки.
Фактическая строка CreateProcessW проверяется с quoting и NUL (32767 единиц).
Все команды разбиения планируются до запуска первой группы; максимум 64 выхода
в группе, меньше при достижении предела по реальным путям. Точные sample bounds
не меняются. FFmpeg 6–8 использует обнаруженный `-filter_complex_script`, FFmpeg 9
использует `-/filter_complex`; описание графа в обоих случаях читается из файла.

Основания: [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw),
[Qt QProcess](https://doc.qt.io/qt-6.8/qprocess.html),
[Qt QFutureWatcher](https://doc.qt.io/qt-6.8/qfuturewatcher.html).
Блокирующие wait не используются в обычных обработчиках GUI; отмена обычного
QtConcurrent::run требует отдельного cooperative token.
