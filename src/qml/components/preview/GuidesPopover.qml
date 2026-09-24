import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Guide sets and their items: which sets show, the library of custom sets, and
// per-item position, colour, opacity and lock. Built-in sets and sets that only
// came with the project are read-only here.
Popup {
    id: root

    property string selectedId: ""
    readonly property var sets: EditorState.guideSets
    readonly property var selected: {
        for (let i = 0; i < sets.length; ++i) {
            if (sets[i].id === selectedId)
                return sets[i]
        }
        return null
    }
    readonly property bool editable: selected !== null && selected.inLibrary

    width: Theme.dialogWidthSm
    padding: Theme.spacingMd
    // Not modal, and pressing the parent button does not count as outside, so that
    // button can toggle the popover instead of closing and reopening it.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    onAboutToShow: {
        if (selected)
            return
        const active = sets.filter(s => s.active)
        selectedId = active.length > 0 ? active[0].id : (sets.length > 0 ? sets[0].id : "")
    }

    background: Rectangle {
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
        radius: Theme.radiusMd
    }

    component SectionLabel: Text {
        width: parent ? parent.width : 0
        topPadding: Theme.spacingSm
        bottomPadding: Theme.spacingXs
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    component PercentField: ThemedNumberField {
        id: field
        property real fraction: 0
        signal committed(real fraction)
        width: 64
        decimals: 1
        step: 0.5
        from: 0
        unit: "%"
        Binding on value {
            when: !field.activeFocus
            value: field.fraction * 100
        }
        onEdited: v => field.committed(v / 100)
    }

    component ItemRow: Column {
        id: row
        required property var item
        readonly property string setId: root.selectedId

        function set(key, value) {
            EditorState.setGuideItemProperty(row.setId, row.item.id, key, value)
        }

        width: parent ? parent.width : 0
        spacing: Theme.spacingXs
        enabled: root.editable

        Item {
            width: parent.width
            height: Theme.iconButtonSize

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: row.item.kind === "v" ? qsTr("Vertical line")
                      : row.item.kind === "h" ? qsTr("Horizontal line")
                      : row.item.kind === "rect" ? qsTr("Margins")
                      : qsTr("Aspect frame")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.editable

                IconButton {
                    glyph: row.item.locked ? Theme.icons.lock : Theme.icons.lockOpen
                    tooltip: row.item.locked ? qsTr("Unlock so it can be dragged on the preview")
                                             : qsTr("Lock so it cannot be dragged on the preview")
                    active: row.item.locked
                    onClicked: row.set("locked", !row.item.locked)
                }
                IconButton {
                    glyph: Theme.icons.trash
                    tooltip: qsTr("Remove guide")
                    onClicked: EditorState.removeGuideItem(row.setId, row.item.id)
                }
            }
        }

        Row {
            spacing: Theme.spacingSm
            visible: row.item.kind === "v" || row.item.kind === "h"

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: row.item.kind === "v" ? qsTr("From left") : qsTr("From top")
            }
            PercentField {
                to: 100
                fraction: row.item.pos
                onCommitted: f => row.set("pos", f)
            }
        }

        Grid {
            columns: 2
            columnSpacing: Theme.spacingSm
            rowSpacing: Theme.spacingXs
            verticalItemAlignment: Grid.AlignVCenter
            visible: row.item.kind === "rect"

            Repeater {
                model: [
                    { key: "left", label: qsTr("Left") },
                    { key: "right", label: qsTr("Right") },
                    { key: "top", label: qsTr("Top") },
                    { key: "bottom", label: qsTr("Bottom") }
                ]
                Row {
                    required property var modelData
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        width: 44
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                    }
                    PercentField {
                        to: 50
                        fraction: row.item[modelData.key]
                        onCommitted: f => row.set(modelData.key, f)
                    }
                }
            }
        }

        Row {
            spacing: Theme.spacingSm
            visible: row.item.kind === "aspect"

            ThemedNumberField {
                id: aspectW
                width: 64
                decimals: 2
                from: 0.01
                to: 100
                Binding on value {
                    when: !aspectW.activeFocus
                    value: row.item.aspectW
                }
                onEdited: v => row.set("aspectW", v)
            }
            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: ":"
            }
            ThemedNumberField {
                id: aspectH
                width: 64
                decimals: 2
                from: 0.01
                to: 100
                Binding on value {
                    when: !aspectH.activeFocus
                    value: row.item.aspectH
                }
                onEdited: v => row.set("aspectH", v)
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingLg

            ColorSwatchField {
                id: colorField
                hex: row.item.color
                tooltip: qsTr("Guide colour")
                onEdited: v => row.set("color", v)
            }
            ThemedSlider {
                id: opacitySlider
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - colorField.width - parent.spacing
                label: qsTr("Opacity")
                valueFormatter: v => Math.round(v * 100) + "%"
                Binding on value {
                    when: !opacitySlider.pressed
                    value: row.item.opacity
                }
                onMoved: row.set("opacity", value)
            }
        }
    }

    contentItem: Column {
        spacing: Theme.spacingXs

        ThemedSwitch {
            width: parent.width
            text: qsTr("Show guides")
            checked: EditorState.guidesEnabled
            onToggled: EditorState.guidesEnabled = checked
        }

        SectionLabel { text: qsTr("Guide sets") }

        Flickable {
            width: parent.width
            height: Math.min(contentHeight, (Theme.controlHeight + Theme.spacingXs) * 6)
            contentHeight: setColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: setColumn
                width: parent.width
                spacing: 0

                Repeater {
                    model: root.sets

                    Rectangle {
                        id: setRow
                        required property var modelData
                        readonly property bool isSelected: modelData.id === root.selectedId

                        width: setColumn.width
                        height: Theme.controlHeight + Theme.spacingXs
                        radius: Theme.radiusSm
                        color: isSelected ? Theme.accent
                               : setHover.hovered ? Theme.popoverHover : "transparent"

                        HoverHandler { id: setHover }
                        TapHandler { onTapped: root.selectedId = setRow.modelData.id }

                        ThemedCheckBox {
                            id: activeBox
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            checked: setRow.modelData.active
                            tooltip: qsTr("Show this set")
                            onToggled: EditorState.setGuideSetActive(setRow.modelData.id, checked)
                        }

                        Text {
                            anchors.left: activeBox.right
                            anchors.right: originLabel.left
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            text: setRow.modelData.name
                            elide: Text.ElideRight
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        Text {
                            id: originLabel
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            text: setRow.modelData.builtIn ? qsTr("Built-in")
                                  : setRow.modelData.inLibrary ? "" : qsTr("From project")
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }
            }
        }

        Row {
            spacing: 0

            IconButton {
                glyph: Theme.icons.plus
                tooltip: qsTr("New guide set")
                onClicked: root.selectedId = EditorState.createGuideSet("")
            }
            IconButton {
                glyph: Theme.icons.copyPlus
                tooltip: qsTr("Duplicate set")
                enabled: root.selected !== null
                onClicked: root.selectedId = EditorState.duplicateGuideSet(root.selectedId)
            }
            IconButton {
                glyph: Theme.icons.download
                tooltip: qsTr("Save to my guide sets")
                visible: root.selected !== null && !root.selected.builtIn && !root.selected.inLibrary
                onClicked: EditorState.saveGuideSetToLibrary(root.selectedId)
            }
            IconButton {
                glyph: Theme.icons.trash
                tooltip: qsTr("Delete set")
                enabled: root.editable
                onClicked: {
                    EditorState.deleteGuideSet(root.selectedId)
                    root.selectedId = ""
                }
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            color: Theme.panelBorder
            visible: root.selected !== null
        }

        ThemedTextField {
            id: nameField
            width: parent.width
            visible: root.editable
            placeholderText: qsTr("Set name")
            Binding on text {
                when: !nameField.activeFocus
                value: root.selected ? root.selected.name : ""
            }
            onEditingFinished: EditorState.renameGuideSet(root.selectedId, text)
        }

        Text {
            width: parent.width
            visible: root.selected !== null && !root.editable
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingSm
            wrapMode: Text.WordWrap
            text: root.selected && root.selected.builtIn
                  ? qsTr("Built-in sets can't be changed. Duplicate one to make an editable copy.")
                  : qsTr("This set came with the project. Save it to your guide sets to edit it.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Flickable {
            width: parent.width
            height: Math.min(contentHeight, 320)
            visible: root.selected !== null
            contentHeight: itemColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: itemColumn
                width: parent.width
                spacing: Theme.spacingLg

                Repeater {
                    // A count, not the list: rows stay alive while their item's values
                    // change, so a dragged slider or focused field is not rebuilt under the user.
                    model: root.selected ? root.selected.items.length : 0

                    ItemRow {
                        required property int index
                        item: root.selected.items[index]
                    }
                }
            }
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingSm
            topPadding: Theme.spacingSm
            visible: root.editable

            Repeater {
                model: [
                    { kind: "v", label: qsTr("Vertical") },
                    { kind: "h", label: qsTr("Horizontal") },
                    { kind: "rect", label: qsTr("Margins") },
                    { kind: "aspect", label: qsTr("Frame") }
                ]
                ThemedButton {
                    required property var modelData
                    glyph: Theme.icons.plus
                    text: modelData.label
                    tooltip: qsTr("Add a guide")
                    onClicked: EditorState.addGuideItem(root.selectedId, modelData.kind)
                }
            }
        }
    }
}
