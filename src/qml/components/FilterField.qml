import QtQuick
import QtQuick.Controls.Basic
import "../"

// A search box in the theme's colours. Not called SearchField: Qt 6.10 ships
// a control by that name, and depending on import order a page would silently
// get Qt's version -- grey, with icons drawn for a different theme.
TextField {
    id: control
    placeholderText: qsTr("Search")

    color: Theme.foreground
    placeholderTextColor: Theme.muted
    font.pixelSize: Theme.fontSizeBody
    selectByMouse: true
    selectionColor: Theme.accented(0.35)
    selectedTextColor: Theme.foreground

    readonly property int iconRoom: Math.round(Theme.controlHeight * 0.95)
    leftPadding: control.iconRoom
    rightPadding: control.text.length > 0 ? control.iconRoom : Theme.controlPaddingX
    topPadding: Theme.inputPaddingY
    bottomPadding: Theme.inputPaddingY
    implicitHeight: Theme.controlHeight

    // Escape clears first, and only an empty field lets it through to close
    // whatever the field sits in.
    Keys.onEscapePressed: function (event) {
        if (control.text.length > 0) {
            control.clear()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.elevated
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        // The magnifier takes the accent once the field is in use, so it
        // stays visible on dark themes instead of fading into the fill.
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: control.iconRoom
            horizontalAlignment: Text.AlignHCenter
            text: "\u{F0349}"
            font.family: Theme.monoFamily
            font.pixelSize: Math.round(Theme.fontSizeBody * 1.3)
            color: control.activeFocus || control.text.length > 0 ? Theme.accent : Theme.foreground
            opacity: control.activeFocus || control.text.length > 0 ? 1 : 0.7
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
    }

    // Clear, only when there is something to clear.
    AbstractButton {
        id: clearButton
        visible: control.text.length > 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: control.iconRoom
        height: control.height
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        onClicked: {
            control.clear()
            control.forceActiveFocus()
        }

        contentItem: Text {
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: "\u{F0159}"
            font.family: Theme.monoFamily
            font.pixelSize: Math.round(Theme.fontSizeBody * 1.2)
            color: clearButton.hovered ? Theme.foreground : Theme.muted
        }
        background: null

        ToolTip.visible: clearButton.hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Clear (Esc)")
    }
}
