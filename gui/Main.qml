pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Transcribe

ApplicationWindow {
    id: window
    required property Backend backend
    width: 1120; height: 780
    minimumWidth: 720; minimumHeight: 420
    visible: true
    title: qsTr("Transcribe")
    color: UiStyle.colors.surface
    font.pixelSize: 14
    property bool closeRequested: false
    readonly property bool expandedNavigation: width >= 1000
    Material.theme: backend.theme.dark ? Material.Dark : Material.Light
    Material.accent: UiStyle.colors.primary
    Material.primary: UiStyle.colors.primary
    Material.foreground: UiStyle.colors.onSurface
    Material.background: UiStyle.colors.surface
    palette.window: UiStyle.colors.surface
    palette.windowText: UiStyle.colors.onSurface
    palette.text: UiStyle.colors.onSurface
    palette.buttonText: UiStyle.colors.onSurface
    palette.base: UiStyle.colors.surfaceContainerLow
    palette.highlight: UiStyle.colors.primary
    palette.highlightedText: UiStyle.colors.onPrimary
    Component.onCompleted: UiStyle.controller = backend.theme
    onClosing: function(close) {
        if (backend.active) { close.accepted = false; closeDialog.open() }
    }
    Connections {
        target: window.backend
        function onChanged(): void { if (window.closeRequested && !window.backend.active) Qt.quit() }
    }
    Shortcut { sequence: "Ctrl+O"; enabled: !window.backend.active; onActivated: { navigation.currentIndex = 0; transcription.chooseSource() } }
    Shortcut { sequence: "Ctrl+Return"; enabled: !window.backend.active && window.backend.draft.source.trim().length > 0; onActivated: transcription.runTask() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: window.close() }
    Shortcut { sequence: "F5"; onActivated: window.backend.refreshHistory() }
    Dialog {
        id: closeDialog
        objectName: "closeConfirmation"
        width: Math.min(window.width - 32, 460)
        height: Math.min(implicitHeight, window.height - 32)
        title: qsTr("Операция ещё выполняется")
        modal: true; anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        enter: Transition {}
        exit: Transition {}
        background: Rectangle { radius: 28; color: UiStyle.colors.surfaceContainerHigh }
        contentItem: ScrollView {
            clip: true
            ColumnLayout {
                width: parent.width; spacing: 16
                Label { text: qsTr("Запросить отмену и закрыть окно после завершения операции? Частичные данные сохранятся; уже сохранённый результат останется готовым."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    UiButton { objectName: "stayButton"; text: qsTr("Остаться"); onClicked: closeDialog.close() }
                    UiButton { objectName: "cancelCloseButton"; text: qsTr("Отменить и закрыть"); onClicked: { window.closeRequested = true; window.backend.cancel(); closeDialog.close() } }
                }
            }
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.preferredWidth: window.expandedNavigation ? 216 : 80
            Layout.fillHeight: true; color: UiStyle.colors.surfaceContainerLow
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 12
                Image { source: "qrc:/icons/icon.png"; Layout.preferredWidth: 40; Layout.preferredHeight: 40; Layout.alignment: Qt.AlignHCenter; fillMode: Image.PreserveAspectFit; Accessible.ignored: true }
                Label { text: "Transcribe"; visible: window.expandedNavigation; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                Item {
                    id: navigation
                    objectName: "navigationTabs"
                    property int currentIndex: 0
                    Layout.fillWidth: true; implicitHeight: navigationColumn.implicitHeight
                    ColumnLayout {
                        id: navigationColumn
                        width: parent.width; spacing: 8
                        Repeater {
                            model: [qsTr("Расшифровка"), qsTr("История"), qsTr("Модели"), qsTr("Настройки")]
                            delegate: UiButton {
                                required property int index
                                required property string modelData
                                Layout.fillWidth: true; implicitHeight: 56
                                text: window.expandedNavigation ? modelData : ""
                                symbol: index
                                flat: true
                                checked: navigation.currentIndex === index
                                Accessible.name: modelData
                                ToolTip.visible: hovered && !window.expandedNavigation
                                ToolTip.text: modelData
                                onClicked: navigation.currentIndex = index
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Label { visible: window.expandedNavigation; text: qsTr("Локально · CPU"); color: UiStyle.colors.onSurfaceVariant; Layout.alignment: Qt.AlignHCenter }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.margins: 24; spacing: 16
            Label { text: [qsTr("Расшифровка"), qsTr("История"), qsTr("Модели"), qsTr("Настройки")][navigation.currentIndex]; font.pixelSize: 28; font.weight: Font.DemiBold }
            StackLayout {
                currentIndex: navigation.currentIndex
                Layout.fillWidth: true; Layout.fillHeight: true
                // Keep pages mounted so scroll and selection survive navigation.
                TranscriptionPage { id: transcription; backend: window.backend }
                HistoryPage { backend: window.backend; onOpenTranscript: navigation.currentIndex = 0 }
                ModelsPage { backend: window.backend }
                AppearancePage { backend: window.backend }
            }
            UiCard {
                visible: window.backend.active
                Layout.fillWidth: true; padding: 12
                RowLayout {
                    anchors.fill: parent; spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: window.backend.modelBusy ? window.backend.modelStatus : window.backend.stage || window.backend.status; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                        UiProgress { Layout.fillWidth: true; value: window.backend.modelBusy ? window.backend.modelProgress : window.backend.progress; indeterminate: value < 0 }
                    }
                    UiButton { text: qsTr("Отменить"); enabled: window.backend.modelBusy || window.backend.canCancel; onClicked: window.backend.cancel() }
                }
            }
        }
    }
}
