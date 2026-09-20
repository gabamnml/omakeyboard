import QtQuick
import "../"

// A key drawn as a key: a face sitting on a darker skirt, lit from above, the
// way the bevel in the wordmark is. Used everywhere a physical key or a
// combination is shown, so "Caps Lock" and "SUPER + E" read as keyboard
// things rather than as configuration strings.
//
// `accent` draws the key in the theme's accent colour, for what a key becomes
// rather than what it is. `pressed` sinks it into its skirt.
Row {
    id: root
    property string text: ""
    // Alternatively a list of labels, for sequences like H J K L that are
    // separate keys rather than one combination.
    property var keys: []
    property bool dimmed: false
    property bool accent: false
    property bool pressed: false
    property bool large: false
    spacing: Theme.sm

    readonly property var tokens: root.keys.length > 0
        ? root.keys
        : (root.text.length === 0
            ? []
            // "SUPER + SHIFT + E" becomes three caps; a lone "+" stays a key.
            : (root.text.trim() === "+"
                ? ["+"]
                : root.text.split("+").map(function (part) { return part.trim() })
                                     .filter(function (part) { return part.length > 0 })))
    // Hyprland spells punctuation by keysym name; a key shows the character.
    readonly property var symbolFor: ({
        grave: "`", comma: ",", period: ".", slash: "/", backslash: "\\",
        minus: "-", equal: "=", semicolon: ";", apostrophe: "'",
        bracketleft: "[", bracketright: "]"
    })
    function display(token) {
        const symbol = root.symbolFor[token.toLowerCase()]
        return symbol !== undefined ? symbol : token
    }

    readonly property bool combination: root.keys.length === 0 && root.tokens.length > 1

    readonly property int capHeight: Math.round(Theme.controlHeight * (root.large ? 0.95 : 0.78))
    readonly property int depth: Math.max(2, Math.round(root.capHeight * 0.12))

    // The face is opaque: it overlaps the skirt, and a translucent face lets
    // the skirt's colour through, which in a light theme put accent text on
    // an accent-coloured key.
    readonly property color faceColor: root.dimmed ? Theme.surface
        : root.accent ? Qt.tint(Theme.surface, Theme.accented(Theme.dark ? 0.22 : 0.14))
        : Qt.lighter(Theme.surface, Theme.dark ? 1.18 : 1.0)
    readonly property color skirtColor: root.dimmed ? Theme.tinted(Theme.border, 0.6)
        : root.accent ? Qt.tint(Theme.surface, Theme.accented(0.55))
        : Theme.border
    readonly property color labelColor: root.dimmed ? Theme.muted
        : root.accent ? Theme.accent
        : Theme.foreground

    Repeater {
        model: root.tokens
        delegate: Row {
            required property string modelData
            required property int index
            spacing: Theme.sm

            Text {
                visible: index > 0 && root.combination
                text: "+"
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                anchors.verticalCenter: parent.verticalCenter
            }

            // The key: the skirt is the full height, the face sits on it and
            // drops into it when pressed.
            Item {
                id: cap
                readonly property bool single: label.text.length <= 1
                implicitWidth: Math.max(cap.single ? root.capHeight : 0,
                                        label.implicitWidth + Theme.xl + Theme.xs)
                implicitHeight: root.capHeight + root.depth

                Rectangle {
                    id: skirt
                    anchors.fill: parent
                    anchors.topMargin: root.depth
                    radius: Math.max(Theme.radiusSmall, 2)
                    color: root.skirtColor
                    Behavior on color { ColorAnimation { duration: Theme.durationMedium } }
                }

                Rectangle {
                    id: face
                    width: parent.width
                    height: root.capHeight
                    y: root.pressed ? root.depth : 0
                    radius: Math.max(Theme.radiusSmall, 2)
                    color: root.faceColor
                    border.width: 1
                    border.color: root.dimmed ? Theme.tinted(Theme.muted, 0.35)
                                 : root.accent ? Theme.accented(0.7)
                                 : Theme.tinted(Theme.foreground, Theme.dark ? 0.16 : 0.22)
                    Behavior on y { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                    Behavior on color { ColorAnimation { duration: Theme.durationMedium } }

                    // Light from above: a hairline along the top edge.
                    Rectangle {
                        anchors.top: parent.top
                        anchors.topMargin: 1
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Math.max(parent.radius, 2)
                        anchors.rightMargin: Math.max(parent.radius, 2)
                        height: 1
                        color: Theme.tinted(Theme.foreground, root.dimmed ? 0.04 : 0.10)
                    }

                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: root.display(modelData)
                        color: root.labelColor
                        font.pixelSize: root.large ? Theme.fontSizeBody : Theme.fontSizeCaption
                        font.weight: root.accent ? Font.DemiBold : Font.Medium
                        font.family: Theme.monoFamily
                    }
                }
            }
        }
    }
}
