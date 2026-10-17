import QtQuick
import QtQuick.Templates as T
import Transcribe

T.TextArea {
    id: control
    implicitWidth: 200; implicitHeight: Math.max(96, contentHeight + 32)
    padding: 16; font.pixelSize: 14
    color: UiStyle.colors.onSurface; selectionColor: UiStyle.colors.primary; selectedTextColor: UiStyle.colors.onPrimary
    background: Rectangle {
        radius: 16; color: UiStyle.colors.surfaceContainerLow
        border.width: control.activeFocus ? 2 : 0; border.color: UiStyle.colors.primary
        Text {
            x: 16; y: 16; width: parent.width - 32
            visible: control.text.length === 0
            text: control.placeholderText; color: UiStyle.colors.onSurfaceVariant
            font: control.font; wrapMode: Text.WordWrap
        }
    }
}
