pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Transcribe

            ScrollView {
                id: modelScroll
                required property Backend backend
                clip: true
    FileDialog {
        id: modelDialog
        property bool vadModel: false
        title: qsTr("Импорт модели из файла")
        nameFilters: [qsTr("Модель GGML (*.bin)")]
        onAccepted: { modelScroll.backend.importModel(modelScroll.backend.filePath(selectedFile), vadModel ? "vad" : modelScroll.backend.draft.model); modelScroll.forceActiveFocus() }
        onRejected: modelScroll.forceActiveFocus()
    }

                ColumnLayout {
                    width: modelScroll.availableWidth
                    spacing: 16
                    Label { text: qsTr("Модели для локального распознавания"); font.bold: true; font.pointSize: modelScroll.font.pointSize + 3 }
                    Label { text: qsTr("small — быстрее и легче; medium — текущая модель по умолчанию; turbo — альтернативная модель. Скорость и качество зависят от записи и компьютера."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: ["small", "medium", "turbo"]
                        delegate: UiCard {
                            id: modelCard
                            required property string modelData
                            Layout.fillWidth: true
                            RowLayout {
                                anchors.fill: parent
                                Label { text: modelCard.modelData; font.pixelSize: 22; Layout.fillWidth: true }
                                UiButton {
                                    text: modelScroll.backend.draft.model === modelCard.modelData ? qsTr("Выбрана") : qsTr("Выбрать")
                                    highlighted: modelScroll.backend.draft.model === modelCard.modelData
                                    enabled: !modelScroll.backend.active
                                    onClicked: modelScroll.backend.setDraftValue("model", modelCard.modelData)
                                }
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        UiButton { text: qsTr("Скачать %1 и VAD").arg(modelScroll.backend.draft.model); enabled: !modelScroll.backend.active; onClicked: modelScroll.backend.downloadModel(modelScroll.backend.draft.model) }
                        UiButton { text: qsTr("Импорт %1…").arg(modelScroll.backend.draft.model); enabled: !modelScroll.backend.active; onClicked: { modelDialog.vadModel = false; modelDialog.open() } }
                        UiButton { text: qsTr("Импорт VAD…"); enabled: !modelScroll.backend.active; onClicked: { modelDialog.vadModel = true; modelDialog.open() } }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(160, modelMessage.implicitHeight)
                        UiTextArea { id: modelMessage; text: modelScroll.backend.modelStatus; readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; Accessible.name: qsTr("Состояние установки модели") }
                    }
                    UiButton { text: qsTr("Сохранить резервную копию и восстановить"); visible: modelScroll.backend.modelCanRecover; enabled: !modelScroll.backend.active; onClicked: modelScroll.backend.recoverModel() }
                    Label { text: qsTr("Резервная копия: %1").arg(modelScroll.backend.modelBackupPath); visible: modelScroll.backend.modelBackupPath.length > 0; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    UiProgress { visible: modelScroll.backend.modelBusy; Layout.fillWidth: true; indeterminate: modelScroll.backend.modelProgress < 0; value: modelScroll.backend.modelProgress; Accessible.name: qsTr("Загрузка модели") }
                    UiButton { text: qsTr("Отменить операцию с моделью"); visible: modelScroll.backend.modelBusy; onClicked: modelScroll.backend.cancel() }
                    Label { text: qsTr("Каталог данных: %1").arg(modelScroll.backend.appHome); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; opacity: 0.7 }
                    Label { text: qsTr("Перед использованием файл проверяется по SHA-256. Для работы с локальными записями после установки моделей интернет не нужен."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
