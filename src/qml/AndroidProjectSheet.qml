import QtQuick
import Drift
import "components"

// What the editor's project title opens: everything that acts on the project as a whole.
//
// This replaces AndroidTopBar's 14-row overflow popup — a 220dp menu of unlabelled corner
// affordances that had to scroll in landscape. The other eight rows did not move into a
// drawer, they went where they belonged: Open and New are tiles on Home → Projects, and
// theme, Extras, updates and Debug info are rows on Home → Me.
AndroidBottomSheet {
    id: root

    signal picked(string actionId)

    title: qsTr("Project")

    // Derived from the row count, not from the column's implicitHeight: a Popup builds its
    // content lazily on first open, so the column still measures empty when the sheet takes
    // its resting height. Same reasoning as AndroidAddMenu.
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

    // Ordered by what you came for: finish it, keep it, then the settings behind it.
    readonly property var options: [
        {
            id: "export",
            label: qsTr("Export"),
            detail: qsTr("Render the finished video"),
            icon: Theme.icons.upload
        },
        {
            id: "save",
            label: qsTr("Save"),
            detail: qsTr("Keep this project on the device"),
            icon: Theme.icons.save
        },
        {
            id: "package",
            label: qsTr("Share a copy"),
            detail: qsTr("One file with the media packed inside"),
            icon: Theme.icons.package
        },
        {
            id: "layout",
            label: qsTr("Canvas & layout"),
            detail: qsTr("Video size, aspect and frame rate"),
            icon: Theme.icons.ratio
        },
        {
            id: "properties",
            label: qsTr("Project properties"),
            detail: qsTr("Name, resolution and timebase"),
            icon: Theme.icons.info
        },
        {
            id: "multicam",
            label: qsTr("Multicam"),
            detail: qsTr("Sync and switch between angles"),
            icon: Theme.icons.shuffle
        },
        {
            id: "settings",
            label: qsTr("App settings"),
            detail: qsTr("Appearance, extras and agent access"),
            icon: Theme.icons.settings
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
                marked: modelData.id === "save" && EditorState.hasUnsavedChanges

                onClicked: {
                    Haptics.select()
                    root.dismiss()
                    root.picked(modelData.id)
                }
            }
        }
    }
}
