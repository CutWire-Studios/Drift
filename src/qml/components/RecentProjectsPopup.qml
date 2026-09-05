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
    signal openRecentRequested(string path)
    signal packageRequested()
    signal saveJsonRequested()
    signal openJsonRequested()
    signal importPremiereRequested()
    signal propertiesRequested()

    component ActionRow: Rectangle {
        id: actionRow

        property alias glyph: rowIcon.glyph
        property alias text: rowLabel.text
        signal triggered()

        width: parent ? parent.width : 0
        height: Theme.controlHeight + Theme.spacingSm
        radius: Theme.radiusSm
        color: actionArea.containsMouse ? Theme.accent : "transparent"

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLg

            IconGlyph {
                id: rowIcon
                iconSize: Theme.iconSizeMd
                iconColor: Theme.foreground
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: rowLabel
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        MouseArea {
            id: actionArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.close()
                actionRow.triggered()
            }
        }
    }

    // Shared row for current / previous projects: status dot, title (+ optional
    // subtitle), and an optional trailing checkmark for the active project.
    // When removable, a hover-visible × removes the entry from recents (not disk).
    component ProjectRow: Rectangle {
        id: projectRow

        property string title
        property string subtitle
        property string tip
        property color statusColor: Theme.constructive
        property bool showCheck: false
        property bool removable: false
        property bool interactive: true
        property bool highlighted: false
        readonly property bool hovered: rowHover.hovered
        signal triggered()
        signal removeRequested()

        width: parent ? parent.width : 0
        height: subtitle.length > 0 ? 48 : 40
        radius: Theme.radiusSm
        color: (hovered && interactive) || highlighted ? Theme.accent : "transparent"

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        HoverHandler {
            id: rowHover
        }

        Row {
            id: leadingRow
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.right: trailingRow.left
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLg

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: projectRow.statusColor
                anchors.verticalCenter: parent.verticalCenter
            }

            Column {
                width: Math.max(0, leadingRow.width - 8 - Theme.spacingLg)
                spacing: 1
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    width: parent.width
                    text: projectRow.title
                    color: projectRow.interactive || projectRow.showCheck
                           ? Theme.foreground : Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: projectRow.showCheck ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: projectRow.subtitle.length > 0
                    text: projectRow.subtitle
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideLeft
                }
            }
        }

        Row {
            id: trailingRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm

            IconGlyph {
                id: checkIcon
                visible: projectRow.showCheck
                glyph: Theme.icons.check
                iconSize: Theme.iconSizeMd
                iconColor: Theme.mutedForeground
                anchors.verticalCenter: parent.verticalCenter
            }

            Item {
                id: removeButton
                width: Theme.iconSizeMd + Theme.spacingSm
                height: Theme.iconSizeMd + Theme.spacingSm
                visible: projectRow.removable
                opacity: projectRow.hovered ? 1 : 0
                anchors.verticalCenter: parent.verticalCenter

                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: Theme.icons.x
                    iconSize: Theme.iconSizeMd
                    iconColor: removeArea.containsMouse ? Theme.foreground : Theme.mutedForeground
                }

                ThemedToolTip {
                    text: qsTr("Remove from recents")
                    visible: removeArea.containsMouse
                }

                MouseArea {
                    id: removeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // Keep the row from treating this as an open click.
                    onClicked: (mouse) => {
                        mouse.accepted = true
                        projectRow.removeRequested()
                    }
                }
            }
        }

        ThemedToolTip {
            text: projectRow.tip
            visible: projectRow.tip.length > 0 && rowHover.hovered && !removeArea.containsMouse
        }

        MouseArea {
            id: rowArea
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: trailingRow.left
            hoverEnabled: true
            cursorShape: projectRow.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (!projectRow.interactive)
                    return
                root.close()
                projectRow.triggered()
            }
        }
    }

    width: Theme.dialogWidthSm
    padding: Theme.spacingMd
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var items: EditorState.recentProjects
    readonly property string currentPath: EditorState.currentProjectPath || ""
    readonly property bool currentSaved: !EditorState.hasUnsavedChanges

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
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
        radius: Theme.radiusMd
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
        ProjectRow {
            title: EditorState.projectName
            subtitle: root.currentSaved ? qsTr("All changes saved")
                                        : qsTr("Unsaved changes")
            tip: root.currentPath.length > 0 ? root.currentPath : ""
            statusColor: root.currentSaved ? Theme.constructive : Theme.destructive
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
            color: Theme.mutedForeground
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
            color: Theme.mutedForeground
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

            delegate: ProjectRow {
                id: previousRow
                required property var modelData
                required property int index

                width: ListView.view.width
                title: previousRow.modelData.name
                       + (previousRow.modelData.exists ? "" : qsTr(" (missing)"))
                subtitle: previousRow.modelData.path
                tip: previousRow.modelData.exists
                     ? previousRow.modelData.path
                     : qsTr("This file has been moved or deleted:\n%1").arg(previousRow.modelData.path)
                statusColor: previousRow.modelData.exists ? Theme.constructive
                                                          : Theme.mutedForeground
                interactive: previousRow.modelData.exists
                removable: true
                highlighted: previousList.currentIndex === index

                onTriggered: root.openRecentRequested(previousRow.modelData.path)
                onRemoveRequested: EditorState.removeRecentProject(previousRow.modelData.path)
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            color: Theme.panelBorder
            opacity: 0.5
        }

        // --- Create / open ---------------------------------------------------
        ActionRow {
            glyph: Theme.icons.plus
            text: qsTr("Initialize new project")
            onTriggered: root.newProjectRequested()
        }

        ActionRow {
            glyph: Theme.icons.folder
            text: qsTr("Open project…")
            onTriggered: root.openFileRequested()
        }

        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            color: Theme.panelBorder
            opacity: 0.5
        }

        // --- Project utilities -----------------------------------------------
        ActionRow {
            glyph: Theme.icons.package
            text: qsTr("Save with media…")
            onTriggered: root.packageRequested()
        }

        ActionRow {
            glyph: Theme.icons.download
            text: qsTr("Save as JSON…")
            onTriggered: root.saveJsonRequested()
        }

        ActionRow {
            glyph: Theme.icons.upload
            text: qsTr("Open JSON…")
            onTriggered: root.openJsonRequested()
        }

        ActionRow {
            glyph: Theme.icons.film
            text: qsTr("Import Premiere project…")
            onTriggered: root.importPremiereRequested()
        }

        ActionRow {
            glyph: Theme.icons.fileText
            text: qsTr("Project properties…")
            onTriggered: root.propertiesRequested()
        }
    }
}
