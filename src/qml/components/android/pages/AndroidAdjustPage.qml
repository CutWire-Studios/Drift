import QtQuick
import QtQuick.Controls.Basic
import Drift
// Colour grade on the selected clip: one icon per grade param, a slider for the chosen one. The
// grade is created on the clip by the first drag, the same as the Color panel does.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1
    property int selectedIndex: 0

    readonly property var colorState: EditorState.color
    readonly property var target: colorState.target
    readonly property bool hasTarget: !!target.hasTarget
    readonly property var params: target.params || ({})

    readonly property var adjustments: [
        { key: "exposure", label: qsTr("Exposure"), icon: Theme.icons.sun, from: -4, to: 4, def: 0 },
        { key: "contrast", label: qsTr("Contrast"), icon: Theme.icons.blend, from: -1, to: 1, def: 0 },
        { key: "saturation", label: qsTr("Saturation"), icon: Theme.icons.sparkles, from: 0, to: 2, def: 1 },
        { key: "temperature", label: qsTr("Temperature"), icon: Theme.icons.snowflake, from: -1, to: 1, def: 0 }
    ]
    readonly property var selected: adjustments[selectedIndex]
    readonly property real selectedValue: params[selected.key] ?? selected.def

    function selectTarget() {
        if (root.trackIndex >= 0 && root.clipIndex >= 0)
            EditorState.selectClip(root.trackIndex, root.clipIndex)
    }

    Component.onCompleted: root.selectTarget()
    onTrackIndexChanged: root.selectTarget()
    onClipIndexChanged: root.selectTarget()

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: adjustColumn.height + Theme.pagePadding * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: adjustColumn
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: scroll.width - Theme.pagePadding * 2
            spacing: Theme.spacing2xl
            enabled: root.hasTarget

            Row {
                id: iconRow
                width: parent.width
                spacing: Theme.spacingSm

                Repeater {
                    model: root.adjustments.length

                    delegate: Column {
                        required property int index
                        readonly property var adjustment: root.adjustments[index]
                        width: (iconRow.width - iconRow.spacing * (root.adjustments.length - 1)) / root.adjustments.length
                        spacing: Theme.spacingXs

                        IconButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            buttonSize: Theme.androidIconButtonSize
                            iconSize: Theme.iconSizeLg
                            glyph: adjustment.icon
                            variant: "ghost"
                            active: root.selectedIndex === index
                            tooltip: adjustment.label
                            onClicked: root.selectedIndex = index
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: adjustment.label
                            color: root.selectedIndex === index
                                   ? Theme.c.androidToolPage.foreground
                                   : Theme.c.androidToolPage.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: root.selectedIndex === index ? Font.Medium : Font.Normal
                        }
                    }
                }
            }

            ThemedSlider {
                id: adjustSlider
                width: parent.width
                label: root.selected.label
                from: root.selected.from
                to: root.selected.to
                valueFormatter: function (v) { return Number(v).toFixed(2) }
                Binding on value {
                    when: !adjustSlider.pressed
                    value: root.selectedValue
                }
                onPressedChanged: {
                    if (pressed)
                        root.colorState.beginEdit(qsTr("Adjust %1").arg(root.selected.label))
                    else
                        root.colorState.commitEdit()
                }
                onMoved: root.colorState.setParam(root.selected.key, value)
            }

            ThemedButton {
                text: qsTr("Reset")
                variant: "secondary"
                enabled: Math.abs(root.selectedValue - root.selected.def) > 0.001
                onClicked: {
                    root.colorState.beginEdit(qsTr("Reset %1").arg(root.selected.label))
                    root.colorState.setParam(root.selected.key, root.selected.def)
                    root.colorState.commitEdit()
                }
            }
        }
    }
}
