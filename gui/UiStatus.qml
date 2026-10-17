import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Transcribe

Pane {
    id: control
    property string text: ""
    property bool failure: false
    property bool active: false
    readonly property color ink: failure ? UiStyle.colors.onErrorContainer : active ? UiStyle.colors.onPrimaryContainer : UiStyle.colors.onSurface
    padding: 12
    background: Rectangle { radius: 12; color: control.failure ? UiStyle.colors.errorContainer : control.active ? UiStyle.colors.primaryContainer : UiStyle.colors.surfaceContainerHigh }
    contentItem: RowLayout {
        spacing: 12
        Text { text: control.failure ? "!" : control.active ? "●" : "i"; color: control.ink; font.pixelSize: 18; font.bold: true; Accessible.ignored: true }
        Label { text: control.text; color: control.ink; wrapMode: Text.WordWrap; textFormat: Text.PlainText; Layout.fillWidth: true }
    }
}
