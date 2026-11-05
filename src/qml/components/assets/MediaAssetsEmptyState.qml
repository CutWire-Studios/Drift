import QtQuick
import Drift
// First-run screen for a project with no media at all — folders included. This area used
// to render as a blank rectangle, with no hint that the panel accepts drops or that an
// Import button exists. Hand-built rather than the shared EmptyState: it carries two
// actions and the supported-format table, which no other empty state needs, and it
// centres in the whole panel instead of sitting against the top edge.
Item {
    id: root

    property bool importing: false
    signal importRequested()
    signal importFolderRequested()

    anchors.fill: parent
    visible: AssetLibrary.count === 0 && BinFolderModel.count === 0 && !importing

    clip: true

    readonly property bool hasMarketplace: !!ShellBridge.main && !!ShellBridge.main.openMarketplace
    readonly property real sectionGap: Theme.spacingXl
    readonly property real buttonsHeight: importRow.height
        + (hasMarketplace ? marketplaceButton.height + sectionGap : 0) + sectionGap
    readonly property real textHeight: titleText.implicitHeight + hintText.implicitHeight + sectionGap
    readonly property real tableHeight: formatBox.height + sectionGap
    readonly property bool showButtons: height >= 48 + sectionGap + textHeight + buttonsHeight
    readonly property bool showFormats: showButtons
        && height >= 48 + sectionGap + textHeight + buttonsHeight + tableHeight
    readonly property bool showIcon: height >= 48 + sectionGap + textHeight
    readonly property bool showHint: height >= titleText.implicitHeight + hintText.implicitHeight + sectionGap

    Column {
        anchors.centerIn: parent
        width: Math.max(1, Math.min(parent.width - Theme.spacing3xl * 2, 300))
        spacing: Theme.spacingXl

        Rectangle {
            visible: root.showIcon
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.c.mediaAssetsEmptyState.iconRadius
            color: Theme.c.mediaAssetsEmptyState.iconBackground

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.film
                iconSize: Theme.iconSizeXl
                iconColor: Theme.c.mediaAssetsEmptyState.icon
            }
        }

        Text {
            id: titleText
            width: parent.width
            text: qsTr("No media yet")
            color: Theme.c.mediaAssetsEmptyState.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            id: hintText
            visible: root.showHint
            width: parent.width
            // The formats table below now carries the kind list, so the hint only has to
            // say how media gets in and what to do with it once it is here.
            text: qsTr("Import files or drop them here, then drag them onto the timeline. Right-click a clip to preview and trim it first.")
            color: Theme.c.mediaAssetsEmptyState.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Row {
            id: importRow
            visible: root.showButtons
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Import media")
                variant: "primary"
                glyph: Theme.icons.upload
                onClicked: importRequested()
            }

            ThemedButton {
                visible: FileDialogs.supportsDirectoryPicker()
                text: qsTr("Import folder")
                glyph: Theme.icons.folderInput
                onClicked: importFolderRequested()
            }
        }

        ThemedButton {
            id: marketplaceButton
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.showButtons && root.hasMarketplace
            text: qsTr("Browse the Marketplace")
            glyph: Theme.icons.store
            onClicked: ShellBridge.main.openMarketplace("")
        }

        Rectangle {
            id: formatBox
            visible: root.showFormats
            width: parent.width
            height: formatColumn.height + Theme.spacingXl * 2
            radius: Theme.c.mediaAssetsEmptyState.formatsRadius
            color: "transparent"
            border.width: Theme.c.mediaAssetsEmptyState.formatsBorderWidth
            border.color: Theme.c.mediaAssetsEmptyState.formatsBorder

            Column {
                id: formatColumn
                anchors.centerIn: parent
                width: parent.width - Theme.spacingXl * 2
                spacing: Theme.spacingMd

                Text {
                    width: parent.width
                    text: qsTr("Supported formats")
                    color: Theme.c.mediaAssetsEmptyState.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                    font.weight: Font.Medium
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.5
                }

                Repeater {
                    model: [
                        { glyph: Theme.icons.video, label: qsTr("Video"),
                          formats: "MP4 · MOV · MKV · WEBM · AVI" },
                        { glyph: Theme.icons.music, label: qsTr("Audio"),
                          formats: "MP3 · WAV · FLAC · AAC · OGG" },
                        { glyph: Theme.icons.image, label: qsTr("Images"),
                          formats: "PNG · JPG · WEBP · HEIC · GIF" },
                        { glyph: Theme.icons.shapes, label: qsTr("Vector"),
                          formats: "SVG · Lottie (.json, .lottie)" },
                        { glyph: Theme.icons.box, label: qsTr("3D"),
                          formats: qsTr("glTF binary (.glb) · Gaussian splats (.sog, .ply, .zip)") },
                        { glyph: Theme.icons.fileText, label: qsTr("PDF"),
                          formats: qsTr("PDF (needs the PDF Viewer addon)") }
                    ]

                    delegate: MediaAssetsFormatRow {
                        columnWidth: formatColumn.width
                    }
                }
            }
        }
    }
}
