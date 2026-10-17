pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Transcribe

ScrollView {
    id: page
    required property Backend backend
    clip: true
    FileDialog {
        id: imageDialog
        title: qsTr("Картинка для палитры")
        nameFilters: [qsTr("Изображения (*.png *.jpg *.jpeg *.webp *.gif *.bmp)")]
        onAccepted: page.backend.theme.chooseImage(selectedFile)
    }
    ColumnLayout {
        width: page.availableWidth; spacing: 20
        UiCard {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent; spacing: 16
                Label { text: qsTr("Ваши цвета"); font.pixelSize: 24; font.weight: Font.DemiBold }
                Label { text: qsTr("Интерфейс подстраивается под ваш фон. Всё происходит локально."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    UiButton { text: qsTr("Текущие обои"); checked: page.backend.theme.source === "wallpaper"; onClicked: page.backend.theme.source = "wallpaper" }
                    UiButton { text: qsTr("Выбрать картинку…"); checked: page.backend.theme.source === "image"; onClicked: imageDialog.open() }
                    UiButton { text: qsTr("Повторить"); onClicked: page.backend.theme.refresh() }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 12
                    Repeater {
                        model: ["#6750a4", "#0061a4", "#386a20", "#984061"]
                        delegate: UiButton {
                            id: swatch
                            required property string modelData
                            width: 56; height: 56
                            text: page.backend.theme.source === "color" && page.backend.theme.customColor.toString() === modelData ? "✓" : ""
                            Accessible.name: qsTr("Цвет %1").arg(modelData)
                            background: Rectangle { radius: 28; color: swatch.modelData; border.width: swatch.visualFocus ? 3 : 1; border.color: UiStyle.colors.onSurface }
                            contentItem: Text { text: swatch.text; color: "white"; font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: page.backend.theme.customColor = modelData
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    UiField {
                        id: customColor
                        objectName: "customColorInput"
                        Layout.fillWidth: true
                        text: page.backend.theme.customColor.toString()
                        placeholderText: "#6750A4"
                        Accessible.name: qsTr("Собственный цвет HEX")
                        validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                        onAccepted: if (acceptableInput) page.backend.theme.customColor = text
                    }
                    UiButton { text: qsTr("Применить"); enabled: customColor.acceptableInput; onClicked: page.backend.theme.customColor = customColor.text }
                }
                Label { text: page.backend.theme.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                UiProgress { Layout.fillWidth: true; visible: page.backend.theme.analyzing; indeterminate: true }
            }
        }
        UiCard {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent; spacing: 16
                Label { text: qsTr("Оформление и язык"); font.pixelSize: 22 }
                Label { text: qsTr("Режим темы") }
                UiCombo {
                    objectName: "themeModePicker"
                    Layout.fillWidth: true
                    model: [qsTr("Системный"), qsTr("Светлый"), qsTr("Тёмный")]
                    currentIndex: Math.max(0, ["system", "light", "dark"].indexOf(page.backend.theme.mode))
                    onActivated: page.backend.theme.mode = ["system", "light", "dark"][currentIndex]
                    Accessible.name: qsTr("Режим темы")
                }
                UiSwitch { objectName: "reducedMotionSwitch"; Layout.fillWidth: true; text: qsTr("Уменьшить анимации"); checked: page.backend.theme.reducedMotion; onToggled: page.backend.theme.reducedMotion = checked }
                Label { text: qsTr("Язык интерфейса") }
                UiCombo {
                    id: languagePicker
                    objectName: "languagePicker"
                    Layout.fillWidth: true
                    model: ["Автоматически / Automatic", "Русский", "English"]
                    currentIndex: page.backend.language ? Math.max(0, ["auto", "ru", "en"].indexOf(page.backend.language.selection)) : 0
                    Accessible.name: qsTr("Язык интерфейса")
                    onActivated: if (page.backend.language) page.backend.language.selection = ["auto", "ru", "en"][currentIndex]
                }
            }
        }
    }
}
