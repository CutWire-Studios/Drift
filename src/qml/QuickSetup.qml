import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

ApplicationWindow {
    id: window

    property string shellPreference: "auto"
    readonly property bool mobile: shellPreference === "mobile"

    // Ordered list of setup pages. To add one: create a QuickSetupPage subtype
    // under components/quicksetup, register it in CMakeLists, and add an entry here.
    // The phone shell has no workspaces, Ask button or rail labels, so the experience and
    // agentic steps would change nothing there.
    readonly property var pages: [
        { id: "welcome", page: welcomePage },
        { id: "theme", page: themePage },
        { id: "scale", page: scalePage },
        { id: "make", page: makePage },
        { id: "experience", page: experiencePage },
        { id: "agentic", page: agenticPage },
        { id: "finish", page: finishPage }
    ].filter(p => !(p.id === "scale" && !scaleLooksOff)
                  && !(mobile && (p.id === "experience" || p.id === "agentic")))

    // The layout is designed for about 1080 logical rows. Outside +-20% of that the UI is cramped
    // or oversized, so the scale step is offered with the size that gets back to 1080 rows.
    readonly property real logicalShortSide: Math.min(Screen.width, Screen.height)
    readonly property bool scaleLooksOff: !mobile && (logicalShortSide < 864 || logicalShortSide > 1296)
    readonly property real recommendedScale: {
        const ideal = logicalShortSide * Screen.devicePixelRatio / 1080
        const steps = [0.75, 0.8, 0.9, 1.0, 1.25, 1.5, 1.75, 2.0]
        let best = 1.0
        for (const step of steps) {
            if (Math.abs(ideal - step) < Math.abs(ideal - best))
                best = step
        }
        return best
    }
    readonly property var downloads: downloadsModel

    property int step: 0
    property int direction: 1
    readonly property bool isFirst: step === 0
    readonly property bool isLast: step === pages.length - 1
    readonly property Item currentPage: pageLoader.item
    readonly property bool canAdvance: currentPage ? currentPage.canAdvance : false

    function go(delta) {
        const target = step + delta
        if (target < 0 || target >= pages.length || transition.running)
            return
        direction = delta
        transition.pendingStep = target
        transition.start()
    }

    function finish(startProject) {
        EditorState.preferences.completeQuickSetup(startProject)
    }

    function next() {
        if (!canAdvance)
            return
        if (isLast)
            finish(currentPage.startsProject === true)
        else
            go(1)
    }

    width: mobile ? Screen.width : 960
    height: mobile ? Screen.height : 680
    minimumWidth: mobile ? 0 : 720
    minimumHeight: mobile ? 0 : 560
    x: Screen.width / 2 - width / 2
    y: Screen.height / 2 - height / 2
    visible: true
    title: qsTr("Drift setup")
    color: Theme.c.quickSetup.background

    QuickSetupDownloads { id: downloadsModel }

    Component { id: welcomePage; QuickSetupWelcomePage { } }
    Component { id: themePage; QuickSetupThemePage { } }
    Component { id: scalePage; QuickSetupScalePage { } }
    Component { id: makePage; QuickSetupMakePage { } }
    Component { id: experiencePage; QuickSetupExperiencePage { } }
    Component { id: agenticPage; QuickSetupAgenticPage { } }
    Component { id: finishPage; QuickSetupFinishPage { } }

    Item {
        anchors.fill: parent
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        anchors.topMargin: SafeArea.margins.top

        Item {
            id: stage

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: bottomBar.top
            clip: true

            Loader {
                id: pageLoader

                width: parent.width
                height: parent.height
                sourceComponent: window.pages[window.step].page
            }

            SequentialAnimation {
                id: transition

                property int pendingStep: 0

                ParallelAnimation {
                    NumberAnimation {
                        target: pageLoader
                        property: "opacity"
                        to: 0
                        duration: Theme.durationFast
                        easing.type: Theme.easing
                    }
                    NumberAnimation {
                        target: pageLoader
                        property: "x"
                        to: -window.direction * 32
                        duration: Theme.durationFast
                        easing.type: Theme.easing
                    }
                }
                ScriptAction {
                    script: {
                        window.step = transition.pendingStep
                        pageLoader.x = window.direction * 32
                    }
                }
                ParallelAnimation {
                    NumberAnimation {
                        target: pageLoader
                        property: "opacity"
                        to: 1
                        duration: Theme.durationBase
                        easing.type: Theme.easing
                    }
                    NumberAnimation {
                        target: pageLoader
                        property: "x"
                        to: 0
                        duration: Theme.durationBase
                        easing.type: Theme.easing
                    }
                }
            }
        }

        Rectangle {
            id: bottomBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: window.mobile ? SafeArea.margins.bottom : 0
            height: (window.mobile ? 84 : 72)
            color: "transparent"

            Rectangle {
                width: parent.width
                height: Theme.c.quickSetup.borderWidth
                color: Theme.c.quickSetup.divider
            }

            ThemedButton {
                id: backButton

                anchors.left: parent.left
                anchors.leftMargin: window.mobile ? Theme.spacingXl : Theme.spacing3xl
                anchors.verticalCenter: parent.verticalCenter
                variant: "ghost"
                text: qsTr("Back")
                visible: !window.isFirst
                topPadding: window.mobile ? Theme.spacingXl : Theme.spacingMd + 1
                bottomPadding: topPadding
                onClicked: window.go(-1)
            }

            QuickSetupStepIndicator {
                anchors.centerIn: parent
                count: window.pages.length
                current: window.step
            }

            ThemedButton {
                id: nextButton

                anchors.right: parent.right
                anchors.rightMargin: window.mobile ? Theme.spacingXl : Theme.spacing3xl
                anchors.verticalCenter: parent.verticalCenter
                variant: "primary"
                enabled: window.canAdvance
                text: window.currentPage && window.currentPage.nextLabel.length > 0
                      ? window.currentPage.nextLabel
                      : window.isLast ? qsTr("Finish") : qsTr("Next")
                topPadding: window.mobile ? Theme.spacingXl : Theme.spacingMd + 1
                bottomPadding: topPadding
                onClicked: window.next()
            }
        }
    }
}
