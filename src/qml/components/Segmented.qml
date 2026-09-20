import QtQuick
import QtQuick.Layouts
import "../"

// Two or three mutually exclusive choices, with one highlight that slides to
// the chosen one.
Rectangle {
    id: root

    property var options: []          // labels
    // Owned by the caller: tapping only reports, so a binding stays a binding.
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: Math.round(Theme.controlHeight * 0.86)
    radius: Theme.radiusSmall
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Rectangle {
        readonly property Item target: repeater.count > root.currentIndex
            ? repeater.itemAt(root.currentIndex) : null
        x: target ? row.x + target.x : 0
        y: 2
        width: target ? target.width : 0
        height: parent.height - 4
        radius: Math.max(0, Theme.radiusSmall - 1)
        color: Theme.accented(0.18)
        border.width: 1
        border.color: Theme.accented(0.6)
        Behavior on x { NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easing } }
        Behavior on width { NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easing } }
    }

    Row {
        id: row
        x: 2
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            id: repeater
            model: root.options

            delegate: Item {
                required property string modelData
                required property int index
                width: label.implicitWidth + Theme.xl * 2
                height: root.height - 4

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: index === root.currentIndex ? Theme.accent : Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: index === root.currentIndex ? Font.DemiBold : Font.Normal
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }

                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: root.activated(index)
                }
            }
        }
    }
}
