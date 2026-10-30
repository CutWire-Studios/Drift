import QtQuick
import Drift
// Full-page phone tool scaffold: preview on top, optional clip-range bar, controls, then a
// footer of X / title / check. One instance lives in AndroidEditor; open it through
// AndroidEditor.openToolPage(source, opts), where source is a Component or a URL relative to
// this file and opts = {title, editLabel, showRangeBar, trackIndex, clipIndex, previewFraction,
// centerActionText, props}. Loaded content may declare scaffold/trackIndex/clipIndex and any
// of props; they are set when present. Open runs one undo group: apply commits it, cancel/Back undoes it.
Item {
    id: root

    property string title: ""
    // Undo step text for the group a page opens. Cancel discards it, so it only names the applied result.
    property string editLabel: ""
    property bool showRangeBar: false
    property int trackIndex: -1
    property int clipIndex: -1
    property real previewFraction: 0.55
    // Optional action centred in the controls header, e.g. "Apply to all".
    property string centerActionText: ""
    default property alias controls: controlsHost.data

    // The one AndroidPreview, and where it lives while no page is open.
    property Item previewItem: null
    property Item previewHome: null
    property Item previewHomeBefore: null

    property bool _open: false
    readonly property bool isOpen: _open
    readonly property alias previewSlot: slot

    // Just before the edit group closes, so a page can still write its last edit into it.
    signal aboutToApply()
    signal applied()
    signal cancelled()
    signal opened()
    signal centerActionTriggered()

    function open(opts) {
        if (root._open)
            return false
        opts = opts || {}
        root.title = opts.title ?? ""
        root.editLabel = opts.editLabel ?? ""
        root.showRangeBar = opts.showRangeBar ?? false
        root.trackIndex = opts.trackIndex ?? EditorState.selectedTrack
        root.clipIndex = opts.clipIndex ?? EditorState.selectedClip
        root.previewFraction = opts.previewFraction ?? 0.55
        root.centerActionText = opts.centerActionText ?? ""

        EditorState.beginEditGroup()
        root._open = true
        root.visible = true
        root._attachPreview()

        root._pendingProps = opts.props || {}
        const src = opts.source
        if (src) {
            if (src.createObject !== undefined)
                loader.sourceComponent = src
            else
                loader.source = src.toString()
        }
        if (root.showRangeBar && root.trackIndex >= 0 && root.clipIndex >= 0)
            EditorState.previewClipRange(root.trackIndex, root.clipIndex)
        root.opened()
        return true
    }

    function apply() {
        if (!root._open)
            return
        root.aboutToApply()
        EditorState.endEditGroup(root.editLabel)
        root._close()
        root.applied()
    }

    function cancel() {
        if (!root._open)
            return
        if (EditorState.endEditGroup(root.editLabel))
            EditorState.undo()
        root._close()
        root.cancelled()
    }

    property var _pendingProps: ({})

    function _configure(item) {
        if (item.scaffold !== undefined)
            item.scaffold = root
        if (item.trackIndex !== undefined)
            item.trackIndex = Qt.binding(() => root.trackIndex)
        if (item.clipIndex !== undefined)
            item.clipIndex = Qt.binding(() => root.clipIndex)
        for (const k in root._pendingProps)
            item[k] = root._pendingProps[k]
    }

    function _close() {
        EditorState.clearPreviewRange()
        EditorState.playing = false
        root._open = false
        root.visible = false
        loader.active = false
        loader.sourceComponent = null
        loader.source = ""
        loader.active = true
        root._detachPreview()
    }

    function _attachPreview() {
        const p = root.previewItem
        if (!p)
            return
        p.parent = slot
        p.anchors.fill = slot
    }

    function _detachPreview() {
        const p = root.previewItem
        if (!p || !root.previewHome)
            return
        p.anchors.fill = undefined
        p.parent = root.previewHome
        if (root.previewHomeBefore)
            p.stackBefore(root.previewHomeBefore)
    }

    visible: false
    z: 100

    readonly property real safeTop: SafeArea.margins.top
    readonly property real safeBottom: Math.max(SafeArea.margins.bottom, Theme.keyboardInset)

    Rectangle {
        anchors.fill: parent
        color: Theme.c.androidToolPage.background
    }

    // Nothing under the page may receive touches.
    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onPressed: mouse => mouse.accepted = true
    }

    Item {
        id: slot
        anchors.top: parent.top
        anchors.topMargin: root.safeTop
        anchors.left: parent.left
        anchors.leftMargin: SafeArea.margins.left
        anchors.right: parent.right
        anchors.rightMargin: SafeArea.margins.right
        height: Math.round((root.height - root.safeTop - footer.height) * root.previewFraction)
        clip: true
    }

    AndroidClipRangeBar {
        id: rangeBar
        anchors.top: slot.bottom
        anchors.left: slot.left
        anchors.right: slot.right
        height: visible ? implicitHeight : 0
        visible: root._open && root.showRangeBar && root.clipIndex >= 0
        trackIndex: root.trackIndex
        clipIndex: root.clipIndex
    }

    Item {
        id: controlsArea
        anchors.top: rangeBar.bottom
        anchors.bottom: footer.top
        anchors.left: slot.left
        anchors.right: slot.right

        Item {
            id: centerAction
            visible: root.centerActionText.length > 0
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            height: visible ? Theme.androidIconButtonSize : 0
            width: actionLabel.implicitWidth + 2 * Theme.spacingLg

            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: parent.height - Theme.spacingSm
                radius: height / 2
                color: Theme.c.androidToolPage.actionBackground
            }

            Text {
                id: actionLabel
                anchors.centerIn: parent
                text: root.centerActionText
                color: Theme.c.androidToolPage.actionForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }

            TapHandler {
                onTapped: root.centerActionTriggered()
            }
        }

        Item {
            id: controlsHost
            anchors.top: centerAction.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            clip: true

            Loader {
                id: loader
                anchors.fill: parent
                onLoaded: if (root._open) root._configure(item)
            }
        }
    }

    Item {
        id: footer
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.androidBottomRailHeight + root.safeBottom

        Rectangle {
            anchors.fill: parent
            color: Theme.c.androidToolPage.footerBackground
        }

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: Theme.c.androidToolPage.borderWidth
            color: Theme.c.androidToolPage.border
        }

        Item {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: SafeArea.margins.left + Theme.spacingMd
            anchors.rightMargin: SafeArea.margins.right + Theme.spacingMd
            height: Theme.androidBottomRailHeight

            IconButton {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.x
                variant: "text"
                tooltip: qsTr("Cancel")
                onClicked: root.cancel()
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 2 * (Theme.androidIconButtonSize + Theme.spacingMd)
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.title
                color: Theme.c.androidToolPage.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
            }

            IconButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: Theme.androidIconButtonSize
                iconSize: Theme.iconSizeLg
                glyph: Theme.icons.check
                variant: "text"
                tooltip: qsTr("Apply")
                onClicked: root.apply()
            }
        }
    }
}
