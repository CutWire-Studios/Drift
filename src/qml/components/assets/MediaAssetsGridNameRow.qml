import QtQuick
import Drift

// Name under a media grid card, with the proxy and edit-friendly pills in front of it.
Row {
    id: root

    property string proxyState: ""
    property bool editFriendly: false
    property string assetName: ""
    property string maskRole: ""

    width: parent ? parent.width : 0
    spacing: Theme.spacingSm

    MediaPill {
        id: cardProxyPill
        anchors.verticalCenter: parent.verticalCenter
        text: root.proxyState === "linked" ? qsTr("Linked proxy") : qsTr("Proxy")
        tooltip: root.proxyState === "linked"
                 ? qsTr("Using a proxy file found next to the original") : ""
        visible: root.proxyState === "ready" || root.proxyState === "queued"
                 || root.proxyState === "building" || root.proxyState === "linked"
        waiting: root.proxyState === "queued" || root.proxyState === "building"
        fontSize: Theme.fontSizeTiny
    }
    MediaPill {
        id: cardMaskPill
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Mask")
        tooltip: qsTr("Drops on the timeline as a mask, not a clip")
        visible: root.maskRole.length > 0
        fontSize: Theme.fontSizeTiny
    }
    MediaPill {
        id: cardEditFriendlyPill
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Edit-friendly")
        tooltip: qsTr("Converted to a constant frame rate for smooth editing")
        accent: Theme.c.mediaPill.editFriendlyBackground
        foreground: Theme.c.mediaPill.editFriendlyForeground
        visible: root.editFriendly
        fontSize: Theme.fontSizeTiny
    }
    Text {
        width: Math.max(0, parent.width
                           - (cardProxyPill.visible ? cardProxyPill.width + parent.spacing : 0)
                           - (cardEditFriendlyPill.visible
                              ? cardEditFriendlyPill.width + parent.spacing : 0)
                           - (cardMaskPill.visible ? cardMaskPill.width + parent.spacing : 0))
        text: root.assetName
        color: Theme.c.mediaAssetsGridNameRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        elide: Text.ElideRight
    }
}
