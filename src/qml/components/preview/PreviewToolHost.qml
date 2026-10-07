import QtQuick
import Drift

// The one interaction tool that owns the preview's pointer, picked by EditorState.preview.activeTool.
// Desktop and phone pass their own components for each tool, so the tools stay platform-specific
// while the switching between them, and the geometry they are laid out on, is shared.
Item {
    id: host

    property Item canvas
    // Tool id ("transform", "mask", "crop", "guideEdit") -> Component. A tool without an entry
    // shows nothing.
    property var tools: ({})
    // Loaded above the tool while it is one of `companionTools`: the platform's companion overlays,
    // such as the depth handles and the asset drop target.
    property Component companion: null
    property var companionTools: ["transform"]

    readonly property string tool: EditorState.preview.activeTool
    // Crop and guide editing work across the whole viewport, so a crop frame can be dragged past
    // the canvas edges to grow it. The other tools mirror the canvas rect from outside it, so
    // grips on a clip that runs past an edge stay drawn and grabbable instead of being clipped.
    readonly property bool fillsViewport: tool === "crop" || tool === "guideEdit"
    // Typed loosely: each platform's tools are different types, and only some have `interacting`.
    readonly property var item: toolLoader.item
    // True while the tool is mid-drag.
    readonly property bool interacting: !!item && item.interacting === true

    Loader {
        id: toolLoader
        x: host.fillsViewport ? 0 : host.canvas.x
        y: host.fillsViewport ? 0 : host.canvas.y
        width: host.fillsViewport ? host.width : host.canvas.width
        height: host.fillsViewport ? host.height : host.canvas.height
        // Grips step aside while the picture moves under them; crop and guide editing are
        // sessions of their own and stay up.
        visible: host.fillsViewport || EditorState.preview.handlesVisible
        sourceComponent: host.tools[host.tool] || null
    }

    Loader {
        anchors.fill: parent
        z: 1
        active: host.companionTools.indexOf(host.tool) >= 0
        sourceComponent: host.companion
    }
}
