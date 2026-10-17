import QtQuick
import QtQuick.Templates as T
import Transcribe

T.TextField {
    id: control
    implicitWidth: 180; implicitHeight: 48
    padding: 12; font.pixelSize: 14; selectByMouse: true
    color: UiStyle.colors.onSurface
    selectionColor: UiStyle.colors.primary
    selectedTextColor: UiStyle.colors.onPrimary
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: 12; color: UiStyle.colors.surfaceContainerLow
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? UiStyle.colors.primary : UiStyle.colors.outline
        opacity: control.enabled ? 1 : 0.45
        Text {
            anchors.fill: parent; anchors.margins: 12
            visible: control.text.length === 0 && control.preeditText.length === 0
            text: control.placeholderText; color: UiStyle.colors.onSurfaceVariant
            font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
    }
}
