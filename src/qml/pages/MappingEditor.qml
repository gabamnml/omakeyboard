import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."
import "../components"

// One sentence: "when I press X, do Y".
//
// The user never picks a mechanism. Whether a rule becomes a key remap, a
// modifier remap, a layer chooser or a ready-made XKB option is decided by the
// outcome they chose, in the engine, where it belongs.
Dialog {
    id: root

    property string profileId: ""
    property string editingId: ""

    // Working state
    property string sourceKey: ""
    property string sourceLabel: ""
    property string targetValue: ""
    property string targetLabel: ""
    property string targetDetail: ""
    property string targetKind: ""      // derived from the chosen outcome
    property string note: ""

    // "press" for a normal remap; "hold" for something on layer 1 and
    // "hold2" for layer 2, each only offered once its layer key exists.
    property string when: "press"

    readonly property bool editing: editingId.length > 0

    // The layer keys, when the profile has them. Their existence is what
    // makes the other modes meaningful, so the mode selector only appears then.
    function chooserFor(layer) {
        const rows = config.mappingList(root.profileId)
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].kind === "layerChooser" && rows[i].enabled && rows[i].layer === layer)
                return rows[i]
        return null
    }
    // Re-read each time the dialog opens: a layer key may have been added.
    property var layerKey: null
    property var layerKey2: null
    onAboutToShow: {
        root.layerKey = root.chooserFor(1)
        root.layerKey2 = root.chooserFor(2)
    }
    readonly property bool canUseLayer: root.layerKey !== null || root.layerKey2 !== null
    readonly property var activeLayerKey: root.when === "hold" ? root.layerKey
                                        : root.when === "hold2" ? root.layerKey2
                                        : null

    // The mode for a layer, falling back to one that exists.
    function holdMode(layer) {
        if (layer === 2 && root.chooserFor(2))
            return "hold2"
        if (layer === 1 && root.chooserFor(1))
            return "hold"
        return root.chooserFor(1) ? "hold" : (root.chooserFor(2) ? "hold2" : "press")
    }

    function openForNew() {
        sourcePreset = false
        editingId = ""
        when = "press"
        sourceKey = ""; sourceLabel = ""
        targetValue = ""; targetLabel = ""; targetDetail = ""; targetKind = ""
        note = ""
        open()
    }

    // From the keyboard map: the key is already chosen by clicking it, so
    // the editor starts at the outcome instead of listening for a key.
    property bool sourcePreset: false
    // `layer` is 0 for the key on its own, or the layer being looked at.
    function openForKey(code, label, layer) {
        editingId = ""
        when = layer > 0 ? root.holdMode(layer) : "press"
        sourceKey = code; sourceLabel = label
        targetValue = ""; targetLabel = ""; targetDetail = ""; targetKind = ""
        note = ""
        sourcePreset = true
        open()
    }

    function openForExisting(row) {
        sourcePreset = false
        editingId = row.mappingId
        when = row.kind === "layerKey" ? (row.layer === 2 ? "hold2" : "hold") : "press"
        sourceKey = row.sourceKey
        // The list shows a layer mapping as "Caps Lock + H"; the editor
        // adds the layer key itself, so it wants just the key.
        const label = row.sourceLabel || ""
        const joint = label.lastIndexOf(" + ")
        sourceLabel = row.kind === "layerKey" && joint > 0 ? label.slice(joint + 3) : label
        note = row.note
        targetKind = row.kind

        if (row.kind === "xkbOption") {
            targetValue = row.xkbOption
            targetLabel = row.targetLabel
            targetDetail = row.xkbOption
        } else {
            const levels = row.levels || []
            targetValue = row.kind === "layerKey"
                ? (row.layer === 2 ? (levels[4] || "") : (levels[2] || ""))
                : (levels.length > 0 ? levels[0] : "")
            targetLabel = targetValue
            targetDetail = ""
        }
        open()
    }

    // What actually gets stored. The only place the taxonomy appears.
    function resolvedKind() {
        if (root.when === "hold" || root.when === "hold2")
            return "layerKey"
        return root.targetKind.length > 0 ? root.targetKind : "keyRemap"
    }

    function resolvedLevels() {
        const kind = root.resolvedKind()
        if (kind === "xkbOption")
            return []
        if (kind === "layerKey")
            return root.when === "hold2"
                ? ["", "", "", "", root.targetValue, root.targetValue]
                : ["", "", root.targetValue, root.targetValue]
        return [root.targetValue]
    }

    readonly property bool complete: sourceKey.length > 0 && targetValue.length > 0

    function save() {
        if (!complete)
            return
        const kind = resolvedKind()
        const option = kind === "xkbOption" ? root.targetValue : ""
        if (editing) {
            config.updateMapping(profileId, editingId, kind, sourceKey, resolvedLevels(),
                                 option, note)
        } else {
            config.addMapping(profileId, kind, sourceKey, resolvedLevels(), option, note)
        }
        close()
    }

    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.space(8) : 620, 640)
    padding: Theme.space(2.5)

    background: Rectangle {
        color: Theme.background
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        // A key event only reaches the item that has focus. If a click left it
        // on a button, the event still travels up the focus chain, so the
        // dialog catches it here rather than letting it fall on the floor
        // while the interface claims to be listening.
        focus: true
        Keys.onPressed: function (event) {
            if (!capture.capturing)
                return
            capture.handleKey(event.nativeScanCode, event.key, event.modifiers)
            event.accepted = true
        }

        spacing: Theme.space(2)

        SectionHeader {
            Layout.fillWidth: true
            title: root.editing ? qsTr("Edit mapping") : qsTr("New mapping")
            subtitle: qsTr("Pick a key, then say what it should do.")
        }

        // --- when: only asked once a layer key exists ---
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.canUseLayer
            spacing: Theme.space(0.75)

            Row {
                spacing: 0

                Repeater {
                    model: {
                        const modes = [{ key: "press", label: qsTr("On its own") }]
                        if (root.layerKey)
                            modes.push({ key: "hold",
                                         label: qsTr("Holding %1").arg(root.layerKey.sourceLabel) })
                        if (root.layerKey2)
                            modes.push({ key: "hold2",
                                         label: qsTr("Holding %1").arg(root.layerKey2.sourceLabel) })
                        return modes
                    }

                    delegate: Rectangle {
                        id: mode
                        required property var modelData
                        readonly property bool selected: modelData.key === root.when

                        implicitWidth: Math.max(Theme.space(18), modeLabel.implicitWidth + Theme.space(4))
                        implicitHeight: Theme.space(5)
                        color: mode.selected ? Theme.accented(0.16) : Theme.elevated
                        border.width: 1
                        border.color: mode.selected ? Theme.accent : Theme.border

                        Text {
                            id: modeLabel
                            anchors.centerIn: parent
                            text: mode.modelData.label
                            color: mode.selected ? Theme.accent : Theme.foreground
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: mode.selected ? Font.DemiBold : Font.Normal
                        }

                        TapHandler { onTapped: root.when = mode.modelData.key }
                    }
                }
            }
        }

        // --- the sentence ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(0.75)

            Text {
                text: root.activeLayerKey
                    ? qsTr("While holding %1, when I press")
                        .arg(root.activeLayerKey.sourceLabel)
                    : qsTr("When I press")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            KeyCaptureField {
                id: sourceField
                Layout.fillWidth: true
                mode: "key"
                value: root.sourceKey
                label: root.sourceLabel
                placeholder: qsTr("No key chosen")
                onCaptured: function (value, label) {
                    root.sourceKey = value
                    root.sourceLabel = label
                    // Carry straight on to the only remaining question, so a
                    // new mapping is: press the key, pick the outcome, done.
                    if (!root.editing && root.targetValue.length === 0)
                        targetPicker.open()
                }
                onBrowseRequested: keyPicker.open()
            }

            Text {
                Layout.fillWidth: true
                visible: capture.capturing || sourceField.problem.length > 0
                text: sourceField.problem.length > 0 ? sourceField.problem : capture.hint
                color: sourceField.problem.length > 0 ? Theme.warning : Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(0.75)

            Text {
                text: qsTr("it should")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space(1)

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.space(5)
                    radius: Theme.radiusSmall
                    color: Theme.elevated
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space(1.5)
                        anchors.rightMargin: Theme.space(1.5)
                        verticalAlignment: Text.AlignVCenter
                        text: root.targetValue.length > 0
                            ? root.targetLabel : qsTr("Choose what it does")
                        color: root.targetValue.length > 0 ? Theme.foreground : Theme.muted
                        font.pixelSize: Theme.fontSizeBody
                        elide: Text.ElideRight
                    }

                    TapHandler { onTapped: targetPicker.open() }
                }

                AppButton {
                    text: root.targetValue.length > 0 ? qsTr("Change") : qsTr("Choose")
                    weight: root.targetValue.length > 0 ? "plain" : "primary"
                    compact: true
                    onClicked: targetPicker.open()
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.targetDetail.length > 0
                text: root.targetDetail
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        // Reading the rule back as a sentence, so there is no doubt about what
        // is about to be saved.
        Rectangle {
            Layout.fillWidth: true
            visible: root.complete
            implicitHeight: summary.implicitHeight + Theme.space(2)
            radius: Theme.radiusSmall
            color: Theme.accented(0.08)
            border.width: 1
            border.color: Theme.accented(0.3)

            RowLayout {
                id: summary
                anchors.fill: parent
                anchors.margins: Theme.space(1)
                spacing: Theme.space(1)

                KeyCap {
                    text: root.activeLayerKey
                        ? root.activeLayerKey.sourceLabel + " + " + root.sourceLabel
                        : root.sourceLabel
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: "→"
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeBody
                }

                Text {
                    Layout.fillWidth: true
                    text: root.targetLabel
                    color: Theme.foreground
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space(1)

            Text {
                Layout.fillWidth: true
                visible: !root.complete
                text: root.sourceKey.length === 0
                    ? qsTr("Start by choosing a key.")
                    : qsTr("Now choose what it should do.")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
            }

            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            AppButton {
                text: root.editing ? qsTr("Save") : qsTr("Add")
                weight: "primary"
                enabled: root.complete
                onClicked: root.save()
            }
        }
    }

    // Opening "New mapping" means the user already knows which key they want,
    // so start listening for it rather than making them click first.
    // Deferred: a Popup assigns focus after it opens, so arming inside
    // onOpened would hand the focus straight back to the dialog.
    onOpened: if (!root.editing && !root.sourcePreset) Qt.callLater(sourceField.start)

    onClosed: capture.cancel()

    CatalogPicker {
        id: keyPicker
        source: "keys"
        heading: qsTr("Which key?")
        parent: Overlay.overlay
        onPicked: function (entryId, label, value, kind) {
            root.sourceKey = value
            root.sourceLabel = label
            if (!root.editing && root.targetValue.length === 0)
                targetPicker.open()
        }
    }

    // One list of outcomes. The kind travels with the entry, so choosing
    // "Control" or "Hold for a second set of keys" is all the user has to do.
    CatalogPicker {
        id: targetPicker
        source: "targets"
        forKey: root.sourceKey
        heading: root.sourceLabel.length > 0
            ? qsTr("What should %1 do?").arg(root.sourceLabel)
            : qsTr("What should it do?")
        parent: Overlay.overlay
        onPicked: function (entryId, label, value, kind) {
            root.targetValue = value
            root.targetLabel = label
            root.targetKind = kind
            const entry = targetPicker.entryFor(entryId)
            root.targetDetail = entry && entry.detail ? entry.detail : ""
        }
    }
}
