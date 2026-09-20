import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."
import "../components"

Item {
    id: page

    // Bound to the property, not copied once: a snapshot taken by Apply shows
    // up here without the page having to be rebuilt.
    readonly property var backups: config.backups

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: page.width
            spacing: Theme.space(2)

            Item { Layout.preferredHeight: Theme.space(1) }

            // --- anything missing ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                visible: config.engineWarnings.length > 0
                spacing: Theme.space(0.75)

                Repeater {
                    model: config.engineWarnings
                    delegate: ConflictBanner {
                        required property string modelData
                        Layout.fillWidth: true
                        severity: "warning"
                        title: qsTr("Missing on this system")
                        detail: modelData
                    }
                }
            }

            // --- backups ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                spacing: Theme.space(1)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.lg

                    SectionHeader {
                        Layout.fillWidth: true
                        title: qsTr("Backups")
                        subtitle: qsTr("A snapshot is taken before every change, and the 20 most recent are kept. Restoring one is itself undoable, and a backup that will not load cleanly is refused.")
                    }

                    AppButton {
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Delete all")
                        weight: "danger"
                        compact: true
                        visible: page.backups.length > 0
                        enabled: !config.busy
                        onClicked: confirmDelete.ask("")
                    }
                }

                Card {
                    Layout.fillWidth: true
                    padding: 0

                    // Twenty snapshots would push everything else off the
                    // page, so the list stops at five and scrolls.
                    ListView {
                        id: backupList
                        readonly property int rowsShown: 5
                        readonly property int rowHeight: Theme.px(46)

                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(count, rowsShown) * rowHeight
                        visible: count > 0
                        model: page.backups
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        // Always visible when there is more below, so it is
                        // clear the list continues.
                        ScrollBar.vertical: ScrollBar {
                            policy: backupList.count > backupList.rowsShown
                                ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }

                        delegate: Item {
                            id: backupRow
                            required property var modelData
                            required property int index

                            width: backupList.width
                            height: backupList.rowHeight

                            Rectangle {
                                anchors.fill: parent
                                color: backupHover.hovered ? Theme.accented(0.05) : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }
                            HoverHandler { id: backupHover }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.rowPaddingX
                                anchors.rightMargin: Theme.md
                                spacing: Theme.space(1.5)

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 1

                                    Text {
                                        Layout.fillWidth: true
                                        text: Qt.formatDateTime(backupRow.modelData.created,
                                                                "d MMM yyyy, HH:mm:ss")
                                        color: Theme.foreground
                                        font.pixelSize: Theme.fontSizeBody
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: backupRow.modelData.label + " · "
                                            + (backupRow.modelData.fileCount === 1
                                                ? qsTr("1 file")
                                                : qsTr("%1 files").arg(backupRow.modelData.fileCount))
                                        color: Theme.muted
                                        font.pixelSize: Theme.fontSizeSmall
                                        elide: Text.ElideRight
                                    }
                                }

                                // Both actions are icons, like every other
                                // row in the app.
                                IconButton {
                                    glyph: "backup"
                                    tip: qsTr("Restore this backup")
                                    enabled: !config.busy
                                    onClicked: {
                                        restoreResult.report =
                                            config.restoreBackup(backupRow.modelData.id)
                                        restoreResult.open()
                                    }
                                }

                                IconButton {
                                    glyph: "trash"
                                    tip: qsTr("Delete this backup")
                                    danger: true
                                    enabled: !config.busy
                                    onClicked: confirmDelete.ask(backupRow.modelData.id)
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Theme.rowPaddingX
                                anchors.rightMargin: Theme.rowPaddingX
                                height: 1
                                color: Theme.border
                                opacity: 0.5
                                visible: backupRow.index < backupList.count - 1
                            }
                        }
                    }

                    Text {
                        visible: page.backups.length === 0
                        Layout.fillWidth: true
                        Layout.margins: Theme.space(2)
                        text: qsTr("No backups yet. One is created the first time you apply a change.")
                        color: Theme.muted
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // --- sharing ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                spacing: Theme.space(1)

                SectionHeader {
                    Layout.fillWidth: true
                    title: qsTr("Share your setup")
                    subtitle: qsTr("One file holds every keyboard, mapping and shortcut. Importing adds to what you have; it never replaces it, and nothing reaches your keyboard until you apply.")
                }

                Card {
                    Layout.fillWidth: true

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(2)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Export or import a configuration")
                                color: Theme.foreground
                                font.pixelSize: Theme.fontSizeBody
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("When you import, you say which of your keyboards each one goes to, since the same keyboard is named differently on every machine.")
                                color: Theme.muted
                                font.pixelSize: Theme.fontSizeSmall
                                wrapMode: Text.WordWrap
                            }
                        }

                        AppButton {
                            text: qsTr("Import…")
                            enabled: !config.busy
                            onClicked: importFileDialog.open()
                        }

                        AppButton {
                            text: qsTr("Export…")
                            enabled: !config.busy
                            onClicked: exportFileDialog.open()
                        }
                    }
                }
            }

            // --- reset ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                spacing: Theme.space(1)

                SectionHeader {
                    Layout.fillWidth: true
                    title: qsTr("Start over")
                    subtitle: qsTr("Removes everything OmaKeyboard added and leaves the rest of your Omarchy configuration untouched.")
                }

                Card {
                    Layout.fillWidth: true

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(2)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Restore Omarchy's defaults")
                                color: Theme.foreground
                                font.pixelSize: Theme.fontSizeBody
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Deletes the generated keymaps and takes the OmaKeyboard block out of hyprland.lua. A backup is taken first.")
                                color: Theme.muted
                                font.pixelSize: Theme.fontSizeSmall
                                wrapMode: Text.WordWrap
                            }
                        }

                        AppButton {
                            text: qsTr("Restore defaults")
                            weight: "danger"
                            enabled: !config.busy
                            onClicked: confirmReset.open()
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.space(3) }
        }
    }

    Dialog {
        id: confirmReset
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(10) : 460, 480)
        padding: Theme.space(2.5)

        background: Rectangle {
            color: Theme.background
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: Theme.space(1.5)

            SectionHeader {
                Layout.fillWidth: true
                title: qsTr("Remove all OmaKeyboard mappings?")
                subtitle: qsTr("Your keyboard goes back to how Omarchy set it up. A backup is taken first, so this can be undone from the list above.")
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Cancel")
                    onClicked: confirmReset.close()
                }

                AppButton {
                    text: qsTr("Remove them")
                    weight: "danger"
                    onClicked: {
                        restoreResult.report = config.restoreDefaults()
                        confirmReset.close()
                        restoreResult.open()
                    }
                }
            }
        }
    }

    Dialog {
        id: restoreResult
        property var report: ({})

        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(10) : 500, 520)
        padding: Theme.space(2.5)

        background: Rectangle {
            color: Theme.background
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: Theme.space(1.5)

            Text {
                Layout.fillWidth: true
                text: restoreResult.report.ok ? qsTr("Done") : qsTr("Nothing was changed")
                color: restoreResult.report.ok ? Theme.success : Theme.error
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: restoreResult.report.message || ""
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: restoreResult.report.steps || []
                delegate: Text {
                    required property string modelData
                    Layout.fillWidth: true
                    text: "· " + modelData
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Close")
                    weight: "primary"
                    onClicked: restoreResult.close()
                }
            }
        }
    }

    // --- sharing ------------------------------------------------------------

    FileDialog {
        id: exportFileDialog
        title: qsTr("Export your keyboard configuration")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("OmaKeyboard configuration (*.json)")]
        defaultSuffix: "json"
        currentFile: "file://" + Qt.resolvedUrl("").toString().replace("file://", "")
        selectedFile: {
            const stamp = Qt.formatDateTime(new Date(), "yyyy-MM-dd")
            return "omakeyboard-" + stamp + ".json"
        }
        onAccepted: {
            const result = config.exportToFile(selectedFile.toString())
            shareResult.show(result.ok ? qsTr("Exported") : qsTr("Nothing was exported"),
                             result.ok
                                ? qsTr("%1 saved: %2 keyboards, %3 mappings and %4 shortcuts.")
                                    .arg(result.path).arg(result.profiles)
                                    .arg(result.mappings).arg(result.shortcuts)
                                : result.message,
                             result.ok)
        }
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("Import a keyboard configuration")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("OmaKeyboard configuration (*.json)"), qsTr("All files (*)")]
        onAccepted: importDialog.show(selectedFile.toString())
    }

    // Choosing where each keyboard in the file lands. A device name from
    // someone else's machine means nothing here, so it is asked rather than
    // guessed -- except when the very same device is attached.
    Dialog {
        id: importDialog

        property string path: ""
        property var report: ({})
        property var targets: []
        property bool withShortcuts: true

        function show(filePath) {
            const report = config.inspectImport(filePath)
            if (!report.ok) {
                shareResult.show(qsTr("Nothing was imported"), report.error, false)
                return
            }
            importDialog.path = filePath
            importDialog.report = report
            importDialog.withShortcuts = report.shortcutCount > 0
            importDialog.targets = (report.profiles || []).map(function (profile) {
                return profile.suggested || "skip"
            })
            importDialog.open()
        }

        function setTarget(index, value) {
            const next = importDialog.targets.slice()
            next[index] = value
            importDialog.targets = next
        }

        // Everywhere a keyboard from the file can go: any keyboard of yours,
        // all keyboards at once, or nowhere.
        readonly property var choices: {
            const list = [{ value: "global", label: qsTr("All keyboards") }]
            const keyboards = config.keyboards
            for (let i = 0; i < keyboards.length; ++i) {
                if (keyboards[i].isGlobal || keyboards[i].deviceName.length === 0)
                    continue
                list.push({ value: keyboards[i].deviceName, label: keyboards[i].name })
            }
            list.push({ value: "skip", label: qsTr("Don't import") })
            return list
        }

        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(8) : 640, Theme.px(660))
        padding: Theme.panelPadding + Theme.xs

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
                title: qsTr("What to import")
                subtitle: importDialog.report.exported
                    ? qsTr("Exported %1. Nothing is applied until you press Apply.")
                        .arg(Qt.formatDateTime(new Date(importDialog.report.exported),
                                               "d MMM yyyy, HH:mm"))
                    : qsTr("Nothing is applied until you press Apply.")
            }

            Repeater {
                model: importDialog.report.profiles || []

                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: Theme.lg

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 1

                        Text {
                            Layout.fillWidth: true
                            text: modelData.isGlobal ? qsTr("All keyboards") : modelData.name
                            color: Theme.foreground
                            font.pixelSize: Theme.fontSizeBody
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: {
                                const count = modelData.mappingCount === 1
                                    ? qsTr("1 mapping") : qsTr("%1 mappings").arg(modelData.mappingCount)
                                if (modelData.isGlobal)
                                    return count
                                return modelData.attached
                                    ? qsTr("%1 · this keyboard is attached").arg(count)
                                    : qsTr("%1 · not a keyboard of yours").arg(count)
                            }
                            color: Theme.muted
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                        }
                    }

                    ComboBox {
                        id: targetBox
                        Layout.preferredWidth: Theme.px(200)
                        model: importDialog.choices
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: {
                            const wanted = importDialog.targets[index]
                            for (let i = 0; i < importDialog.choices.length; ++i)
                                if (importDialog.choices[i].value === wanted)
                                    return i
                            return importDialog.choices.length - 1
                        }
                        onActivated: importDialog.setTarget(index, currentValue)

                        contentItem: Text {
                            leftPadding: Theme.controlPaddingX
                            rightPadding: targetBox.indicator.width
                            text: targetBox.displayText
                            color: Theme.foreground
                            font.pixelSize: Theme.fontSizeSmall
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.elevated
                            border.width: 1
                            border.color: targetBox.activeFocus ? Theme.accent : Theme.border
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: (importDialog.report.shortcutCount || 0) > 0
                spacing: Theme.lg

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Shortcuts")
                        color: Theme.foreground
                        font.pixelSize: Theme.fontSizeBody
                    }

                    Text {
                        Layout.fillWidth: true
                        text: importDialog.report.shortcutCount === 1
                            ? qsTr("1 shortcut in this file")
                            : qsTr("%1 shortcuts in this file").arg(importDialog.report.shortcutCount)
                        color: Theme.muted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }

                ToggleSwitch {
                    checked: importDialog.withShortcuts
                    onToggled: importDialog.withShortcuts = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Cancel")
                    onClicked: importDialog.close()
                }

                AppButton {
                    text: qsTr("Import")
                    weight: "primary"
                    onClicked: {
                        const result = config.importFromFile(importDialog.path,
                                                             importDialog.targets,
                                                             importDialog.withShortcuts)
                        importDialog.close()
                        shareResult.show(
                            result.ok ? qsTr("Imported") : qsTr("Nothing was imported"),
                            result.ok
                                ? qsTr("Added %1 mappings and %2 shortcuts to %3. Review them, then press Apply.")
                                    .arg(result.mappings).arg(result.shortcuts)
                                    .arg((result.keyboards || []).join(", "))
                                : qsTr("Every keyboard in the file was set to \"Don't import\"."),
                            result.ok)
                    }
                }
            }
        }
    }

    Dialog {
        id: shareResult
        property string heading: ""
        property string body: ""
        property bool good: true

        function show(heading, body, good) {
            shareResult.heading = heading
            shareResult.body = body
            shareResult.good = good
            shareResult.open()
        }

        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(10) : 500, 520)
        padding: Theme.space(2.5)

        background: Rectangle {
            color: Theme.background
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: Theme.space(1.5)

            Text {
                Layout.fillWidth: true
                text: shareResult.heading
                color: shareResult.good ? Theme.success : Theme.error
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: shareResult.body
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WrapAnywhere
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Close")
                    weight: "primary"
                    onClicked: shareResult.close()
                }
            }
        }
    }

    // Deleting a backup is the one thing here that cannot be undone, so it is
    // the one thing that asks.
    Dialog {
        id: confirmDelete

        property string backupId: ""
        readonly property bool all: confirmDelete.backupId.length === 0

        function ask(backupId) {
            confirmDelete.backupId = backupId
            confirmDelete.open()
        }

        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(10) : 460, 480)
        padding: Theme.space(2.5)

        background: Rectangle {
            color: Theme.background
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: Theme.space(1.5)

            SectionHeader {
                Layout.fillWidth: true
                title: confirmDelete.all
                    ? (page.backups.length === 1
                        ? qsTr("Delete the only backup?")
                        : qsTr("Delete all %1 backups?").arg(page.backups.length))
                    : qsTr("Delete this backup?")
                subtitle: qsTr("The snapshot files are removed for good. Your current configuration is not touched, but you will not be able to go back to this point.")
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDelete.close()
                }

                AppButton {
                    text: confirmDelete.all ? qsTr("Delete all") : qsTr("Delete")
                    weight: "danger"
                    onClicked: {
                        if (confirmDelete.all)
                            config.deleteAllBackups()
                        else
                            config.deleteBackup(confirmDelete.backupId)
                        confirmDelete.close()
                    }
                }
            }
        }
    }
}
