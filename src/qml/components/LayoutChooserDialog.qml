import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown on a fresh empty project so the user picks a platform canvas before editing.
// Category chips → template list (icon + label). "Decide later" keeps the default size
// and marks the layout chosen so neither this dialog nor ProjectSetupDialog reappear
// for the current project (New Project clears that and asks again).
ThemedDialog {
    id: root

    title: qsTr("Choose your video layout")
    acceptText: qsTr("Continue")
    rejectVariant: "secondary"
    preferredWidth: Theme.dialogWidthLg

    property string activeCategory: "youtube"
    property string templateId: "yt_video"
    property string qualityId: "1080p"
    // First-launch uses "Decide later"; Settings reopen uses "Cancel".
    property bool fromSettings: false

    rejectText: fromSettings ? qsTr("Cancel") : qsTr("Decide later")
    closePolicy: fromSettings ? Popup.CloseOnEscape | Popup.CloseOnPressOutside : Popup.NoAutoClose

    // Catalog and arithmetic live in the LayoutPresets singleton; this dialog is one of two
    // views over them. Keeping the data here is what let the phone copy drift into a second,
    // disagreeing answer set.
    readonly property var categories: LayoutPresets.categories
    readonly property var templates: LayoutPresets.templates
    readonly property var qualities: LayoutPresets.qualities

    readonly property var categoryTemplates: LayoutPresets.templatesFor(activeCategory)
    readonly property var selectedTemplate: LayoutPresets.templateById(templateId)
    readonly property var selectedQuality: LayoutPresets.qualityById(qualityId)

    readonly property string aspect: selectedTemplate.aspect || "16:9"
    readonly property var outSize: LayoutPresets.sizeFor(templateId, qualityId, 0, 0)
    readonly property int outWidth: outSize.width
    readonly property int outHeight: outSize.height
    readonly property int qualityEdge: selectedQuality.shortEdge

    readonly property real aspectBoxSize: 88
    readonly property real aspectScale: {
        const w = Math.max(1, outWidth)
        const h = Math.max(1, outHeight)
        return Math.min(aspectBoxSize / w, aspectBoxSize / h)
    }
    readonly property real previewW: Math.max(8, outWidth * aspectScale)
    readonly property real previewH: Math.max(8, outHeight * aspectScale)

    function selectCategory(categoryId) {
        activeCategory = categoryId
        if (selectedTemplate.category === categoryId)
            return
        for (let i = 0; i < templates.length; ++i) {
            if (templates[i].category === categoryId) {
                templateId = templates[i].id
                return
            }
        }
    }

    function resetSelection() {
        activeCategory = "youtube"
        templateId = "yt_video"
        qualityId = "1080p"
    }

    function matchCurrentProject() {
        const match = LayoutPresets.matchProject(EditorState.projectWidth(),
                                                 EditorState.projectHeight())
        templateId = match.templateId
        activeCategory = match.categoryId
        qualityId = match.qualityId
    }

    function openChooser() {
        fromSettings = false
        resetSelection()
        open()
    }

    function openFromSettings() {
        fromSettings = true
        matchCurrentProject()
        open()
    }

    onAccepted: {
        EditorState.setProjectSetup(outWidth, outHeight, EditorState.projectFps())
        EditorState.markProjectLayoutChosen()
    }

    // First-launch dismiss. This used to call markProjectLayoutChosen(), which stopped
    // the chooser reopening and let the essential-packs nudge follow — but the same
    // flag also gates ProjectSetupDialog, so "Decide later" silently meant "never ask",
    // and dropping the first clip no longer offered to set the canvas up. The canvas is
    // still undecided here, so report the dismissal instead of faking a decision.
    signal firstRunDismissed()

    onRejected: {
        if (!fromSettings)
            root.firstRunDismissed()
    }

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 600

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: fromSettings
                  ? qsTr("Pick a platform template and quality. This updates the project video size.")
                  : qsTr("Pick a category, then a template and quality. You can change this anytime in Settings → Choose layout.")
        }

        Flickable {
            id: categoryFlick
            width: parent.width
            height: Theme.controlHeight
            contentWidth: categoryRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: categoryRow
                height: parent.height
                spacing: Theme.spacingMd

                Repeater {
                    model: root.categories
                    delegate: AbstractButton {
                        id: catBtn
                        required property var modelData

                        // Match ThemedChip / quality pills: Control padding owns the
                        // inset so icon+label stay optically centered in the pill.
                        height: Theme.controlHeight
                        horizontalPadding: Theme.spacingXl
                        verticalPadding: 0
                        spacing: Theme.spacingMd
                        implicitWidth: leftPadding + catContent.implicitWidth + rightPadding
                        checkable: false
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus

                        readonly property bool selected: modelData.id === root.activeCategory

                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: {
                                if (catBtn.selected)
                                    return Theme.panelSecondaryBg
                                if (catBtn.down)
                                    return Theme.panelMuted
                                if (catBtn.hovered)
                                    return Theme.popoverHover
                                return Theme.panelAccent
                            }
                            border.width: Theme.borderWidth
                            border.color: catBtn.selected
                                          ? Theme.panelSecondaryBorder
                                          : Theme.panelBorder
                        }

                        contentItem: Item {
                            id: catContent
                            implicitWidth: catRow.implicitWidth
                            implicitHeight: Math.max(catIcon.implicitHeight, catLabel.implicitHeight)

                            Row {
                                id: catRow
                                anchors.centerIn: parent
                                spacing: catBtn.spacing

                                IconGlyph {
                                    id: catIcon
                                    anchors.verticalCenter: parent.verticalCenter
                                    glyph: catBtn.modelData.icon
                                    iconSize: Theme.fontSizeSm
                                    iconColor: catBtn.selected
                                               ? Theme.panelSecondaryForeground
                                               : Theme.panelForeground
                                }

                                Text {
                                    id: catLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    // Match icon box height so verticalCenter lines up
                                    // with the glyph, not the taller font metrics box.
                                    height: catIcon.iconSize
                                    verticalAlignment: Text.AlignVCenter
                                    text: catBtn.modelData.label
                                    color: catBtn.selected
                                           ? Theme.panelSecondaryForeground
                                           : Theme.panelForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    font.weight: catBtn.selected ? Font.Medium : Font.Normal
                                }
                            }
                        }

                        onClicked: root.selectCategory(modelData.id)

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.NoButton
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }
        }

        ThemedLabel {
            text: qsTr("Template")
            tone: "default"
            size: "sm"
        }

        Rectangle {
            width: parent.width
            height: Math.min(220, Math.max(44, root.categoryTemplates.length * 44) + 2)
            radius: Theme.radiusSm
            color: Theme.appBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            clip: true

            ListView {
                id: templateList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.categoryTemplates
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: templateList.width
                    height: 44
                    highlighted: modelData.id === root.templateId
                    hoverEnabled: true

                    background: Rectangle {
                        color: {
                            if (row.highlighted)
                                return Theme.panelSecondaryBg
                            if (row.hovered)
                                return Theme.popoverHover
                            return "transparent"
                        }
                    }

                    contentItem: Item {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10

                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingLg

                            IconGlyph {
                                id: templateIcon
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: row.modelData.icon
                                iconSize: 16
                                iconColor: row.highlighted
                                           ? Theme.panelSecondaryForeground
                                           : Theme.panelForeground
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1

                                Text {
                                    text: row.modelData.label
                                    color: row.highlighted
                                           ? Theme.panelSecondaryForeground
                                           : Theme.panelForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                    font.weight: Font.Medium
                                }

                                Text {
                                    text: row.modelData.detail
                                    color: Theme.mutedForeground
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }
                        }

                        // Mini aspect-ratio box on the right of each row.
                        Item {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 28

                            readonly property real aw: {
                                switch (row.modelData.aspect) {
                                case "9:16": return 12
                                case "4:5": return 16
                                case "1:1": return 20
                                default: return 26
                                }
                            }
                            readonly property real ah: {
                                switch (row.modelData.aspect) {
                                case "9:16": return 22
                                case "4:5": return 20
                                case "1:1": return 20
                                default: return 15
                                }
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.aw
                                height: parent.ah
                                radius: 2
                                color: "transparent"
                                border.width: Theme.borderWidth
                                border.color: row.highlighted ? Theme.primary : Theme.panelBorder
                            }
                        }
                    }

                    onClicked: root.templateId = modelData.id
                }
            }
        }

        ThemedLabel {
            text: qsTr("Quality")
            tone: "default"
            size: "sm"
        }

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: root.qualities
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: root.qualityId === modelData.id
                    chipHeight: 28
                    onClicked: root.qualityId = modelData.id
                }
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingXl

            Rectangle {
                width: root.aspectBoxSize
                height: root.aspectBoxSize
                radius: Theme.radiusSm
                color: Theme.appBackground
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder

                Rectangle {
                    anchors.centerIn: parent
                    width: root.previewW
                    height: root.previewH
                    radius: 3
                    color: Theme.panelAccent
                    border.width: Theme.borderWidth
                    border.color: Theme.primary

                    // A morph between two aspect ratios, not an entrance.
                    Behavior on width {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easingInOut }
                    }
                    Behavior on height {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easingInOut }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: root.aspect
                        color: Theme.panelForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                    }
                }
            }

            Column {
                width: parent.width - root.aspectBoxSize - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Row {
                    spacing: 8

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: root.selectedTemplate.icon || ""
                        iconSize: 16
                        iconColor: Theme.panelForeground
                    }

                    ThemedLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        tone: "default"
                        size: "sm"
                        text: root.selectedTemplate.label
                    }
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    font.family: Theme.monoFontFamily
                    text: qsTr("%1×%2 · %3")
                          .arg(root.outWidth)
                          .arg(root.outHeight)
                          .arg(root.aspect)
                }

                ThemedLabel {
                    width: parent.width
                    size: "xs"
                    text: qsTr("Preview shows the canvas aspect ratio")
                }
            }
        }
    }
}
