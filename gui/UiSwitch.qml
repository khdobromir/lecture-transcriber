import QtQuick
import QtQuick.Templates as T
import Transcribe

T.Switch {
    id: control
    implicitWidth: implicitContentWidth + 72; implicitHeight: 48
    leftPadding: 68; font.pixelSize: 14; hoverEnabled: true
    indicator: Rectangle {
        width: 52; height: 32; x: 4; y: (control.height - height) / 2; radius: 16
        color: control.checked ? UiStyle.colors.primary : UiStyle.colors.surfaceContainerHigh
        border.width: control.visualFocus ? 3 : 1; border.color: control.checked ? UiStyle.colors.primary : UiStyle.colors.outline
        Rectangle {
            width: 24; height: 24; radius: 12; y: 4; x: control.checked ? 24 : 4
            color: control.checked ? UiStyle.colors.onPrimary : UiStyle.colors.outline
            Behavior on x { NumberAnimation { duration: UiStyle.duration } }
        }
    }
    contentItem: Text { text: control.text; font: control.font; color: UiStyle.colors.onSurface; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
}
