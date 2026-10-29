import QtQuick
import QtQuick.Controls.Basic
import Drift
import QtQuick.Window

// --- Centre: workspace switcher + global search -------------------------
// One bar, two targets: the left segment switches workspaces, the rest is the search field.
// An empty search lists the workspaces too, so switching works from the keyboard.
Rectangle {
    id: searchBox
    signal exportRequested()
    signal downloadsRequested()
    signal languageRequested()
    signal videoSizeRequested()
    signal searchFinished()
    function focusField() {
        searchInput.forceActiveFocus()
        searchInput.selectAll()
    }
    function dismiss() {
        searchInput.text = ""
        searchPopup.close()
        searchInput.focus = false
    }
    implicitHeight: Theme.controlHeight
    radius: Theme.c.editorHeaderSearchField.radius
    color: Theme.c.editorHeaderSearchField.background
    border.width: searchInput.activeFocus ? Theme.c.editorHeaderSearchField.borderWidthFocus
                                          : Theme.c.editorHeaderSearchField.borderWidth
    border.color: Theme.c.editorHeaderSearchField.border

    readonly property string activeWorkspaceName: {
        const list = Workspaces.workspaces
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === Workspaces.activeId)
                return list[i].name
        }
        return ""
    }

    Item {
        id: workspaceSegment
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: workspaceRow.implicitWidth + Theme.spacingXl * 2

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Workspace: %1").arg(searchBox.activeWorkspaceName)

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Math.max(0, searchBox.radius - 2)
            color: Theme.c.editorHeaderSearchField.workspaceBackgroundHover
            visible: workspaceArea.containsMouse || workspaceMenu.opened
        }

        Row {
            id: workspaceRow
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingXl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm

            Text {
                text: qsTr("Workspace:")
                color: Theme.c.editorHeaderSearchField.workspacePrefix
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: searchBox.activeWorkspaceName
                color: Theme.c.editorHeaderSearchField.workspaceForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }

            IconGlyph {
                glyph: Theme.icons.chevronsUpDown
                iconSize: Theme.iconSizeSm
                iconColor: Theme.c.editorHeaderSearchField.icon
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        ThemedToolTip {
            visible: workspaceArea.containsMouse && !workspaceMenu.opened
            text: qsTr("Switch workspace")
        }

        MouseArea {
            id: workspaceArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: workspaceMenu.popup(0, searchBox.height + Theme.spacingMd)
        }

        ThemedContextMenu {
            id: workspaceMenu
            implicitWidth: 248

            Instantiator {
                model: Workspaces.workspaces

                ThemedMenuItem {
                    required property var modelData
                    text: modelData.name
                    icon.name: modelData.id === Workspaces.activeId ? Theme.icons.check : ""
                    onTriggered: Workspaces.activeId = modelData.id
                }

                onObjectAdded: (index, object) => workspaceMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => workspaceMenu.removeItem(object)
            }

            ThemedMenuSeparator { }

            ThemedMenuItem {
                text: qsTr("Customize layouts in Settings…")
                icon.name: Theme.icons.sliders
                onTriggered: Qt.callLater(() => ShellBridge.main.openSettings("workspaces"))
            }
        }
    }

    Rectangle {
        id: segmentDivider
        anchors.left: workspaceSegment.right
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: parent.height - Theme.spacingMd * 2
        color: Theme.c.editorHeaderSearchField.divider
    }

    IconGlyph {
        id: searchLens
        anchors.left: segmentDivider.right
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.search
        iconSize: Theme.iconSizeMd
        iconColor: Theme.c.editorHeaderSearchField.icon
    }

    TextField {
        id: searchInput
        anchors.left: searchLens.right
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingXl
        anchors.verticalCenter: parent.verticalCenter
        background: null
        leftPadding: 0
        rightPadding: 0
        color: Theme.c.editorHeaderSearchField.foreground
        placeholderText: {
            const keys = EditorState.shortcutFor("search")
            return keys.length > 0 ? qsTr("Search  %1").arg(Theme.shortcutDisplay(keys))
                                   : qsTr("Search")
        }
        placeholderTextColor: Theme.c.editorHeaderSearchField.placeholder
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        selectByMouse: true
        selectedTextColor: Theme.c.editorHeaderSearchField.selectionForeground
        selectionColor: Theme.c.editorHeaderSearchField.selection

        onTextEdited: searchPopup.search(text)
        onActiveFocusChanged: if (activeFocus) searchPopup.search(text)

        // The popup never takes focus, so the field drives it.
        Keys.onUpPressed: (event) => { searchPopup.moveHighlight(-1); event.accepted = true }
        Keys.onDownPressed: (event) => { searchPopup.moveHighlight(1); event.accepted = true }
        Keys.onReturnPressed: (event) => { searchPopup.activate(searchPopup.highlightIndex); event.accepted = true }
        Keys.onEnterPressed: (event) => { searchPopup.activate(searchPopup.highlightIndex); event.accepted = true }
        Keys.onEscapePressed: (event) => { searchBox.searchFinished(); event.accepted = true }
    }

    GlobalSearchPopup {
        id: searchPopup
        y: searchBox.height + Theme.spacingMd
        x: (searchBox.width - width) / 2
        width: Math.max(searchBox.width, 480)
        onFinished: searchBox.searchFinished()
    }

    Connections {
        target: ShellBridge
        function onDialogRequested(name, params) {
            switch (name) {
            case "export": searchBox.exportRequested(); break
            case "downloads": searchBox.downloadsRequested(); break
            case "language": searchBox.languageRequested(); break
            case "videoSize": searchBox.videoSizeRequested(); break
            case "mobileShell": ShellBridge.main.switchShell("mobile"); break
            }
        }
    }
}
