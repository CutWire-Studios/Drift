import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Drift
// One Drift Asset. The card takes the asset's own shape: a lower third is a strip, a button a
// pill, a face prop a circle, so a shelf reads as what each overlay is before any label does.
// The animated preview plays while the card is hovered or focused (or, on touch, while the
// host says it is in view); otherwise a still is shown. Nothing else moves.
FocusScope {
    id: root

    required property var asset
    property real previewHeight: 80
    property bool compact: Theme.compact
    // Touch has no hover: the host plays cards that are on screen instead.
    property bool autoplay: false
    // "Add" for Lottie and objects; "Apply" for a face prop when a clip is selected.
    property string actionLabel: qsTr("Add")
    // Briefly true after the action succeeded, so the button can say "Added".
    property bool justAdded: false

    signal openRequested()
    signal actionRequested()

    readonly property string kind: asset.mediaKind || ""
    readonly property real aspect: kind === "lottie" && Number(asset.height) > 0
                                   ? Number(asset.width) / Number(asset.height) : 1
    readonly property string installState: {
        void MarketInstall.revision
        return MarketInstall.installState(asset)
    }
    readonly property bool playing: autoplay || hover.hovered || root.activeFocus

    width: Math.round(previewHeight * aspect)
    implicitHeight: column.implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: asset.title || ""
    Accessible.description: asset.subtitle || ""

    Keys.onReturnPressed: root.openRequested()
    Keys.onEnterPressed: root.openRequested()
    Keys.onSpacePressed: root.actionRequested()

    function metaText() {
        const bits = []
        const styles = (asset.variants || []).length
        if (styles > 1)
            bits.push(qsTr("%n style(s)", "", styles))
        if (kind === "lottie") {
            const d = Number(asset.durationMs || 0) / 1000
            if (d > 0)
                bits.push(qsTr("%1 s").arg(Math.round(d * 10) / 10))
            const n = Object.keys((asset.extra || {}).slots || {}).length
            if (n > 0)
                bits.push(qsTr("%n colour(s)", "", n))
        }
        return bits.join("  ")
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: {
            root.forceActiveFocus()
            root.openRequested()
        }
    }

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingSm

        Item {
            id: stage
            width: parent.width
            height: root.previewHeight

            // The previews carry their own backdrop, so the rounding has to be a mask rather than
            // a rounded rectangle behind them.
            Item {
                id: media
                anchors.fill: parent
                visible: false
                layer.enabled: true

                Rectangle {
                    anchors.fill: parent
                    color: Theme.c.driftAssetCard.background
                }

                Image {
                    id: still
                    anchors.fill: parent
                    source: root.asset.thumbUrl || ""
                    fillMode: root.kind === "lottie" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.height: Math.ceil(root.previewHeight * Screen.devicePixelRatio)
                }

                // Only decoded while playing: 72 animated WebPs held open at once would cost far
                // more than re-reading one from the network cache on the next hover.
                AnimatedImage {
                    id: motion
                    anchors.fill: parent
                    source: root.playing ? (root.asset.previewUrl || "") : ""
                    fillMode: still.fillMode
                    asynchronous: true
                    playing: root.playing && status === AnimatedImage.Ready
                    opacity: playing ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                    }
                }
            }

            Rectangle {
                id: mask
                anchors.fill: parent
                radius: root.kind === "face-prop" ? Theme.c.driftAssetCard.radiusPill : Theme.c.driftAssetCard.radius
                visible: false
                layer.enabled: true
            }

            MultiEffect {
                anchors.fill: parent
                source: media
                maskEnabled: true
                maskSource: mask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            SkeletonBox {
                anchors.fill: parent
                visible: still.status === Image.Loading
                radius: mask.radius
            }

            Rectangle {
                anchors.fill: parent
                radius: mask.radius
                color: "transparent"
                border.width: Theme.c.driftAssetCard.borderWidth
                border.color: Theme.c.driftAssetCard.borderFocus
                visible: root.activeFocus && !hover.hovered
            }

            // The one action, on the preview so it never shifts the label rows. Always there on
            // touch; on desktop it appears with hover or keyboard focus.
            Item {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: root.kind === "face-prop" ? 0 : Theme.spacingSm
                width: actionButton.visible ? actionButton.width : progress.width
                height: actionButton.visible ? actionButton.height : progress.height
                visible: root.compact || hover.hovered || root.activeFocus
                         || root.installState === "installing" || root.justAdded

                CircularProgress {
                    id: progress
                    visible: root.installState === "installing"
                    indeterminate: true
                    size: Theme.spacing2xl + Theme.spacingSm
                    progressColor: Theme.c.driftAssetCard.accent
                }

                ThemedButton {
                    id: actionButton
                    visible: root.installState !== "installing"
                    variant: root.justAdded ? "secondary" : "primary"
                    text: root.justAdded ? qsTr("Added")
                                         : (root.installState === "failed" ? qsTr("Retry") : root.actionLabel)
                    implicitHeight: root.compact ? Theme.controlHeightSm : Theme.controlHeightSm - Theme.spacingSm
                    font.pixelSize: Theme.fontSizeXs
                    leftPadding: Theme.spacingLg
                    rightPadding: Theme.spacingLg
                    onClicked: root.actionRequested()
                }
            }
        }

        Text {
            width: parent.width
            text: root.asset.title || ""
            color: Theme.c.driftAssetCard.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            elide: Text.ElideRight
            horizontalAlignment: root.kind === "face-prop" ? Text.AlignHCenter : Text.AlignLeft
        }

        Row {
            width: parent.width
            spacing: Theme.spacingSm
            visible: meta.text.length > 0 || installed.visible

            Text {
                id: meta
                width: Math.min(implicitWidth, parent.width - (installed.visible ? installed.width + parent.spacing : 0))
                text: root.metaText()
                color: Theme.c.driftAssetCard.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideRight
            }

            IconGlyph {
                id: installed
                visible: root.installState === "installed"
                glyph: Theme.icons.check
                iconSize: Theme.iconSizeSm
                iconColor: Theme.c.driftAssetCard.icon
                anchors.verticalCenter: meta.verticalCenter

                ThemedToolTip {
                    text: qsTr("Downloaded, works offline")
                    visible: installedHover.hovered
                }
                HoverHandler { id: installedHover }
            }
        }
    }
}
