import QtQuick
import QtQuick.Window
import Drift

// One window per `drift --preview=Name`. Loads Name_Preview (a fixture next to the component)
// when there is one, otherwise Name itself, against the app's real singletons.
Window {
    id: root

    required property string componentName
    // Set by --preview-shot: save <shotDir>/<componentName>.png once drawn, then close.
    property string shotDir: ""
    // Read by main() after the event loop ends, for the exit code.
    property bool shotOk: false
    property string errorText: ""

    function resolve() {
        const fixture = Qt.createComponent("Drift", root.componentName + "_Preview", Component.PreferSynchronous)
        // Fall back only when there is no fixture; a fixture that fails to compile is the error to show.
        const noFixture = fixture.status === Component.Error
                          && fixture.errorString().includes("contains no type named")
        const component = noFixture
                          ? Qt.createComponent("Drift", root.componentName, Component.PreferSynchronous)
                          : fixture
        if (component.status === Component.Ready)
            return component
        root.errorText = component.errorString()
        console.error("preview " + root.componentName + ": " + root.errorText)
        return null
    }

    // A fixture whose component is a ToolWindow exposes it as `toolWindow`; otherwise the item itself.
    function toolWindowOf(item) {
        return item && item.toolWindow !== undefined ? item.toolWindow : item
    }

    width: 800
    height: 600
    visible: true
    title: "%1 — preview".arg(root.componentName)
    color: Theme.c.previewHost.background

    onFrameSwapped: {
        if (root.shotDir.length > 0 && !shotTimer.running && !shotTimer.fired)
            shotTimer.start()
    }

    // Image providers and Loaders inside the component finish a few frames after the first.
    Timer {
        id: shotTimer

        property bool fired: false

        // A fixture with async setup (imports, probes) sets previewReady to false until it is done.
        property int waited: 0

        interval: 250
        repeat: true
        onTriggered: {
            const pending = loader.item && loader.item.previewReady === false
            if (pending && waited < 60) {
                waited += 1
                return
            }
            stop()
            fired = true
            const path = root.shotDir + "/" + root.componentName + ".png"
            // A ToolWindow keeps its content in a Window of its own, so grab that window's content.
            const tool = root.toolWindowOf(loader.item)
            const toolWindow = tool && tool._host ? tool : null
            // A Popup (dialog) draws in the overlay, outside the stage. Its background's parent is the
            // popup item, sized to the dialog, so grabbing that takes the dialog alone.
            const popup = !toolWindow && tool && tool.open !== undefined && tool.background ? tool : null
            const target = toolWindow ? toolWindow._host.contentItem
                                      : popup ? popup.background.parent : stage
            target.grabToImage(result => {
                const saved = result.saveToFile(path)
                if (!saved)
                    console.error("preview " + root.componentName + ": could not write " + path)
                root.shotOk = saved && root.errorText.length === 0
                if (toolWindow)
                    toolWindow.close()
                if (popup)
                    popup.close()
                root.close()
            })
        }
    }

    // Grabbed for --preview-shot; the window colour is not part of an item grab.
    Item {
        id: stage

        // Sized to the content, not the window: a tiling WM may not give the window its requested
        // size, and the shot must still show the whole component.
        width: loader.item && loader.item.implicitWidth > 0 ? loader.item.implicitWidth : root.width
        height: loader.item && loader.item.implicitHeight > 0 ? loader.item.implicitHeight : root.height

        Rectangle {
            anchors.fill: parent
            color: Theme.c.previewHost.background
        }

        Loader {
            id: loader

            anchors.fill: parent
            sourceComponent: root.resolve()
            onStatusChanged: {
                // Required properties left unset fail here, after the component itself compiled.
                if (status === Loader.Error || (status === Loader.Ready && !item))
                    root.errorText = ("%1 cannot be created on its own (see the log). Add %1_Preview.qml next to it to supply what it needs.")
                                     .arg(root.componentName)
            }
            onLoaded: {
                // ToolWindow is hidden until shown; a preview is meant to be seen.
                const tool = root.toolWindowOf(item)
                if (tool && typeof tool.show === "function")
                    tool.show()
                else if (tool && typeof tool.open === "function")
                    tool.open()
                if (item && item.implicitWidth > 0 && item.implicitHeight > 0) {
                    root.width = item.implicitWidth
                    root.height = item.implicitHeight
                }
            }
        }

        Text {
            anchors.fill: parent
            anchors.margins: 16
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.c.previewHost.error
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.Wrap
        }
    }
}
