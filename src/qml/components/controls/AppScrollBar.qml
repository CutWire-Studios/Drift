pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Basic style's default ScrollBar reads control.palette.dark/mid, which the platform
// theme (e.g. KDE Breeze) populates independently of the QQC2 style. Override both
// visual pieces with Theme colors so it doesn't pick up the desktop color scheme.
ScrollBar {
    id: control

    padding: 2
    hoverEnabled: true

    // Attached as ScrollBar.vertical/horizontal, the bar's parent is the Flickable, so every list
    // with an AppScrollBar gets touchpad momentum without a change at each call site.
    property bool kinetic: true
    property KineticWheel _kineticWheel: null

    function _attachKinetic() {
        if (_kineticWheel) {
            _kineticWheel.destroy()
            _kineticWheel = null
        }
        if (!_ready || !kinetic || !(parent instanceof Flickable))
            return
        _kineticWheel = kineticComponent.createObject(parent, { flickable: parent })
        _kineticWheel.vertical = Qt.binding(() => control.orientation === Qt.Vertical)
    }

    property bool _ready: false
    onParentChanged: _attachKinetic()
    onKineticChanged: _attachKinetic()
    Component.onCompleted: {
        _ready = true
        _attachKinetic()
    }
    Component.onDestruction: if (_kineticWheel) _kineticWheel.destroy()

    Component {
        id: kineticComponent
        KineticWheel { }
    }

    contentItem: ThemeSlot {
        slot: "AppScrollBarHandle"
        control: control
        fallback: Component {
            Rectangle {
                id: handle
                implicitWidth: control.interactive ? 8 : 2
                implicitHeight: control.interactive ? 8 : 2
                radius: Theme.c.appScrollBar.handleRadius
                color: control.pressed ? Theme.c.appScrollBar.handleBackgroundPressed
                                      : (control.hovered ? Theme.c.appScrollBar.handleBackgroundHover
                                                         : Theme.c.appScrollBar.handleBackground)
                opacity: 0.0

                states: State {
                    name: "active"
                    when: control.policy === ScrollBar.AlwaysOn || (control.active && control.size < 1.0)
                    PropertyChanges {
                        handle.opacity: control.policy === ScrollBar.AlwaysOn ? 1.0 : 0.85
                    }
                }

                transitions: Transition {
                    from: "active"
                    SequentialAnimation {
                        PauseAnimation { duration: 450 }
                        NumberAnimation { target: handle; duration: 200; property: "opacity"; to: 0.0 }
                    }
                }
            }
        }
    }

    // Visible track for always-on bars (e.g. the timeline) so the handle reads as
    // a position indicator; transient bars keep an invisible track.
    background: Rectangle {
        radius: Theme.c.appScrollBar.trackRadius
        color: Theme.c.appScrollBar.trackBackground
        opacity: control.policy === ScrollBar.AlwaysOn ? 0.45 : 0.0
    }
}
