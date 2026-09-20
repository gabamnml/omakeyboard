import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OmaKeyboard
import "../"

// A searchable list over one of the system catalogs: actions, physical keys,
// remap targets, or keyboard options. Every picker in the app is this.
Dialog {
    id: root

    // "actions" | "keys" | "keysyms" | "xkbOptions" | "targets"
    property string source: "actions"
    // Only meaningful for "targets": narrows the ready-made behaviours to the
    // ones that apply to this physical key.
    property string forKey: ""
    property string heading: qsTr("Choose")
    property string selectedId: ""

    signal picked(string entryId, string label, string value, string kind)

    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.space(8) : 560, 620)
    height: Math.min(parent ? parent.height - Theme.space(8) : 520, 580)
    padding: 0

    onOpened: {
        search.text = ""
        catalog.query = ""
        search.forceActiveFocus()
    }

    background: Rectangle {
        color: Theme.background
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border
    }

    CatalogModel {
        id: catalog
        source: root.source
        forKey: root.forKey
    }

    // Let a caller read an entry back without walking the model.
    function entryFor(entryId) { return catalog.entry(entryId) }

    contentItem: ColumnLayout {
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.space(2)
            spacing: Theme.space(1.5)

            Text {
                text: root.heading
                color: Theme.foreground
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.DemiBold
            }

            FilterField {
                id: search
                Layout.fillWidth: true
                onTextChanged: catalog.query = text
                Keys.onDownPressed: list.forceActiveFocus()
                Keys.onReturnPressed: if (list.count > 0) list.choose(list.currentIndex)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: catalog
            currentIndex: 0
            boundsBehavior: Flickable.StopAtBounds

            // Selection by row index, so the keyboard path and the click path
            // both go through one place.
            function choose(index) {
                const entry = catalog.at(index)
                if (!entry || !entry.id)
                    return
                list.currentIndex = index
                root.picked(entry.id, entry.label, entry.value, entry.kind)
                root.close()
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            section.property: "category"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                required property string section
                width: list.width
                implicitHeight: Theme.space(3.5)
                color: Theme.background
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(2)
                    anchors.verticalCenter: parent.verticalCenter
                    text: section
                    color: Theme.muted
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.DemiBold
                }
            }

            delegate: ItemDelegate {
                id: row
                required property string entryId
                required property string label
                required property string detail
                required property string value
                required property string kind
                required property int index

                width: list.width
                implicitHeight: Theme.space(6)

                onClicked: list.choose(row.index)

                background: Rectangle {
                    color: row.hovered || list.currentIndex === row.index
                        ? Theme.accented(0.12) : "transparent"
                }

                contentItem: ColumnLayout {
                    spacing: 1

                    Text {
                        Layout.fillWidth: true
                        text: row.label
                        color: Theme.foreground
                        font.pixelSize: Theme.fontSizeBody
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: row.detail.length > 0 && row.detail !== row.label
                        text: row.detail
                        color: Theme.muted
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.monoFamily
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.space(1.5)

            Text {
                text: catalog.count === 1
                    ? qsTr("1 result")
                    : qsTr("%1 results").arg(catalog.count)
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Cancel")
                compact: true
                onClicked: root.close()
            }
        }
    }
}
