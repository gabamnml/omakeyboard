import QtQuick
import QtQuick.Controls.Basic
import "../"

// Buttons come in three weights: primary for the one action a screen is about,
// plain for everything else, danger for the destructive ones.
Button {
    id: control

    property string weight: "plain"   // "primary" | "plain" | "danger"
    property bool compact: false

    implicitHeight: control.compact
        ? Math.round(Theme.controlHeight * 0.86)
        : Theme.controlHeight
    leftPadding: Theme.controlPaddingX
    rightPadding: Theme.controlPaddingX

    readonly property color tone: weight === "primary" ? Theme.accent
                                : weight === "danger" ? Theme.error
                                : Theme.foreground

    contentItem: Text {
        text: control.text
        font.pixelSize: control.compact ? Theme.fontSizeSmall : Theme.fontSizeBody
        font.weight: control.weight === "primary" ? Font.DemiBold : Font.Normal
        color: !control.enabled ? Theme.muted
             : control.weight === "primary" ? Theme.background
             : control.tone
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    scale: control.down ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: {
            if (!control.enabled)
                return Theme.tinted(Theme.muted, 0.12)
            if (control.weight === "primary")
                return control.down ? Qt.darker(Theme.accent, 1.2) : Theme.accent
            if (control.hovered || control.down)
                return Theme.tinted(control.tone, control.down ? 0.22 : 0.12)
            return "transparent"
        }
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        border.width: control.visualFocus ? 2 : (control.weight === "primary" ? 0 : 1)
        border.color: control.visualFocus ? (control.weight === "primary" ? Theme.foreground : Theme.accent)
                    : control.enabled ? Theme.border : Theme.tinted(Theme.muted, 0.3)
    }
}
