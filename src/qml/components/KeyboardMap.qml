import QtQuick
import QtQuick.Controls.Basic
import "../"

// The keyboard drawn as a map of what each key does. Only the block every
// keyboard from 60% up shares, so nothing is drawn that a keyboard might not
// have. Widths are in key units; the whole block is 15 units wide.
//
// `layerShown` switches to what the keys do while a layer key is held (1 or 2): that
// key sinks, the keys with a job light up, and the rest step back.
Item {
    id: root

    property var map: ({ rows: [] })
    property int layerShown: 0
    readonly property bool showLayer: root.layerShown > 0
    // The key that selects the layer being shown.
    readonly property string layerKeyCode: root.layerShown === 1 ? (root.map.layerKey || "")
                                         : root.layerShown === 2 ? (root.map.layerKey2 || "")
                                         : ""
    // Codes physically held right now, from the key tester.
    property var held: ({})
    // Clicking a key is how a mapping for it is made or edited.
    property bool interactive: true
    signal keyClicked(var entry)

    readonly property var rows: root.map.rows || []
    readonly property real unit: Math.floor(root.width / 15)
    readonly property real gap: Math.max(2, Math.round(root.unit * 0.09))
    readonly property real capHeight: root.unit - root.gap
    readonly property real depth: Math.max(2, Math.round(root.unit * 0.07))

    implicitHeight: root.rows.length * root.unit + root.depth

    Repeater {
        model: root.rows

        delegate: Item {
            id: rowItem
            required property var modelData
            required property int index

            y: rowItem.index * root.unit
            width: root.width
            height: root.unit

            Repeater {
                model: rowItem.modelData

                delegate: Item {
                    id: key
                    required property var modelData
                    required property int index

                    // Sum of the widths before this key in its row.
                    readonly property real offset: {
                        let total = 0
                        for (let i = 0; i < key.index; ++i)
                            total += rowItem.modelData[i].width
                        return total
                    }

                    readonly property var base: key.modelData.base || null
                    readonly property var layerRule: (root.layerShown === 2 ? key.modelData.layer2
                                                                       : key.modelData.layer) || null
                    readonly property bool isActiveLayerKey: key.modelData.code === root.layerKeyCode
                    readonly property bool isLayerKey: key.base !== null && key.base.kind === "layerKey"
                    readonly property bool baseMapped: key.base !== null && key.base.enabled
                    readonly property bool layerMapped: key.layerRule !== null && key.layerRule.enabled
                    readonly property bool disabledRule: (key.base !== null && !key.base.enabled)
                        || (root.showLayer && key.layerRule !== null && !key.layerRule.enabled)

                    // Lit: the key does something of yours in the view shown.
                    readonly property bool lit: root.showLayer
                        ? (key.layerMapped || key.isActiveLayerKey)
                        : key.baseMapped
                    readonly property bool held: root.held[key.modelData.code] === true
                    readonly property bool pressed: key.held
                        || key.isActiveLayerKey
                    readonly property bool faded: root.showLayer && !key.lit && !key.held

                    readonly property string mainLabel: {
                        if (root.showLayer && key.layerMapped)
                            return key.layerRule.label
                        if (!root.showLayer && key.baseMapped && key.base.kind === "remap")
                            return key.base.label
                        return key.modelData.label
                    }
                    // The key's own name, kept small in the corner once the
                    // big label says what it has become.
                    readonly property bool behaviour: !root.showLayer && key.base !== null
                        && (key.base.kind === "layerKey" || key.base.kind === "option")
                    readonly property bool showsOrigin: (key.behaviour || key.mainLabel !== key.modelData.label)
                        && key.modelData.label.length > 0

                    x: key.offset * root.unit
                    width: key.modelData.width * root.unit - root.gap
                    height: root.capHeight + root.depth
                    opacity: key.faded ? 0.28 : 1
                    Behavior on opacity { NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easing } }

                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: root.depth
                        radius: Math.max(Theme.radiusSmall, 3)
                        color: key.lit ? Qt.tint(Theme.surface, Theme.accented(0.6))
                                       : Qt.darker(Theme.surface, Theme.dark ? 1.25 : 1.12)
                        Behavior on color { ColorAnimation { duration: Theme.durationMedium } }
                    }

                    Rectangle {
                        id: face
                        width: parent.width
                        height: root.capHeight
                        y: key.pressed ? root.depth : 0
                        radius: Math.max(Theme.radiusSmall, 3)
                        color: key.pressed ? Qt.tint(Theme.surface, Theme.accented(Theme.dark ? 0.45 : 0.35))
                             : key.lit ? Qt.tint(Theme.surface, Theme.accented(keyHover.hovered && root.interactive
                                                                                ? (Theme.dark ? 0.3 : 0.2)
                                                                                : (Theme.dark ? 0.2 : 0.13)))
                             : keyHover.hovered && root.interactive
                               ? Qt.tint(Qt.lighter(Theme.surface, Theme.dark ? 1.12 : 1.0), Theme.accented(0.08))
                             : Qt.lighter(Theme.surface, Theme.dark ? 1.12 : 1.0)
                        border.width: key.held ? 2 : 1
                        border.color: key.held ? Theme.accent
                                    : keyHover.hovered && root.interactive ? Theme.accented(0.9)
                                    : key.lit ? Theme.accented(0.75)
                                    : key.disabledRule ? Theme.tinted(Theme.warning, 0.5)
                                    : Theme.tinted(Theme.foreground, Theme.dark ? 0.12 : 0.18)
                        Behavior on y { NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easing } }
                        Behavior on color { ColorAnimation { duration: Theme.durationMedium } }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.topMargin: 1
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 3
                            anchors.rightMargin: 3
                            height: 1
                            color: Theme.tinted(Theme.foreground, 0.08)
                        }

                        Text {
                            visible: key.showsOrigin
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.leftMargin: Math.round(root.unit * 0.1)
                            anchors.topMargin: Math.round(root.unit * 0.06)
                            width: parent.width - Math.round(root.unit * 0.14)
                            elide: Text.ElideRight
                            text: key.modelData.label
                            color: key.lit ? Theme.accented(0.75) : Theme.muted
                            font.pixelSize: Math.max(7, Math.round(root.unit * 0.19))
                        }

                        // A behaviour rather than a key: its icon takes the
                        // centre, and the key's own name moves to the corner.
                        Icon {
                            visible: key.behaviour
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: Math.round(root.unit * 0.08)
                            name: key.isLayerKey ? "layers" : "recipe"
                            font.pixelSize: Math.round(root.unit * 0.36)
                            color: key.lit ? Theme.accent : Theme.muted
                        }

                        Text {
                            visible: !key.behaviour
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: key.showsOrigin ? Math.round(root.unit * 0.08) : 0
                            width: parent.width - Math.round(root.unit * 0.14)
                            height: root.capHeight * 0.6
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            // Shrinks a long word to fit its key instead of
                            // cutting it to "Sup…".
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 7
                            text: key.mainLabel
                            color: key.lit ? Theme.accent : Theme.foreground
                            font.pixelSize: Math.max(8, Math.round(root.unit * (key.mainLabel.length <= 2 ? 0.34 : 0.26)))
                            font.weight: key.lit ? Font.DemiBold : Font.Normal
                        }
                    }

                    HoverHandler {
                        id: keyHover
                        cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                    TapHandler {
                        enabled: root.interactive
                        onTapped: root.keyClicked(key.modelData)
                    }
                    ToolTip.visible: keyHover.hovered && tip.length > 0
                    ToolTip.delay: 350
                    ToolTip.text: tip
                    readonly property string tip: {
                        const name = key.modelData.name || key.modelData.label || key.modelData.code
                        if (root.showLayer && key.layerRule)
                            return name + " → " + key.layerRule.label + (key.layerRule.enabled ? "" : qsTr(" (off)"))
                        if (root.showLayer && !key.isLayerKey)
                            return root.interactive ? qsTr("%1 · click to give it a job on the layer").arg(name) : ""
                        if (!key.base)
                            return root.interactive ? qsTr("%1 · click to remap").arg(name) : ""
                        if (key.base.kind === "layerKey")
                            return qsTr("%1 is your layer key").arg(name)
                        if (key.base.kind === "option")
                            return key.base.detail
                        return name + " → " + key.base.label + (key.base.enabled ? "" : qsTr(" (off)"))
                    }
                }
            }
        }
    }
}
