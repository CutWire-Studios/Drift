import QtQuick
import Drift

QuickSetupMockEditor {
    id: root

    property string kind: ""
    property bool running: false
    property var keycaps: []
    property real phase: 0

    readonly property var mc: Theme.c.quickSetupMockEditor
    readonly property real corner: mc.radius
    readonly property var workspaceNames: [qsTr("Edit"), qsTr("Color"), qsTr("Keyframes"), qsTr("Subtitles")]
    readonly property var workspaceLayouts: [
        [[0, 0, 0.27, 0.58], [0.28, 0, 0.72, 0.58]],
        [[0, 0, 0.6, 0.58], [0.61, 0, 0.39, 0.58]],
        [[0, 0, 0.5, 0.58], [0.51, 0, 0.49, 0.58]],
        [[0, 0, 0.3, 0.58], [0.31, 0, 0.69, 0.58]]
    ]
    readonly property var extrasTiles: [
        { glyph: "wand-sparkles", label: qsTr("Effects") },
        { glyph: "download", label: qsTr("Transitions") },
        { glyph: "sparkles", label: qsTr("Looks") },
        { glyph: "type", label: qsTr("Fonts") },
        { glyph: "package", label: qsTr("Stickers") },
        { glyph: "bot", label: qsTr("AI tools") }
    ]

    function seg(a, b) {
        return Math.max(0, Math.min(1, (phase - a) / (b - a)))
    }

    function smooth(t) {
        return t * t * (3 - 2 * t)
    }

    onRunningChanged: if (!running) phase = 0
    onKindChanged: phase = 0

    NumberAnimation on phase {
        from: 0
        to: 1
        duration: root.kind === "shortcuts" ? 6400 : 7200
        loops: Animation.Infinite
        running: root.running
    }

    Item {
        id: searchArt

        anchors.fill: parent
        visible: root.kind === "search"

        readonly property real fade: root.smooth(root.seg(0, 0.08)) * (1 - root.seg(0.92, 1))
        readonly property string query: qsTr("color")
        readonly property int typed: Math.floor(root.seg(0.14, 0.4) * query.length + 0.0001)
        readonly property var rows: [qsTr("Color workspace"), qsTr("Color grade effect"), qsTr("Appearance settings")]

        Rectangle {
            anchors.fill: parent
            color: root.mc.window
            opacity: 0.55 * searchArt.fade
        }

        Rectangle {
            id: palette

            width: root.width * 0.6
            height: paletteColumn.height + root.gap * 4
            x: (root.width - width) / 2
            y: root.height * 0.12 - (1 - searchArt.fade) * 14
            opacity: searchArt.fade
            radius: root.corner
            color: root.mc.panel
            border.width: root.mc.borderWidth
            border.color: root.mc.border

            Column {
                id: paletteColumn

                x: root.gap * 2
                y: root.gap * 2
                width: parent.width - root.gap * 4
                spacing: root.gap

                Row {
                    height: 22
                    spacing: root.gap * 1.5

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.search
                        iconSize: 12
                        iconColor: root.mc.textMuted
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: searchArt.query.slice(0, searchArt.typed) + (Math.floor(root.phase * 24) % 2 === 0 ? "|" : "")
                        color: root.mc.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }

                Rectangle {
                    width: parent.width
                    height: root.mc.borderWidth
                    color: root.mc.border
                }

                Repeater {
                    model: searchArt.rows

                    Rectangle {
                        required property int index
                        required property string modelData

                        width: paletteColumn.width
                        height: 22
                        radius: root.corner / 2
                        color: index === (root.phase < 0.72 ? 0 : 1) ? root.mc.tile : "transparent"
                        opacity: root.smooth(root.seg(0.3 + index * 0.05, 0.4 + index * 0.05))

                        Text {
                            x: root.gap * 1.5
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: root.mc.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeTiny
                        }
                    }
                }
            }
        }
    }

    Item {
        id: workspaceArt

        anchors.fill: parent
        visible: root.kind === "workspaces"

        readonly property real stepPos: root.phase * 4
        readonly property int step: Math.min(3, Math.floor(stepPos))
        readonly property real local: stepPos - Math.floor(stepPos)
        readonly property int current: local > 0.4 ? step : (step + 3) % 4
        readonly property real menuOpen: root.smooth(Math.max(0, Math.min(1, local / 0.12, (0.5 - local) / 0.1)))

        Rectangle {
            id: pill

            anchors.horizontalCenter: parent.horizontalCenter
            y: (root.headerHeight - height) / 2
            width: root.width * 0.34
            height: root.headerHeight * 0.7
            radius: root.corner
            color: root.mc.tile

            Row {
                anchors.centerIn: parent
                spacing: root.gap

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.workspaceNames[workspaceArt.current]
                    color: root.mc.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                    font.weight: Font.Medium
                }

                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronsUpDown
                    iconSize: 10
                    iconColor: root.mc.textMuted
                }
            }
        }

        Rectangle {
            x: 0
            y: root.headerHeight
            width: root.width
            height: root.height - root.headerHeight
            color: root.mc.window

            readonly property var layout: root.workspaceLayouts[workspaceArt.current]

            Repeater {
                model: 2

                Rectangle {
                    id: panelRect

                    required property int index
                    readonly property var cell: parent.layout[index]

                    x: root.gap + cell[0] * (root.width - root.gap * 2)
                    y: root.gap + cell[1] * parent.height
                    width: cell[2] * (root.width - root.gap * 2) - (cell[0] + cell[2] < 1 ? root.gap : 0)
                    height: cell[3] * parent.height
                    radius: root.corner
                    color: root.mc.panel
                    border.width: root.mc.borderWidth
                    border.color: root.mc.border

                    Behavior on x { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easingInOut } }
                    Behavior on y { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easingInOut } }
                    Behavior on width { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easingInOut } }
                    Behavior on height { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easingInOut } }

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.7
                        height: parent.height * 0.6
                        radius: root.corner / 2
                        color: root.mc.tile
                        opacity: index === 1 ? 1 : 0.7
                    }
                }
            }

            Rectangle {
                x: root.gap
                y: root.gap + parent.height * 0.6
                width: root.width - root.gap * 2
                height: parent.height * 0.4 - root.gap * 2
                radius: root.corner
                color: root.mc.panel
                border.width: root.mc.borderWidth
                border.color: root.mc.border

                Column {
                    anchors.fill: parent
                    anchors.margins: root.gap
                    spacing: root.gap

                    Repeater {
                        model: 3

                        Rectangle {
                            required property int index

                            width: parent.width
                            height: (parent.height - root.gap * 2) / 3
                            radius: root.corner / 2
                            color: root.mc.track

                            Rectangle {
                                x: parent.width * (0.06 + index * 0.1)
                                width: parent.width * 0.5
                                height: parent.height
                                radius: root.corner / 2
                                color: index === 0 ? root.mc.clipVideo : index === 1 ? root.mc.clipText : root.mc.clipAudio
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            x: pill.x
            y: pill.y + pill.height + root.gap
            width: pill.width
            height: menuColumn.height + root.gap * 2
            radius: root.corner
            color: root.mc.panel
            border.width: root.mc.borderWidth
            border.color: root.mc.border
            opacity: workspaceArt.menuOpen
            visible: opacity > 0.01

            Column {
                id: menuColumn

                x: root.gap
                y: root.gap
                width: parent.width - root.gap * 2

                Repeater {
                    model: root.workspaceNames

                    Rectangle {
                        required property int index
                        required property string modelData

                        width: menuColumn.width
                        height: 18
                        radius: root.corner / 2
                        color: index === workspaceArt.step ? root.mc.tile : "transparent"

                        Text {
                            x: root.gap * 1.5
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: root.mc.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeTiny
                        }
                    }
                }
            }
        }
    }

    Item {
        id: extrasArt

        anchors.fill: parent
        visible: root.kind === "extras"

        readonly property real fade: root.smooth(root.seg(0.04, 0.14)) * (1 - root.seg(0.9, 1))
        readonly property real progress: root.seg(0.3, 0.68)
        readonly property bool done: root.phase >= 0.68

        Rectangle {
            id: extrasButton

            x: root.width - width - root.gap * 2 - exportSpace
            y: (root.headerHeight - height) / 2
            width: root.headerHeight * 0.7
            height: width
            radius: root.corner
            color: extrasArt.fade > 0.5 ? root.mc.tile : "transparent"

            readonly property real exportSpace: 54

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.puzzle
                iconSize: 12
                iconColor: root.mc.text
            }
        }

        Rectangle {
            anchors.fill: parent
            color: root.mc.window
            opacity: 0.55 * extrasArt.fade
        }

        Rectangle {
            id: extrasPanel

            width: root.width * 0.66
            height: root.height * 0.7
            x: (root.width - width) / 2
            y: root.height * 0.22 - (1 - extrasArt.fade) * 12
            opacity: extrasArt.fade
            radius: root.corner
            color: root.mc.panel
            border.width: root.mc.borderWidth
            border.color: root.mc.border

            Grid {
                anchors.fill: parent
                anchors.margins: root.gap * 2
                columns: 3
                spacing: root.gap * 1.5

                Repeater {
                    model: root.extrasTiles

                    Rectangle {
                        id: tile

                        required property int index
                        required property var modelData
                        readonly property bool active: index === 1

                        width: (extrasPanel.width - root.gap * 4 - root.gap * 3) / 3
                        height: (extrasPanel.height - root.gap * 4 - root.gap * 1.5) / 2
                        radius: root.corner
                        color: root.mc.tile

                        Column {
                            anchors.centerIn: parent
                            spacing: root.gap

                            IconGlyph {
                                anchors.horizontalCenter: parent.horizontalCenter
                                glyph: tile.active && extrasArt.done ? Theme.icons.check : tile.modelData.glyph
                                iconSize: 18
                                iconColor: tile.active && extrasArt.done ? root.mc.primary : root.mc.text
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: tile.modelData.label
                                color: root.mc.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeTiny
                            }
                        }

                        Rectangle {
                            x: root.gap * 1.5
                            y: parent.height - height - root.gap * 1.5
                            width: (parent.width - root.gap * 3) * extrasArt.progress
                            height: 3
                            radius: 1.5
                            color: root.mc.primary
                            visible: tile.active && extrasArt.progress > 0 && !extrasArt.done
                        }
                    }
                }
            }
        }
    }

    Item {
        id: shortcutsArt

        anchors.fill: parent
        visible: root.kind === "shortcuts"

        readonly property real fade: root.smooth(root.seg(0, 0.06)) * (1 - root.seg(0.94, 1))
        readonly property real stepPos: root.phase * Math.max(1, root.keycaps.length)
        readonly property int current: Math.floor(stepPos)
        readonly property real press: Math.sin(Math.PI * Math.min(1, (stepPos - current) * 2.2))

        Rectangle {
            anchors.fill: parent
            color: root.mc.window
            opacity: 0.55 * shortcutsArt.fade
        }

        Row {
            id: keyRow

            anchors.centerIn: parent
            spacing: root.gap * 3
            opacity: shortcutsArt.fade

            Repeater {
                model: root.keycaps

                Column {
                    id: group

                    required property int index
                    required property var modelData
                    readonly property bool active: index === shortcutsArt.current

                    spacing: root.gap * 1.5
                    scale: active ? 1 - 0.1 * shortcutsArt.press : 1
                    opacity: active ? 1 : 0.55

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: root.gap

                        Repeater {
                            model: group.modelData.parts

                            Rectangle {
                                required property string modelData

                                width: Math.max(26, keyLabel.implicitWidth + 14)
                                height: 26
                                radius: root.corner
                                color: group.active ? root.mc.primary : root.mc.panel
                                border.width: root.mc.borderWidth
                                border.color: root.mc.border

                                Text {
                                    id: keyLabel

                                    anchors.centerIn: parent
                                    text: modelData
                                    color: group.active ? root.mc.primaryForeground : root.mc.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeTiny
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: group.modelData.label
                        color: root.mc.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTiny
                    }
                }
            }
        }
    }

    Item {
        id: aiArt

        anchors.fill: parent
        visible: root.kind === "ai"

        readonly property int stage: Math.min(2, Math.floor(root.phase * 3))
        readonly property real local: root.phase * 3 - Math.floor(root.phase * 3)
        readonly property var chips: [qsTr("Auto subtitles"), qsTr("People cutout"), qsTr("Find scenes")]
        readonly property var words: qsTr("Hello and welcome back").split(" ")
        readonly property int shownWords: stage === 0 ? Math.floor(root.smooth(Math.min(1, local / 0.7)) * words.length + 0.0001) : 0

        Rectangle {
            x: root.stageX
            y: root.stageY
            width: root.stageWidth
            height: root.stageHeight
            radius: root.corner
            color: root.mc.panel
            border.width: root.mc.borderWidth
            border.color: root.mc.border
            clip: true

            Row {
                x: root.gap
                y: root.gap
                spacing: root.gap

                Repeater {
                    model: aiArt.chips

                    Rectangle {
                        required property int index
                        required property string modelData

                        width: chipLabel.implicitWidth + root.gap * 3
                        height: 16
                        radius: height / 2
                        color: index === aiArt.stage ? root.mc.primary : root.mc.tile

                        Text {
                            id: chipLabel

                            anchors.centerIn: parent
                            text: modelData
                            color: index === aiArt.stage ? root.mc.primaryForeground : root.mc.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeTiny
                        }

                        Behavior on color { ColorAnimation { duration: Theme.durationBase } }
                    }
                }
            }

            Rectangle {
                id: aiFrame

                anchors.horizontalCenter: parent.horizontalCenter
                y: 16 + root.gap * 2
                height: parent.height - y - root.gap
                width: Math.min(height * 16 / 9, parent.width - root.gap * 2)
                radius: root.corner / 2
                color: root.mc.track
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: root.mc.clipVideo
                    opacity: aiArt.stage === 1 ? 0.6 * (1 - root.smooth(Math.min(1, Math.max(0, (aiArt.local - 0.25) / 0.35)))) : 0.6
                }

                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: parent.width * 0.3
                    height: parent.height * 0.85
                    opacity: aiArt.stage === 1 ? 1 : 0.5

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height * 0.12
                        width: parent.height * 0.26
                        height: width
                        radius: width / 2
                        color: root.mc.textMuted
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height * 0.42
                        width: parent.width
                        height: parent.height * 0.7
                        radius: width / 3
                        color: root.mc.textMuted
                    }
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: root.gap * 1.5
                    width: captionText.implicitWidth + root.gap * 3
                    height: 16
                    radius: root.corner / 2
                    color: root.mc.window
                    opacity: aiArt.shownWords > 0 ? 0.9 : 0

                    Behavior on width { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }

                    Text {
                        id: captionText

                        anchors.centerIn: parent
                        text: aiArt.words.slice(0, aiArt.shownWords).join(" ")
                        color: root.mc.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTiny
                    }
                }
            }
        }

        Repeater {
            model: [0.32, 0.56]

            Rectangle {
                required property int index
                required property real modelData

                readonly property real appear: aiArt.stage === 2 ? root.smooth(Math.min(1, Math.max(0, (aiArt.local - 0.15 - index * 0.25) / 0.2))) : 0

                x: root.gap * 2 + modelData * (root.width - root.gap * 4)
                y: root.timelineY + root.gap
                width: 2
                height: root.timelineHeight * 0.34 * appear
                color: root.mc.playhead
                visible: appear > 0
            }
        }
    }

    Item {
        id: touchArt

        anchors.fill: parent
        visible: root.kind === "touch"

        readonly property real zoom: 1 + 1.2 * root.smooth(root.seg(0.05, 0.4)) - 1.2 * root.smooth(root.seg(0.45, 0.52))
        readonly property real spread: 8 + 46 * root.smooth(root.seg(0.05, 0.4))
        readonly property real hold: root.seg(0.58, 0.8)
        readonly property real menu: root.smooth(root.seg(0.78, 0.86)) * (1 - root.seg(0.94, 1))

        Rectangle {
            id: touchTimeline

            x: root.gap
            y: root.timelineY
            width: root.width - root.gap * 2
            height: root.timelineHeight
            radius: root.corner
            color: root.mc.panel
            border.width: root.mc.borderWidth
            border.color: root.mc.border
            clip: true

            Column {
                anchors.fill: parent
                anchors.margins: root.gap
                spacing: root.gap

                Repeater {
                    model: root.trackClips

                    Rectangle {
                        id: lane

                        required property int index
                        required property var modelData

                        width: parent.width
                        height: (touchTimeline.height - root.gap * 5) / 3
                        radius: root.corner / 2
                        color: root.mc.track
                        clip: true

                        Repeater {
                            model: lane.modelData

                            Rectangle {
                                required property int index
                                required property var modelData

                                readonly property bool picked: lane.index === 0 && index === 1 && touchArt.hold > 0.95

                                x: lane.width * (0.42 + (modelData.x - 0.42) * touchArt.zoom)
                                width: lane.width * modelData.w * touchArt.zoom
                                height: lane.height
                                radius: root.corner / 2
                                color: root.mc[modelData.c]
                                border.width: picked ? 2 : 0
                                border.color: root.mc.primary
                            }
                        }
                    }
                }
            }

            Rectangle {
                x: touchTimeline.width * 0.42
                width: 2
                height: parent.height
                color: root.mc.playhead
            }
        }

        Repeater {
            model: 2

            Rectangle {
                required property int index

                visible: root.phase < 0.46
                x: touchTimeline.x + touchTimeline.width * 0.42 + (index === 0 ? -1 : 1) * touchArt.spread - width / 2
                y: touchTimeline.y + touchTimeline.height * 0.5 + (index === 0 ? 1 : -1) * touchArt.spread * 0.25 - height / 2
                width: 22
                height: width
                radius: width / 2
                color: root.mc.primary
                opacity: 0.45 * root.smooth(root.seg(0, 0.05))
                border.width: 2
                border.color: root.mc.primary
            }
        }

        Item {
            id: pressPoint

            x: touchTimeline.x + touchTimeline.width * (0.42 + (0.34 + 0.11 - 0.42) * touchArt.zoom)
            y: touchTimeline.y + touchTimeline.height * 0.2
            visible: root.phase >= 0.56 && root.phase < 0.96

            Rectangle {
                anchors.centerIn: parent
                width: 12 + 26 * (1 - touchArt.hold)
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: root.mc.primary
                opacity: 1 - touchArt.hold * 0.6
            }

            Rectangle {
                anchors.centerIn: parent
                width: 18
                height: width
                radius: width / 2
                color: root.mc.primary
                opacity: 0.45
            }

            Rectangle {
                x: 10
                y: -touchMenuLabel.implicitHeight - 18
                width: touchMenuLabel.implicitWidth + root.gap * 3
                height: touchMenuLabel.implicitHeight + root.gap * 2
                radius: root.corner
                color: root.mc.panel
                border.width: root.mc.borderWidth
                border.color: root.mc.border
                opacity: touchArt.menu
                visible: opacity > 0.01

                Text {
                    id: touchMenuLabel

                    anchors.centerIn: parent
                    text: qsTr("Select multiple")
                    color: root.mc.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                }
            }
        }
    }
}
