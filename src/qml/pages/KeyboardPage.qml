import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."
import "../components"

// The centre of the app: which keyboard, and what its keys do.
Item {
    id: page

    property string activeProfile: config.activeProfileId

    // Re-read whenever the mappings move, since a recipe stops being on offer
    // once its keys are taken.
    property var recipes: []

    // List or keyboard, and in the keyboard, base or layer.
    property bool mapView: false
    // 0 for the keys on their own, 1 or 2 for a layer.
    property int layerView: 0
    // Layers that exist on this keyboard, as [number, key label] pairs.
    readonly property var layers: {
        const list = []
        if (page.keyMap.layerKey)
            list.push({ layer: 1, key: page.keyMap.layerKey, label: page.keyMap.layerKeyLabel })
        if (page.keyMap.layerKey2)
            list.push({ layer: 2, key: page.keyMap.layerKey2, label: page.keyMap.layerKey2Label })
        return list
    }
    // Holding a layer key while testing shows that layer, whatever the
    // selector says; letting go puts the view back.
    readonly property int shownLayer: {
        for (let i = 0; i < page.layers.length; ++i)
            if (tester.held[page.layers[i].key] === true)
                return page.layers[i].layer
        return page.layerView
    }

    // Named entry points, so Main can open these for a screenshot without
    // reaching into the page's children.
    function newMapping() { mappingEditor.openForNew() }
    function showMap(layer) { page.mapView = true; page.layerView = layer }
    function startTester() { page.mapView = true; tester.arm() }
    function showRecipes() { page.refreshRecipes(); recipeDialog.open() }

    // A click on the map: edit what the key already does in the view shown,
    // or start a new mapping with the key filled in.
    function editKey(entry) {
        const onLayer = page.shownLayer
        const layerRule = onLayer === 2 ? entry.layer2 : onLayer === 1 ? entry.layer : null
        const isLayerKey = entry.code === page.keyMap.layerKey || entry.code === page.keyMap.layerKey2
        let id = ""
        if (layerRule)
            id = layerRule.id
        else if (entry.base && (onLayer === 0 || isLayerKey))
            id = entry.base.id
        if (id.length > 0) {
            const rows = config.mappingList(page.activeProfile)
            for (let i = 0; i < rows.length; ++i) {
                const row = rows[i]
                if (row.id !== id)
                    continue
                mappingEditor.openForExisting({
                    mappingId: row.id, kind: row.kind, sourceKey: row.sourceKey,
                    sourceLabel: row.sourceLabel, targetLabel: row.targetLabel,
                    levels: row.levels, xkbOption: row.xkbOption, note: row.note,
                    layer: row.layer,
                })
                return
            }
        }
        mappingEditor.openForKey(entry.code, entry.name || entry.label, onLayer)
    }
    property var keyMap: ({ rows: [] })
    function refreshMap() {
        page.keyMap = config.keyboardMap(page.activeProfile)
        if ((page.layerView === 1 && !page.keyMap.layerKey)
                || (page.layerView === 2 && !page.keyMap.layerKey2))
            page.layerView = 0
    }
    Connections {
        target: config
        function onMappingsChanged() { page.refreshMap() }
        function onStateReloaded() { page.refreshMap() }
    }

    Shortcut {
        sequences: ["Ctrl+K"]
        enabled: page.visible
        onActivated: page.mapView = !page.mapView
    }
    Shortcut {
        sequences: ["Ctrl+L"]
        enabled: page.visible && page.mapView && page.layers.length > 0
        // Steps through: on its own, each layer, and back.
        onActivated: {
            const order = [0].concat(page.layers.map(function (l) { return l.layer }))
            page.layerView = order[(order.indexOf(page.layerView) + 1) % order.length]
        }
    }
    function refreshRecipes() { page.recipes = config.recipeList(page.activeProfile) }
    onActiveProfileChanged: { refreshRecipes(); refreshMap() }
    Component.onCompleted: { refreshRecipes(); refreshMap() }
    Connections {
        target: mappingModel
        function onCountChanged() { page.refreshRecipes() }
    }

    Connections {
        target: config
        function onActiveProfileChanged() { page.activeProfile = config.activeProfileId }
    }

    // Cycling keyboards from the keyboard, since that is what this page is for.
    Shortcut {
        sequences: ["Ctrl+N"]
        enabled: page.visible
        onActivated: mappingEditor.openForNew()
    }
    Shortcut {
        sequences: ["Ctrl+Tab"]
        onActivated: page.cycleProfile(1)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab"]
        onActivated: page.cycleProfile(-1)
    }

    function cycleProfile(step) {
        const list = config.keyboardList()
        if (list.length < 2)
            return
        let current = 0
        for (let index = 0; index < list.length; ++index)
            if (list[index].profileId === page.activeProfile)
                current = index
        const next = (current + step + list.length) % list.length
        const entry = list[next]
        const id = config.ensureProfileForDevice(entry.deviceName, entry.name)
        if (id.length > 0)
            config.activeProfileId = id
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panelPadding
        spacing: Theme.panelGap

        // --- keyboard selector -------------------------------------------
        // Per-keyboard rules are the point, so the choice of keyboard is the
        // first thing on the page rather than buried in a settings pane.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(1)

            Text {
                text: qsTr("Keyboard")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.space(1)

                // Every keyboard attached, plus the all-keyboards entry. No
                // "add" step: selecting one is what creates its rules.
                Repeater {
                    model: config.keyboards

                    delegate: Rectangle {
                        id: chip
                        required property var modelData

                        readonly property bool selected:
                            modelData.profileId.length > 0
                            && modelData.profileId === page.activeProfile
                        implicitWidth: chipRow.implicitWidth + Theme.space(3)
                        implicitHeight: Theme.space(6)
                        radius: Theme.radiusSmall
                        color: selected ? Theme.accented(0.14)
                             : chipHover.hovered ? Theme.accented(0.05) : Theme.surface
                        border.width: 1
                        border.color: selected ? Theme.accent
                                    : chipHover.hovered ? Theme.accented(0.5) : Theme.border
                        opacity: chip.modelData.enabled ? 1.0 : 0.55
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                        HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }

                        RowLayout {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: Theme.lg

                            // How it is attached, which is also how most people
                            // tell their keyboards apart.
                            Icon {
                                name: chip.modelData.isGlobal ? "layers"
                                    : chip.modelData.transport === "bluetooth" ? "bluetooth"
                                    : chip.modelData.transport === "usb" ? "usb"
                                    : chip.modelData.transport === "internal" ? "internal"
                                    : "keyboard"
                                size: 1.45
                                color: chip.selected ? Theme.accent : Theme.muted
                                Layout.preferredWidth: Theme.px(20)
                            }

                        ColumnLayout {
                            id: chipContent
                            spacing: 1

                            RowLayout {
                                spacing: Theme.space(0.75)

                                Text {
                                    Layout.maximumWidth: Theme.space(32)
                                    text: chip.modelData.name
                                    color: Theme.foreground
                                    font.pixelSize: Theme.fontSizeBody
                                    font.weight: chip.selected ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }

                                // A profile for an unplugged keyboard is not
                                // broken: it applies again when that keyboard
                                // comes back.
                                Rectangle {
                                    visible: !chip.modelData.isGlobal
                                    implicitWidth: Theme.space(0.75)
                                    implicitHeight: Theme.space(0.75)
                                    radius: width / 2
                                    color: chip.modelData.connected ? Theme.success : Theme.muted
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }

                            Text {
                                text: {
                                    const count = chip.modelData.mappingCount
                                    const what = count === 0 ? qsTr("No mappings yet")
                                               : count === 1 ? qsTr("1 mapping")
                                               : qsTr("%1 mappings").arg(count)
                                    if (chip.modelData.isGlobal)
                                        return qsTr("Every keyboard · ") + what
                                    if (!chip.modelData.connected)
                                        return qsTr("Not connected · ") + what
                                    return chip.modelData.isMain
                                        ? qsTr("In use now · ") + what
                                        : what
                                }
                                color: Theme.muted
                                font.pixelSize: Theme.fontSizeSmall
                            }
                        }
                        }

                        TapHandler {
                            onTapped: {
                                const id = config.ensureProfileForDevice(chip.modelData.deviceName,
                                                                        chip.modelData.name)
                                if (id.length > 0)
                                    config.activeProfileId = id
                            }
                        }
                    }
                }
            }
        }

        // --- profile actions ---------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: page.activeProfile.length > 0

            Text {
                text: qsTr("Mappings")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            Segmented {
                Layout.leftMargin: Theme.md
                options: [qsTr("List"), qsTr("Keyboard")]
                currentIndex: page.mapView ? 1 : 0
                onActivated: function (index) { page.mapView = index === 1 }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                // Quiet on purpose: a rarely used action should not shout
                // next to the one people came here for. Discard undoes it.
                text: qsTr("Clear this keyboard")
                compact: true
                visible: {
                    const list = config.keyboardList()
                    for (let index = 0; index < list.length; ++index)
                        if (list[index].profileId === page.activeProfile)
                            return !list[index].isGlobal && list[index].mappingCount > 0
                    return false
                }
                onClicked: config.removeProfile(page.activeProfile)
            }

            AppButton {
                text: qsTr("Recipes")
                compact: true
                visible: mappingList.count > 0
                onClicked: recipeDialog.open()
            }

            AppButton {
                text: qsTr("Add mapping")
                weight: "primary"
                compact: true
                onClicked: mappingEditor.openForNew()
            }
        }

        // --- mapping list -------------------------------------------------
        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ListView {
                id: mappingList
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: mappingList.count > 0 && !page.mapView
                clip: true
                model: mappingModel
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // Rows arrive, leave and make room with a short slide, so an
                // edit is seen happening rather than the list redrawing.
                add: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
                        NumberAnimation { property: "x"; from: -Theme.xxl; to: 0; duration: Theme.durationSlow; easing.type: Theme.easing }
                    }
                }
                remove: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
                        NumberAnimation { property: "x"; to: Theme.xxl; duration: Theme.durationMedium; easing.type: Theme.easing }
                    }
                }
                displaced: Transition {
                    NumberAnimation { properties: "x,y"; duration: Theme.durationMedium; easing.type: Theme.easing }
                    NumberAnimation { property: "opacity"; to: 1; duration: Theme.durationFast }
                }

                delegate: Item {
                    id: row
                    required property string mappingId
                    required property string kind
                    required property string kindLabel
                    required property bool isEnabled
                    required property string sourceKey
                    required property string sourceLabel
                    required property string targetLabel
                    required property var levels
                    required property string xkbOption
                    required property string note
                    required property bool supported
                    required property string limit

                    // What the key becomes is itself a key, except for a
                    // layer key and a ready-made option, which are behaviours.
                    readonly property bool targetIsKey: row.kind === "keyRemap"
                        || row.kind === "modifierRemap" || row.kind === "layerKey"

                    width: mappingList.width
                    implicitHeight: Theme.rowHeight + Theme.md

                    Rectangle {
                        anchors.fill: parent
                        color: hover.hovered ? Theme.accented(0.05) : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }

                    HoverHandler { id: hover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.rowPaddingX + Theme.xs
                        anchors.rightMargin: Theme.md
                        spacing: Theme.rowGap

                        KeyCap {
                            text: row.sourceLabel
                            dimmed: !row.isEnabled
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Icon {
                            name: "arrow"
                            size: 0.95
                            color: row.isEnabled && hover.hovered ? Theme.accent : Theme.muted
                        }

                        KeyCap {
                            visible: row.targetIsKey
                            keys: [row.targetLabel]
                            accent: row.isEnabled
                            dimmed: !row.isEnabled
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // A behaviour gets a label shaped like a key's, so the
                        // row keeps its rhythm, with an icon saying what it is.
                        // It gives up width before the row's buttons do.
                        Rectangle {
                            visible: !row.targetIsKey
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: true
                            Layout.minimumWidth: Theme.px(90)
                            Layout.maximumWidth: implicitWidth
                            implicitWidth: behaviourRow.implicitWidth + Theme.xl * 2
                            implicitHeight: Math.round(Theme.controlHeight * 0.78)
                            radius: Math.max(Theme.radiusSmall, 2)
                            color: row.isEnabled ? Theme.accented(0.10) : "transparent"
                            border.width: 1
                            border.color: row.isEnabled ? Theme.accented(0.45) : Theme.tinted(Theme.muted, 0.35)

                            RowLayout {
                                id: behaviourRow
                                anchors.fill: parent
                                anchors.leftMargin: Theme.xl
                                anchors.rightMargin: Theme.xl
                                spacing: Theme.md

                                Icon {
                                    name: row.kind === "layerChooser" ? "layers" : "recipe"
                                    size: 0.95
                                    color: row.isEnabled ? Theme.accent : Theme.muted
                                }

                                Text {
                                    id: behaviourText
                                    Layout.fillWidth: true
                                    text: row.targetLabel
                                    color: row.isEnabled ? Theme.accent : Theme.muted
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight

                                    // A long option description can still be
                                    // cut in a narrow window; hovering shows it.
                                    HoverHandler { id: behaviourHover }
                                    ToolTip.visible: behaviourText.truncated && behaviourHover.hovered
                                    ToolTip.delay: 400
                                    ToolTip.text: row.targetLabel
                                }
                            }
                        }

                        // The user's own note, or why a mapping cannot apply.
                        // The keys already say what kind of mapping it is.
                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.md
                            text: !row.supported ? row.limit
                                : row.note.length > 0 ? row.note
                                : row.kind === "layerChooser"
                                    ? (row.levels.length > 0 && row.levels[0] === "ISO_Level5_Shift"
                                        ? qsTr("Hold it for a third set of keys")
                                        : qsTr("Hold it for a second set of keys"))
                                : ""
                            color: row.supported ? Theme.muted : Theme.warning
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                        }

                        ToggleSwitch {
                            checked: row.isEnabled
                            onToggled: config.setMappingEnabled(page.activeProfile,
                                                                row.mappingId, checked)
                        }

                        IconButton {
                            glyph: "edit"
                            tip: qsTr("Edit")
                            onClicked: mappingEditor.openForExisting(row)
                        }

                        IconButton {
                            glyph: "trash"
                            tip: qsTr("Delete")
                            danger: true
                            onClicked: config.removeMapping(page.activeProfile, row.mappingId)
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.rowPaddingX
                        anchors.rightMargin: Theme.rowPaddingX
                        implicitHeight: 1
                        color: Theme.border
                        opacity: 0.5
                        visible: row.y + row.height < mappingList.contentHeight
                    }
                }

            }

            // The same mappings, drawn on the keys they belong to.
            Flickable {
                id: mapFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Its size comes from the card, never from what it holds: a
                // long line inside must not be able to widen the map.
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                visible: page.mapView
                contentWidth: width
                contentHeight: mapColumn.implicitHeight + Theme.panelPadding * 2
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationMedium } }

                ColumnLayout {
                    id: mapColumn
                    x: Theme.panelPadding
                    y: Theme.panelPadding
                    width: mapFlick.width - Theme.panelPadding * 2
                    spacing: Theme.panelGap

                    // Wraps rather than overflowing when three views and the
                    // test button do not fit on one line.
                    Flow {
                        Layout.preferredWidth: mapColumn.width
                        Layout.maximumWidth: mapColumn.width
                        spacing: Theme.lg

                        Segmented {
                            visible: page.layers.length > 0
                            options: [qsTr("On its own")].concat(page.layers.map(function (l) {
                                return qsTr("Holding %1").arg(l.label)
                            }))
                            currentIndex: {
                                for (let i = 0; i < page.layers.length; ++i)
                                    if (page.layers[i].layer === page.shownLayer)
                                        return i + 1
                                return 0
                            }
                            onActivated: function (index) {
                                page.layerView = index === 0 ? 0 : page.layers[index - 1].layer
                            }
                        }

                        AppButton {
                            text: tester.listening ? qsTr("Stop") : qsTr("Test keys")
                            compact: true
                            weight: tester.listening ? "primary" : "plain"
                            onClicked: tester.listening ? capture.stopProbe() : tester.arm()
                        }
                    }

                    // The map doubles as the key tester: while it listens,
                    // every key pressed sinks on the drawing, and holding the
                    // layer key shows the layer.
                    FocusScope {
                        id: tester
                        // Sized from the column, not from its content: a
                        // layout left to itself let a long caption below
                        // stretch the whole map past the card.
                        Layout.preferredWidth: Math.min(mapColumn.width, Theme.px(760))
                        Layout.maximumWidth: mapColumn.width
                        Layout.alignment: Qt.AlignHCenter
                        implicitHeight: keyMapView.implicitHeight

                        property bool armed: false
                        readonly property bool listening: tester.armed && capture.capturing
                        property var held: ({})
                        property var reading: ({})

                        function arm() {
                            tester.held = ({})
                            tester.reading = ({})
                            tester.armed = true
                            tester.forceActiveFocus()
                            capture.startProbe()
                        }

                        function setHeld(code, down) {
                            const next = Object.assign({}, tester.held)
                            if (down)
                                next[code] = true
                            else
                                delete next[code]
                            tester.held = next
                        }

                        Connections {
                            target: capture
                            function onCapturingChanged() {
                                if (!capture.capturing) {
                                    tester.armed = false
                                    tester.held = ({})
                                }
                            }
                        }
                        onVisibleChanged: if (!visible && tester.armed) capture.stopProbe()

                        Keys.onPressed: function (event) {
                            if (!tester.listening)
                                return
                            event.accepted = true
                            if (event.key === Qt.Key_Escape) {
                                capture.stopProbe()
                                return
                            }
                            const reading = capture.describeKey(event.nativeScanCode, event.key,
                                                                event.modifiers, event.text)
                            capture.handleKey(event.nativeScanCode, event.key, event.modifiers)
                            if (reading.code)
                                tester.setHeld(reading.code, true)
                            if (!event.isAutoRepeat)
                                tester.reading = reading
                        }
                        Keys.onReleased: function (event) {
                            if (!tester.listening || event.isAutoRepeat)
                                return
                            event.accepted = true
                            const reading = capture.describeKey(event.nativeScanCode, event.key,
                                                                event.modifiers, event.text)
                            if (reading.code)
                                tester.setHeld(reading.code, false)
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -Theme.lg
                            radius: Theme.radius
                            color: Theme.accented(0.05)
                            border.width: 1
                            border.color: Theme.accented(0.6)
                            opacity: tester.listening ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationMedium } }
                        }

                        KeyboardMap {
                            id: keyMapView
                            width: parent.width
                            map: page.keyMap
                            layerShown: page.shownLayer
                            held: tester.held
                            interactive: !tester.listening
                            onKeyClicked: function (entry) { page.editKey(entry) }
                        }
                    }

                    // What the last key did, while testing.
                    RowLayout {
                        Layout.preferredWidth: mapColumn.width
                        Layout.maximumWidth: mapColumn.width
                        Layout.topMargin: Theme.sm
                        visible: tester.listening
                        spacing: Theme.lg

                        Text {
                            visible: !tester.reading.code && !tester.reading.produced
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: page.layers.length > 0
                                ? qsTr("Press any key. Hold %1 to see its layer. Esc stops.")
                                    .arg(page.layers.map(function (l) { return l.label }).join(qsTr(" or ")))
                                : qsTr("Press any key. Esc stops.")
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.WordWrap
                        }

                        KeyCap {
                            visible: !!tester.reading.codeLabel
                            keys: [tester.reading.codeLabel || ""]
                        }
                        Icon {
                            visible: !!tester.reading.codeLabel
                            name: "arrow"
                            color: Theme.muted
                        }
                        KeyCap {
                            visible: !!tester.reading.produced
                            keys: [tester.reading.produced || ""]
                            accent: true
                        }
                        Text {
                            visible: !!tester.reading.codeLabel
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            // The reading comes from the keyboard as it is
                            // applied now, not from unsaved edits.
                            text: config.dirty ? qsTr("typed with your applied settings; apply to test the new ones")
                                               : qsTr("what this key types right now")
                            color: Theme.muted
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Text.ElideRight
                        }
                    }

                    // What the colours mean, said once.
                    Text {
                        Layout.preferredWidth: mapColumn.width
                        Layout.maximumWidth: mapColumn.width
                        text: page.shownLayer
                            ? qsTr("Lit keys do something while %1 is held; the small label is what the key normally types. Click a key to give it a job on the layer.")
                                .arg(page.shownLayer === 2 ? page.keyMap.layerKey2Label
                                                           : page.keyMap.layerKeyLabel)
                            : qsTr("Lit keys are remapped; the small label is the key's own name. An orange outline is a mapping you turned off. Click any key to remap it. Function keys, arrows and the keypad are left out: not every keyboard has them.")
                        color: Theme.muted
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Empty state that teaches by example: a few setups people
            // actually want, each one click away, and the editor for
            // anything else.
            Flickable {
                id: emptyFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: mappingList.count === 0 && !page.mapView
                contentHeight: emptyState.implicitHeight + Theme.huge * 2
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationMedium } }

                ColumnLayout {
                    id: emptyState
                    width: Math.min(parent.width - Theme.huge * 2, Theme.px(620))
                    x: (parent.width - width) / 2
                    y: Math.max(Theme.huge, (emptyFlick.height - implicitHeight) / 2)
                    spacing: Theme.panelGap

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.xs

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Start with a recipe")
                            color: Theme.foreground
                            font.pixelSize: Theme.fontSizeHeading
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Pick one to add it to this keyboard. Nothing changes until you apply.")
                            color: Theme.muted
                            font.pixelSize: Theme.fontSizeSmall
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: emptyState.width > Theme.px(480) ? 2 : 1
                        rowSpacing: Theme.lg
                        columnSpacing: Theme.lg

                        Repeater {
                            model: page.recipes
                            delegate: RecipeCard {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                recipe: modelData
                                onChosen: function (recipeId) {
                                    config.addRecipe(page.activeProfile, recipeId)
                                }
                            }
                        }
                    }

                    AppButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Or make your own")
                        onClicked: mappingEditor.openForNew()
                    }
                }
            }
        }

        // --- conflicts for this page -------------------------------------
        // A nested layout stretches by default; this one must only take the
        // room its banners need, or it halves the list above when empty.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: Theme.space(0.75)

            // Only what concerns the keyboard on screen. A finding about
            // another profile belongs on that profile's page, not repeated
            // here -- two keyboards remapping Caps Lock would otherwise show
            // the same warning twice with no way to tell them apart.
            Repeater {
                model: config.conflicts.filter(function (conflict) {
                    return !conflict.profileId || conflict.profileId === page.activeProfile
                })

                delegate: ConflictBanner {
                    required property var modelData
                    Layout.fillWidth: true
                    severity: modelData.severity
                    title: modelData.title
                    detail: modelData.detail
                    suggestion: modelData.suggestion
                }
            }
        }
    }

    MappingEditor {
        id: mappingEditor
        profileId: page.activeProfile
        parent: Overlay.overlay
    }


    Dialog {
        id: recipeDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(8) : 640, Theme.px(660))
        padding: Theme.panelPadding + Theme.xs
        onAboutToShow: page.refreshRecipes()

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
                NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
        }

        background: Rectangle {
            color: Theme.background
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: Theme.panelGap

            SectionHeader {
                Layout.fillWidth: true
                title: qsTr("Recipes")
                subtitle: qsTr("Ready-made setups. Each one adds ordinary mappings you can edit afterwards.")
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Theme.lg
                columnSpacing: Theme.lg

                Repeater {
                    model: page.recipes
                    delegate: RecipeCard {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        recipe: modelData
                        onChosen: function (recipeId) {
                            config.addRecipe(page.activeProfile, recipeId)
                            recipeDialog.close()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Close")
                    compact: true
                    onClicked: recipeDialog.close()
                }
            }
        }
    }
}
