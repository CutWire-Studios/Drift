import QtQuick
import Drift

// One eligible cut or overlap: a "+" button when it has no transition, a toffee when it has.
Item {
    id: transitionRegion
    property var panel
    property int trackIndex: -1
    property real clipAreaHeight: 0
    property real hoverX: -1
    property real hoverY: -1
    required property var modelData

    z: 10
    width: parent.width
    height: parent.height

    Loader {
        active: !transitionRegion.modelData.hasTransition
        sourceComponent: TimelineCutButton {
            panel: transitionRegion.panel
            trackIndex: transitionRegion.trackIndex
            clipAreaHeight: transitionRegion.clipAreaHeight
            hoverX: transitionRegion.hoverX
            hoverY: transitionRegion.hoverY
            region: transitionRegion.modelData
        }
    }

    Loader {
        active: transitionRegion.modelData.hasTransition
        sourceComponent: TimelineTransitionToffee {
            panel: transitionRegion.panel
            trackIndex: transitionRegion.trackIndex
            clipAreaHeight: transitionRegion.clipAreaHeight
            region: transitionRegion.modelData
        }
    }
}
