import QtQuick
import QtQuick.Templates as T
import Transcribe

T.ProgressBar {
    id: control
    implicitWidth: 160; implicitHeight: 6
    background: Rectangle { radius: 3; color: UiStyle.colors.secondaryContainer }
    contentItem: Item {
        clip: true
        Rectangle {
            width: control.indeterminate ? parent.width / 3 : control.position * parent.width
            height: parent.height; radius: 3; color: UiStyle.colors.primary
            SequentialAnimation on x {
                loops: Animation.Infinite; running: control.visible && control.indeterminate && !UiStyle.reducedMotion
                NumberAnimation { from: 0; to: control.width * 2 / 3; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { from: control.width * 2 / 3; to: 0; duration: 700; easing.type: Easing.InOutSine }
            }
        }
    }
}
