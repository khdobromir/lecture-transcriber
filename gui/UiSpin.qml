import QtQuick
import QtQuick.Templates as T
import Transcribe

T.SpinBox {
    id: control
    implicitWidth: 142; implicitHeight: 44
    leftPadding: 38; rightPadding: 38; editable: true
    font.pixelSize: 14
    contentItem: TextInput {
        text: control.displayText; color: UiStyle.colors.onSurface; font: control.font
        selectionColor: UiStyle.colors.primary; selectedTextColor: UiStyle.colors.onPrimary
        horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable; validator: control.validator; inputMethodHints: Qt.ImhDigitsOnly
    }
    up.indicator: Rectangle {
        x: control.width - 36; height: control.height; width: 36; radius: 12
        color: control.up.pressed ? UiStyle.colors.secondaryContainer : "transparent"
        Text { anchors.centerIn: parent; text: "+"; color: UiStyle.colors.onSurface; font.pixelSize: 20 }
    }
    down.indicator: Rectangle {
        height: control.height; width: 36; radius: 12
        color: control.down.pressed ? UiStyle.colors.secondaryContainer : "transparent"
        Text { anchors.centerIn: parent; text: "−"; color: UiStyle.colors.onSurface; font.pixelSize: 20 }
    }
    background: Rectangle { radius: 12; color: UiStyle.colors.surfaceContainerLow; border.width: control.activeFocus ? 2 : 1; border.color: control.activeFocus ? UiStyle.colors.primary : UiStyle.colors.outline }
}
