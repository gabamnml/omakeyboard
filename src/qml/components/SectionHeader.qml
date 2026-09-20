import QtQuick
import QtQuick.Layouts
import "../"

ColumnLayout {
    id: root
    property string title: ""
    property string subtitle: ""
    // For a header sharing its row with buttons: one line each, elided,
    // rather than a title that grows taller as the buttons appear.
    property bool singleLine: false
    spacing: Theme.xxs

    Text {
        Layout.fillWidth: true
        text: root.title
        color: Theme.foreground
        font.pixelSize: Theme.fontSizeSubtitle
        font.weight: Font.DemiBold
        wrapMode: root.singleLine ? Text.NoWrap : Text.WordWrap
        elide: root.singleLine ? Text.ElideRight : Text.ElideNone
    }

    Text {
        Layout.fillWidth: true
        visible: root.subtitle.length > 0
        text: root.subtitle
        color: Theme.muted
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: root.singleLine ? Text.NoWrap : Text.WordWrap
        elide: root.singleLine ? Text.ElideRight : Text.ElideNone
    }
}
