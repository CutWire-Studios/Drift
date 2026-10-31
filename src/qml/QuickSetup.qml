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
    readonly property var pages: [
        { id: "welcome", page: welcomePage },
        { id: "theme", page: themePage },
        { id: "scale", page: scalePage },
        { id: "addons", page: addonsPage },
        { id: "tips", page: tipsPage },
        { id: "community", page: communityPage }
    ].filter(p => !(mobile && p.id === "scale"))

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

    function next() {
        if (!canAdvance)
            return
        if (isLast)
            EditorState.preferences.completeQuickSetup()
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

    Connections {
        target: window.currentPage
        ignoreUnknownSignals: true
        function onSkipRequested() { window.go(1) }
    }

    Component { id: welcomePage; QuickSetupWelcomePage { } }
    Component { id: themePage; QuickSetupThemePage { } }
    Component { id: scalePage; QuickSetupScalePage { } }
    Component { id: addonsPage; QuickSetupAddonsPage { } }
    Component { id: tipsPage; QuickSetupTipsPage { } }
    Component { id: communityPage; QuickSetupCommunityPage { } }

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
