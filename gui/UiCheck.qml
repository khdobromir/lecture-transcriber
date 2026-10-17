import QtQuick
import QtQuick.Templates as T
import Transcribe

T.CheckBox {
    id: control
    implicitWidth: implicitContentWidth + 44; implicitHeight: 44
    leftPadding: 40; rightPadding: 4; hoverEnabled: true
    font.pixelSize: 14
    indicator: Rectangle {
        x: 8; y: (control.height - height) / 2; width: 22; height: 22; radius: 6
        color: control.checked ? UiStyle.colors.primary : "transparent"
        border.width: control.visualFocus ? 3 : 2; border.color: control.visualFocus || control.checked ? UiStyle.colors.primary : UiStyle.colors.outline
        opacity: control.enabled ? 1 : 0.45
        Text { anchors.centerIn: parent; text: "✓"; color: UiStyle.colors.onPrimary; visible: control.checked; font.pixelSize: 16 }
    }
    contentItem: Text {
        text: control.text; font: control.font; color: UiStyle.colors.onSurface
        opacity: control.enabled ? 1 : 0.45; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
    }
    background: Rectangle { radius: 12; color: UiStyle.colors.secondaryContainer; opacity: control.down ? 0.25 : control.hovered ? 0.12 : 0 }
}
