import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../"

// A number that can also be left alone. "Inherit" is a real state, not zero:
// Omarchy already sets these, and overriding one with a guessed value would be
// worse than not touching it.
ColumnLayout {
    id: root

    property string label: ""
    property string hint: ""
    property int value: -1          // -1 means inherited
    property int inheritedValue: -1 // what the system does right now
    property int minimum: 1
    property int maximum: 255
    property string unit: ""

    signal changed(int value)

    readonly property bool overridden: root.value >= 0

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
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeBody
            }

            Text {
                Layout.fillWidth: true
                text: root.overridden
                    ? root.hint
                    : (root.inheritedValue >= 0
                        ? qsTr("Following Omarchy: %1%2").arg(root.inheritedValue).arg(root.unit)
                        : qsTr("Following Omarchy"))
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        SpinBox {
            id: spin
            from: root.minimum
            to: root.maximum
            editable: true
            enabled: root.overridden
            value: root.overridden ? root.value : (root.inheritedValue >= 0
                                                   ? root.inheritedValue : root.minimum)
            onValueModified: root.changed(value)

            implicitWidth: Theme.space(13)
            implicitHeight: Theme.space(4.5)

            contentItem: TextInput {
                text: spin.textFromValue(spin.value, spin.locale)
                color: spin.enabled ? Theme.foreground : Theme.muted
                font.pixelSize: Theme.fontSizeBody
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                readOnly: !spin.editable || !spin.enabled
                validator: spin.validator
                selectByMouse: true
                selectionColor: Theme.accented(0.35)
                onTextEdited: {
                    const parsed = parseInt(text)
                    if (!isNaN(parsed) && parsed >= root.minimum && parsed <= root.maximum)
                        root.changed(parsed)
                }
            }

            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.elevated
                border.width: 1
                border.color: spin.enabled ? Theme.border : Theme.tinted(Theme.muted, 0.25)
            }

            up.indicator: Rectangle {
                x: spin.width - width
                height: spin.height / 2
                implicitWidth: Theme.space(3)
                color: spin.up.hovered ? Theme.accented(0.15) : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: spin.enabled ? Theme.foreground : Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            down.indicator: Rectangle {
                x: spin.width - width
                y: spin.height / 2
                height: spin.height / 2
                implicitWidth: Theme.space(3)
                color: spin.down.hovered ? Theme.accented(0.15) : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "−"
                    color: spin.enabled ? Theme.foreground : Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        AppButton {
            text: root.overridden ? qsTr("Reset") : qsTr("Override")
            compact: true
            onClicked: {
                if (root.overridden) {
                    root.changed(-1)
                } else {
                    root.changed(root.inheritedValue >= 0 ? root.inheritedValue : root.minimum)
                }
            }
        }
    }
}
