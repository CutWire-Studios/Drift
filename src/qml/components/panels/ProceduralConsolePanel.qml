pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Output of the last Apply or Check (Lua errors, print output, shader compile logs) and, below it,
// what the selected clip's latest evaluation reported.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "procedural-console"
        title: qsTr("Procedural console")
        keywords: "lua errors print log shader compile output diagnostics"
    }

    property bool shown: true
    property var liveLines: []
    property string liveKey: ""

    readonly property var clipData: EditorState.selectedClipData
    readonly property bool hasProcedural: !!clipData && clipData.kind === "procedural" && !!clipData.procedural

    function levelColor(level) {
        switch (level) {
        case "error": return Theme.c.proceduralConsolePanel.error
        case "warning": return Theme.c.proceduralConsolePanel.warning
        case "shader": return Theme.c.proceduralConsolePanel.shader
        case "info": return Theme.c.proceduralConsolePanel.info
        default: return Theme.c.proceduralConsolePanel.log
        }
    }

    function pollLive() {
        if (!hasProcedural || !clipData.id) {
            applyLive([])
            return
        }
        const d = ProceduralEditor.live(clipData.id)
        const lines = []
        if (d.known === true) {
            if (d.error)
                lines.push({ "level": "error", "text": d.error })
            for (const e of d.errors || [])
                lines.push({ "level": "error", "text": e })
            for (const l of d.log || [])
                lines.push({ "level": "log", "text": l })
            for (const s of d.shaders || []) {
                if (!s.ok || (s.log && s.log.trim().length > 0))
                    lines.push({ "level": s.ok ? "shader" : "error", "text": s.name + ": " + s.log.trim() })
            }
        }
        applyLive(lines)
    }

    // The evaluation serial moves every frame; only a change in the text itself replaces the model.
    function applyLive(lines) {
        const key = JSON.stringify(lines)
        if (key === liveKey)
            return
        liveKey = key
        liveLines = lines
    }

    Timer {
        interval: 500
        repeat: true
        running: root.visible && root.hasProcedural
        triggeredOnStart: true
        onTriggered: root.pollLive()
    }

    onClipDataChanged: pollLive()

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Console")

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.trash
            variant: "text"
            tooltip: qsTr("Clear the Apply output")
            enabled: ProceduralEditor.consoleLines.length > 0
            onClicked: ProceduralEditor.clearConsole()
        }
    }

    ListView {
        id: output
        anchors.top: header.bottom
        anchors.bottom: liveBox.visible ? liveBox.top : parent.bottom
        width: parent.width
        clip: true
        model: ProceduralEditor.consoleLines
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }
        onCountChanged: Qt.callLater(positionViewAtEnd)

        delegate: TextEdit {
            required property var modelData
            width: output.width
            padding: Theme.spacingXs
            leftPadding: Theme.spacingMd
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            text: modelData.text
            color: root.levelColor(modelData.level)
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Text {
            anchors.centerIn: parent
            visible: output.count === 0
            text: qsTr("Apply or Check a package to see its output here")
            color: Theme.c.proceduralConsolePanel.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }

    Rectangle {
        id: liveBox
        visible: root.hasProcedural
        anchors.bottom: parent.bottom
        width: parent.width
        height: visible ? Math.min(parent.height * 0.4, liveHeader.height + Math.max(liveList.contentHeight, Theme.controlHeightSm)) : 0
        color: Theme.c.proceduralConsolePanel.background

        Rectangle {
            width: parent.width
            height: Theme.c.proceduralConsolePanel.borderWidth
            color: Theme.c.proceduralConsolePanel.border
        }

        Rectangle {
            id: liveHeader
            width: parent.width
            height: Theme.controlHeightSm
            color: Theme.c.proceduralConsolePanel.headerBackground

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Selected clip")
                color: Theme.c.proceduralConsolePanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        ListView {
            id: liveList
            anchors.top: liveHeader.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            model: root.liveLines
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: TextEdit {
                required property var modelData
                width: liveList.width
                padding: Theme.spacingXs
                leftPadding: Theme.spacingMd
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: modelData.text
                color: root.levelColor(modelData.level)
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                anchors.centerIn: parent
                visible: liveList.count === 0
                text: qsTr("No errors or output")
                color: Theme.c.proceduralConsolePanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
