import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

// Slim CapCut-style editor top bar.
Item {
    id: root

    signal backRequested()
    signal exportRequested()
    signal exportProgressRequested()
    // The project title is the affordance: a wide, self-labelling target in the middle of
    // the bar rather than an unlabelled glyph in the corner.
    signal projectMenuRequested()
    signal cropRequested()

    // Quick edit promotes Crop into the bar. In the full editor it is a button inside the
    // Settings tab's canvas section, which is the right home for a project-wide setting but
    // four taps from a clip — and reframing a vertical clip is most of what a phone edit is.
    property bool quickMode: false

    // Status bar / camera cutout. Without it the Back, Undo and Export buttons sat
    // underneath the system bar on every edge-to-edge device.
    readonly property real topInset: SafeArea.margins.top
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    height: Theme.androidTopBarHeight + topInset
    width: parent ? parent.width : 0

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    Item {
        id: barBody
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset
        height: Theme.androidTopBarHeight

        IconButton {
            id: backBtn
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.androidIconButtonSize
            iconSize: Theme.iconSizeLg
            glyph: Theme.icons.chevronLeft
            variant: "text"
            tooltip: qsTr("Back")
            onClicked: root.backRequested()
        }

        AbstractButton {
            id: titleBtn
            anchors.left: backBtn.right
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.androidIconButtonSize
            hoverEnabled: true

            Accessible.role: Accessible.Button
            Accessible.name: titleText.text
            Accessible.description: qsTr("Project actions")

            scale: titleBtn.down ? Theme.pressScale : 1.0

            Behavior on scale {
                NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
            }

            background: Rectangle {
                radius: Theme.radiusSm
                color: titleBtn.down ? Theme.panelAccent : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }

            contentItem: Row {
                spacing: Theme.spacingMd
                leftPadding: Theme.spacingMd
                rightPadding: Theme.spacingMd

                Text {
                    id: titleText
                    anchors.verticalCenter: parent.verticalCenter
                    // Clamped against what the right-hand cluster actually leaves, not a
                    // fixed 140: the export pill is wider than the icon it replaced.
                    width: Math.min(implicitWidth,
                                    Math.max(0, barBody.width - actionsRow.width - backBtn.width
                                                - Theme.iconSizeMd - Theme.spacing3xl))
                    text: EditorState.projectName.length > 0
                          ? EditorState.projectName
                          : qsTr("Untitled")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: EditorState.hasUnsavedChanges ? Theme.destructive : Theme.constructive
                }

                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronDown
                    iconSize: Theme.iconSizeMd
                    iconColor: Theme.mutedForeground
                }
            }

            onClicked: {
                Haptics.press()
                root.projectMenuRequested()
            }
        }

        Row {
            id: actionsRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs

            // Icon-only alongside Undo/Redo rather than a labelled pill: back, title,
            // three icons and the Export pill already fill a 360dp bar, and a fourth
            // labelled control would elide the project title to nothing.
            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.crop
                variant: "text"
                visible: root.quickMode
                tooltip: qsTr("Crop")
                onClicked: root.cropRequested()
            }

            // Undo/Redo stay icon-only — the one documented exception, and long-pressable
            // for a label now that IconButton shows its tooltip on touch.
            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.undo
                variant: "text"
                tooltip: qsTr("Undo")
                enabled: EditorState.undoAvailable
                onClicked: EditorState.undo()
            }

            IconButton {
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.redo
                variant: "text"
                tooltip: qsTr("Redo")
                enabled: EditorState.redoAvailable
                onClicked: EditorState.redo()
            }

            // Doubles as the way back into a render whose progress sheet was dismissed:
            // without it a running export is unreachable and uncancellable, there being no
            // second window on a phone to leave it open in. The ring is drawn around the
            // glyph rather than replacing it, so the pill keeps its width and its label
            // while the render runs.
            ThemedButton {
                id: exportPill
                anchors.verticalCenter: parent.verticalCenter
                variant: "primary"
                glyph: Theme.icons.upload
                text: qsTr("Export")
                tooltip: EditorState.exportInProgress ? qsTr("Show export progress")
                                                      : qsTr("Export")
                onClicked: EditorState.exportInProgress ? root.exportProgressRequested()
                                                        : root.exportRequested()

                CircularProgress {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingXl + Theme.iconSizeMd / 2 - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    visible: EditorState.exportInProgress
                    size: Theme.iconSizeXl
                    strokeWidth: 2
                    value: EditorState.exportProgress
                }
            }
        }
    }
}
