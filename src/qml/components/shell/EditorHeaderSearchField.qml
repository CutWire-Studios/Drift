import QtQuick
import QtQuick.Controls.Basic
import Drift
import QtQuick.Window

// --- Centre: global search ----------------------------------------------
Rectangle {
    id: searchBox
    signal exportRequested()
    signal downloadsRequested()
    signal languageRequested()
    signal videoSizeRequested()
    signal bakesRequested()
    signal cropRequested()
    signal searchFinished()
    // Ask mode sends the text to the command bar model and lists suggested commands instead of
    // search results. It lasts until the field is dismissed.
    property bool askMode: false
    function focusField() {
        if (askMode) {
            askMode = false
            searchInput.text = ""
        }
        searchInput.forceActiveFocus()
        searchInput.selectAll()
        searchPopup.search(searchInput.text)
    }
    function focusAsk() {
        if (!EditorState.preferences.commandBarEnabled)
            return
        if (!askMode)
            searchInput.text = ""
        askMode = true
        searchInput.forceActiveFocus()
        searchInput.selectAll()
        searchPopup.ask(searchInput.text)
    }
    function dismiss() {
        askMode = false
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

    IconGlyph {
        id: searchLens
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingXl
        anchors.verticalCenter: parent.verticalCenter
        glyph: searchBox.askMode ? Theme.icons.sparkles : Theme.icons.search
        iconSize: Theme.iconSizeMd
        iconColor: Theme.c.editorHeaderSearchField.icon
    }

    TextField {
        id: searchInput
        anchors.left: searchLens.right
        anchors.right: askButton.visible ? askButton.left : parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: askButton.visible ? Theme.spacingSm : Theme.spacingXl
        anchors.verticalCenter: parent.verticalCenter
        background: null
        leftPadding: 0
        rightPadding: 0
        color: Theme.c.editorHeaderSearchField.foreground
        placeholderText: {
            if (searchBox.askMode)
                return qsTr("Describe an edit, e.g. “fade in over half a second”")
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

        onTextEdited: searchBox.askMode ? searchPopup.ask(text) : searchPopup.search(text)
        onActiveFocusChanged: {
            if (!activeFocus)
                return
            if (searchBox.askMode)
                searchPopup.ask(text)
            else
                searchPopup.search(text)
        }

        // The popup never takes focus, so the field drives it.
        Keys.onUpPressed: (event) => { searchPopup.moveHighlight(-1); event.accepted = true }
        Keys.onDownPressed: (event) => { searchPopup.moveHighlight(1); event.accepted = true }
        Keys.onReturnPressed: (event) => { searchPopup.activate(searchPopup.highlightIndex); event.accepted = true }
        Keys.onEnterPressed: (event) => { searchPopup.activate(searchPopup.highlightIndex); event.accepted = true }
        Keys.onEscapePressed: (event) => { searchBox.searchFinished(); event.accepted = true }
    }

    // Opens ask mode straight away. Shown while the field is idle, so it never covers typed text.
    Rectangle {
        id: askButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height - Theme.spacingSm * 2
        width: askRow.implicitWidth + Theme.spacingLg * 2
        radius: Math.max(0, searchBox.radius - 2)
        visible: EditorState.preferences.commandBarEnabled && EditorState.preferences.agenticUse !== "none"
                 && !searchInput.activeFocus
                 && !EditorState.commandBar.busy
        color: askArea.containsMouse ? Theme.c.editorHeaderSearchField.workspaceBackgroundHover : "transparent"

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Ask")
        Accessible.onPressAction: searchBox.focusAsk()

        Row {
            id: askRow
            anchors.centerIn: parent
            spacing: Theme.spacingSm

            IconGlyph {
                glyph: Theme.icons.sparkles
                iconSize: Theme.iconSizeSm
                iconColor: Theme.c.editorHeaderSearchField.icon
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: qsTr("Ask")
                color: Theme.c.editorHeaderSearchField.workspaceForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        RichToolTip {
            visible: askArea.containsMouse
            title: qsTr("Ask")
            actionId: "askCommand"
            body: qsTr("Describe an edit in plain words and Drift does it, e.g. “fade in over half a second”.")
        }

        MouseArea {
            id: askArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: searchBox.focusAsk()
        }
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingXl
        anchors.verticalCenter: parent.verticalCenter
        visible: EditorState.commandBar.busy
        text: qsTr("Thinking…")
        color: Theme.c.editorHeaderSearchField.placeholder
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        SequentialAnimation on opacity {
            running: EditorState.commandBar.busy
            loops: Animation.Infinite
            NumberAnimation { to: 0.35; duration: 600 }
            NumberAnimation { to: 1; duration: 600 }
        }
    }

    GlobalSearchPopup {
        id: searchPopup
        y: searchBox.height + Theme.spacingMd
        x: (searchBox.width - width) / 2
        width: Math.max(searchBox.width, 480)
        onFinished: searchBox.searchFinished()
        onQueryPicked: (text) => {
            searchInput.text = text
            searchInput.forceActiveFocus()
            searchPopup.search(text)
        }
    }

    Connections {
        target: ShellBridge
        function onDialogRequested(name, params) {
            switch (name) {
            case "export": searchBox.exportRequested(); break
            case "downloads": searchBox.downloadsRequested(); break
            case "language": searchBox.languageRequested(); break
            case "videoSize": searchBox.videoSizeRequested(); break
            case "bakes": searchBox.bakesRequested(); break
            case "cropVideo": searchBox.cropRequested(); break
            case "mobileShell": ShellBridge.main.switchShell("mobile"); break
            }
        }
    }
}
