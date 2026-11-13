import QtQuick
import QtQuick.Controls.Basic
import Drift
// The phone view over LayoutPresets — what Android opens instead of the desktop
// LayoutChooserDialog. Same catalog, same arithmetic, laid out for a thumb: chips and
// aspect cards rather than a capped list inside a bordered box.
//
// The selection is previewed here and committed once, on Done. Applying each tap live
// reads better, but AppController::setProjectSetup pushes an undo command and rebases
// the clip layout on every distinct value, so tapping through six templates would leave
// six "Project setup" entries behind the sheet. Close (X) leaves the project untouched.
AndroidBottomSheet {
    id: root

    title: qsTr("Canvas & layout")
    doneText: qsTr("Done")
    // The point of committing on Done rather than on every tap is that you can watch the
    // canvas re-lay-out behind the sheet as you choose; that only works without a scrim.
    blocking: false

    // Distinguishes committing from dismissing. Closing the sheet any other way — X, Back, a
    // drag down — leaves the project on whatever it already had, so a caller that is waiting to
    // do something *because* a canvas was chosen must not hear about that.
    signal applied()

    onDoneRequested: {
        root.apply()
        root.applied()
        root.dismiss()
    }

    property string activeCategory: LayoutPresets.defaultCategoryId(true)
    property string templateId: LayoutPresets.defaultTemplateId(true)
    property string qualityId: "1080p"
    property int fps: 30
    // Only meaningful while the Custom template is selected.
    property int customWidth: 1080
    property int customHeight: 1920
    // Latched rather than derived from `fps`: a project already on 29 fps has to open on
    // the Custom chip, and typing 30 into the field must not then jump the selection.
    property bool customFps: false

    readonly property var fpsPresets: [24, 25, 30, 50, 60]

    // customTemplate is deliberately absent from LayoutPresets.templates — a free size needs
    // width and height fields to mean anything and the desktop dialog has none. This view has
    // them, so it appends it to the category it belongs to.
    property bool favoritesOnly: false
    property int favoritesTick: 0

    readonly property var categoryTemplates: {
        void root.favoritesTick
        const isFav = t => EditorState.isAssetFavorite("layouts", t.id)
        if (root.favoritesOnly)
            return LayoutPresets.templates.filter(isFav)
        const out = LayoutPresets.templatesFor(root.activeCategory)
        if (root.activeCategory === LayoutPresets.customTemplate.category)
            out.push(LayoutPresets.customTemplate)
        return out.filter(isFav).concat(out.filter(t => !isFav(t)))
    }

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() { root.favoritesTick++ }
    }

    readonly property var selectedTemplate: LayoutPresets.templateById(root.templateId)
    readonly property string aspect: selectedTemplate.aspect || "16:9"
    readonly property bool customSize: root.aspect === "custom"
    // "custom" is a free size, not a ratio, so it is not something to print as one.
    readonly property string aspectLabel: root.customSize ? qsTr("Custom") : root.aspect

    readonly property var outSize: LayoutPresets.sizeFor(root.templateId, root.qualityId,
                                                         root.customWidth, root.customHeight)
    readonly property int outWidth: outSize.width
    readonly property int outHeight: outSize.height

    function selectCategory(categoryId) {
        root.favoritesOnly = false
        root.activeCategory = categoryId
        if (root.selectedTemplate.category === categoryId)
            return
        const list = root.categoryTemplates
        if (list.length > 0)
            root.templateId = list[0].id
    }

    function openSheet() {
        const w = EditorState.projectFile.projectWidth()
        const h = EditorState.projectFile.projectHeight()
        const match = LayoutPresets.matchProject(w, h)
        const nearest = LayoutPresets.sizeFor(match.templateId, match.qualityId, w, h)
        root.customWidth = w
        root.customHeight = h
        root.qualityId = match.qualityId
        // matchProject returns the *nearest* preset, which for a canvas that is not one of
        // them is still a different size. Opening on it would make Done silently resize the
        // project; a canvas off the catalog is a custom canvas and says so.
        if (nearest.width === w && nearest.height === h) {
            root.templateId = match.templateId
            root.activeCategory = match.categoryId
        } else {
            root.templateId = LayoutPresets.customTemplate.id
            root.activeCategory = LayoutPresets.customTemplate.category
        }
        root.fps = EditorState.projectFile.projectFps()
        root.customFps = root.fpsPresets.indexOf(root.fps) < 0
        root.open()
    }

    function apply() {
        EditorState.projectFile.setProjectSetup(root.outWidth, root.outHeight, root.fps)
        EditorState.markProjectLayoutChosen()
    }

    // Section headings all sit on the same page margin; the scrolling chip rows below
    // them carry that margin as padding instead, so they can bleed to the screen edge.

    Item {
        anchors.fill: parent

        Flickable {
            id: scroll
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            clip: true
            contentWidth: width
            contentHeight: column.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: column
                width: scroll.width
                topPadding: Theme.spacingLg
                bottomPadding: Theme.spacingLg
                spacing: Theme.spacingXl

                // Category chips. Edge to edge so the row reads as scrollable, with the
                // page margin carried by the Row's own padding.
                Flickable {
                    width: parent.width
                    height: Theme.controlHeight
                    contentWidth: categoryRow.implicitWidth
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: categoryRow
                        height: parent.height
                        leftPadding: Theme.androidPagePadding
                        rightPadding: Theme.androidPagePadding
                        spacing: Theme.androidTouchGap

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
                            model: LayoutPresets.categories

                            delegate: ThemedChip {
                                required property var modelData
                                text: modelData.label
                                chipHeight: Theme.controlHeight
                                selected: modelData.id === root.activeCategory && !root.favoritesOnly
                                onClicked: root.selectCategory(modelData.id)
                            }
                        }
                    }
                }

                AndroidLayoutSectionLabel { text: qsTr("Template") }

                Flickable {
                    width: parent.width
                    height: Theme.androidLayoutCardHeight
                    contentWidth: templateRow.implicitWidth
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: templateRow
                        height: parent.height
                        leftPadding: Theme.androidPagePadding
                        rightPadding: Theme.androidPagePadding
                        spacing: Theme.androidTouchGap

                        Repeater {
                            model: root.categoryTemplates

                            delegate: AndroidLayoutTemplateCard {
                                selected: modelData.id === root.templateId
                                customWidth: root.customWidth
                                customHeight: root.customHeight
                                onChosen: (templateId) => root.templateId = templateId
                            }
                        }
                    }
                }

                // Free size: typed, so quality has nothing left to decide.
                Row {
                    x: Theme.androidPagePadding
                    width: parent.width - Theme.androidPagePadding * 2
                    spacing: Theme.spacingXl
                    visible: root.customSize

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: Theme.spacingSm

                        ThemedLabel { text: qsTr("Width"); size: "sm" }

                        ThemedNumberField {
                            width: parent.width
                            from: 16
                            to: 16384
                            step: 2
                            unit: "px"
                            value: root.customWidth
                            onEdited: v => root.customWidth = v
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: Theme.spacingSm

                        ThemedLabel { text: qsTr("Height"); size: "sm" }

                        ThemedNumberField {
                            width: parent.width
                            from: 16
                            to: 16384
                            step: 2
                            unit: "px"
                            value: root.customHeight
                            onEdited: v => root.customHeight = v
                        }
                    }
                }

                AndroidLayoutSectionLabel {
                    text: qsTr("Quality")
                    visible: !root.customSize
                }

                Flickable {
                    width: parent.width
                    height: Theme.controlHeightSm
                    contentWidth: qualityRow.implicitWidth
                    clip: true
                    visible: !root.customSize
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: qualityRow
                        height: parent.height
                        leftPadding: Theme.androidPagePadding
                        rightPadding: Theme.androidPagePadding
                        spacing: Theme.androidTouchGap

                        Repeater {
                            model: LayoutPresets.qualities

                            delegate: ThemedChip {
                                required property var modelData
                                text: modelData.label
                                chipHeight: Theme.controlHeightSm
                                selected: root.qualityId === modelData.id
                                onClicked: root.qualityId = modelData.id
                            }
                        }
                    }
                }

                AndroidLayoutSectionLabel { text: qsTr("Frames per second") }

                Flickable {
                    width: parent.width
                    height: Theme.controlHeightSm
                    contentWidth: fpsRow.implicitWidth
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: fpsRow
                        height: parent.height
                        leftPadding: Theme.androidPagePadding
                        rightPadding: Theme.androidPagePadding
                        spacing: Theme.androidTouchGap

                        Repeater {
                            model: root.fpsPresets

                            delegate: ThemedChip {
                                required property int modelData
                                text: String(modelData)
                                chipHeight: Theme.controlHeightSm
                                selected: !root.customFps && root.fps === modelData
                                onClicked: {
                                    root.customFps = false
                                    root.fps = modelData
                                }
                            }
                        }

                        ThemedChip {
                            text: qsTr("Custom")
                            chipHeight: Theme.controlHeightSm
                            selected: root.customFps
                            onClicked: root.customFps = true
                        }
                    }
                }

                ThemedNumberField {
                    x: Theme.androidPagePadding
                    width: (parent.width - Theme.androidPagePadding * 2 - Theme.spacingXl) / 2
                    visible: root.customFps
                    from: 1
                    to: 240
                    unit: "fps"
                    value: root.fps
                    onEdited: v => root.fps = v
                }

                // Summary: the one place the sheet states what Done will do. Ported from the
                // picker this replaces — the aspect morph is what makes a ratio change legible.
                Row {
                    x: Theme.androidPagePadding
                    width: parent.width - Theme.androidPagePadding * 2
                    spacing: Theme.spacingXl

                    AndroidLayoutSummarySwatch {
                        id: aspectBox
                        outWidth: root.outWidth
                        outHeight: root.outHeight
                        aspectLabel: root.aspectLabel
                    }

                    Column {
                        width: parent.width - aspectBox.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingSm

                        Row {
                            spacing: Theme.spacingLg

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: root.selectedTemplate.icon || ""
                                iconSize: Theme.iconSizeMd
                                iconColor: Theme.c.androidLayoutSheet.icon
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
                            text: qsTr("%1×%2 · %3 · %4 fps")
                                  .arg(root.outWidth)
                                  .arg(root.outHeight)
                                  .arg(root.aspectLabel)
                                  .arg(root.fps)
                        }
                    }
                }
            }
        }
    }
}
