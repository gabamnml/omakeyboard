import QtQuick
import QtQuick.Layouts
import "../"

// The one container shape in the app: a flat panel with a hairline border.
// Omarchy's own surfaces are flat, so no shadows.
//
// The inner container is a ColumnLayout, not an Item: an Item does not take
// its size from its children, so the card would collapse to zero height and
// everything below it would be laid out on top of its content. Content placed
// in a Card is therefore a layout child -- use Layout.fillWidth, not
// anchors.fill.
Rectangle {
    id: root

    default property alias content: inner.data
    property int padding: Theme.panelPadding

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    implicitWidth: inner.implicitWidth + root.padding * 2
    implicitHeight: inner.implicitHeight + root.padding * 2

    ColumnLayout {
        id: inner
        anchors.fill: parent
        anchors.margins: root.padding
        spacing: Theme.rowGap
    }
}
