pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Transcribe

ApplicationWindow {
    id: gallery
    required property Backend backend
    width: 720; height: 540; visible: true
    color: UiStyle.colors.surface
    Component.onCompleted: UiStyle.controller = backend.theme
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        RowLayout {
            UiButton { objectName: "probeButton"; text: "Material 3"; highlighted: true }
            UiButton { text: "Disabled"; enabled: false }
            UiCheck { objectName: "probeCheck"; text: "Checkbox"; checked: true }
        }
        UiField { objectName: "probeField"; text: "Selected text"; Layout.fillWidth: true }
        UiSwitch { objectName: "probeSwitch"; text: "Switch"; checked: true }
        UiCombo { model: ["First", "Second"] }
        UiSpin { from: 0; to: 256; value: 4 }
        UiTextArea { text: "Material surface"; Layout.fillWidth: true }
        UiButton { text: "List item"; Layout.fillWidth: true; checked: true }
        UiProgress { Layout.fillWidth: true; value: 0.65 }
        Item { Layout.fillHeight: true }
    }
}
