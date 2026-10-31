import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// What the editor's project title opens: everything that acts on the project as a whole.
//
// This replaces AndroidTopBar's 14-row overflow popup — a 220dp menu of unlabelled corner
// affordances that had to scroll in landscape. Open and New are tiles on Home → Projects;
// the app-level rows (theme, Extras, updates, Debug info) end the list.
AndroidBottomSheet {
    id: root

    signal picked(string actionId)

    title: qsTr("Project")

    // Derived from the row count, not from the column's implicitHeight: a Popup builds its
    // content lazily on first open, so the column still measures empty when the sheet takes
    // its resting height. Same reasoning as AndroidAddMenu.
    readonly property real desiredHeight: Theme.androidSheetHeaderHeight
                                          + root.visibleOptions.length * Theme.androidAddRowHeight
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
            id: "saveAs",
            label: qsTr("Save as"),
            detail: qsTr("Keep the original and carry on in a copy"),
            icon: Theme.icons.copy
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
            id: "crop",
            label: qsTr("Crop video size"),
            detail: qsTr("Drag the preview edges to change what’s included"),
            icon: Theme.icons.crop
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
            id: "extras",
            label: qsTr("Extras"),
            detail: qsTr("Add-ons and model packs"),
            icon: Theme.icons.puzzle
        },
        {
            id: "settings",
            label: qsTr("Settings"),
            detail: qsTr("Appearance, extras and agent access"),
            icon: Theme.icons.settings
        },
        {
            id: "theme",
            label: Theme.darkMode ? qsTr("Light mode") : qsTr("Dark mode"),
            detail: qsTr("Switch the app’s colour scheme"),
            icon: Theme.darkMode ? Theme.icons.sun : Theme.icons.moon
        },
        {
            id: "desktopLayout",
            label: qsTr("Switch to desktop layout"),
            detail: qsTr("Use the wide editor layout"),
            icon: Theme.icons.monitor
        },
        {
            id: "update",
            label: qsTr("Update available"),
            detail: qsTr("A newer version of Drift is ready"),
            icon: Theme.icons.download,
            shown: Updates.updateAvailable
        },
        {
            id: "debug",
            label: qsTr("Debug info"),
            detail: qsTr("Version, device and logs"),
            icon: Theme.icons.info
        },
        {
            id: "about",
            label: qsTr("About Drift"),
            detail: qsTr("Version, licence and credits"),
            icon: Theme.icons.info
        }
    ]

    readonly property var visibleOptions: options.filter(o => o.shown !== false)

    Flickable {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: optionColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: optionColumn
            width: scroll.width
            topPadding: Theme.spacingLg
            bottomPadding: Theme.spacingLg

            Repeater {
                model: root.visibleOptions

                delegate: SheetActionRow {
                    required property var modelData
                    width: optionColumn.width
                    label: modelData.label
                    detail: modelData.detail
                    glyph: modelData.icon
                    sideInset: root.safeLeft
                    sideInsetRight: root.safeRight
                    marked: (modelData.id === "save" && EditorState.projectFile.hasUnsavedChanges)
                            || (modelData.id === "extras" && ShellBridge.main?.addonAttentionNeeded)
                            || modelData.id === "update"

                    onClicked: {
                        Haptics.select()
                        root.dismiss()
                        root.picked(modelData.id)
                    }
                }
            }
        }
    }
}
