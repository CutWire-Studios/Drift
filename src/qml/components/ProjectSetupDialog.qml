import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedDialog {
    id: root

    property int assetIndex: -1
    property var pendingRunner: null
    property int outWidth: 1920
    property int outHeight: 1080
    property int outFps: 30
    property string aspectMode: "source"
    property string sourceName: ""

    title: qsTr("Set up your video")
    acceptText: qsTr("Create")
    preferredWidth: Theme.dialogWidthMd

    function openForAsset(index, runner) {
        assetIndex = index
        pendingRunner = runner
        const suggested = EditorState.suggestedProjectSetupForAsset(index)
        outWidth = suggested.width || 1920
        outHeight = suggested.height || 1080
        outFps = suggested.fps || 30
        aspectMode = suggested.aspect || "source"
        sourceName = suggested.name || ""
        open()
    }

    function applyAspectPreset(mode) {
        aspectMode = mode
        if (mode === "16:9") {
            outHeight = Math.max(16, Math.round(outWidth * 9 / 16))
        } else if (mode === "9:16") {
            outHeight = Math.max(16, Math.round(outWidth * 16 / 9))
        } else if (mode === "4:3") {
            outHeight = Math.max(16, Math.round(outWidth * 3 / 4))
        } else if (mode === "1:1") {
            outHeight = outWidth
        } else if (mode === "source") {
            const suggested = EditorState.suggestedProjectSetupForAsset(assetIndex)
            outWidth = suggested.width || outWidth
            outHeight = suggested.height || outHeight
        }
    }

    onAccepted: {
        EditorState.setProjectSetup(outWidth, outHeight, outFps)
        EditorState.markProjectLayoutChosen()
        if (typeof pendingRunner === "function")
            pendingRunner()
        pendingRunner = null
    }

    onRejected: {
        pendingRunner = null
    }

    contentItem: Column {
        spacing: 12
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            size: "sm"
            text: sourceName.length > 0
                  ? qsTr("First clip “%1”. Choose the video size before it is placed.").arg(sourceName)
                  : qsTr("Choose the video size before adding your first clip.")
        }

        ThemedLabel {
            text: qsTr("Aspect ratio")
        }

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: [
                    { id: "source", label: qsTr("Match clip") },
                    { id: "16:9", label: "16:9" },
                    { id: "9:16", label: "9:16" },
                    { id: "4:3", label: "4:3" },
                    { id: "1:1", label: "1:1" },
                    { id: "custom", label: qsTr("Custom") }
                ]

                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: root.aspectMode === modelData.id
                    chipHeight: Theme.controlHeightSm
                    onClicked: root.applyAspectPreset(modelData.id)
                }
            }
        }

        // Were raw ThemedTextFields that silently reverted bad input and had no
        // bounds at all, so a width of 1 or 100000 was accepted without comment.
        Row {
            width: parent.width
            spacing: Theme.spacingLg

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: Theme.spacingSm
                ThemedLabel { text: qsTr("Width") }
                ThemedNumberField {
                    width: parent.width
                    from: 16
                    to: 16384
                    step: 2
                    unit: "px"
                    value: root.outWidth
                    onEdited: v => {
                        root.outWidth = v
                        if (root.aspectMode !== "custom" && root.aspectMode !== "source")
                            root.applyAspectPreset(root.aspectMode)
                        else
                            root.aspectMode = "custom"
                    }
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: Theme.spacingSm
                ThemedLabel { text: qsTr("Height") }
                ThemedNumberField {
                    width: parent.width
                    from: 16
                    to: 16384
                    step: 2
                    unit: "px"
                    value: root.outHeight
                    onEdited: v => {
                        root.outHeight = v
                        root.aspectMode = "custom"
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            ThemedLabel { text: qsTr("Frames per second") }
            ThemedNumberField {
                width: parent.width / 2 - Theme.spacingSm
                from: 1
                to: 240
                unit: "fps"
                value: root.outFps
                onEdited: v => root.outFps = v
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width - resetButton.width - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                tone: "default"
                size: "sm"
                font.family: Theme.monoFontFamily
                text: qsTr("Video: %1×%2, %3 frames per second").arg(root.outWidth).arg(root.outHeight).arg(root.outFps)
            }

            // Recovering the suggested values previously meant knowing to
            // re-click the "Match clip" chip.
            ThemedButton {
                id: resetButton
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Reset")
                variant: "ghost"
                glyph: Theme.icons.reset
                tooltip: qsTr("Restore the size suggested by your first clip")
                onClicked: root.applyAspectPreset("source")
            }
        }
    }
}
