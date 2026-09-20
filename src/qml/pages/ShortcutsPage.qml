import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."
import "../components"

Item {
    id: page

    // Shaped for RecipeCard: the combination it would bind and what it does.
    property var recipes: []
    function refreshRecipes() {
        page.recipes = config.shortcutRecipeList().map(function (recipe) {
            return {
                id: recipe.id,
                title: recipe.title,
                detail: recipe.detail,
                combo: recipe.keys,
                outcome: recipe.action,
                available: recipe.available,
                reason: recipe.reason,
            }
        })
    }
    Component.onCompleted: refreshRecipes()
    Connections {
        target: shortcutModel
        function onCountChanged() { page.refreshRecipes() }
    }

    Shortcut {
        sequences: ["Ctrl+N"]
        enabled: page.visible
        onActivated: shortcutEditor.openForNew()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panelPadding
        spacing: Theme.panelGap

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: qsTr("Your shortcuts")
                color: Theme.muted
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Recipes")
                compact: true
                visible: shortcutList.count > 0
                onClicked: recipeDialog.open()
            }

            AppButton {
                text: qsTr("See what is already bound")
                compact: true
                onClicked: existingDialog.open()
            }

            AppButton {
                text: qsTr("Add shortcut")
                weight: "primary"
                compact: true
                enabled: config.capabilities.shortcuts
                onClicked: shortcutEditor.openForNew()
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ListView {
                id: shortcutList
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: shortcutList.count > 0
                clip: true
                model: shortcutModel
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                add: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
                        NumberAnimation { property: "x"; from: -Theme.xxl; to: 0; duration: Theme.durationSlow; easing.type: Theme.easing }
                    }
                }
                remove: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
                        NumberAnimation { property: "x"; to: Theme.xxl; duration: Theme.durationMedium; easing.type: Theme.easing }
                    }
                }
                displaced: Transition {
                    NumberAnimation { properties: "x,y"; duration: Theme.durationMedium; easing.type: Theme.easing }
                    NumberAnimation { property: "opacity"; to: 1; duration: Theme.durationFast }
                }

                delegate: Item {
                    id: row
                    required property string shortcutId
                    required property bool isEnabled
                    required property string keys
                    required property string description
                    required property string actionKind
                    required property string actionLabel
                    required property string actionValue
                    required property bool unbindFirst

                    width: shortcutList.width
                    implicitHeight: Theme.rowHeight

                    Rectangle {
                        anchors.fill: parent
                        color: hover.hovered ? Theme.accented(0.05) : "transparent"
                    }
                    HoverHandler { id: hover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.rowPaddingX
                        anchors.rightMargin: Theme.md
                        spacing: Theme.rowGap

                        KeyCap {
                            text: row.keys
                            dimmed: !row.isEnabled
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: Theme.space(24)
                        }

                        Text {
                            text: "→"
                            color: Theme.muted
                            font.pixelSize: Theme.fontSizeBody
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: row.description.length > 0
                                    ? row.description : row.actionValue
                                color: row.isEnabled ? Theme.foreground : Theme.muted
                                font.pixelSize: Theme.fontSizeBody
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: row.actionLabel + " · " + row.actionValue
                                color: Theme.muted
                                font.pixelSize: Theme.fontSizeSmall
                                elide: Text.ElideRight
                            }
                        }

                        ToggleSwitch {
                            checked: row.isEnabled
                            onToggled: config.setShortcutEnabled(row.shortcutId, checked)
                        }

                        IconButton {
                            glyph: "edit"
                            tip: qsTr("Edit")
                            onClicked: shortcutEditor.openForExisting(row)
                        }

                        IconButton {
                            glyph: "trash"
                            tip: qsTr("Delete")
                            danger: true
                            onClicked: config.removeShortcut(row.shortcutId)
                        }
                    }
                }

            }

            // Empty state: a few shortcuts Omarchy does not have yet, each on
            // a key combination that is still free on this system.
            Flickable {
                id: emptyFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: shortcutList.count === 0
                contentHeight: emptyState.implicitHeight + Theme.huge * 2
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: emptyState
                    width: Math.min(parent.width - Theme.huge * 2, Theme.px(620))
                    x: (parent.width - width) / 2
                    y: Math.max(Theme.huge, (emptyFlick.height - implicitHeight) / 2)
                    spacing: Theme.panelGap

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.xs

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Add a shortcut Omarchy doesn't have")
                            color: Theme.foreground
                            font.pixelSize: Theme.fontSizeHeading
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Each one uses a combination nothing is bound to yet. Omarchy's own shortcuts keep working.")
                            color: Theme.muted
                            font.pixelSize: Theme.fontSizeSmall
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: emptyState.width > Theme.px(480) ? 2 : 1
                        rowSpacing: Theme.lg
                        columnSpacing: Theme.lg

                        Repeater {
                            model: page.recipes
                            delegate: RecipeCard {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                recipe: modelData
                                onChosen: function (recipeId) { config.addShortcutRecipe(recipeId) }
                            }
                        }
                    }

                    AppButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Or make your own")
                        enabled: config.capabilities.shortcuts
                        onClicked: shortcutEditor.openForNew()
                    }
                }
            }
        }
    }

    function newMapping() { shortcutEditor.openForNew() }
    function showRecipes() { page.refreshRecipes(); recipeDialog.open() }

    ShortcutEditor {
        id: shortcutEditor
        parent: Overlay.overlay
    }

    // Everything currently bound, recovered from the Lua configuration. Useful
    // before inventing a combination, and the only place these are visible
    // together with their actions.
    Dialog {
        id: existingDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(8) : 620, 680)
        height: Math.min(parent ? parent.height - Theme.space(8) : 560, 620)
        padding: 0

        background: Rectangle {
            color: Theme.background
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.space(2)
                spacing: Theme.space(1.5)

                SectionHeader {
                    Layout.fillWidth: true
                    title: qsTr("Already bound")
                    subtitle: qsTr("Read from your Hyprland configuration, Omarchy's defaults included.")
                }

                FilterField {
                    id: bindingSearch
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search bindings")
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

            ListView {
                id: bindingList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                model: {
                    const needle = bindingSearch.text.trim().toLowerCase()
                    const all = config.existingBindings()
                    if (needle.length === 0)
                        return all
                    return all.filter(function (entry) {
                        return (entry.keys + " " + entry.description + " " + entry.action)
                            .toLowerCase().indexOf(needle) >= 0
                    })
                }

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    required property var modelData
                    // A delegate's parent is the view's contentItem, which does
                    // not carry the view's width -- taking it from there gives
                    // zero-width rows and a list that looks empty.
                    width: bindingList.width
                    implicitHeight: Theme.space(6.5)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space(2)
                        anchors.rightMargin: Theme.space(2)
                        spacing: Theme.space(1.5)

                        KeyCap {
                            text: modelData.keys
                            Layout.preferredWidth: Theme.space(26)
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: modelData.description.length > 0
                                    ? modelData.description : qsTr("(no description)")
                                color: Theme.foreground
                                font.pixelSize: Theme.fontSizeBody
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.action
                                color: Theme.muted
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.monoFamily
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            visible: modelData.fromOmaKeyboard
                            text: qsTr("yours")
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.space(1.5)
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Close")
                    compact: true
                    onClicked: existingDialog.close()
                }
            }
        }
    }

    Dialog {
        id: recipeDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.space(8) : 640, Theme.px(660))
        padding: Theme.panelPadding + Theme.xs
        onAboutToShow: page.refreshRecipes()

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
                NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationMedium; easing.type: Theme.easing }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
        }

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
                title: qsTr("Recipes")
                subtitle: qsTr("Shortcuts Omarchy does not have, on combinations that are still free.")
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Theme.lg
                columnSpacing: Theme.lg

                Repeater {
                    model: page.recipes
                    delegate: RecipeCard {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        recipe: modelData
                        onChosen: function (recipeId) {
                            config.addShortcutRecipe(recipeId)
                            recipeDialog.close()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Close")
                    compact: true
                    onClicked: recipeDialog.close()
                }
            }
        }
    }
}
