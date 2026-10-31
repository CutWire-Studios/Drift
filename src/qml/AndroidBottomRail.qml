import QtQuick
import QtQuick.Controls.Basic
import Drift
// Bottom tool strip: the [+] fixed at the left, then one icon-over-label entry per tool in a
// horizontally scrolling row. Entries are plain data in `items`; the editor decides what each opens.
Item {
    id: root

    // "" | "effects" | "sounds" | "transitions", plus any tab id the
    // Add menu routes to.
    property string activeId: ""
    signal tabRequested(string tabId)
    // A full-page tool; AndroidEditor.openTool() maps the id to a page.
    signal toolRequested(string toolId, var props)
    signal editTextRequested()
    signal addRequested()

    readonly property bool hasSelection: {
        void EditorState.selectionRevision
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }

    readonly property real bottomInset: SafeArea.margins.bottom
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    height: Theme.androidBottomRailHeight + bottomInset
    width: parent ? parent.width : 0

    readonly property var clipData: {
        void EditorState.selectionRevision
        return root.hasSelection ? EditorState.selectedClipData : ({})
    }
    readonly property string clipKind: root.clipData.kind || ""
    readonly property string adjustmentKind: root.clipData.adjustmentKind || ""
    readonly property var clipCaps: root.clipData.caps || []
    readonly property bool hasTransition: EditorState.selectedTransitionTrack >= 0
    readonly property bool multiSelection: {
        void EditorState.selectionRevision
        return EditorState.selection.length > 1
    }

    function can(cap) {
        return root.clipCaps.indexOf(cap) >= 0
    }

    // The strip follows what is selected, mirroring which tabs PropertiesPanel shows for it.
    // kind: "tab" uses the assets sheet, "text" starts the inline editor, "tool" goes
    // through toolRequested. An entry's `props` are handed to the page.
    function buildItems() {
        const tool = (id, label, icon, props) => ({ id: id, kind: "tool", label: label, icon: icon, props: props })
        const details = tool("details", qsTr("Details"), Theme.icons.info)
        const effects = tool("filters", qsTr("Effects"), Theme.icons.wand)
        const adjust = tool("adjust", qsTr("Adjust"), Theme.icons.slidersVertical)
        const transform = tool("transform", qsTr("Transform"), Theme.icons.maximize)
        const crop = tool("crop", qsTr("Crop"), Theme.icons.crop)
        const volume = tool("audio", qsTr("Volume"), Theme.icons.volumeHigh)
        const speed = tool("speed", qsTr("Speed"), Theme.icons.gauge)
        const animation = tool("animation", qsTr("Animation"), Theme.icons.sparkles)
        const mask = tool("mask", qsTr("Mask"), Theme.icons.mask)
        const blending = tool("blending", qsTr("Blending"), Theme.icons.blend)

        if (root.hasTransition)
            return [tool("transition", qsTr("Transition"), Theme.icons.chevronsRight)]
        if (!root.hasSelection) {
            return [
                tool("background", qsTr("Background"), Theme.icons.image),
                tool("crop", qsTr("Crop"), Theme.icons.crop, { mode: "video" }),
                effects,
                adjust,
                { id: "sounds", kind: "tab", label: qsTr("Audio FX"), icon: Theme.icons.audioLines },
                { id: "transitions", kind: "tab", label: qsTr("Transitions"), icon: Theme.icons.chevronsRight }
            ]
        }
        if (root.multiSelection)
            return []

        if (root.clipKind === "adjustment") {
            switch (root.adjustmentKind) {
            case "videoEffects": return [effects, mask, blending, details]
            case "mask": return [tool("maskEdit", qsTr("Mask"), Theme.icons.mask), blending, details]
            case "audioEffects": return [tool("audioFxEdit", qsTr("Audio FX"), Theme.icons.audioLines), details]
            case "transform": return [transform, animation, details]
            case "camera": return [tool("camera", qsTr("Camera"), Theme.icons.video), details]
            }
            return [details]
        }

        switch (root.clipKind) {
        case "video":
            return [effects, adjust, volume, speed, transform, crop, animation, mask, blending,
                    tool("stabilize", qsTr("Stabilize"), Theme.icons.locateFixed), details]
        case "composite":
            return [effects, adjust, volume, speed, transform, animation, mask, blending, details]
        case "image":
            return [effects, adjust, transform, crop, animation, mask, blending, details]
        case "audio":
            return [volume, speed, { id: "sounds", kind: "tab", label: qsTr("Audio FX"), icon: Theme.icons.audioLines }, details]
        case "text":
            return [{ id: "editText", kind: "text", label: qsTr("Edit text"), icon: Theme.icons.pencil },
                    tool("textStyle", qsTr("Text style"), Theme.icons.brush),
                    tool("textSettings", qsTr("Text settings"), Theme.icons.settings),
                    tool("textAnim", qsTr("Text animation"), Theme.icons.sparkles),
                    transform, animation, effects, mask, blending, details]
        case "subtitle":
            return [tool("subtitles", qsTr("Subtitles"), Theme.icons.captions),
                    tool("textStyle", qsTr("Text style"), Theme.icons.brush), transform, blending, details]
        case "shape":
            return [tool("shapeStyle", qsTr("Shape style"), Theme.icons.shapes), transform, animation,
                    effects, mask, blending, details]
        case "vector":
            return [tool("motion", qsTr("Motion"), Theme.icons.layers), transform, animation, effects,
                    mask, blending, details]
        case "pdf":
            return [tool("pdf", qsTr("PDF"), Theme.icons.fileText), transform, animation, effects,
                    mask, blending, details]
        case "model3d":
            return [tool("model3d", qsTr("3D Model"), Theme.icons.box), transform, effects, mask, blending, details]
        }
        return [details]
    }

    // Only the ids and labels decide the entries, so an edit that merely changes clip data does not
    // rebuild the list and snap the strip back to its start.
    readonly property string itemsKey: JSON.stringify(root.buildItems().map(e => [e.id, e.label]))
    property var items: []
    onItemsKeyChanged: {
        root.items = root.buildItems()
        strip.positionViewAtBeginning()
        stripFade.restart()
    }
    Component.onCompleted: root.items = root.buildItems()

    Rectangle {
        anchors.fill: parent
        color: Theme.c.androidBottomRail.background
    }

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.c.androidBottomRail.borderWidth
        color: Theme.c.androidBottomRail.border
        z: 1
    }

    Item {
        id: railBody
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomInset
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset

        AndroidRailAddButton {
            id: addButton
            anchors.left: parent.left
            width: Theme.androidRailFabSize + 2 * Theme.spacingLg
            onRequested: root.addRequested()
        }

        Rectangle {
            id: divider
            anchors.left: addButton.right
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.c.androidBottomRail.borderWidth
            height: parent.height * 0.5
            color: Theme.c.androidBottomRail.border
        }

        Text {
            anchors.left: divider.right
            anchors.leftMargin: Theme.spacingLg
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            visible: root.multiSelection && !root.hasTransition
            text: qsTr("%n clip(s) selected", "", EditorState.selection.length)
            color: Theme.c.androidClipToolbarHint.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }

        ListView {
            id: strip
            anchors.left: divider.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            orientation: ListView.Horizontal
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.items

            NumberAnimation {
                id: stripFade
                target: strip
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.durationBase
                easing.type: Theme.easing
            }

            delegate: AndroidRailButton {
                id: btn
                required property var modelData
                entry: modelData
                height: strip.height
                width: Math.max(Theme.androidRailFabSize + Theme.spacingLg, strip.width / 5.5)
                selected: root.activeId === modelData.id
                                onClicked: {
                    Haptics.select()
                    if (modelData.kind === "text") {
                        root.editTextRequested()
                    } else if (modelData.kind === "tab") {
                        root.tabRequested(modelData.id)
                    } else {
                        root.toolRequested(modelData.id, modelData.props)
                    }
                }
            }
        }
    }
}
