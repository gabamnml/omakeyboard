import QtQuick
import QtQuick.Layouts
import "../"

// One ready-made setup, shown as what you would press and what you would get.
// The whole card is the button: the keys are the explanation.
Rectangle {
    id: root

    property var recipe: ({})
    signal chosen(string recipeId)

    readonly property bool available: root.recipe.available === true

    implicitHeight: layout.implicitHeight + Theme.panelPadding * 2
    radius: Theme.radius
    color: hover.hovered && root.available ? Theme.accented(0.06) : Theme.surface
    border.width: 1
    border.color: hover.hovered && root.available ? Theme.accented(0.8) : Theme.border
    opacity: root.available ? 1 : 0.45
    scale: tap.pressed && root.available ? 0.985 : 1

    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }

    HoverHandler { id: hover; cursorShape: root.available ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler {
        id: tap
        enabled: root.available
        onTapped: root.chosen(root.recipe.id)
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.panelPadding
        spacing: Theme.lg

        // What you press, then what you get. A Flow rather than a row: a
        // layer recipe is nine keys long, and in a narrow card the result
        // moves to a second line instead of spilling out of the card.
        Flow {
            id: keysFlow
            Layout.fillWidth: true
            spacing: Theme.md

            // A layer recipe reads "Caps Lock + H J K L": the held key, then
            // the keys it changes. A shortcut is one combination.
            Row {
                id: pressed
                spacing: Theme.md
                readonly property var from: root.recipe.from || []

                KeyCap {
                    visible: (root.recipe.combo || "").length > 0
                    text: root.recipe.combo || ""
                }

                KeyCap {
                    visible: pressed.from.length > 0
                    keys: pressed.from.slice(0, 1)
                }

                Text {
                    visible: pressed.from.length > 1
                    anchors.verticalCenter: parent.verticalCenter
                    text: "+"
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                }

                KeyCap {
                    visible: pressed.from.length > 1
                    keys: pressed.from.slice(1)
                }
            }

            Row {
                spacing: Theme.md
                height: pressed.height

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "arrow"
                    size: 1.0
                    color: hover.hovered && root.available ? Theme.accent : Theme.muted
                }

                KeyCap {
                    visible: (root.recipe.to || []).length > 0
                    keys: root.recipe.to || []
                    accent: true
                }

                // A shortcut ends in an action rather than a key.
                Text {
                    visible: (root.recipe.outcome || "").length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, keysFlow.width - Theme.xxl * 2)
                    text: root.recipe.outcome || ""
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.xxs

            Text {
                Layout.fillWidth: true
                text: root.recipe.title || ""
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: root.available ? (root.recipe.detail || "")
                                     : (root.recipe.reason || qsTr("This keyboard already uses one of these keys."))
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        // Cards in a row share its height; the spare room goes below the
        // text, so every card reads from the top.
        Item { Layout.fillHeight: true }
    }
}
