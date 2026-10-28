import QtQuick
import Drift

Column {
    id: root

    property var row
    property int effectIndex: -1
    spacing: 4
    width: parent ? parent.width : implicitWidth
    visible: row && row.paramData.type === "curve"

    readonly property bool hue: row && row.paramData.curveMode === "hue"

    readonly property var defaultPoints: hue
        ? [{x: 0, y: 0.5, ease: "spline"}, {x: 1, y: 0.5, ease: "spline"}]
        : [{x: 0, y: 0, ease: "spline"}, {x: 1, y: 1, ease: "spline"}]

    readonly property string pointsJson: {
        const v = row ? row.paramData.value : null
        return typeof v === "string" ? v : JSON.stringify(v ?? root.defaultPoints)
    }

    function writePoints(json) {
        EditorState.previewSetEffectTextParam(EditorState.selectedTrack, EditorState.selectedClip,
                                              root.effectIndex, root.row.paramData.key, json)
    }

    Item {
        width: parent.width
        height: Theme.controlHeightSm

        Text {
            anchors.left: parent.left
            anchors.right: resetButton.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: root.row ? root.row.paramData.label : ""
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        IconButton {
            id: resetButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            tooltip: qsTr("Reset %1").arg(root.row.paramData.label)
            buttonSize: Theme.controlHeightSm
            onClicked: {
                EditorState.beginPreviewDrag(qsTr("Reset %1").arg(root.row.paramData.label))
                root.writePoints(JSON.stringify(root.defaultPoints))
                EditorState.commitPreviewDrag()
            }
        }
    }

    CurveEditor {
        visible: root.visible
        width: Math.min(parent.width, 260)
        showTabs: false
        hueMode: root.hue
        curves: root.hue
            ? ({ hue: root.pointsJson })
            : ({ master: root.pointsJson, r: root.pointsJson, g: root.pointsJson, b: root.pointsJson })
        onBegan: EditorState.beginPreviewDrag(qsTr("Edit %1").arg(root.row.paramData.label))
        onChanged: (channel, json) => root.writePoints(json)
        onEnded: EditorState.commitPreviewDrag()
    }
}
