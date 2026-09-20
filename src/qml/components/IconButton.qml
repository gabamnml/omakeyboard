import QtQuick
import QtQuick.Controls.Basic
import "../"

// A square button carrying one glyph, for the actions every row repeats.
// Quiet until hovered, so a list of twenty mappings is not twenty rows of
// buttons.
AbstractButton {
    id: control

    property string glyph: ""
    property bool danger: false
    property string tip: ""

    readonly property color tone: control.danger ? Theme.error : Theme.foreground

    implicitWidth: Math.round(Theme.controlHeight * 0.9)
    implicitHeight: Math.round(Theme.controlHeight * 0.9)
    hoverEnabled: true

    ToolTip.visible: control.tip.length > 0 && control.hovered
    ToolTip.delay: 600
    ToolTip.text: control.tip

    contentItem: Icon {
        name: control.glyph
        size: 1.05
        color: !control.enabled ? Theme.tinted(Theme.muted, 0.5)
             : control.hovered ? control.tone
             : Theme.muted
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down ? Theme.tinted(control.tone, 0.20)
             : control.hovered ? Theme.tinted(control.tone, 0.10)
             : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
}
