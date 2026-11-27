pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

Item {
    id: root

    property var report: ({})
    property string emptyText: qsTr("Nothing is lost.")
    property var collapsed: ({})

    readonly property var issues: root.report.issues ?? []
    readonly property real fps: {
        const v = EditorState.projectFile.projectFps()
        return v > 0 ? v : 30
    }
    readonly property var groups: {
        const rank = { error: 0, dropped: 1, degraded: 2, info: 3 }
        const byKey = {}
        const out = []
        for (const issue of root.issues) {
            const key = issue.severity + "/" + issue.category
            if (!byKey[key]) {
                byKey[key] = { key: key, severity: issue.severity, category: issue.category, items: [], total: 0 }
                out.push(byKey[key])
            }
            byKey[key].items.push(issue)
            byKey[key].total += issue.count
        }
        out.sort((a, b) => (rank[a.severity] ?? 9) - (rank[b.severity] ?? 9))
        return out
    }

    onReportChanged: root.collapsed = ({})

    function severityLabel(severity) {
        switch (severity) {
        case "error": return qsTr("Error")
        case "dropped": return qsTr("Dropped")
        case "degraded": return qsTr("Degraded")
        default: return qsTr("Info")
        }
    }

    function severityColor(severity) {
        switch (severity) {
        case "error": return Theme.c.interchange.severityError
        case "dropped": return Theme.c.interchange.severityDropped
        case "degraded": return Theme.c.interchange.severityDegraded
        default: return Theme.c.interchange.severityInfo
        }
    }

    function categoryLabel(category) {
        switch (category) {
        case "transition": return qsTr("Transitions")
        case "effect": return qsTr("Effects")
        case "speed": return qsTr("Speed changes")
        case "keyframes": return qsTr("Keyframes")
        case "text": return qsTr("Text")
        case "nested": return qsTr("Nested sequences")
        case "sequence": return qsTr("Sequences")
        case "marker": return qsTr("Markers")
        case "audio": return qsTr("Audio")
        case "generator": return qsTr("Generators")
        case "media": return qsTr("Media")
        case "format": return qsTr("Format")
        case "track": return qsTr("Tracks")
        case "disabled": return qsTr("Disabled clips")
        case "clips": return qsTr("Clips")
        default: return category
        }
    }

    function timecode(us) {
        const totalFrames = Math.round(us / 1000000 * root.fps)
        const fpsInt = Math.max(1, Math.round(root.fps))
        const h = Math.floor(totalFrames / (fpsInt * 3600))
        const m = Math.floor(totalFrames / (fpsInt * 60)) % 60
        const s = Math.floor(totalFrames / fpsInt) % 60
        const f = totalFrames % fpsInt
        function pad(n) { return n.toString().padStart(2, "0") }
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(f)
    }

    function toggle(key) {
        const next = Object.assign({}, root.collapsed)
        next[key] = !next[key]
        root.collapsed = next
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: content.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: content
            width: flick.width - Theme.spacingLg
            spacing: Theme.spacingLg

            Rectangle {
                width: parent.width
                height: summary.implicitHeight + Theme.spacingLg * 2
                radius: Theme.c.interchange.radius
                color: Theme.c.interchange.summaryBackground

                Row {
                    id: summary
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacing3xl

                    Repeater {
                        model: [
                            { label: qsTr("Tracks"), value: root.report.tracks ?? 0, warn: false },
                            { label: qsTr("Clips"), value: root.report.clips ?? 0, warn: false },
                            { label: qsTr("Sequences"), value: root.report.sequences ?? 0, warn: false },
                            { label: qsTr("Assets"), value: root.report.assets ?? 0, warn: false },
                            { label: qsTr("Missing assets"), value: root.report.missingAssets ?? 0, warn: true }
                        ]

                        Column {
                            id: stat

                            required property var modelData

                            Text {
                                text: stat.modelData.value
                                color: stat.modelData.warn && stat.modelData.value > 0
                                       ? Theme.c.interchange.missingAssets : Theme.c.interchange.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeBase * 1.4
                                font.bold: true
                            }

                            Text {
                                text: stat.modelData.label
                                color: stat.modelData.warn && stat.modelData.value > 0
                                       ? Theme.c.interchange.missingAssets : Theme.c.interchange.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: root.groups.length === 0
                text: root.emptyText
                color: Theme.c.interchange.success
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: root.groups

                Column {
                    id: group

                    required property var modelData
                    readonly property bool open: !root.collapsed[group.modelData.key]

                    width: content.width
                    spacing: Theme.spacingXs

                    AbstractButton {
                        id: header

                        width: parent.width
                        implicitHeight: headerRow.implicitHeight + Theme.spacingMd * 2
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        onClicked: root.toggle(group.modelData.key)

                        Accessible.role: Accessible.Button
                        Accessible.name: headerText.text

                        background: Rectangle {
                            radius: Theme.c.interchange.radius
                            color: header.hovered ? Theme.c.interchange.groupHeaderHover : "transparent"
                        }

                        contentItem: Row {
                            id: headerRow
                            spacing: Theme.spacingMd
                            leftPadding: Theme.spacingSm

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.spacingMd
                                height: Theme.spacingMd
                                radius: width / 2
                                color: root.severityColor(group.modelData.severity)
                            }

                            Text {
                                id: headerText
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.severityLabel(group.modelData.severity) + " · "
                                      + root.categoryLabel(group.modelData.category)
                                      + " (" + group.modelData.total + ")"
                                color: Theme.c.interchange.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeBase
                                font.bold: true
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: group.open ? "▾" : "▸"
                                color: Theme.c.interchange.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeBase
                            }
                        }
                    }

                    Repeater {
                        model: group.open ? group.modelData.items : []

                        Column {
                            id: issue

                            required property var modelData
                            readonly property string detail: {
                                const parts = []
                                if (issue.modelData.itemName)
                                    parts.push(issue.modelData.itemName)
                                if (issue.modelData.at >= 0)
                                    parts.push(root.timecode(issue.modelData.at))
                                if (issue.modelData.count > 1)
                                    parts.push("×" + issue.modelData.count)
                                return parts.join("  ·  ")
                            }

                            x: Theme.spacingXl
                            width: group.width - Theme.spacingXl
                            spacing: 0

                            Text {
                                width: parent.width
                                text: issue.modelData.message
                                color: Theme.c.interchange.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: parent.width
                                visible: issue.detail !== ""
                                text: issue.detail
                                color: Theme.c.interchange.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                elide: Text.ElideRight
                            }

                            Item { width: 1; height: Theme.spacingSm }
                        }
                    }
                }
            }
        }
    }
}
