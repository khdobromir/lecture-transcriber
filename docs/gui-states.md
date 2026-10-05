# Состояния GUI и владение workers

GUI использует Qt Quick Controls и Layouts. C++ управляет задачей, моделью и
файлами; QML показывает состояния и вызывает разрешённые действия. Компонент
получает Backend как required property. Модели списка принадлежат GUI thread.
Основание: [Qt Quick best practices](https://doc.qt.io/qt-6.8/qtquick-bestpractices.html),
[Qt Quick performance](https://doc.qt.io/qt-6.8/qtquick-performance.html).

## Текущая задача и сохранённый результат

`TaskState` и guards определены в `gui/states.hpp`:

| Из | Разрешённые переходы |
|---|---|
| idle/completed/interrupted/failed | starting |
| starting | running/cancel_requested/failed/interrupted |
| running | cancel_requested/finalizing/failed/interrupted |
| cancel_requested | finalizing/failed/interrupted |
| finalizing | completed/cancel_requested/failed/interrupted |

Повторный переход в то же состояние допустим. `busy` выводится из состояния;
новая задача запрещена, пока задача или установка модели активна. `canCancel`
становится false после terminal event. Завершённый backend с согласованным
успехом переходит в finalizing на время независимой проверки manifest/экспортов.
Поздняя отмена не отменяет подтверждённый commit. Обычное закрытие асинхронно
ждёт окончания задачи и/или операции с моделью.

`taskResultDirectory` относится к текущему запуску; `resultDirectory`, текст и
`selectedStatus` — к выбранному сохранённому результату. Историю можно открыть
во время задачи и затем вернуться к её результату. Preflight и FailedToStart
меняют только текущую задачу. Retry сохраняет параметры в памяти; cookies не
записываются в QSettings. Каталог и журналы доступны отдельно для текущей задачи.
Ошибка связи не делает сохранённый completed manifest ошибочным: его можно
открыть из истории отдельно от состояния текущего процесса.

Каждый запуск, выбор результата и scan имеют поколение. Preview/history workers
получают снимок параметров и cooperative cancellation token. Outstanding flag
остаётся true до обработки finished в GUI thread: уже завершённый worker с
ещё не доставленным callback нельзя переиспользовать для нового чтения.
Устаревший callback не применяет результат к новому выбору или scan.
Чтение TXT ограничено 512 KiB; хвост объясняется в тексте, полный TXT открывается
отдельным действием. Ошибки preview/history и предупреждения имеют свой контекст.

## Установка модели

`ModelState`: idle, waiting_lock, checking_existing, downloading, verifying,
publishing, cancelling, ready, failed, cancelled. Неактивное состояние допускает
новую операцию только через waiting_lock. Рабочие фазы переходят друг в друга;
import может сообщать снимки фаз, пропуская уже завершённые короткие фазы.
Из cancelling разрешён только фактический итог ready/failed/cancelled.

Hash, import, backup и replace выполняются в workers. Отмена проверяется при
ожидании lock, между блоками чтения/копирования и при повторах replace. Только
GUI thread использует QNetworkReply, watcher и QObject. HTTP callbacks проверяют
identity reply и поколение операции. Публикация и сохранение выбора модели
заканчиваются под `.install.lock`; ready/failed/cancelled освобождают владение
ровно после завершения worker. Успешный commit сохраняет ready при поздней отмене.

Повреждённая установленная модель не удаляется. Пользователь явно выбирает
«Сохранить резервную копию и восстановить»: сначала создаётся отдельная копия
под installation lock, затем новый файл проверяется по SHA-256 и публикуется.
Ошибка сети/hash/записи сохраняет оригинал и завершённую резервную копию.
Импорт неверного файла также не заменяет установленную модель.

Деструктор — страховка: выставляет cooperative cancellation, отключает callbacks
и соединяет workers, перехватывая исключения уже завершённой работы. Штатный
close не вызывает блокирующий join в обработчике UI. Tests используют data-only
Readers/Workers hooks и barriers для stale callback, teardown и commit/cancel.

## Интерфейс и проверки

Стандартные Controls обеспечивают клавиатурные действия; Ctrl+O, Ctrl+Enter,
Ctrl+Q и F5 документированы в интерфейсе. Native dialogs возвращают фокус.
Длинные ошибки задачи и модели доступны для выделения в ограниченных ScrollView;
пути и сохранённые сообщения отображаются как plain text. Минимальное окно
720×600; экспортные действия переносятся по ширине.

Controller/QML tests проверяют guards, preflight и FailedToStart/retry,
A→B→A, устаревший scan, исключения workers, teardown, cancel hash/publish,
позднюю отмену после commit, backup recovery, close при задаче/model lock и
Tab/Shift+Tab. Ручные native dialogs/portal/clipboard/DPI в установленных
пользовательских окружениях отмечаются отдельно в отчёте кандидата.
