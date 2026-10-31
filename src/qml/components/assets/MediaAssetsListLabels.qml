import QtQuick
import Drift

// Name, badges and kind line of a media list or tree row.
Column {
    id: root

    property bool folder: false
    property string assetName: ""
    property string kind: ""
    property string duration: ""
    property string proxyState: ""
    property bool editFriendly: false
    property string maskRole: ""

    anchors.verticalCenter: parent.verticalCenter

    Row {
        id: rowNameRow
        width: parent.width
        spacing: Theme.spacingSm

        MediaPill {
            id: rowProxyPill
            anchors.verticalCenter: parent.verticalCenter
            text: root.proxyState === "linked" ? qsTr("Linked proxy") : qsTr("Proxy")
            tooltip: root.proxyState === "linked"
                     ? qsTr("Using a proxy file found next to the original") : ""
            visible: root.proxyState === "ready"
                     || root.proxyState === "queued"
                     || root.proxyState === "building"
                     || root.proxyState === "linked"
            waiting: root.proxyState === "queued" || root.proxyState === "building"
        }
        MediaPill {
            id: rowMaskPill
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Mask")
            tooltip: qsTr("Drops on the timeline as a mask, not a clip")
            visible: root.maskRole.length > 0
        }
        MediaPill {
            id: rowEditFriendlyPill
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Edit-friendly")
            tooltip: qsTr("Converted to a constant frame rate for smooth editing")
            accent: Theme.c.mediaPill.editFriendlyBackground
            foreground: Theme.c.mediaPill.editFriendlyForeground
            visible: root.editFriendly
        }
        Text {
            text: root.assetName
            color: Theme.c.mediaAssetsListLabels.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
            width: Math.max(0, parent.width
                               - (rowProxyPill.visible ? rowProxyPill.width + parent.spacing : 0)
                               - (rowEditFriendlyPill.visible
                                  ? rowEditFriendlyPill.width + parent.spacing : 0)
                               - (rowMaskPill.visible ? rowMaskPill.width + parent.spacing : 0))
        }
    }
    Text {
        visible: !root.folder
        text: root.kind + (root.duration.length > 0 ? " · " + root.duration : "")
        color: Theme.c.mediaAssetsListLabels.foregroundMuted
        font.pixelSize: Theme.fontSizeXs
        font.family: Theme.fontFamily
        // Was unbounded, so it overflowed the row
        // at narrow panel widths.
        elide: Text.ElideRight
        width: parent.width
    }
}
