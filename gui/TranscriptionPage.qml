pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Transcribe

ScrollView {
    id: page
    required property Backend backend
    clip: true
    function chooseSource(): void { inputDialog.open() }
    function runTask(): void {
        if (backend.active || source.text.trim().length === 0) return
        backend.start(source.text.trim(), {
            model: customModel.text.trim() || modelPicker.currentText,
            output: outputPath.text.trim(),
            threads: threadCount.value, chunks: chunkCount.value, jobs: jobCount.value,
            vad: vad.checked, prompt: prompt.text, cookies: cookies.text.trim(), browser: browser.text.trim(),
            cache: cache.checked, refreshCache: refreshCache.checked, cacheDirectory: cachePath.text.trim(),
            cacheLimit: cacheLimit.value, keepAudio: keepAudio.checked
        })
    }
    FileDialog {
        id: inputDialog
        title: qsTr("Выберите видео или аудиофайл")
        fileMode: FileDialog.OpenFile
        onAccepted: { source.text = page.backend.filePath(selectedFile); source.forceActiveFocus() }
        onRejected: source.forceActiveFocus()
    }
    FolderDialog {
        id: outputDialog
        title: qsTr("Каталог результатов")
        onAccepted: { outputPath.text = page.backend.filePath(selectedFolder); outputPath.forceActiveFocus() }
        onRejected: outputPath.forceActiveFocus()
    }
    GridLayout {
        width: page.availableWidth
        columns: width >= 844 ? 2 : 1
        columnSpacing: 24; rowSpacing: 20
        ColumnLayout {
            Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 400
            spacing: 20
                    UiCard {
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 10
                            Label { text: qsTr("Видео, аудио или ссылка"); font.bold: true }
                            RowLayout {
                                Layout.fillWidth: true
                                UiField {
                                    id: source
                                    text: page.backend.draft.source
                                    onTextChanged: page.backend.setDraftValue("source", text)
                                    objectName: "sourceInput"
                                    placeholderText: qsTr("Вставьте URL или выберите файл")
                                    Layout.fillWidth: true
                                    enabled: !page.backend.active
                                    Accessible.name: qsTr("Файл или URL")
                                    onAccepted: page.runTask()
                                }
                                UiButton { text: qsTr("Выбрать файл…"); enabled: !page.backend.active; onClicked: inputDialog.open() }
                            }
                            GridLayout {
                                columns: 2
                                Layout.fillWidth: true
                                Label { text: qsTr("Модель") }
                                UiCombo { id: modelPicker; model: ["small", "medium", "turbo"]; currentIndex: Math.max(0, ["small", "medium", "turbo"].indexOf(page.backend.draft.model)); onActivated: page.backend.setDraftValue("model", currentText); enabled: !page.backend.active; Accessible.name: qsTr("Модель распознавания") }
                                Label { text: qsTr("Результаты"); Layout.columnSpan: 2 }
                                UiField { id: outputPath; text: page.backend.draft.output; onTextChanged: page.backend.setDraftValue("output", text); Layout.fillWidth: true; enabled: !page.backend.active; Accessible.name: qsTr("Каталог результатов") }
                                UiButton { text: "…"; enabled: !page.backend.active; Accessible.name: qsTr("Выбрать каталог результатов"); onClicked: outputDialog.open() }
                            }
                            UiCheck { id: advanced; text: qsTr("Расширенные настройки") }
                            GridLayout {
                                visible: advanced.checked
                                columns: 1
                                Layout.fillWidth: true
                                enabled: !page.backend.active
                                Label { text: qsTr("Потоки / части / работники") }
                                RowLayout {
                                    UiSpin { id: threadCount; onValueModified: page.backend.setDraftValue("threads", value); from: 0; to: 256; value: page.backend.draft.threads; editable: true; Accessible.name: qsTr("Потоки; ноль означает автоматически") }
                                    UiSpin { id: chunkCount; onValueModified: page.backend.setDraftValue("chunks", value); from: 1; to: 256; value: page.backend.draft.chunks; editable: true; Accessible.name: qsTr("Число частей") }
                                    UiSpin { id: jobCount; onValueModified: page.backend.setDraftValue("jobs", value); from: 0; to: chunkCount.value; value: page.backend.draft.jobs; editable: true; Accessible.name: qsTr("Работники; ноль означает автоматически") }
                                }
                                Label { text: qsTr("0 — автоматически; одна часть по умолчанию"); opacity: 0.65; Layout.columnSpan: 1 }
                                Label { text: qsTr("Термины лекции") }
                                UiField { id: prompt; text: page.backend.draft.prompt || ""; onTextChanged: page.backend.setDraftValue("prompt", text); Layout.fillWidth: true; Accessible.name: qsTr("Подсказка с терминами") }
                                Label { text: qsTr("Другая модель (.bin)") }
                                UiField { id: customModel; text: page.backend.draft.customModel || ""; onTextChanged: page.backend.setDraftValue("customModel", text); Layout.fillWidth: true; placeholderText: qsTr("Необязательно: полный путь"); Accessible.name: qsTr("Путь к собственной модели") }
                                Label { text: qsTr("Файл cookies") }
                                UiField { id: cookies; text: page.backend.draft.cookies || ""; onTextChanged: page.backend.setDraftValue("cookies", text); Layout.fillWidth: true; placeholderText: qsTr("Файл Netscape; не сохраняется в настройках"); Accessible.name: qsTr("Путь к файлу cookies") }
                                Label { text: qsTr("Cookies из браузера") }
                                UiField { id: browser; text: page.backend.draft.browser || ""; onTextChanged: page.backend.setDraftValue("browser", text); Layout.fillWidth: true; placeholderText: "firefox / chromium:profile"; Accessible.name: qsTr("Браузер и профиль для cookies") }
                                Label { text: qsTr("Каталог кэша") }
                                UiField { id: cachePath; text: page.backend.draft.cacheDirectory || ""; onTextChanged: page.backend.setDraftValue("cacheDirectory", text); Layout.fillWidth: true; placeholderText: qsTr("По умолчанию"); Accessible.name: qsTr("Каталог медиакэша") }
                                Label { text: qsTr("Лимит кэша, ГиБ") }
                                UiSpin { id: cacheLimit; onValueModified: page.backend.setDraftValue("cacheLimit", value); from: 1; to: 1048576; value: page.backend.draft.cacheLimit; editable: true; Accessible.name: qsTr("Лимит кэша в ГиБ") }
                                Flow { Layout.fillWidth: true; spacing: 8; UiCheck { id: vad; onToggled: page.backend.setDraftValue("vad", checked); text: qsTr("Определять речь (VAD)"); checked: page.backend.draft.vad } UiCheck { id: keepAudio; checked: page.backend.draft.keepAudio; onToggled: page.backend.setDraftValue("keepAudio", checked); text: qsTr("Сохранить рабочее аудио") } }
                                Flow { Layout.fillWidth: true; spacing: 8; UiCheck { id: cache; onToggled: page.backend.setDraftValue("cache", checked); text: qsTr("Использовать кэш"); checked: page.backend.draft.cache } UiCheck { id: refreshCache; checked: page.backend.draft.refreshCache && cache.checked; onToggled: page.backend.setDraftValue("refreshCache", checked); text: qsTr("Скачать заново"); enabled: cache.checked; onEnabledChanged: if (!enabled) page.backend.setDraftValue("refreshCache", false) } }
                            }
                            Flow {
                                Layout.fillWidth: true; spacing: 8
                                UiButton { text: qsTr("Начать расшифровку"); highlighted: true; enabled: !page.backend.active && source.text.trim().length > 0; onClicked: page.runTask() }
                                UiButton { text: qsTr("Отменить"); visible: page.backend.busy; enabled: page.backend.canCancel; onClicked: page.backend.cancel() }
                            }
                        }
                    }
                    UiCard {
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            UiStatus { text: qsTr("Текущая задача · %1").arg(page.backend.status); failure: page.backend.error.length > 0; active: page.backend.busy; Layout.fillWidth: true }
                            Label { text: page.backend.stage; visible: page.backend.busy; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            ScrollView {
                                visible: page.backend.error.length > 0
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(120, taskMessage.implicitHeight)
                                UiTextArea { id: taskMessage; objectName: "taskError"; text: page.backend.error; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; Accessible.name: qsTr("Ошибка текущей задачи") }
                            }
                            Label { text: qsTr("Предупреждение задачи: %1").arg(page.backend.warning); visible: page.backend.warning.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                            UiProgress { Layout.fillWidth: true; indeterminate: page.backend.progress < 0; value: page.backend.progress; visible: page.backend.busy; Accessible.name: qsTr("Прогресс распознавания") }
                            Label { text: page.backend.eta; visible: text.length > 0 && page.backend.busy }
                            Flow {
                                Layout.fillWidth: true; spacing: 8
                                UiButton { objectName: "retryTask"; text: qsTr("Повторить задачу"); visible: page.backend.canRetry; onClicked: page.backend.retry() }
                                UiButton { text: qsTr("Каталог задачи"); enabled: page.backend.taskResultDirectory.length > 0; onClicked: page.backend.openTaskResult() }
                                UiButton { text: qsTr("Журналы задачи"); enabled: page.backend.taskResultDirectory.length > 0; onClicked: page.backend.openTaskResult("logs") }
                            }
                        }
                    }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 420
            spacing: 16
                    UiCard {
                        visible: page.backend.resultDirectory.length > 0
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            Label { objectName: "selectedResultStatus"; text: qsTr("Выбранный результат · %1").arg(page.backend.selectedStatus); font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: page.backend.resultDirectory; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; opacity: 0.7 }
                            UiButton { text: qsTr("Показать результат текущей задачи"); visible: page.backend.taskResultDirectory.length > 0 && page.backend.resultDirectory !== page.backend.taskResultDirectory; onClicked: page.backend.viewCurrentResult() }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                UiButton { text: qsTr("Каталог"); onClicked: page.backend.openResult() }
                                UiButton { text: qsTr("Полный TXT"); onClicked: page.backend.openResult("txt") }
                                UiButton { text: "SRT"; onClicked: page.backend.openResult("srt") }
                                UiButton { text: "VTT"; onClicked: page.backend.openResult("vtt") }
                                UiButton { text: qsTr("Журналы"); onClicked: page.backend.openResult("logs") }
                            }
                            Label { text: page.backend.previewError; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                    Label { text: qsTr("Текст расшифровки"); font.bold: true }
                    Label { text: page.backend.transcriptNotice; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 260
                        UiTextArea {
                            text: page.backend.transcript
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
}
