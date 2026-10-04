import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

// Everything about one Drift Asset, and the place to set its colours before adding it, so a
// brand colour is picked once rather than fixed clip by clip afterwards.
Rectangle {
    id: root

    property var asset: ({})
    property string categoryLabel: ""
    property bool compact: Theme.compact
    // Colour slot overrides chosen here: {slotId: "#RRGGBB"}.
    property var slotValues: ({})
    // Empty selects the default design. Set when the asset has a variants list.
    property string selectedVariantId: ""

    signal backRequested()
    // Lottie / object: add at the playhead. Face prop: apply to the selected clip.
    signal useRequested(var slots)
    // Lottie / object: media bin only. Face prop: the Face Props library only.
    signal keepRequested()

    readonly property string kind: asset.kind || ""
    readonly property var styleList: asset.variants || []
    // The design on screen. A pack with variants carries a full copy of each design, including
    // the default, so the row can swap preview, colours and timing without leaving the asset.
    readonly property var selectedStyle: {
        const list = root.styleList
        if (!list || list.length === 0)
            return null
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === root.selectedVariantId)
                return list[i]
        }
        return list[0]
    }
    readonly property var design: root.selectedStyle || root.asset
    readonly property string installState: {
        void DriftAssets.revision
        return asset.id ? DriftAssets.variantState(asset.id, root.selectedVariantId) : "none"
    }
    readonly property bool clipSelected: AppController.selectedClip >= 0
    readonly property real aspect: kind === "lottie" && Number(design.preview_height) > 0
                                   ? Number(design.preview_width) / Number(design.preview_height) : 1

    color: Theme.panelBackground

    // A Rectangle takes no input, and TapHandlers only grab passively, so without this a tap
    // on the panel (a colour swatch, a button) also reached the card lying underneath it.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true }
    }
    onAssetChanged: {
        selectedVariantId = ""
        slotValues = ({})
    }

    function playbackText() {
        switch (design.playback) {
        case "loop": return qsTr("Loops seamlessly")
        case "intro-hold": return qsTr("Plays in, then holds")
        case "intro-hold-outro": return qsTr("Plays in, holds, plays out")
        }
        return ""
    }

    function licenseUrl() {
        return asset.license === "CC-BY-NC-SA-4.0" ? "https://creativecommons.org/licenses/by-nc-sa/4.0/" : ""
    }

    // Fixed above the scrolling body so Back is always in reach. The phone sheet has none.
    Row {
        id: header
        x: Theme.pagePadding
        spacing: Theme.spacingSm
        topPadding: Theme.spacingSm
        bottomPadding: Theme.spacingSm
        visible: !root.compact

        IconButton {
            glyph: Theme.icons.chevronLeft
            tooltip: qsTr("Back")
            onClicked: root.backRequested()
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.categoryLabel
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }

    Flickable {
        id: flick
        anchors.top: header.visible ? header.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentHeight: body.implicitHeight + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: body
            x: root.compact ? Theme.androidPagePadding : Theme.pagePadding
            width: flick.width - x * 2
            spacing: Theme.spacingXl
            topPadding: root.compact ? 0 : Theme.spacingSm

            Rectangle {
                width: parent.width
                height: root.kind === "lottie" ? Math.min(220, width / root.aspect) : Math.min(240, width)
                radius: Theme.radiusMd
                color: Theme.panelAccent
                clip: true

                AnimatedImage {
                    anchors.fill: parent
                    source: root.design.preview_url || ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    playing: root.visible
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    width: parent.width
                    text: root.asset.name || ""
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase * 1.2
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: root.design.description || ""
                    visible: text.length > 0
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    lineHeight: 1.3
                    wrapMode: Text.WordWrap
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.styleList.length > 1

                Text {
                    text: qsTr("Style")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingLg

                    Repeater {
                        model: root.styleList
                        delegate: Column {
                            id: styleChoice
                            required property var modelData
                            spacing: Theme.spacingXs

                            readonly property bool chosen: modelData.id === (root.selectedStyle && root.selectedStyle.id)

                            Rectangle {
                                id: styleSwatch
                                width: Theme.spacing3xl * 2
                                height: width
                                radius: root.kind === "face-prop" ? height / 2 : Theme.radiusSm
                                color: Theme.panelAccent
                                border.width: styleChoice.chosen || styleHover.hovered ? Theme.borderWidthFocus : Theme.borderWidth
                                border.color: styleChoice.chosen ? Theme.primary : Theme.panelBorder
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: styleSwatch.border.width
                                    source: modelData.thumb_url || ""
                                    fillMode: root.kind === "lottie" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                }

                                HoverHandler { id: styleHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    onTapped: {
                                        if (root.selectedVariantId === modelData.id)
                                            return
                                        root.selectedVariantId = modelData.id
                                        root.slotValues = ({})
                                    }
                                }

                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.name || ""
                                Accessible.checkable: true
                                Accessible.checked: styleChoice.chosen
                            }
                            Text {
                                width: styleSwatch.width
                                text: modelData.name || ""
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: slotRepeater.count > 0

                Item {
                    width: parent.width
                    height: coloursLabel.implicitHeight
                    Text {
                        id: coloursLabel
                        text: qsTr("Colours")
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.Medium
                    }
                    Text {
                        id: resetColours
                        anchors.right: parent.right
                        anchors.verticalCenter: coloursLabel.verticalCenter
                        text: qsTr("Reset")
                        visible: Object.keys(root.slotValues).length > 0
                        color: resetHover.hovered ? Theme.panelForeground : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        HoverHandler { id: resetHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.slotValues = ({}) }
                    }
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingLg

                    Repeater {
                        id: slotRepeater
                        model: Object.keys(root.design.slots || {})
                        delegate: Column {
                            required property string modelData
                            spacing: Theme.spacingXs

                            Rectangle {
                                id: swatch
                                width: Theme.spacing3xl + Theme.spacingSm
                                height: width
                                radius: Theme.radiusSm
                                color: root.slotValues[modelData] || root.design.slots[modelData]
                                border.width: swatchHover.hovered ? Theme.borderWidthFocus : Theme.borderWidth
                                border.color: swatchHover.hovered ? Theme.primary : Theme.panelBorder

                                HoverHandler { id: swatchHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    onTapped: {
                                        const dialog = colourDialog.ensure()
                                        dialog.slotId = modelData
                                        dialog.selectedColor = swatch.color
                                        dialog.open()
                                    }
                                }
                            }
                            Text {
                                anchors.horizontalCenter: swatch.horizontalCenter
                                text: modelData
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXs

                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: {
                        if (root.kind === "lottie") {
                            const d = Number(root.design.duration || 0)
                            const parts = []
                            if (d > 0)
                                parts.push(qsTr("%1 s").arg(Math.round(d * 10) / 10))
                            if (root.playbackText().length > 0)
                                parts.push(root.playbackText())
                            if (root.design.text_area)
                                parts.push(qsTr("room for your text"))
                            return parts.join(", ")
                        }
                        if (root.kind === "object")
                            return qsTr("3D model, loops every %1 s").arg(Math.round(Number((root.design.animation || {}).duration || 0) * 10) / 10)
                        return qsTr("Tracks a face in the clip it is applied to")
                    }
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    visible: !!root.asset.license
                    text: root.licenseUrl().length > 0
                          ? qsTr("Licence <a href=\"%1\">CC BY-NC-SA 4.0</a>").arg(root.licenseUrl())
                          : qsTr("Licence %1").arg(root.asset.license || "")
                    textFormat: Text.StyledText
                    linkColor: Theme.accentOnPanel
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    wrapMode: Text.WordWrap
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                }
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedButton {
                    variant: "primary"
                    enabled: root.installState !== "installing"
                             && (root.kind !== "face-prop" || root.clipSelected)
                    text: root.kind === "face-prop" ? qsTr("Apply to selected clip") : qsTr("Add at playhead")
                    tooltip: root.kind === "face-prop" && !root.clipSelected
                             ? qsTr("Select a clip with a face on the timeline first") : ""
                    onClicked: root.useRequested(root.slotValues)
                }
                ThemedButton {
                    variant: "secondary"
                    enabled: root.installState !== "installing"
                    text: root.kind === "face-prop" ? qsTr("Add to Face props") : qsTr("Add to media bin")
                    onClicked: root.keepRequested()
                }
            }

            Text {
                width: parent.width
                visible: root.kind === "face-prop" && !root.clipSelected
                text: qsTr("Select a clip with a face on the timeline to apply this prop.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
            }
        }
    }

    LazyLoader {
        id: colourDialog
        sourceComponent: Component {
            ThemedColorDialog {
                property string slotId: ""
                title: qsTr("Choose colour")
                onAccepted: {
                    const next = Object.assign({}, root.slotValues)
                    next[slotId] = selectedColor.toString()
                    root.slotValues = next
                }
            }
        }
    }
}
