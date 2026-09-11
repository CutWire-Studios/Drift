import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"

// What the bottom rail's [+] opens: the five things that put a new clip on the
// timeline, which used to be five permanent rail slots each.
//
// Sized to its own content rather than the standard 55% — a five-row menu that
// took over half the screen would hide the timeline it is adding to. Expanding is
// pinned to the same height for the same reason; dragging down still dismisses.
AndroidBottomSheet {
    id: root

    // Asset tab to open once a kind is chosen; AndroidEditor routes it to the
    // assets sheet exactly as the old rail tabs did.
    signal picked(string tabId)

    title: qsTr("Add to timeline")

    // Derived from the row count, NOT from optionColumn.implicitHeight: a Popup
    // builds its content lazily on first open, so the column still measured an
    // empty 16dp when the sheet took its resting height — which then froze at 103dp
    // with every row laid out below the bottom of the screen.
    readonly property real desiredHeight: Theme.androidSheetHeaderHeight
                                          + root.options.length * Theme.androidAddRowHeight
                                          + Theme.spacingLg * 2
                                          + Theme.spacing2xl
                                          + root.safeBottom
    sheetHeightFraction: root.height > 0
                         ? Math.min(Theme.androidSheetExpandedFraction,
                                    root.desiredHeight / root.height)
                         : Theme.androidSheetHeightFraction
    sheetExpandedFraction: sheetHeightFraction

    // Order follows the timeline layers you build in: footage first, then the
    // words over it, then the decoration. Mirrors AssetsPanel's tab order so the
    // sheet that opens next is not in a different sequence.
    readonly property var options: [
        {
            id: "media",
            label: qsTr("Media"),
            detail: qsTr("Video, photos and audio from this device"),
            icon: Theme.icons.film
        },
        {
            id: "market",
            label: qsTr("Market"),
            detail: qsTr("Stock photos, video and audio"),
            icon: Theme.icons.store
        },
        {
            id: "text",
            label: qsTr("Text"),
            detail: qsTr("A title or caption you type"),
            icon: Theme.icons.type
        },
        {
            id: "subtitles",
            label: qsTr("Subtitles"),
            detail: qsTr("Captions, generated or imported"),
            icon: Theme.icons.captions
        },
        {
            id: "stickers",
            label: qsTr("Stickers"),
            detail: qsTr("Emoji and sticker graphics"),
            icon: Theme.icons.smile
        },
        {
            id: "shapes",
            label: qsTr("Shapes"),
            detail: qsTr("Boxes, circles and lines"),
            icon: Theme.icons.shapes
        },
        {
            id: "templates",
            label: qsTr("Effect templates"),
            detail: qsTr("Saved stacks of effects to drop on a clip"),
            icon: Theme.icons.layers
        },
        {
            id: "scenes",
            label: qsTr("Scenes"),
            detail: qsTr("Jump between the sections of this edit"),
            icon: Theme.icons.listVideo
        },
        {
            id: "masks",
            label: qsTr("Masks"),
            detail: qsTr("Cut a shape or a subject out of the selected clip"),
            icon: Theme.icons.mask
        }
    ]

    Column {
        id: optionColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        topPadding: Theme.spacingLg
        bottomPadding: Theme.spacingLg

        Repeater {
            model: root.options

            delegate: SheetActionRow {
                required property var modelData
                width: optionColumn.width
                label: modelData.label
                detail: modelData.detail
                glyph: modelData.icon
                sideInset: root.safeLeft
                sideInsetRight: root.safeRight

                onClicked: {
                    Haptics.select()
                    root.dismiss()
                    root.picked(modelData.id)
                }
            }
        }
    }
}
