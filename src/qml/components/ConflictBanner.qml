import QtQuick
import QtQuick.Layouts
import "../"

// One conflict, explained. Every finding carries what is wrong and what to do
// about it, so the banner is never just a red mark.
Rectangle {
    id: root

    property string severity: "info"   // "info" | "warning" | "error"
    property string title: ""
    property string detail: ""
    property string suggestion: ""

    readonly property color tone: severity === "error" ? Theme.error
                                : severity === "warning" ? Theme.warning
                                : Theme.accent

    color: Theme.tinted(tone, 0.10)
    radius: Theme.radiusSmall
    border.width: 1
    border.color: Theme.tinted(tone, 0.45)

    implicitHeight: layout.implicitHeight + Theme.xxl

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.md
        spacing: Theme.lg

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 3
            implicitWidth: Theme.md
            implicitHeight: Theme.md
            radius: width / 2
            color: root.tone
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                Layout.fillWidth: true
                text: root.title
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: root.detail.length > 0
                text: root.detail
                color: Theme.foreground
                opacity: 0.85
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: root.suggestion.length > 0
                text: root.suggestion
                color: root.tone
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }
    }
}
