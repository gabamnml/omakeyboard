import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."
import "../components"

// Things an advanced user does with a keyboard: tune how it behaves, and check
// that a mapping actually took effect. Both act on the profile selected on the
// Keyboard page, so there is one notion of "which keyboard" in the app.
Item {
    id: page

    property string activeProfile: config.activeProfileId
    property var behaviour: ({})
    property var lastKey: ({})
    property var checkReport: ({})

    // Scroll the tester into view, so Ctrl+T works from anywhere on the page.
    function revealProbe() {
        const flick = scroller.contentItem
        if (!flick)
            return
        const pos = probeArea.mapToItem(flick.contentItem, 0, 0)
        const target = pos.y - Theme.space(2)
        const limit = Math.max(0, flick.contentHeight - flick.height)
        flick.contentY = Math.max(0, Math.min(target, limit))
    }

    function refresh() {
        page.activeProfile = config.activeProfileId
        page.behaviour = config.behaviourFor(page.activeProfile)
    }

    Component.onCompleted: refresh()

    Connections {
        target: config
        function onStateReloaded() { page.refresh() }
        function onActiveProfileChanged() { page.refresh() }
    }

    // Leaving the page must not leave the tester swallowing keys.
    onVisibleChanged: if (!visible) capture.stopProbe()

    // The tester is reachable from the keyboard, which is the whole point of
    // the app. Deliberately not armed by tabbing onto the area: focus alone
    // must not take the keyboard away from the rest of the desktop.
    Shortcut {
        sequences: ["Ctrl+T"]
        enabled: page.visible
        onActivated: {
            if (capture.capturing)
                capture.stopProbe()
            else
                probeArea.arm()
        }
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: page.width
            spacing: Theme.space(2)

            Item { Layout.preferredHeight: Theme.space(1) }

            // --- which keyboard these settings apply to ------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                spacing: Theme.space(1)

                RowLayout {
                    Layout.fillWidth: true

                    SectionHeader {
                        Layout.fillWidth: true
                        title: qsTr("Keyboard behaviour")
                        subtitle: {
                            const list = config.profileList()
                            for (let i = 0; i < list.length; ++i)
                                if (list[i].id === page.activeProfile)
                                    return list[i].isGlobal
                                        ? qsTr("Applies to every keyboard. Switch keyboard on the Keyboard page.")
                                        : qsTr("Applies to %1 only.").arg(list[i].name)
                            return ""
                        }
                    }

                    AppButton {
                        text: qsTr("Follow Omarchy again")
                        compact: true
                        visible: !(page.behaviour.isEmpty === true)
                        onClicked: {
                            config.clearBehaviour(page.activeProfile)
                            page.refresh()
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(1.5)

                        NumberSetting {
                            Layout.fillWidth: true
                            label: qsTr("Repeat rate")
                            hint: qsTr("Keys per second once repeating starts.")
                            unit: qsTr(" keys/s")
                            minimum: 1
                            maximum: 255
                            value: page.behaviour.repeatRate !== undefined
                                ? page.behaviour.repeatRate : -1
                            inheritedValue: page.behaviour.liveRepeatRate !== undefined
                                ? page.behaviour.liveRepeatRate : -1
                            onChanged: function (value) {
                                config.setBehaviourNumber(page.activeProfile, "repeatRate", value)
                                page.refresh()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                            opacity: 0.5
                        }

                        NumberSetting {
                            Layout.fillWidth: true
                            label: qsTr("Repeat delay")
                            hint: qsTr("How long a key is held before it starts repeating.")
                            unit: qsTr(" ms")
                            minimum: 100
                            maximum: 4000
                            value: page.behaviour.repeatDelay !== undefined
                                ? page.behaviour.repeatDelay : -1
                            inheritedValue: page.behaviour.liveRepeatDelay !== undefined
                                ? page.behaviour.liveRepeatDelay : -1
                            onChanged: function (value) {
                                config.setBehaviourNumber(page.activeProfile, "repeatDelay", value)
                                page.refresh()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                            opacity: 0.5
                        }

                        FlagSetting {
                            Layout.fillWidth: true
                            label: qsTr("Num Lock on at start")
                            hint: qsTr("Num Lock is enabled when the session begins.")
                            state: page.behaviour.numlockOnStart || "inherit"
                            onChanged: function (state) {
                                config.setBehaviourFlag(page.activeProfile, "numlockOnStart", state)
                                page.refresh()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                            opacity: 0.5
                        }

                        FlagSetting {
                            Layout.fillWidth: true
                            label: qsTr("Shortcuts from this keyboard")
                            hint: qsTr("Off means keys from this keyboard never trigger a shortcut — useful for a device you only use for text, or a macro pad you drive from elsewhere.")
                            state: page.behaviour.shortcutsEnabled || "inherit"
                            settable: page.behaviour.shortcutsSettable === true
                            unavailableReason: qsTr("Hyprland can only set this per keyboard. Add the keyboard on the Keyboard page to use it.")
                            onChanged: function (state) {
                                config.setBehaviourFlag(page.activeProfile, "shortcutsEnabled", state)
                                page.refresh()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                            opacity: 0.5
                        }

                        FlagSetting {
                            Layout.fillWidth: true
                            label: qsTr("Match shortcuts by symbol")
                            hint: qsTr("Shortcuts follow the symbol a key produces rather than its position. Matters once you use more than one layout.")
                            state: page.behaviour.resolveBindsBySym || "inherit"
                            onChanged: function (state) {
                                config.setBehaviourFlag(page.activeProfile, "resolveBindsBySym", state)
                                page.refresh()
                            }
                        }
                    }
                }
            }

            // --- key tester ----------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                spacing: Theme.space(1)

                SectionHeader {
                    Layout.fillWidth: true
                    title: qsTr("Key tester")
                    subtitle: qsTr("Press a key and see exactly what the system reports. The fastest way to confirm a remap or a layer really took effect.")
                }

                Card {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(1.5)

                        // The listening area. While it is armed every shortcut
                        // is suspended, so a layer combination reaches the app
                        // instead of firing a binding.
                        Rectangle {
                            id: probeArea
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.space(14)
                            radius: Theme.radiusSmall
                            color: probeArea.listening ? Theme.accented(0.07) : Theme.elevated
                            border.width: 1
                            border.color: probeArea.listening ? Theme.accent : Theme.border

                            focus: false
                            activeFocusOnTab: true

                            // Whether *this* area is the one listening. Tracked
                            // with a flag rather than derived from activeFocus:
                            // clicking Stop moves focus to the button, which
                            // would make "listening" false before the click is
                            // handled -- and Stop would restart instead of stop.
                            property bool armed: false
                            readonly property bool listening: probeArea.armed && capture.capturing

                            function arm() {
                                // A new session starts blank, so an old reading
                                // cannot be mistaken for the key just pressed.
                                page.lastKey = ({})
                                probeArea.armed = true
                                probeArea.forceActiveFocus()
                                capture.startProbe()
                                // Arming from the keyboard is useless if the
                                // thing you are about to read is off screen.
                                page.revealProbe()
                            }

                            // However listening ended -- Stop, Escape, the
                            // timeout -- the panel goes back to idle instead of
                            // leaving the last reading on screen looking live.
                            Connections {
                                target: capture
                                function onCapturingChanged() {
                                    if (!capture.capturing) {
                                        probeArea.armed = false
                                        page.lastKey = ({})
                                    }
                                }
                            }

                            Keys.onPressed: function (event) {
                                event.accepted = true
                                if (event.key === Qt.Key_Escape) {
                                    capture.stopProbe()
                                    return
                                }
                                page.lastKey = capture.describeKey(event.nativeScanCode,
                                                                  event.key,
                                                                  event.modifiers,
                                                                  event.text)
                                capture.handleKey(event.nativeScanCode, event.key, event.modifiers)
                            }

                            TapHandler { onTapped: probeArea.arm() }

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: parent.width - Theme.space(4)
                                spacing: Theme.space(1)

                                Text {
                                    Layout.fillWidth: true
                                    visible: !probeArea.listening
                                    text: qsTr("Click here or press Ctrl+T, then press any key")
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontSizeBody
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: probeArea.listening && !(page.lastKey.code !== undefined)
                                    text: qsTr("Listening… press a key. Escape stops.")
                                    color: Theme.accent
                                    font.pixelSize: Theme.fontSizeBody
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                // The reading.
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: page.lastKey.code !== undefined
                                    spacing: Theme.space(0.5)

                                    RowLayout {
                                        Layout.alignment: Qt.AlignHCenter
                                        spacing: Theme.space(1)

                                        KeyCap {
                                            text: (page.lastKey.modifiers
                                                    && page.lastKey.modifiers.length > 0)
                                                ? page.lastKey.modifiers + " + " + (page.lastKey.produced || "?")
                                                : (page.lastKey.produced || "?")
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: {
                                            const physical = page.lastKey.codeLabel
                                                && page.lastKey.codeLabel.length > 0
                                                ? page.lastKey.codeLabel
                                                : qsTr("unknown key")
                                            return qsTr("Physical key: %1").arg(physical)
                                        }
                                        color: Theme.foreground
                                        font.pixelSize: Theme.fontSizeBody
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("XKB <%1> · keycode %2")
                                            .arg(page.lastKey.code || "?")
                                            .arg(page.lastKey.keycode || 0)
                                        color: Theme.muted
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.family: Theme.monoFamily
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: (page.lastKey.binding || "").length > 0
                                        text: qsTr("As a shortcut: %1").arg(page.lastKey.binding)
                                        color: Theme.muted
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.family: Theme.monoFamily
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space(1)

                            Text {
                                Layout.fillWidth: true
                                // Honest about the one thing the tester cannot
                                // see: Hyprland consumes a bound combination
                                // before any window gets it.
                                text: probeArea.listening
                                    ? qsTr("A combination Hyprland already has bound never reaches this window. Everything else, including a layer you have applied, shows up here.")
                                    : qsTr("Idle. Nothing is intercepted.")
                                color: Theme.muted
                                font.pixelSize: Theme.fontSizeSmall
                                wrapMode: Text.WordWrap
                            }

                            AppButton {
                                text: probeArea.listening ? qsTr("Stop") : qsTr("Start listening (Ctrl+T)")
                                weight: probeArea.listening ? "plain" : "primary"
                                compact: true
                                onClicked: {
                                    if (probeArea.listening)
                                        capture.stopProbe()
                                    else
                                        probeArea.arm()
                                }
                            }
                        }
                    }
                }
            }

            // --- check without applying ----------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                spacing: Theme.space(1)

                SectionHeader {
                    Layout.fillWidth: true
                    title: qsTr("Check the configuration")
                    subtitle: qsTr("Runs validation, conflict detection and a real keymap compile without writing anything.")
                }

                Card {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space(1)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space(1)

                            Text {
                                Layout.fillWidth: true
                                text: page.checkReport.message || qsTr("Not checked yet.")
                                color: page.checkReport.ok === undefined ? Theme.muted
                                     : page.checkReport.ok ? Theme.success : Theme.error
                                font.pixelSize: Theme.fontSizeBody
                                wrapMode: Text.WordWrap
                            }

                            AppButton {
                                text: qsTr("Check now")
                                compact: true
                                enabled: !config.busy
                                onClicked: page.checkReport = config.preview()
                            }
                        }

                        Repeater {
                            model: page.checkReport.steps || []
                            delegate: Text {
                                required property string modelData
                                Layout.fillWidth: true
                                text: "· " + modelData
                                color: Theme.muted
                                font.pixelSize: Theme.fontSizeSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // --- the generated files, for inspection ---------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(3)
                Layout.rightMargin: Theme.space(3)
                spacing: Theme.space(1)

                RowLayout {
                    Layout.fillWidth: true

                    SectionHeader {
                        Layout.fillWidth: true
                        title: qsTr("Generated files")
                        subtitle: qsTr("Written for you. Here so you can check them, not so you have to edit them.")
                    }

                    AppButton {
                        text: filesView.visible ? qsTr("Hide") : qsTr("Show")
                        compact: true
                        onClicked: filesView.visible = !filesView.visible
                    }
                }

                ColumnLayout {
                    id: filesView
                    Layout.fillWidth: true
                    visible: false
                    spacing: Theme.space(1)

                    property var generated: ({})
                    onVisibleChanged: if (visible) generated = config.generatedPreview()

                    Text {
                        Layout.fillWidth: true
                        visible: filesView.generated.ok === false
                        text: filesView.generated.error || ""
                        color: Theme.error
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: filesView.generated.keymaps || []

                        delegate: Card {
                            required property var modelData
                            Layout.fillWidth: true

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space(0.75)

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.deviceName.length > 0
                                        ? modelData.deviceName : qsTr("All keyboards")
                                    color: Theme.foreground
                                    font.pixelSize: Theme.fontSizeBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: modelData.path.length > 0
                                    text: modelData.path
                                    color: Theme.muted
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.monoFamily
                                    elide: Text.ElideMiddle
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.space(24)
                                    visible: modelData.source.length > 0
                                    color: Theme.elevated
                                    radius: Theme.radiusSmall

                                    ScrollView {
                                        anchors.fill: parent
                                        anchors.margins: Theme.space(1)
                                        clip: true

                                        TextArea {
                                            text: modelData.source
                                            readOnly: true
                                            color: Theme.foreground
                                            font.pixelSize: Theme.fontSizeSmall
                                            font.family: Theme.monoFamily
                                            selectByMouse: true
                                            selectionColor: Theme.accented(0.35)
                                            background: null
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.space(3) }
        }
    }
}
