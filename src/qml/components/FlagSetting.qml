import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../"

// Three states, not two: on, off, and "whatever Omarchy decided".
ColumnLayout {
    id: root

    property string label: ""
    property string hint: ""
    property string state: "inherit"   // "inherit" | "on" | "off"
    property string inheritedLabel: ""
    property bool settable: true
    property string unavailableReason: ""

    signal changed(string state)

    spacing: Theme.space(0.75)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(1)

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: root.label
                color: root.settable ? Theme.foreground : Theme.muted
                font.pixelSize: Theme.fontSizeBody
            }

            Text {
                Layout.fillWidth: true
                text: !root.settable
                    ? root.unavailableReason
                    : (root.state === "inherit"
                        ? (root.inheritedLabel.length > 0
                            ? qsTr("Following Omarchy: %1").arg(root.inheritedLabel)
                            : qsTr("Following Omarchy"))
                        : root.hint)
                color: root.settable ? Theme.muted : Theme.warning
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        Row {
            spacing: 0
            enabled: root.settable
            opacity: root.settable ? 1.0 : 0.5

            Repeater {
                model: [
                    { key: "inherit", label: qsTr("Auto") },
                    { key: "on", label: qsTr("On") },
                    { key: "off", label: qsTr("Off") },
                ]

                delegate: Rectangle {
                    id: segment
                    required property var modelData
                    required property int index
                    readonly property bool selected: modelData.key === root.state

                    implicitWidth: Theme.space(8)
                    implicitHeight: Theme.space(4.5)
                    color: segment.selected ? Theme.accented(0.18) : Theme.elevated
                    border.width: 1
                    border.color: segment.selected ? Theme.accent : Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: segment.modelData.label
                        color: segment.selected ? Theme.accent : Theme.foreground
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: segment.selected ? Font.DemiBold : Font.Normal
                    }

                    TapHandler {
                        onTapped: root.changed(segment.modelData.key)
                    }
                }
            }
        }
    }
}
