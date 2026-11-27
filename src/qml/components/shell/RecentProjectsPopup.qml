import QtQuick
import QtQuick.Controls.Basic
import Drift

// Project switcher dropdown: current project, recent projects to switch to, and
// actions to start or open another. Saved status uses green/red dots on the
// current project (and green on disk-backed recents). Signals are handled by
// EditorHeader, which owns the file dialogs.
Popup {
    id: root

    signal openFileRequested()
    signal newProjectRequested()
    signal closeProjectRequested()
    signal openRecentRequested(string path)
    signal saveAsRequested()
    signal exportRequested()
    signal renderQueueRequested()
    signal packageRequested()
    signal saveJsonRequested()
    signal openJsonRequested()
    signal openFolderRequested()
    signal saveFolderRequested()
    signal exportBundleRequested()
    signal renderTemplateRequested()
    signal importExportRequested()
    signal propertiesRequested()
    signal healthRequested()


    // Shared row for current / previous projects: status dot, title (+ optional
    // subtitle), and an optional trailing checkmark for the active project.
    // When removable, a hover-visible × removes the entry from recents (not disk).

    width: Theme.dialogWidthSm
    padding: Theme.spacingMd
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var items: EditorState.projectFile.recentProjects
    readonly property string currentPath: EditorState.projectFile.currentProjectPath || ""
    readonly property bool currentSaved: !EditorState.projectFile.hasUnsavedChanges

    // Recents excluding the open project — those are the ones you can switch to.
    readonly property var previousItems: {
        const path = root.currentPath
        const all = root.items
        const out = []
        for (let i = 0; i < all.length; ++i) {
            if (all[i].path !== path)
                out.push(all[i])
        }
        return out
    }

    background: Rectangle {
        color: Theme.c.recentProjectsPopup.background
        border.width: Theme.c.recentProjectsPopup.borderWidth
        border.color: Theme.c.recentProjectsPopup.border
        radius: Theme.c.recentProjectsPopup.radius
    }

    // Opacity plus scale, matching ThemedContextMenu.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 1.0
                to: 0.96
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    contentItem: Column {
        spacing: Theme.spacingXs

        // --- Current project -------------------------------------------------
        RecentProjectsProjectRow {
            popup: root
            modelData: ({})
            index: -1
            title: EditorState.projectFile.projectName
            subtitle: root.currentSaved ? qsTr("All changes saved")
                                        : qsTr("Unsaved changes")
            tip: root.currentPath.length > 0 ? root.currentPath : ""
            statusColor: root.currentSaved ? Theme.c.recentProjectsPopup.statusSaved
                                           : Theme.c.recentProjectsPopup.statusUnsaved
            showCheck: true
            interactive: false
        }

        // --- Previous projects -----------------------------------------------
        Text {
            width: parent.width
            leftPadding: Theme.spacingLg
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingXs
            visible: root.previousItems.length > 0
            text: qsTr("Previous projects")
            color: Theme.c.recentProjectsPopup.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }

        Text {
            width: parent.width
            leftPadding: Theme.spacingLg
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingSm
            visible: root.previousItems.length === 0
            text: qsTr("No previous projects")
            color: Theme.c.recentProjectsPopup.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        ListView {
            id: previousList
            width: parent.width
            height: Math.min(contentHeight, 48 * 5)
            visible: root.previousItems.length > 0
            clip: true
            model: root.previousItems
            keyNavigationEnabled: true
            focus: true
            currentIndex: -1

            ScrollBar.vertical: AppScrollBar { }

            Keys.onReturnPressed: previousList.openCurrent()
            Keys.onEnterPressed: previousList.openCurrent()

            function openCurrent() {
                if (currentIndex < 0 || currentIndex >= root.previousItems.length)
                    return
                const entry = root.previousItems[currentIndex]
                if (!entry.exists)
                    return
                root.close()
                root.openRecentRequested(entry.path)
            }

            delegate: RecentProjectsProjectRow {
                popup: root
                id: previousRow

                width: ListView.view.width
                title: previousRow.modelData.name
                       + (previousRow.modelData.exists ? "" : qsTr(" (missing)"))
                subtitle: previousRow.modelData.path
                tip: previousRow.modelData.exists
                     ? previousRow.modelData.path
                     : qsTr("This file has been moved or deleted:\n%1").arg(previousRow.modelData.path)
                statusColor: previousRow.modelData.exists ? Theme.c.recentProjectsPopup.statusExists
                                                          : Theme.c.recentProjectsPopup.statusMissing
                interactive: previousRow.modelData.exists
                removable: true
                highlighted: previousList.currentIndex === index

                onTriggered: root.openRecentRequested(previousRow.modelData.path)
                onRemoveRequested: EditorState.projectFile.removeRecentProject(previousRow.modelData.path)
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.c.recentProjectsPopup.borderWidth
            color: Theme.c.recentProjectsPopup.divider
            opacity: 0.5
        }

        // --- Create / open ---------------------------------------------------
        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.plus
            text: qsTr("New project")
            shortcutId: "newProject"
            onTriggered: root.newProjectRequested()
        }

        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.folder
            text: qsTr("Open…")
            shortcutId: "open"
            onTriggered: root.openFileRequested()
        }

        Rectangle {
            width: parent.width
            height: Theme.c.recentProjectsPopup.borderWidth
            color: Theme.c.recentProjectsPopup.divider
            opacity: 0.5
        }

        // --- Save and export -------------------------------------------------
        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.copy
            text: qsTr("Save as…")
            shortcutId: "saveAs"
            onTriggered: root.saveAsRequested()
        }

        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.package
            text: qsTr("Save with media…")
            onTriggered: root.packageRequested()
        }

        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.upload
            text: qsTr("Export video…")
            onTriggered: root.exportRequested()
        }

        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.layers
            text: qsTr("Render queue…")
            onTriggered: root.renderQueueRequested()
        }

        RecentProjectsActionRow {
            id: importExportRow
            popup: root
            glyph: Theme.icons.download
            text: qsTr("Import / Export")
            hasSubmenu: true
            onTriggered: importExportMenu.popup(importExportRow, importExportRow.width - Theme.spacingMd, 0)

            ThemedContextMenu {
                id: importExportMenu
                implicitWidth: 280

                ThemedMenuItem {
                    text: qsTr("Save project as JSON…")
                    onTriggered: { root.close(); root.saveJsonRequested() }
                }

                ThemedMenuItem {
                    text: qsTr("Open project from JSON…")
                    onTriggered: { root.close(); root.openJsonRequested() }
                }

                ThemedMenuItem {
                    text: qsTr("Open project folder…")
                    onTriggered: { root.close(); root.openFolderRequested() }
                }

                ThemedMenuItem {
                    text: qsTr("Save as project folder…")
                    onTriggered: { root.close(); root.saveFolderRequested() }
                }

                ThemedMenuItem {
                    text: qsTr("Export .drift bundle…")
                    onTriggered: { root.close(); root.exportBundleRequested() }
                }

                ThemedMenuItem {
                    text: qsTr("Render with data…")
                    visible: EditorState.projectFile.folder.isTemplate
                    height: visible ? implicitHeight : 0
                    onTriggered: { root.close(); root.renderTemplateRequested() }
                }

                ThemedMenuItem {
                    text: qsTr("Import/Export…")
                    onTriggered: { root.close(); root.importExportRequested() }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.c.recentProjectsPopup.borderWidth
            color: Theme.c.recentProjectsPopup.divider
            opacity: 0.5
        }

        // --- Project utilities -----------------------------------------------
        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.fileText
            text: qsTr("Project properties…")
            onTriggered: root.propertiesRequested()
        }

        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.warning
            text: qsTr("Check for missing media…")
            onTriggered: root.healthRequested()
        }

        Rectangle {
            width: parent.width
            height: Theme.c.recentProjectsPopup.borderWidth
            color: Theme.c.recentProjectsPopup.divider
            opacity: 0.5
        }

        // --- Close --------------------------------------------------------
        // Same confirm-if-dirty gate as New / Open, but lands on the start screen
        // instead of picking a replacement project for you.
        RecentProjectsActionRow {
            popup: root
            glyph: Theme.icons.x
            text: qsTr("Close project")
            onTriggered: root.closeProjectRequested()
        }
    }
}
