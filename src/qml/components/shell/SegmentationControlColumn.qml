import QtQuick
import Drift

Column {
    id: sidebar
    required property var tool
    property alias backendBox: backendBox
    property alias qualityBox: qualityBox
    width: 260
    height: parent.height
    spacing: Theme.spacingLg

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: EditorState.segmentation.backendUsesPoints
              ? (Theme.touchInput
                 ? qsTr("Tap marks the subject, press and hold marks what to exclude. Tap a marker to remove it.")
                 : qsTr("Left-click marks the subject, right-click marks what to exclude. Click a marker to remove it."))
              : qsTr("Everyone in the shot is cut out automatically — there is nothing to click.")
    }

    // Only worth a control when there is a choice to make. The list is what is installed,
    // so this disappears entirely on a machine with one cutout addon.
    Column {
        width: parent.width
        spacing: Theme.spacingSm
        visible: backendBox.count > 1

        ThemedLabel { text: qsTr("Cut out") }

        ThemedComboBox {
            id: backendBox
            width: parent.width
            enabled: !EditorState.segmentation.running && !EditorState.segmentation.encoding
            textRole: "label"
            valueRole: "value"
            model: tool.backendModel
            onActivated: EditorState.segmentation.setBackend(currentValue, qualityBox.currentValue || "")
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm
        visible: !EditorState.segmentation.backendUsesPoints && qualityBox.count > 1

        ThemedLabel { text: qsTr("Quality") }

        ThemedComboBox {
            id: qualityBox
            width: parent.width
            enabled: !EditorState.segmentation.running && !EditorState.segmentation.encoding
            textRole: "label"
            valueRole: "value"
            model: tool.qualityModel
            onActivated: EditorState.segmentation.setBackend(EditorState.segmentation.backend, currentValue)
        }
    }

    ThemedLabel {
        width: parent.width
        text: qsTr("AI: %1").arg(EditorState.segmentation.modelVariant() || qsTr("not installed"))
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm

        ThemedLabel { text: qsTr("Result") }

        // No mode to pick any more: the cutout always lands as a mask layer on the clip's
        // own lane. Keeping the subject or the background is one Invert toggle in the
        // Masks tab afterwards, rather than a choice that has to be made up front and
        // redone from scratch if it was the wrong one.
        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: !EditorState.segmentation.forTemplate
            text: qsTr("Adds a mask layer under the clip. The clip itself is left alone — "
                       + "flip it to the background, or remove it, from the Masks tab.")
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: EditorState.segmentation.forTemplate
            text: qsTr("The cutout is only for this effect — no extra tracks are added.")
        }
    }

    ThemedButton {
        width: parent.width
        visible: EditorState.segmentation.backendUsesPoints
        variant: "secondary"
        text: qsTr("Clear points")
        enabled: EditorState.segmentation.points.length > 0 && !EditorState.segmentation.running
        onClicked: EditorState.segmentation.clearPoints()
    }

    ThemedButton {
        width: parent.width
        variant: "primary"
        text: EditorState.segmentation.running
              ? qsTr("Cutting out… %1%").arg(Math.round(EditorState.segmentation.progress * 100))
              : (EditorState.segmentation.forTemplate
                 ? qsTr("Cut out & apply effect")
                 : qsTr("Remove background"))
        enabled: (!EditorState.segmentation.backendUsesPoints
                  || EditorState.segmentation.points.length > 0)
                 && !EditorState.segmentation.running && !EditorState.segmentation.encoding
        onClicked: EditorState.segmentation.runSession(
            EditorState.segmentation.forTemplate ? "template" : "adjustment")
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm
        visible: EditorState.segmentation.running

        LabelledProgressRing {
            anchors.horizontalCenter: parent.horizontalCenter
            value: EditorState.segmentation.progress
            indeterminate: EditorState.segmentation.progress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: EditorState.segmentation.status
        }

        ThemedButton {
            width: parent.width
            variant: "destructive"
            text: qsTr("Cancel")
            onClicked: EditorState.segmentation.cancel()
        }
    }

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: !EditorState.segmentation.running
        text: qsTr("Each moment is processed, so longer clips take longer.")
    }
}
