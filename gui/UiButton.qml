import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import Transcribe

T.Button {
    id: control
    property int symbol: -1
    implicitWidth: Math.max(48, contentItem.implicitWidth + 32)
    implicitHeight: 44
    padding: 12
    font.pixelSize: 14
    font.weight: Font.Medium
    hoverEnabled: true
    readonly property color ink: highlighted ? UiStyle.colors.onPrimary : UiStyle.colors.onSurface
    contentItem: RowLayout {
        spacing: control.text.length > 0 && control.symbol >= 0 ? 12 : 0
        Item { Layout.fillWidth: true }
        UiIcon { visible: control.symbol >= 0; symbol: control.symbol; ink: control.ink; opacity: control.enabled ? 1 : 0.45; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
        Text {
            text: control.text; font: control.font; color: control.ink
            visible: control.text.length > 0; opacity: control.enabled ? 1 : 0.45
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight; textFormat: Text.PlainText
        }
        Item { Layout.fillWidth: true }
    }
    background: Rectangle {
        radius: height / 2
        color: control.highlighted ? UiStyle.colors.primary : control.checked ? UiStyle.colors.secondaryContainer : control.flat ? "transparent" : UiStyle.colors.surfaceContainerHigh
        opacity: control.enabled ? 1 : 0.45
        border.width: control.visualFocus ? 2 : 0
        border.color: control.highlighted ? UiStyle.colors.onPrimary : UiStyle.colors.primary
        Rectangle {
            anchors.fill: parent; radius: parent.radius; color: control.ink
            opacity: control.down ? 0.16 : control.hovered ? 0.08 : 0
            Behavior on opacity { NumberAnimation { duration: UiStyle.duration } }
        }
    }
}
