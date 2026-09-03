import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift
import "components"

Rectangle {
    id: root

    height: Theme.headerHeight
    color: Theme.appBackground

    property string projectName: EditorState.projectName

    readonly property var projectFilter: [qsTr("Drift project (*.drift)")]
    readonly property var projectMimeTypes: ["application/x-drift-project"]

    // Action to run after Save or Don't Save resolves. Null when idle.
    property var _pendingAfterUnsaved: null

    // Runs `action` immediately when clean; otherwise opens the unsaved prompt.
    // Used by New / Open / Recent / Quit and the matching shortcuts.
    function confirmIfDirty(action) {
        if (!EditorState.hasUnsavedChanges) {
            action()
            return
        }
        root._pendingAfterUnsaved = action
        unsavedDialog.openDialog()
    }

    function openProject() {
        root.confirmIfDirty(function () {
            var url = FileDialogs.openFile(qsTr("Open Project"), root.projectFilter,
                                           root.projectMimeTypes)
            if (url != "")
                EditorState.loadProject(url)
        })
    }

    function requestNewProject() {
        root.confirmIfDirty(function () {
            EditorState.newProject()
        })
    }

    function openRecent(path) {
        root.confirmIfDirty(function () {
            EditorState.openRecentProject(path)
        })
    }

    // Returns true when the project is clean after the attempt. False if the
    // user cancelled Save As or the write failed — callers must not continue.
    function saveProject() {
        if (EditorState.currentProjectPath && EditorState.currentProjectPath.length > 0) {
            EditorState.saveProject(EditorState.fileUrl(EditorState.currentProjectPath))
            return !EditorState.hasUnsavedChanges
        }
        var url = FileDialogs.saveFile(qsTr("Save Project"), root.projectFilter,
                                       EditorState.projectName, "drift", "",
                                       root.projectMimeTypes)
        if (url == "")
            return false
        EditorState.saveProject(url)
        return !EditorState.hasUnsavedChanges
    }

    // Raw document JSON: an export, not a project file. Always asks for a path and leaves the
    // current .drift association untouched.
    function saveProjectJson() {
        var url = FileDialogs.saveFile(qsTr("Save Project JSON"),
                                       [qsTr("JSON document (*.json)")],
                                       EditorState.projectName, "json", "",
                                       ["application/json"])
        if (url != "")
            EditorState.saveProjectJson(url)
    }

    // Inverse of saveProjectJson. Confirms unsaved work like Open, because it replaces the
    // timeline. The JSON does not become the current project path.
    function openProjectJson() {
        root.confirmIfDirty(function () {
            var url = FileDialogs.openFile(qsTr("Open Project JSON"),
                                           [qsTr("JSON document (*.json)")],
                                           ["application/json"])
            if (url != "")
                EditorState.loadProjectJson(url)
        })
    }

    // Save As with every source file copied in, so the result opens on a machine that has none of
    // the media. Always asks for a path: it is a different artefact from the working save.
    function packageProject() {
        var url = FileDialogs.saveFile(qsTr("Save Shareable Copy"), root.projectFilter,
                                       EditorState.projectName, "drift", "",
                                       root.projectMimeTypes)
        if (url != "")
            EditorState.packageProject(url)
    }

    function exportVideo() {
        exportDialog.openDialog()
    }

    // True once the user has dismissed the progress dialog while an export is
    // still running; drives the circular-progress badge next to Export.
    property bool exportProgressDismissed: false

    Connections {
        target: EditorState
        function onProjectNameChanged() { root.projectName = EditorState.projectName }
        function onExportInProgressChanged() {
            if (EditorState.exportInProgress) {
                root.exportProgressDismissed = false
                exportProgressDialog.openDialog()
            }
        }
        function onSaveRequested() { root.saveProject() }
        function onOpenRequested() { root.openProject() }
        function onNewProjectRequested() { root.requestNewProject() }
    }

    ExportDialog {
        id: exportDialog
    }

    ExportProgressDialog {
        id: exportProgressDialog
        onClosed: if (EditorState.exportInProgress) root.exportProgressDismissed = true
    }

    ProjectPropertiesDialog {
        id: projectPropertiesDialog
    }

    PackageProgressDialog {
        id: packageProgressDialog
    }

    LanguageChooserDialog {
        id: languageChooserDialog
    }

    AgentAccessDialog {
        id: agentAccessDialog
    }

    VideoSizeDialog {
        id: videoSizeDialog
    }

    UnsavedChangesDialog {
        id: unsavedDialog

        // Null pending *before* close(): Dialog.close() rejects, and onRejected
        // must not wipe an action we are about to run.
        onSaveChosen: {
            if (!root.saveProject())
                return
            const action = root._pendingAfterUnsaved
            root._pendingAfterUnsaved = null
            close()
            if (action)
                action()
        }
        onDiscardChosen: {
            const action = root._pendingAfterUnsaved
            root._pendingAfterUnsaved = null
            close()
            if (action)
                action()
        }
        onRejected: root._pendingAfterUnsaved = null
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.borderWidth
        color: Theme.panelBorder
        opacity: 0.5
    }

    // The three groups used to be independently anchored, so at narrow widths
    // they overlapped instead of compressing. A RowLayout arbitrates the space.
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        anchors.verticalCenterOffset: 1
        spacing: Theme.spacingLg

        // --- Left: project switcher + save ------------------------------------
        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingLg

            // Gmail-style status pill: saved/unsaved dot, project name, chevron.
            Rectangle {
                id: projectsButton

                readonly property bool open: recentPopup.visible
                readonly property bool saved: !EditorState.hasUnsavedChanges

                // Cap width so a long name does not shove the right-side actions.
                width: Math.min(projectsRow.implicitWidth + Theme.spacingXl * 2, 260)
                height: 32
                radius: Theme.radiusPill
                color: projectsArea.containsMouse || open ? Theme.popoverHover : Theme.accent
                anchors.verticalCenter: parent.verticalCenter
                clip: true

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Projects")
                Accessible.description: saved ? qsTr("All changes saved")
                                              : qsTr("Unsaved changes")

                Row {
                    id: projectsRow
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingXl
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingMd

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: projectsButton.saved ? Theme.constructive : Theme.destructive
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        // Cap against the pill's max width (dot + gaps + chevron + padding).
                        readonly property real maxTextWidth: 260 - Theme.spacingXl * 2
                                                             - 8 - Theme.iconSizeSm
                                                             - Theme.spacingMd * 2
                        width: Math.min(implicitWidth, maxTextWidth)
                        text: root.projectName
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    IconGlyph {
                        glyph: Theme.icons.chevronDown
                        iconSize: Theme.iconSizeSm
                        iconColor: Theme.mutedForeground
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                ThemedToolTip {
                    visible: projectsArea.containsMouse && !projectsButton.open
                    text: projectsButton.saved
                          ? qsTr("Projects — click to switch or start new")
                          : qsTr("Unsaved changes — click to switch or start new")
                }

                MouseArea {
                    id: projectsArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: recentPopup.open()
                }

                RecentProjectsPopup {
                    id: recentPopup
                    y: parent.height + Theme.spacingMd
                    onOpenFileRequested: root.openProject()
                    onNewProjectRequested: root.requestNewProject()
                    onOpenRecentRequested: (path) => root.openRecent(path)
                    onPackageRequested: root.packageProject()
                    onSaveJsonRequested: root.saveProjectJson()
                    onOpenJsonRequested: root.openProjectJson()
                    onPropertiesRequested: projectPropertiesDialog.openDialog()
                }
            }

            IconButton {
                glyph: Theme.icons.save
                variant: "ghost"
                text: qsTr("Save")
                tooltip: {
                    const keys = EditorState.shortcutFor("save")
                    return keys.length > 0 ? qsTr("Save project (%1)").arg(Theme.shortcutDisplay(keys))
                                           : qsTr("Save project")
                }
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.saveProject()
            }

            IconButton {
                glyph: Theme.icons.ratio
                variant: "ghost"
                text: qsTr("Video")
                active: videoSizeDialog.visible || EditorState.canvasCropMode
                tooltip: qsTr("Video size and layout")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: videoSizeDialog.openDialog()
            }
        }

        // Absorbs leftover space so the two groups stay apart but can compress.
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        // --- Right: appearance | extras | agent | infrequent | export ------
        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingSm

            component HeaderSeparator: Item {
                width: Theme.spacingLg + Theme.borderWidth
                height: 32
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: Theme.borderWidth
                    height: 16
                    anchors.centerIn: parent
                    color: Theme.panelBorder
                }
            }

            // Workspace switcher. Portrait projects default to the portrait
            // arrangement, but the choice stays the user's: a tall canvas on an
            // ultrawide display is still comfortable in the landscape workspace, and
            // a portrait *display* suits the portrait one whatever the canvas is.
            // Picking either explicitly stops the canvas from driving it; "Auto"
            // hands it back.
            Item {
                id: workspaceButton
                implicitWidth: workspaceBtn.implicitWidth
                implicitHeight: workspaceBtn.implicitHeight
                width: implicitWidth
                height: implicitHeight
                anchors.verticalCenter: parent.verticalCenter

                readonly property bool portrait: {
                    const win = root.Window.window
                    return win ? win.portraitWorkspace : false
                }

                IconButton {
                    id: workspaceBtn
                    anchors.fill: parent
                    glyph: workspaceButton.portrait ? Theme.icons.smartphone : Theme.icons.monitor
                    variant: "ghost"
                    text: qsTr("Workspace")
                    active: workspaceMenu.opened
                    tooltip: workspaceButton.portrait ? qsTr("Workspace: portrait")
                                                      : qsTr("Workspace: landscape")
                    onClicked: workspaceMenu.popup(0, workspaceButton.height + Theme.spacingMd)
                }

                ThemedContextMenu {
                    id: workspaceMenu
                    implicitWidth: 236

                    // The active entry swaps its own icon for a tick rather than
                    // adding a trailing column — every row keeps a glyph, so the
                    // labels stay aligned.
                    ThemedMenuItem {
                        text: qsTr("Auto (follow canvas)")
                        icon.name: EditorState.workspaceLayoutOverridden ? Theme.icons.grid
                                                                         : Theme.icons.check
                        onTriggered: EditorState.clearWorkspaceLayoutPreference()
                    }

                    ThemedMenuItem {
                        text: qsTr("Landscape")
                        icon.name: EditorState.workspaceLayoutOverridden
                                   && EditorState.workspaceLayoutPreferred === "landscape"
                                   ? Theme.icons.check : Theme.icons.monitor
                        onTriggered: EditorState.setWorkspaceLayoutPreference("landscape")
                    }

                    ThemedMenuItem {
                        text: qsTr("Portrait")
                        icon.name: EditorState.workspaceLayoutOverridden
                                   && EditorState.workspaceLayoutPreferred === "portrait"
                                   ? Theme.icons.check : Theme.icons.smartphone
                        onTriggered: EditorState.setWorkspaceLayoutPreference("portrait")
                    }
                }
            }

            IconButton {
                glyph: Theme.darkMode ? Theme.icons.sun : Theme.icons.moon
                variant: "ghost"
                text: qsTr("Theme")
                tooltip: Theme.darkMode ? qsTr("Switch to light mode") : qsTr("Switch to dark mode")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: Theme.toggleDarkMode()
            }

            IconButton {
                glyph: Theme.icons.languages
                variant: "ghost"
                text: qsTr("Language")
                tooltip: qsTr("Language for menus and labels")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: languageChooserDialog.openFromHeader()
            }

            HeaderSeparator {}

            // Extras. Pulses with a red shockwave while essential packs or updates need
            // attention — the dialog itself never opens on its own (see UpdateDialog).
            Item {
                id: extrasButton
                implicitWidth: extrasBtn.implicitWidth
                implicitHeight: extrasBtn.implicitHeight
                width: implicitWidth
                height: implicitHeight
                anchors.verticalCenter: parent.verticalCenter

                readonly property bool attention: {
                    const win = root.Window.window
                    return win && win.addonAttentionNeeded
                }

                // Expanding rings behind the icon — reads as a soft shockwave, not a badge.
                Repeater {
                    model: 2

                    Item {
                        id: wave
                        required property int index

                        anchors.centerIn: parent
                        width: Theme.iconButtonSize
                        height: Theme.iconButtonSize
                        scale: 0.85
                        opacity: 0
                        visible: extrasButton.attention
                        z: -1

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.destructive
                        }

                        SequentialAnimation {
                            running: extrasButton.attention
                            loops: Animation.Infinite

                            PauseAnimation { duration: wave.index * 700 }
                            ParallelAnimation {
                                NumberAnimation {
                                    target: wave
                                    property: "opacity"
                                    from: 0.55
                                    to: 0
                                    duration: 1400
                                    easing.type: Easing.OutCubic
                                }
                                NumberAnimation {
                                    target: wave
                                    property: "scale"
                                    from: 0.85
                                    to: 1.75
                                    duration: 1400
                                    easing.type: Easing.OutCubic
                                }
                            }
                            PauseAnimation { duration: (1 - wave.index) * 700 }
                        }
                    }
                }

                IconButton {
                    id: extrasBtn
                    anchors.fill: parent
                    glyph: Theme.icons.puzzle
                    variant: "ghost"
                    text: qsTr("Extras")
                    tooltip: extrasButton.attention
                             ? qsTr("Recommended packs and updates")
                             : qsTr("Extras")
                    onClicked: root.Window.window.openExtras()
                }
            }

            // Only exists while there is a newer release to tell the user about; the check itself
            // is silent, so this dot is the whole notification.
            Item {
                implicitWidth: updateBtn.implicitWidth
                implicitHeight: updateBtn.implicitHeight
                width: implicitWidth
                height: implicitHeight
                anchors.verticalCenter: parent.verticalCenter
                visible: Updates.updateAvailable

                IconButton {
                    id: updateBtn
                    anchors.fill: parent
                    glyph: Theme.icons.download
                    variant: "ghost"
                    text: qsTr("Update")
                    tooltip: qsTr("Drift %1 is available").arg(Updates.latestVersion)
                    onClicked: root.Window.window.openUpdateDialog()
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: width / 2
                    color: Theme.primary
                    border.width: Theme.borderWidth
                    border.color: Theme.panelBackground
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 3
                }
            }

            HeaderSeparator {}

            IconButton {
                glyph: Theme.icons.bot
                variant: "ghost"
                text: qsTr("Agent")
                active: EditorState.mcpRunning
                tooltip: EditorState.mcpRunning
                         ? qsTr("Agent access is on")
                         : qsTr("Agent access")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: agentAccessDialog.openDialog()
            }

            HeaderSeparator {}

            // Infrequent tools, icon-only, kept off the main labeled cluster.
            IconButton {
                glyph: Theme.icons.shuffle
                variant: "ghost"
                tooltip: qsTr("Multicam")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    const win = root.Window.window
                    if (win)
                        win.openMulticam()
                }
            }

            IconButton {
                glyph: Theme.icons.bug
                variant: "ghost"
                tooltip: qsTr("Debug info")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    const win = root.Window.window
                    if (win)
                        win.openDebugInfo()
                }
            }

            HeaderSeparator {}

            Rectangle {
                id: exportProgressBadge
                width: Theme.iconButtonSize
                height: Theme.iconButtonSize
                radius: width / 2
                color: badgeMouse.containsMouse ? Theme.popoverHover : "transparent"
                anchors.verticalCenter: parent.verticalCenter
                visible: opacity > 0
                opacity: EditorState.exportInProgress && root.exportProgressDismissed ? 1 : 0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                }

                CircularProgress {
                    anchors.centerIn: parent
                    size: Theme.iconSizeXl
                    strokeWidth: 3
                    value: EditorState.exportProgress
                }

                ThemedToolTip {
                    visible: badgeMouse.containsMouse
                    text: qsTr("Export in progress (%1%) — click to view").arg(Math.round(EditorState.exportProgress * 100))
                }

                MouseArea {
                    id: badgeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.exportProgressDismissed = false
                        exportProgressDialog.openDialog()
                    }
                }
            }

            // Primary CTA. Kept as a bespoke gradient button (the documented
            // exception to Themed* chrome), but it now has the hover, pressed and
            // disabled states every other control has.
            Rectangle {
                id: exportButton

                readonly property bool busy: EditorState.exportInProgress

                width: exportRow.implicitWidth + Theme.spacing3xl
                height: 32
                radius: Theme.radiusMd
                color: Theme.exportGlow
                anchors.verticalCenter: parent.verticalCenter
                opacity: busy ? 0.55 : 1
                scale: exportMouse.pressed && !busy ? 0.97 : 1

                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on scale {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Export video")

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: parent.radius - 2
                    // Brightens on hover, darkens on press.
                    opacity: exportButton.busy ? 1
                             : (exportMouse.pressed ? 0.85 : (exportMouse.containsMouse ? 1 : 0.94))

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.exportGradientTop }
                        GradientStop { position: 1.0; color: Theme.exportGradientBottom }
                    }

                    Row {
                        id: exportRow
                        anchors.centerIn: parent
                        spacing: Theme.spacingMd

                        IconGlyph {
                            glyph: Theme.icons.upload
                            iconSize: Theme.iconSizeMd
                            iconColor: Theme.primaryForeground
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: qsTr("Export")
                            color: Theme.primaryForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        // Fixed-width so ticking percentages do not resize the
                        // button and jolt the whole header row.
                        Text {
                            visible: exportButton.busy
                            width: visible ? 34 : 0
                            text: Math.round(EditorState.exportProgress * 100) + "%"
                            color: Theme.primaryForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeSm
                            horizontalAlignment: Text.AlignRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                ThemedToolTip {
                    visible: exportMouse.containsMouse
                    text: exportButton.busy ? qsTr("Export already in progress")
                                            : qsTr("Export video")
                }

                MouseArea {
                    id: exportMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: exportButton.busy ? Qt.ArrowCursor : Qt.PointingHandCursor
                    onClicked: if (!exportButton.busy) root.exportVideo()
                }
            }
        }
    }
}
