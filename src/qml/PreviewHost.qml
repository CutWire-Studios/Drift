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

    width: 800
    height: 600
    visible: true
    title: qsTr("%1 — preview").arg(root.componentName)
    color: Theme.c.previewHost.background

    onFrameSwapped: {
        if (root.shotDir.length > 0 && !shotTimer.running && !shotTimer.fired)
            shotTimer.start()
    }

    // Image providers and Loaders inside the component finish a few frames after the first.
    Timer {
        id: shotTimer

        property bool fired: false

        interval: 250
        onTriggered: {
            fired = true
            const path = root.shotDir + "/" + root.componentName + ".png"
            stage.grabToImage(result => {
                const saved = result.saveToFile(path)
                if (!saved)
                    console.error("preview " + root.componentName + ": could not write " + path)
                root.shotOk = saved && root.errorText.length === 0
                root.close()
            })
        }
    }

    // Grabbed for --preview-shot; the window colour is not part of an item grab.
    Item {
        id: stage

        anchors.fill: parent

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
                    root.errorText = qsTr("%1 cannot be created on its own (see the log). Add %1_Preview.qml next to it to supply what it needs.")
                                     .arg(root.componentName)
            }
            onLoaded: {
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
