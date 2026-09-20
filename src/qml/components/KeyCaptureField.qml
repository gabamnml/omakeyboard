import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../"

// "Press a key". There is no keyboard grab on Hyprland, so this reads whatever
// the compositor forwards: a combination it already has bound is consumed
// first and never arrives. The hint says so, and the key list is the way to
// reach those.
Item {
    id: root

    // "key" captures a physical key; "shortcut" captures a combination.
    property string mode: "key"
    property string value: ""        // XKB code, or the binding spelling
    property string label: ""        // what to show
    property string placeholder: qsTr("Not set")

    signal captured(string value, string label)
    signal browseRequested()

    // Why the last keystroke could not be used, when it could not be.
    property string problem: ""

    implicitHeight: Theme.space(5)
    implicitWidth: Theme.space(30)

    // Same reasoning as the key tester: a button click moves focus, so whether
    // this field is the one listening cannot be read off activeFocus.
    property bool armed: false
    readonly property bool listening: root.armed && capture.capturing

    function start() {
        root.problem = ""
        root.armed = true
        root.forceActiveFocus()
        capture.start(root.mode)
    }

    // Opening this from a button leaves the focus on that button, and a Popup
    // assigns focus of its own after it opens. Keys only reach an item that
    // has focus, so while armed the field takes it back -- otherwise the
    // interface says "Listening" while the keystrokes go somewhere else.
    onActiveFocusChanged: {
        if (root.armed && !root.activeFocus && capture.capturing)
            Qt.callLater(root.forceActiveFocus)
    }

    // The service reports a capture by first turning `capturing` off and then
    // emitting the result. Disarming on that state change would therefore
    // switch this handler off one signal before the result arrives -- so the
    // guard lives inside each handler, and the disarm for the cases that end
    // without a result (cancel, timeout) is deferred to the next turn.
    Connections {
        target: capture

        function onCapturingChanged() {
            if (!capture.capturing)
                Qt.callLater(root.disarmIfIdle)
        }

        function onKeyCaptured(code, label) {
            if (!root.armed || root.mode !== "key")
                return
            root.armed = false
            root.value = code
            root.label = label
            root.captured(code, label)
        }

        function onShortcutCaptured(keys) {
            if (!root.armed || root.mode !== "shortcut")
                return
            root.armed = false
            root.value = keys
            root.label = keys
            root.captured(keys, keys)
        }

        function onCaptureFailed(reason) {
            if (root.armed)
                root.problem = reason
        }
    }

    function disarmIfIdle() {
        if (!capture.capturing)
            root.armed = false
    }

    // Every key event goes to the service, which decides what to do with it.
    Keys.onPressed: function (event) {
        if (!capture.capturing)
            return
        if (capture.handleKey(event.nativeScanCode, event.key, event.modifiers))
            event.accepted = true
        else
            event.accepted = true   // swallow partial chords too
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.space(1)

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: Theme.elevated
            border.width: 1
            border.color: root.listening ? Theme.accent : Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space(1.5)
                anchors.rightMargin: Theme.space(1)
                spacing: Theme.space(1)

                Text {
                    visible: root.listening
                    text: qsTr("Listening…")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeBody

                    SequentialAnimation on opacity {
                        running: root.listening
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.4; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }

                KeyCap {
                    visible: !root.listening && root.label.length > 0
                    text: root.label
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    visible: !root.listening && root.label.length === 0
                    text: root.placeholder
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeBody
                }

                Item { Layout.fillWidth: true }
            }

            TapHandler {
                onTapped: root.start()
            }
        }

        AppButton {
            text: root.listening ? qsTr("Cancel") : qsTr("Press a key")
            compact: true
            onClicked: {
                if (root.listening)
                    capture.cancel()
                else
                    root.start()
            }
        }

        AppButton {
            text: qsTr("Browse")
            compact: true
            onClicked: root.browseRequested()
        }
    }
}
