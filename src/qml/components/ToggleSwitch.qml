import QtQuick
import QtQuick.Controls.Basic
import "../"

// On or off for one mapping or shortcut. The knob travels and the track fades
// into the accent, so the change is seen as well as read.
Switch {
    id: control

    indicator: Rectangle {
        implicitWidth: Theme.px(34)
        implicitHeight: Theme.px(18)
        x: control.leftPadding
        y: control.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.tinted(Theme.muted, 0.3)
        Behavior on color { ColorAnimation { duration: Theme.durationMedium } }

        // Keyboard focus, drawn just outside the track.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: control.visualFocus
        }

        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            y: 2
            width: parent.height - 4
            height: width
            radius: width / 2
            color: Theme.background
            Behavior on x {
                NumberAnimation { duration: Theme.durationMedium; easing.type: Easing.OutBack; easing.overshoot: 1.4 }
            }
        }
    }
}
