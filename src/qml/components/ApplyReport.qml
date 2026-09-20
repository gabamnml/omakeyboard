import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../"

// What an apply did, stage by stage. The pipeline runs in one go, so the
// stages are revealed in the order they ran rather than animated as a fake
// progress bar: every tick on screen is a step that really passed.
Dialog {
    id: root

    property var report: ({})
    readonly property var stages: root.report.stages || []
    // How many stages are on screen so far.
    property int revealed: 0
    readonly property bool finished: root.revealed >= root.stages.length
    property bool showDetails: false

    function show(result) {
        root.report = result
        root.revealed = 0
        root.showDetails = false
        root.open()
        revealTimer.restart()
    }

    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.space(10) : 520, 500)
    padding: Theme.panelPadding + Theme.xs
    title: ""

    Timer {
        id: revealTimer
        interval: 85
        repeat: true
        onTriggered: {
            if (root.revealed < root.stages.length)
                root.revealed += 1
            else
                stop()
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast }
    }

    Overlay.modal: Rectangle {
        color: Theme.tinted(Theme.background, 0.6)
        Behavior on opacity { NumberAnimation { duration: Theme.durationMedium } }
    }

    background: Rectangle {
        color: Theme.background
        radius: Theme.radius
        border.width: 1
        border.color: root.finished
            ? (root.report.ok ? Theme.tinted(Theme.success, 0.6) : Theme.tinted(Theme.error, 0.6))
            : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durationSlow } }
    }

    contentItem: ColumnLayout {
        spacing: Theme.panelGap

        // --- verdict ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.lg

            Rectangle {
                id: badge
                implicitWidth: Theme.px(34)
                implicitHeight: Theme.px(34)
                radius: width / 2
                color: !root.finished ? Theme.accented(0.12)
                     : root.report.ok ? Theme.tinted(Theme.success, 0.16)
                     : Theme.tinted(Theme.error, 0.16)
                Behavior on color { ColorAnimation { duration: Theme.durationSlow } }

                Icon {
                    anchors.centerIn: parent
                    name: !root.finished ? "keyboard"
                        : root.report.ok ? "check"
                        : (root.report.rolledBack ? "undo" : "close")
                    size: 1.35
                    color: !root.finished ? Theme.accent
                         : root.report.ok ? Theme.success : Theme.error
                    scale: root.finished ? 1 : 0.85
                    Behavior on scale { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutBack; easing.overshoot: 2.2 } }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.xxs

                Text {
                    Layout.fillWidth: true
                    text: !root.finished ? qsTr("Applying…")
                        : root.report.ok ? qsTr("Applied")
                        : root.report.rolledBack ? qsTr("Rolled back")
                        : qsTr("Nothing was changed")
                    color: !root.finished ? Theme.foreground
                         : root.report.ok ? Theme.success : Theme.error
                    font.pixelSize: Theme.fontSizeHeading
                    font.weight: Font.DemiBold
                }

                // Errors from Lua or Hyprland can run to dozens of lines; the
                // summary stays short and Details has the rest.
                Text {
                    id: summary
                    Layout.fillWidth: true
                    text: root.finished ? (root.report.message || "") : ""
                    visible: text.length > 0
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
        }

        // --- the stages ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            visible: root.stages.length > 0

            Repeater {
                model: root.stages

                delegate: RowLayout {
                    id: stageRow
                    required property var modelData
                    required property int index

                    readonly property bool shown: index < root.revealed
                    readonly property string phase: stageRow.shown ? modelData.state : "waiting"
                    readonly property color tone: stageRow.phase === "done" ? Theme.success
                        : stageRow.phase === "failed" ? Theme.error
                        : stageRow.phase === "undone" ? Theme.warning
                        : Theme.muted

                    Layout.fillWidth: true
                    spacing: Theme.lg
                    opacity: stageRow.shown ? 1 : 0.25
                    Behavior on opacity { NumberAnimation { duration: Theme.durationMedium } }

                    // The rail: a dot per stage joined by a line, so the list
                    // reads as one pipeline rather than seven sentences.
                    Item {
                        implicitWidth: Theme.px(20)
                        implicitHeight: Theme.px(26)

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 1
                            visible: stageRow.index < root.stages.length - 1 || stageRow.index > 0
                            anchors.topMargin: stageRow.index === 0 ? parent.height / 2 : 0
                            anchors.bottomMargin: stageRow.index === root.stages.length - 1 ? parent.height / 2 : 0
                            color: Theme.border
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: Theme.px(18)
                            height: width
                            radius: width / 2
                            color: stageRow.phase === "pending" || stageRow.phase === "waiting"
                                   || stageRow.phase === "skipped"
                                ? Theme.background : Theme.tinted(stageRow.tone, 0.18)
                            border.width: 1
                            border.color: stageRow.phase === "waiting" ? Theme.border : stageRow.tone
                            scale: stageRow.shown ? 1 : 0.6
                            Behavior on scale { NumberAnimation { duration: Theme.durationMedium; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                            Behavior on color { ColorAnimation { duration: Theme.durationMedium } }

                            Icon {
                                anchors.centerIn: parent
                                name: stageRow.phase === "done" ? "check"
                                    : stageRow.phase === "failed" ? "close"
                                    : stageRow.phase === "undone" ? "undo"
                                    : stageRow.phase === "skipped" ? "minus"
                                    : ""
                                size: 0.8
                                color: stageRow.tone
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: stageRow.phase === "done" || stageRow.phase === "failed"
                               || stageRow.phase === "undone"
                            ? Theme.foreground : Theme.muted
                        font.pixelSize: Theme.fontSizeBody
                        font.strikeout: stageRow.phase === "skipped"
                        elide: Text.ElideRight
                    }

                    Text {
                        text: stageRow.phase === "skipped" ? qsTr("not needed")
                            : stageRow.phase === "pending" ? qsTr("not reached")
                            : stageRow.phase === "failed" ? qsTr("stopped here")
                            : ""
                        color: stageRow.tone
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
            }
        }

        // --- the raw trace, for when something needs explaining ---
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(detailColumn.implicitHeight, Theme.px(180))
            visible: root.showDetails
            contentWidth: availableWidth
            clip: true

        ColumnLayout {
            id: detailColumn
            width: parent.width
            spacing: 2

            Text {
                Layout.fillWidth: true
                visible: summary.truncated
                text: root.report.message || ""
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeCaption
                wrapMode: Text.WrapAnywhere
            }

            Repeater {
                model: root.report.steps || []
                delegate: Text {
                    required property string modelData
                    Layout.fillWidth: true
                    text: "· " + modelData
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.WordWrap
                }
            }
        }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.md

            AppButton {
                text: root.showDetails ? qsTr("Hide details") : qsTr("Details")
                compact: true
                visible: (root.report.steps || []).length > 0 || summary.truncated
                onClicked: root.showDetails = !root.showDetails
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Close")
                weight: "primary"
                enabled: root.finished
                onClicked: root.close()
            }
        }
    }
}
