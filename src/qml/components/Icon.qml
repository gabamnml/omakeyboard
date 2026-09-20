import QtQuick
import "../"

// A glyph from the Nerd Font. Sized against the text beside it, not in pixels
// of its own, so it scales with the theme's type.
Text {
    id: root
    property string name: ""
    property real size: 1.15

    text: Theme.icon[root.name] || ""
    color: Theme.foreground
    font.pixelSize: Math.round(Theme.fontSizeBody * root.size)
    font.family: Theme.monoFamily
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    Behavior on color { ColorAnimation { duration: Theme.durationMedium } }
}
