pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Transcribe

            ColumnLayout {
                id: page
                required property Backend backend
                signal openTranscript()
                RowLayout { Label { text: qsTr("Сохранённые результаты"); font.bold: true } Item { Layout.fillWidth: true } UiButton { text: qsTr("Обновить"); onClicked: page.backend.refreshHistory() } }
                Label { text: page.backend.historyError; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                ListView {
                    id: historyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: page.backend.history
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
                        background: Rectangle { radius: 16; color: historyItem.highlighted || historyItem.hovered ? UiStyle.colors.secondaryContainer : UiStyle.colors.surfaceContainerLow; border.width: historyItem.visualFocus ? 2 : 0; border.color: UiStyle.colors.primary }
                        Accessible.name: text
                        onClicked: { page.backend.viewResult(directory); page.openTranscript() }
                    }
                    Label { anchors.centerIn: parent; visible: historyList.count === 0; text: qsTr("Здесь появятся результаты из выбранных каталогов."); wrapMode: Text.WordWrap }
                }
            }
