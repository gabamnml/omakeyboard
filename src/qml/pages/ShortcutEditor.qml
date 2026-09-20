import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."
import "../components"

// Add or edit a shortcut: a combination, and an action from the catalogue of
// what this system can actually do.
Dialog {
    id: root

    property string editingId: ""
    property string keys: ""
    property string description: ""
    property string actionKind: ""
    property string actionValue: ""
    property string actionLabel: ""
    property string focusMatch: ""
    property bool unbindFirst: true

    readonly property bool editing: editingId.length > 0
    readonly property bool complete: keys.length > 0 && actionValue.length > 0

    function openForNew() {
        editingId = ""
        keys = ""; description = ""
        actionKind = ""; actionValue = ""; actionLabel = ""
        focusMatch = ""; unbindFirst = true
        open()
    }

    function openForExisting(row) {
        editingId = row.shortcutId
        keys = row.keys
        description = row.description
        actionKind = row.actionKind
        actionValue = row.actionValue
        actionLabel = row.actionLabel + " · " + row.actionValue
        focusMatch = row.focusMatch
        unbindFirst = row.unbindFirst
        open()
    }

    function save() {
        if (!complete)
            return
        if (editing) {
            config.updateShortcut(editingId, keys, description, actionKind, actionValue,
                                  focusMatch)
            config.setShortcutUnbindFirst(editingId, unbindFirst)
        } else {
            const id = config.addShortcut(keys, description, actionKind, actionValue, focusMatch)
            if (id.length > 0)
                config.setShortcutUnbindFirst(id, unbindFirst)
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
            title: root.editing ? qsTr("Edit shortcut") : qsTr("Add shortcut")
            subtitle: qsTr("Press the combination you want, then choose what it should do.")
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(0.75)

            Text {
                text: qsTr("Shortcut")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            KeyCaptureField {
                Layout.fillWidth: true
                mode: "shortcut"
                value: root.keys
                label: root.keys
                placeholder: qsTr("No combination yet")
                onCaptured: function (value, label) { root.keys = value }
                onBrowseRequested: keyPicker.open()
            }

            Text {
                Layout.fillWidth: true
                visible: capture.capturing
                text: capture.hint
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(0.75)

            Text {
                text: qsTr("Action")
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
                        text: root.actionValue.length > 0
                            ? root.actionLabel : qsTr("Nothing chosen")
                        color: root.actionValue.length > 0 ? Theme.foreground : Theme.muted
                        font.pixelSize: Theme.fontSizeBody
                        elide: Text.ElideRight
                    }
                }

                AppButton {
                    text: qsTr("Choose")
                    compact: true
                    onClicked: actionPicker.open()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(0.75)

            Text {
                text: qsTr("Name (optional)")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            FilterField {
                Layout.fillWidth: true
                placeholderText: qsTr("Shown in Omarchy's keybindings list")
                text: root.description
                onTextChanged: root.description = text
            }
        }

        // Replacing an existing binding is the normal case on Omarchy, where
        // most combinations are already taken, so the switch defaults to on.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space(1.5)

            Switch {
                checked: root.unbindFirst
                onToggled: root.unbindFirst = checked

                indicator: Rectangle {
                    implicitWidth: Theme.space(4.5)
                    implicitHeight: Theme.space(2.5)
                    x: parent.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: height / 2
                    color: parent.checked ? Theme.accent : Theme.tinted(Theme.muted, 0.3)

                    Rectangle {
                        x: parent.parent.checked ? parent.width - width - 2 : 2
                        y: 2
                        width: parent.height - 4
                        height: width
                        radius: width / 2
                        color: Theme.background
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Take this combination from whatever has it")
                    color: Theme.foreground
                    font.pixelSize: Theme.fontSizeBody
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Releases the existing binding first. Turn this off and a clash becomes an error instead.")
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space(1)

            Item { Layout.fillWidth: true }

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

    onClosed: capture.cancel()

    CatalogPicker {
        id: actionPicker
        source: "actions"
        heading: qsTr("What should it do?")
        parent: Overlay.overlay
        onPicked: function (entryId, label, value, kind) {
            root.actionKind = kind
            root.actionValue = value
            root.actionLabel = label + " · " + value
        }
    }

    // A combination can also be assembled from the key list, for the cases a
    // capture cannot reach.
    CatalogPicker {
        id: keyPicker
        source: "keysyms"
        heading: qsTr("Choose the key")
        parent: Overlay.overlay
        onPicked: function (entryId, label, value, kind) {
            root.keys = root.keys.length > 0 ? root.keys : value
        }
    }
}
