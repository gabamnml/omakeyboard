import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: window

    width: 1040
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    visible: true
    title: qsTr("OmaKeyboard")
    color: Theme.background

    // A normal Wayland toplevel: no layer shell, no panel, nothing that needs
    // the compositor's cooperation beyond what any application gets.

    readonly property var pages: [
        { key: "keyboard",  icon: "keyboard",  label: qsTr("Keyboard"),  hint: qsTr("Remap keys and build layers") },
        { key: "shortcuts", icon: "shortcuts", label: qsTr("Shortcuts"), hint: qsTr("Key combinations that run actions") },
        { key: "advanced",  icon: "advanced",  label: qsTr("Advanced"),  hint: qsTr("Keyboard behaviour, key tester and checks") },
        { key: "settings",  icon: "settings",  label: qsTr("Settings"),  hint: qsTr("Backups and resetting") },
    ]
    property int pageIndex: 0

    // A page arrives with a short fade and rise, so switching reads as
    // moving somewhere rather than the window flickering.
    onPageIndexChanged: pageEnter.restart()

    // A keyboard app that can only be driven with a mouse would be an odd
    // thing. Ctrl+1..4 switch pages, Ctrl+Return applies.
    Shortcut {
        sequences: ["Ctrl+1"]
        onActivated: window.pageIndex = 0
    }
    Shortcut {
        sequences: ["Ctrl+2"]
        onActivated: window.pageIndex = 1
    }
    Shortcut {
        sequences: ["Ctrl+3"]
        onActivated: window.pageIndex = 2
    }
    Shortcut {
        sequences: ["Ctrl+4"]
        onActivated: window.pageIndex = 3
    }
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: config.dirty && !config.busy && config.errorCount === 0
                 && config.environment.ok !== false
        onActivated: window.applyNow()
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: config.undoLabel.length > 0
        onActivated: config.undoRemoval()
    }
    Shortcut {
        sequences: [StandardKey.Refresh]
        onActivated: config.reload()
    }

    function applyNow() {
        applyReport.show(config.apply())
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- navigation ---
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.px(200)
            color: Theme.elevated

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.panelPadding
                spacing: Theme.panelGap

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    // The wordmark is drawn in Omarchy's own letterforms, so
                    // sourceSize is pinned to the rendered size: an SVG scaled
                    // after rasterising would turn its 15-unit steps to mush.
                    Image {
                        id: wordmark
                        Layout.fillWidth: true
                        // Height follows the asset rather than a number copied
                        // from it, so regenerating the wordmark cannot leave a
                        // stale aspect ratio behind.
                        Layout.preferredHeight: wordmark.implicitWidth > 0
                            ? Math.round(width * wordmark.implicitHeight / wordmark.implicitWidth)
                            : 0
                        source: "qrc:/wordmark.svg"
                        fillMode: Image.PreserveAspectFit
                        smooth: false
                        sourceSize.width: Math.round(width * Screen.devicePixelRatio)
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.xxs
                        // What the app does, not what it is: the wordmark
                        // above already says it is a keyboard app for Omarchy.
                        text: qsTr("Remap keys and shortcuts")
                        color: Theme.muted
                        font.pixelSize: Theme.fontSizeCaption
                        elide: Text.ElideRight
                    }
                }

                // The selection is one highlight that slides between entries,
                // not four backgrounds switching on and off.
                Item {
                    Layout.fillWidth: true
                    implicitHeight: navColumn.implicitHeight

                    Rectangle {
                        id: navHighlight
                        readonly property Item target: navRepeater.count > window.pageIndex
                            ? navRepeater.itemAt(window.pageIndex) : null
                        width: parent.width
                        height: target ? target.height : 0
                        y: target ? target.y : 0
                        radius: Theme.radiusSmall
                        color: Theme.accented(0.14)
                        Behavior on y { NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easing } }

                        Rectangle {
                            width: Theme.xxs
                            height: parent.height
                            color: Theme.accent
                            radius: Theme.radiusSmall
                        }
                    }

                    Column {
                        id: navColumn
                        width: parent.width
                        spacing: 2

                        Repeater {
                            id: navRepeater
                            model: window.pages
                            delegate: ItemDelegate {
                                id: navItem
                                required property var modelData
                                required property int index
                                readonly property bool current: window.pageIndex === index

                                width: navColumn.width
                                implicitHeight: Theme.controlHeight + Theme.xs
                                onClicked: window.pageIndex = index

                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: !navItem.current && navItem.hovered
                                        ? Theme.accented(0.06) : "transparent"
                                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                }

                                contentItem: Row {
                                    leftPadding: Theme.controlPaddingX
                                    spacing: Theme.lg

                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: navItem.modelData.icon
                                        width: Theme.px(16)
                                        color: navItem.current ? Theme.accent : Theme.muted
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: navItem.modelData.label
                                        color: navItem.current ? Theme.accent : Theme.foreground
                                        font.pixelSize: Theme.fontSizeBody
                                        font.weight: navItem.current ? Font.DemiBold : Font.Normal
                                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // A warning you have to go looking for is a warning nobody
                // reads, so the count stays in view -- but quietly, as a small
                // marker rather than a wall of text.
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: config.errorCount > 0 || config.warningCount > 0
                    spacing: Theme.space(0.5)

                    Repeater {
                        model: [
                            { count: config.errorCount, tone: Theme.error,
                              one: qsTr("1 conflict to resolve"),
                              many: qsTr("%1 conflicts to resolve") },
                            { count: config.warningCount, tone: Theme.warning,
                              one: qsTr("1 warning"),
                              many: qsTr("%1 warnings") },
                        ]

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            visible: modelData.count > 0
                            spacing: Theme.space(0.75)

                            Rectangle {
                                implicitWidth: Theme.space(0.75)
                                implicitHeight: Theme.space(0.75)
                                radius: width / 2
                                color: modelData.tone
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.count === 1
                                    ? modelData.one
                                    : modelData.many.arg(modelData.count)
                                color: modelData.tone
                                font.pixelSize: Theme.fontSizeSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: !config.hyprlandRunning
                    text: qsTr("Saved changes will take effect at your next login.")
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 1
            color: Theme.border
        }

        // --- content ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Theme.px(56)
                color: Theme.background

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space(3)
                    anchors.rightMargin: Theme.space(3)
                    spacing: Theme.panelGap

                    SectionHeader {
                        Layout.fillWidth: true
                        singleLine: true
                        title: window.pages[window.pageIndex].label
                        subtitle: window.pages[window.pageIndex].hint
                    }

                    Row {
                        spacing: Theme.md
                        opacity: config.dirty ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: Theme.durationMedium } }

                        // Breathes slowly while there is something to apply:
                        // noticeable at a glance, never nagging.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.md
                            height: width
                            radius: width / 2
                            color: Theme.warning
                            SequentialAnimation on opacity {
                                running: config.dirty
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.35; duration: 1100; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 1.0; duration: 1100; easing.type: Easing.InOutSine }
                            }
                        }

                        Text {
                            text: qsTr("Unsaved changes")
                            color: Theme.warning
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }

                    AppButton {
                        text: qsTr("Discard")
                        visible: config.dirty
                        enabled: !config.busy
                        onClicked: config.discardChanges()
                    }

                    AppButton {
                        text: config.busy ? qsTr("Applying…") : qsTr("Apply")
                        weight: "primary"
                        enabled: config.dirty && !config.busy && config.errorCount === 0
                                 && config.environment.ok !== false
                        onClicked: window.applyNow()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }

            // Nothing below this can work if the system is not one the app
            // can write to, so it is said once, at the top, in plain words.
            ConflictBanner {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.panelPadding
                Layout.rightMargin: Theme.panelPadding
                Layout.topMargin: Theme.panelPadding
                visible: config.environment.ok === false
                severity: "error"
                title: config.environment.title || ""
                detail: config.environment.detail || ""
                suggestion: config.environment.suggestion || ""
            }

            StackLayout {
                id: pageStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.pageIndex
                transform: Translate { id: pageShift }

                ParallelAnimation {
                    id: pageEnter
                    NumberAnimation { target: pageStack; property: "opacity"; from: 0; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
                    NumberAnimation { target: pageShift; property: "y"; from: Theme.lg; to: 0; duration: Theme.durationSlow; easing.type: Theme.easing }
                }

                KeyboardPage {}
                ShortcutsPage {}
                AdvancedPage {}
                SettingsPage {}
            }

            // Status line, present only when there is something to report,
            // and it fades rather than blinking out.
            Rectangle {
                id: statusBar
                // The last thing said, kept while the line fades out so it
                // does not go blank before it disappears.
                property string message: ""

                Layout.fillWidth: true
                visible: opacity > 0
                opacity: config.status.length > 0 ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
                implicitHeight: Theme.px(26)
                color: Theme.elevated

                Connections {
                    target: config
                    function onStatusChanged() {
                        if (config.status.length > 0)
                            statusBar.message = config.status
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(3)
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space(3)
                    anchors.verticalCenter: parent.verticalCenter
                    text: statusBar.message
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
            }
        }
    }

    // Deleting is immediate, and this is what makes that safe: the last
    // deletion can be taken back for a few seconds, or with Ctrl+Z.
    Rectangle {
        id: undoNotice
        readonly property bool shown: config.undoLabel.length > 0

        parent: Overlay.overlay
        z: 10
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - (shown ? height + Theme.huge + Theme.px(26) : -Theme.huge)
        opacity: shown ? 1 : 0
        visible: opacity > 0
        width: undoRow.implicitWidth + Theme.panelPadding * 2
        height: Theme.controlHeight + Theme.lg * 2
        radius: Theme.radius
        color: Theme.elevated
        border.width: 1
        border.color: Theme.border

        Behavior on y { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }
        Behavior on opacity { NumberAnimation { duration: Theme.durationMedium } }

        // Restarted for every new deletion, so each one gets its full time.
        Timer {
            id: undoTimer
            interval: 7000
            onTriggered: config.dismissUndo()
        }
        Connections {
            target: config
            function onUndoChanged() {
                if (config.undoLabel.length > 0)
                    undoTimer.restart()
                else
                    undoTimer.stop()
            }
        }
        // Reading it is not a reason for it to vanish.
        HoverHandler {
            onHoveredChanged: hovered ? undoTimer.stop() : undoTimer.restart()
        }

        RowLayout {
            id: undoRow
            anchors.centerIn: parent
            spacing: Theme.xl

            Icon {
                name: "trash"
                color: Theme.muted
            }

            Text {
                text: config.undoLabel
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeBody
            }

            AppButton {
                text: qsTr("Undo")
                weight: "primary"
                compact: true
                onClicked: config.undoRemoval()
            }

            IconButton {
                glyph: "close"
                tip: qsTr("Dismiss")
                onClicked: config.dismissUndo()
            }
        }
    }

    // The result of an apply, stage by stage: when something is rolled back,
    // the user gets to see exactly which step failed.
    ApplyReport {
        id: applyReport
        parent: Overlay.overlay
    }
}
