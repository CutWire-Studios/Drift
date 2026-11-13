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
    // Lists starred templates from every category; picking a category turns it off.
    property bool favoritesOnly: false
    property int favoritesTick: 0

    rejectText: fromSettings ? qsTr("Cancel") : qsTr("Decide later")
    closePolicy: fromSettings ? Popup.CloseOnEscape | Popup.CloseOnPressOutside : Popup.NoAutoClose

    // Catalog and arithmetic live in the LayoutPresets singleton; this dialog is one of two
    // views over them. Keeping the data here is what let the phone copy drift into a second,
    // disagreeing answer set.
    readonly property var categories: LayoutPresets.categories
    readonly property var templates: LayoutPresets.templates
    readonly property var qualities: LayoutPresets.qualities

    readonly property var categoryTemplates: {
        void favoritesTick
        const isFav = t => EditorState.isAssetFavorite("layouts", t.id)
        if (favoritesOnly)
            return LayoutPresets.templates.filter(isFav)
        const list = LayoutPresets.templatesFor(activeCategory)
        return list.filter(isFav).concat(list.filter(t => !isFav(t)))
    }
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
        favoritesOnly = false
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
        favoritesOnly = false
        activeCategory = "youtube"
        templateId = "yt_video"
        qualityId = "1080p"
    }

    function matchCurrentProject() {
        const match = LayoutPresets.matchProject(EditorState.projectFile.projectWidth(),
                                                 EditorState.projectFile.projectHeight())
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
        EditorState.projectFile.setProjectSetup(outWidth, outHeight, EditorState.projectFile.projectFps())
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

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() { root.favoritesTick++ }
    }

    // The question can be answered elsewhere while the first-run chooser is up — an MCP agent
    // setting the canvas, say. close() rather than reject() so this is not mistaken for
    // "Decide later". Gated on visible, not opened: opened only turns true once the enter
    // transition finishes, and an answer arriving mid-animation would otherwise be missed.
    Connections {
        target: EditorState
        enabled: root.visible && !root.fromSettings
        function onProjectLayoutChosenChanged() {
            if (EditorState.projectLayoutChosen)
                root.close()
        }
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

                IconButton {
                    glyph: Theme.icons.star
                    variant: "ghost"
                    buttonSize: Theme.controlHeight
                    iconSize: 14
                    active: root.favoritesOnly
                    tooltip: qsTr("Favorites")
                    onClicked: root.favoritesOnly = !root.favoritesOnly
                }

                Repeater {
                    model: root.categories
                    delegate: LayoutChooserCategoryButton { dialog: root }
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
            radius: Theme.c.layoutChooserDialog.listRadius
            color: Theme.c.layoutChooserDialog.listBackground
            border.width: Theme.c.layoutChooserDialog.listBorderWidth
            border.color: Theme.c.layoutChooserDialog.listBorder
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

                    delegate: LayoutChooserTemplateRow { dialog: root }
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
                    chipHeight: Theme.controlHeightSm
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
                radius: Theme.c.layoutChooserDialog.previewRadius
                color: Theme.c.layoutChooserDialog.previewBackground
                border.width: Theme.c.layoutChooserDialog.previewBorderWidth
                border.color: Theme.c.layoutChooserDialog.previewBorder

                Rectangle {
                    anchors.centerIn: parent
                    width: root.previewW
                    height: root.previewH
                    radius: Theme.c.layoutChooserDialog.frameRadius
                    color: Theme.c.layoutChooserDialog.frameBackground
                    border.width: Theme.c.layoutChooserDialog.frameBorderWidth
                    border.color: Theme.c.layoutChooserDialog.frameBorder

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
                        color: Theme.c.layoutChooserDialog.foreground
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
                        iconColor: Theme.c.layoutChooserDialog.icon
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
