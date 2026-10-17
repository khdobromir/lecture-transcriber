pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Transcribe

T.ComboBox {
    id: control
    implicitWidth: 180; implicitHeight: 48
    leftPadding: 14; rightPadding: 36; font.pixelSize: 14
    contentItem: Text {
        text: control.displayText; color: UiStyle.colors.onSurface; font: control.font
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: Text { x: control.width - 28; y: (control.height - height) / 2; text: "⌄"; color: UiStyle.colors.onSurface; font.pixelSize: 20 }
    background: Rectangle {
        color: UiStyle.colors.surfaceContainerLow; radius: 12
        border.width: control.visualFocus ? 2 : 1; border.color: control.visualFocus ? UiStyle.colors.primary : UiStyle.colors.outline
        opacity: control.enabled ? 1 : 0.45
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        width: control.width; height: 48
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        contentItem: Text { text: option.text; color: UiStyle.colors.onSurface; font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { radius: 8; color: option.highlighted ? UiStyle.colors.secondaryContainer : UiStyle.colors.surfaceContainerHigh }
    }
    popup: Popup {
        y: control.height + 4; width: control.width; padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 240)
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.delegateModel; currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { color: UiStyle.colors.surfaceContainerHigh; radius: 16; border.color: UiStyle.colors.outlineVariant }
        enter: Transition {}
        exit: Transition {}
    }
}
