pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Transcribe

ApplicationWindow {
    id: window
    required property Backend backend
    width: 1040
    height: 820
    minimumWidth: 720
    minimumHeight: 600
    visible: true
    title: qsTr("Transcribe")
    property bool closeRequested: false

    function runTask(): void {
        if (backend.active) return
        backend.start(source.text.trim(), {
            model: customModel.text.trim() || modelPicker.currentText,
            output: outputPath.text.trim(),
            threads: threadCount.value, chunks: chunkCount.value, jobs: jobCount.value,
            vad: vad.checked, prompt: prompt.text, cookies: cookies.text.trim(), browser: browser.text.trim(),
            cache: cache.checked, refreshCache: refreshCache.checked, cacheDirectory: cachePath.text.trim(),
            cacheLimit: cacheLimit.value, keepAudio: keepAudio.checked
        })
    }
    onClosing: function(close) {
        if (backend.active) { close.accepted = false; closeDialog.open() }
    }
    Connections {
        target: window.backend
        function onChanged(): void { if (window.closeRequested && !window.backend.active) Qt.quit() }
    }
    Shortcut { sequence: "Ctrl+O"; enabled: !window.backend.active; onActivated: inputDialog.open() }
    Shortcut { sequence: "Ctrl+Return"; enabled: !window.backend.active && source.text.trim().length > 0; onActivated: window.runTask() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: window.close() }
    Shortcut { sequence: "F5"; onActivated: window.backend.refreshHistory() }

    FileDialog {
        id: inputDialog
        title: qsTr("Выберите видео или аудиофайл")
        fileMode: FileDialog.OpenFile
        onAccepted: { source.text = window.backend.filePath(selectedFile); source.forceActiveFocus() }
        onRejected: source.forceActiveFocus()
    }
    FolderDialog {
        id: outputDialog
        title: qsTr("Каталог результатов")
        onAccepted: { outputPath.text = window.backend.filePath(selectedFolder); outputPath.forceActiveFocus() }
        onRejected: outputPath.forceActiveFocus()
    }
    FileDialog {
        id: modelDialog
        property bool vadModel: false
        title: qsTr("Импорт модели из файла")
        nameFilters: [qsTr("Модель GGML (*.bin)")]
        onAccepted: { window.backend.importModel(window.backend.filePath(selectedFile), vadModel ? "vad" : modelPicker.currentText); tabs.forceActiveFocus() }
        onRejected: tabs.forceActiveFocus()
    }
    Dialog {
        id: closeDialog
        objectName: "closeConfirmation"
        width: Math.min(window.width - 48, 460)
        title: qsTr("Операция ещё выполняется")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            Label { text: qsTr("Запросить отмену и закрыть окно после завершения операции? Частичные данные сохранятся; уже сохранённый результат останется готовым."); wrapMode: Text.WordWrap; Layout.maximumWidth: 400 }
            RowLayout {
                Button { objectName: "stayButton"; text: qsTr("Остаться"); onClicked: { closeDialog.close(); source.forceActiveFocus() } }
                Button { objectName: "cancelCloseButton"; text: qsTr("Отменить и закрыть"); onClicked: { window.closeRequested = true; window.backend.cancel(); closeDialog.close() } }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Transcribe"); font.pointSize: window.font.pointSize + 8; font.bold: true }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Русская речь · локально · CPU"); opacity: 0.7 }
        }
        TabBar {
            id: tabs
            objectName: "navigationTabs"
            Layout.fillWidth: true
            TabButton { text: qsTr("Расшифровка") }
            TabButton { text: qsTr("История") }
            TabButton { text: qsTr("Модели") }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                id: taskScroll
                clip: true
                ColumnLayout {
                    width: taskScroll.availableWidth
                    spacing: 14
                    Frame {
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 10
                            Label { text: qsTr("Видео, аудио или ссылка"); font.bold: true }
                            RowLayout {
                                Layout.fillWidth: true
                                TextField {
                                    id: source
                                    objectName: "sourceInput"
                                    placeholderText: qsTr("Вставьте URL или выберите файл")
                                    Layout.fillWidth: true
                                    enabled: !window.backend.active
                                    Accessible.name: qsTr("Файл или URL")
                                    onAccepted: window.runTask()
                                }
                                Button { text: qsTr("Выбрать файл…"); enabled: !window.backend.active; onClicked: inputDialog.open() }
                            }
                            RowLayout {
                                Label { text: qsTr("Модель") }
                                ComboBox { id: modelPicker; model: ["small", "medium", "turbo"]; currentIndex: Math.max(0, ["small", "medium", "turbo"].indexOf(window.backend.modelSelection)); enabled: !window.backend.active; Accessible.name: qsTr("Модель распознавания") }
                                Label { text: qsTr("Результаты") }
                                TextField { id: outputPath; text: window.backend.outputDirectory; Layout.fillWidth: true; enabled: !window.backend.active; Accessible.name: qsTr("Каталог результатов") }
                                Button { text: "…"; enabled: !window.backend.active; Accessible.name: qsTr("Выбрать каталог результатов"); onClicked: outputDialog.open() }
                            }
                            CheckBox { id: advanced; text: qsTr("Расширенные настройки") }
                            GridLayout {
                                visible: advanced.checked
                                columns: 2
                                Layout.fillWidth: true
                                enabled: !window.backend.active
                                Label { text: qsTr("Потоки / части / работники") }
                                RowLayout {
                                    SpinBox { id: threadCount; from: 0; to: 256; value: 0; editable: true; Accessible.name: qsTr("Потоки; ноль означает автоматически") }
                                    SpinBox { id: chunkCount; from: 1; to: 256; value: 1; editable: true; Accessible.name: qsTr("Число частей") }
                                    SpinBox { id: jobCount; from: 0; to: chunkCount.value; value: 0; editable: true; Accessible.name: qsTr("Работники; ноль означает автоматически") }
                                }
                                Label { text: qsTr("0 — автоматически; одна часть по умолчанию"); opacity: 0.65; Layout.columnSpan: 2 }
                                Label { text: qsTr("Термины лекции") }
                                TextField { id: prompt; Layout.fillWidth: true; Accessible.name: qsTr("Подсказка с терминами") }
                                Label { text: qsTr("Другая модель (.bin)") }
                                TextField { id: customModel; Layout.fillWidth: true; placeholderText: qsTr("Необязательно: полный путь"); Accessible.name: qsTr("Путь к собственной модели") }
                                Label { text: qsTr("Файл cookies") }
                                TextField { id: cookies; Layout.fillWidth: true; placeholderText: qsTr("Файл Netscape; не сохраняется в настройках"); Accessible.name: qsTr("Путь к файлу cookies") }
                                Label { text: qsTr("Cookies из браузера") }
                                TextField { id: browser; Layout.fillWidth: true; placeholderText: "firefox / chromium:profile"; Accessible.name: qsTr("Браузер и профиль для cookies") }
                                Label { text: qsTr("Каталог кэша") }
                                TextField { id: cachePath; Layout.fillWidth: true; placeholderText: qsTr("По умолчанию"); Accessible.name: qsTr("Каталог медиакэша") }
                                Label { text: qsTr("Лимит кэша, ГиБ") }
                                SpinBox { id: cacheLimit; from: 1; to: 1048576; value: 10; editable: true; Accessible.name: qsTr("Лимит кэша в ГиБ") }
                                RowLayout { Layout.columnSpan: 2; CheckBox { id: vad; text: qsTr("Определять речь (VAD)"); checked: true } CheckBox { id: keepAudio; text: qsTr("Сохранить рабочее аудио") } }
                                RowLayout { Layout.columnSpan: 2; CheckBox { id: cache; text: qsTr("Использовать кэш"); checked: true } CheckBox { id: refreshCache; text: qsTr("Скачать заново"); enabled: cache.checked; onEnabledChanged: if (!enabled) checked = false } }
                            }
                            RowLayout {
                                Button { text: qsTr("Начать расшифровку"); highlighted: true; enabled: !window.backend.active && source.text.trim().length > 0; onClicked: window.runTask() }
                                Button { text: qsTr("Отменить"); visible: window.backend.busy; enabled: window.backend.canCancel; onClicked: window.backend.cancel() }
                                Item { Layout.fillWidth: true }
                                Label { text: qsTr("Ctrl+O · Ctrl+Enter"); opacity: 0.6 }
                            }
                        }
                    }
                    Frame {
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            Label { text: qsTr("Текущая задача · %1").arg(window.backend.status); font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: window.backend.stage; visible: window.backend.busy; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            ScrollView {
                                visible: window.backend.error.length > 0
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(120, taskMessage.implicitHeight)
                                TextArea { id: taskMessage; objectName: "taskError"; text: window.backend.error; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; Accessible.name: qsTr("Ошибка текущей задачи") }
                            }
                            Label { text: qsTr("Предупреждение задачи: %1").arg(window.backend.warning); visible: window.backend.warning.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                            ProgressBar { Layout.fillWidth: true; indeterminate: window.backend.progress < 0; value: window.backend.progress; visible: window.backend.busy; Accessible.name: qsTr("Прогресс распознавания") }
                            Label { text: window.backend.eta; visible: text.length > 0 && window.backend.busy }
                            RowLayout {
                                Button { objectName: "retryTask"; text: qsTr("Повторить задачу"); visible: window.backend.canRetry; onClicked: window.backend.retry() }
                                Button { text: qsTr("Каталог задачи"); enabled: window.backend.taskResultDirectory.length > 0; onClicked: window.backend.openTaskResult() }
                                Button { text: qsTr("Журналы задачи"); enabled: window.backend.taskResultDirectory.length > 0; onClicked: window.backend.openTaskResult("logs") }
                            }
                        }
                    }
                    Frame {
                        visible: window.backend.resultDirectory.length > 0
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            Label { objectName: "selectedResultStatus"; text: qsTr("Выбранный результат · %1").arg(window.backend.selectedStatus); font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: window.backend.resultDirectory; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; opacity: 0.7 }
                            Button { text: qsTr("Показать результат текущей задачи"); visible: window.backend.taskResultDirectory.length > 0 && window.backend.resultDirectory !== window.backend.taskResultDirectory; onClicked: window.backend.viewCurrentResult() }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Button { text: qsTr("Каталог"); onClicked: window.backend.openResult() }
                                Button { text: qsTr("Полный TXT"); onClicked: window.backend.openResult("txt") }
                                Button { text: "SRT"; onClicked: window.backend.openResult("srt") }
                                Button { text: "VTT"; onClicked: window.backend.openResult("vtt") }
                                Button { text: qsTr("Журналы"); onClicked: window.backend.openResult("logs") }
                            }
                            Label { text: window.backend.previewError; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                    Label { text: qsTr("Текст расшифровки"); font.bold: true }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 260
                        TextArea {
                            text: window.backend.transcript
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            placeholderText: qsTr("Текст появится во время распознавания. Полный результат сохраняется в TXT.")
                            Accessible.name: qsTr("Частичный текст расшифровки")
                        }
                    }
                }
            }
            ColumnLayout {
                RowLayout { Label { text: qsTr("Сохранённые результаты"); font.bold: true } Item { Layout.fillWidth: true } Button { text: qsTr("Обновить"); onClicked: window.backend.refreshHistory() } }
                Label { text: window.backend.historyError; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                ListView {
                    id: historyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: window.backend.history
                    spacing: 4
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: historyItem
                        required property string title
                        required property string status
                        required property string modelName
                        required property string directory
                        required property string date
                        required property bool available
                        width: historyList.width
                        enabled: available
                        text: title + "\n" + (available ? status : qsTr("Результат недоступен")) + " · " + modelName + " · " + date
                        contentItem: Label { text: historyItem.text; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
                        Accessible.name: text
                        onClicked: { window.backend.viewResult(directory); tabs.currentIndex = 0 }
                    }
                    Label { anchors.centerIn: parent; visible: historyList.count === 0; text: qsTr("Здесь появятся результаты из выбранных каталогов."); wrapMode: Text.WordWrap }
                }
            }
            ScrollView {
                id: modelScroll
                clip: true
                ColumnLayout {
                    width: modelScroll.availableWidth
                    spacing: 16
                    Label { text: qsTr("Модели для локального распознавания"); font.bold: true; font.pointSize: window.font.pointSize + 3 }
                    Label { text: qsTr("small — быстрее и легче; medium — текущая модель по умолчанию; turbo — альтернативная модель. Скорость и качество зависят от записи и компьютера."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: qsTr("Выберите модель на вкладке «Расшифровка». Загрузка также установит Silero VAD. Уже проверенные модели сохраняются."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout {
                        Button { text: qsTr("Скачать %1 и VAD").arg(modelPicker.currentText); enabled: !window.backend.active; onClicked: window.backend.downloadModel(modelPicker.currentText) }
                        Button { text: qsTr("Импорт %1…").arg(modelPicker.currentText); enabled: !window.backend.active; onClicked: { modelDialog.vadModel = false; modelDialog.open() } }
                        Button { text: qsTr("Импорт VAD…"); enabled: !window.backend.active; onClicked: { modelDialog.vadModel = true; modelDialog.open() } }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(160, modelMessage.implicitHeight)
                        TextArea { id: modelMessage; text: window.backend.modelStatus; readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; Accessible.name: qsTr("Состояние установки модели") }
                    }
                    Button { text: qsTr("Сохранить резервную копию и восстановить"); visible: window.backend.modelCanRecover; enabled: !window.backend.active; onClicked: window.backend.recoverModel() }
                    Label { text: qsTr("Резервная копия: %1").arg(window.backend.modelBackupPath); visible: window.backend.modelBackupPath.length > 0; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    ProgressBar { visible: window.backend.modelBusy; Layout.fillWidth: true; indeterminate: window.backend.modelProgress < 0; value: window.backend.modelProgress; Accessible.name: qsTr("Загрузка модели") }
                    Button { text: qsTr("Отменить операцию с моделью"); visible: window.backend.modelBusy; onClicked: window.backend.cancel() }
                    Label { text: qsTr("Каталог данных: %1").arg(window.backend.appHome); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; opacity: 0.7 }
                    Label { text: qsTr("Перед использованием файл проверяется по SHA-256. Для работы с локальными записями после установки моделей интернет не нужен."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
