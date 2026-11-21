import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Drift

QuickSetupPage {
    id: root

    readonly property string discordUrl: "https://cutwire.org/discord"
    readonly property string email: "contact@cutwire.org"
    readonly property var prefs: EditorState.preferences
    readonly property var downloads: Window.window ? Window.window.downloads : null
    readonly property string make: prefs.whatYouMake
    readonly property bool startsProject: make !== ""
    readonly property var makeNames: ({
        youtube: qsTr("YouTube"),
        shorts: qsTr("Shorts"),
        films: qsTr("Films"),
        podcasts: qsTr("Podcasts"),
        tutorials: qsTr("Tutorials")
    })
    readonly property var levelNames: ({
        first: qsTr("First time"),
        some: qsTr("Some experience"),
        pro: qsTr("Pro")
    })
    readonly property var projectLabels: ({
        youtube: qsTr("Start a YouTube project"),
        shorts: qsTr("Start a vertical project"),
        films: qsTr("Start a widescreen project"),
        podcasts: qsTr("Start a podcast project"),
        tutorials: qsTr("Start a tutorial project")
    })
    property bool showDetails: false

    // Illustration slot "illustration": the finished editor layout assembling panel by panel in
    // the user's accent colour, one idea: this is your Drift.
    illustration: Component { OnboardingFinishIllustration { } }
    title: qsTr("You're all set")
    subtitle: qsTr("Your Drift: %1").arg((startsProject ? [makeNames[make], levelNames[prefs.experienceLevel]]
                                                        : [levelNames[prefs.experienceLevel]]).join(" · "))
    glyph: Theme.icons.check
    nextLabel: startsProject ? projectLabels[make] : qsTr("Open the editor")
    contentMaxWidth: 640

    Component.onCompleted: {
        if (downloads)
            downloads.optionalActive = true
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: root.downloads !== null

        ThemedLabel {
            width: parent.width
            size: "sm"
            tone: "default"
            text: root.downloads && root.downloads.allDone
                  ? qsTr("Add-ons installed")
                  : qsTr("Installing add-ons: %1 of %2").arg(root.downloads ? root.downloads.doneCount : 0)
                        .arg(root.downloads ? root.downloads.wantedEntries.length : 0)
        }

        ThemedProgressBar {
            width: parent.width
            barHeight: root.mobile ? Theme.spacingMd : Theme.spacingSm
            value: root.downloads ? root.downloads.overall : 0
        }

        ThemedLabel {
            width: parent.width
            visible: root.downloads !== null && root.downloads.busy
            size: "sm"
            text: qsTr("You can continue now. Downloads carry on in the editor.")
        }

        Rectangle {
            width: parent.width
            height: warningRow.implicitHeight + Theme.spacingXl * 2
            visible: root.downloads !== null && root.downloads.showWarning
            radius: Theme.c.quickSetup.radius
            color: Theme.c.quickSetup.panelBackground
            border.width: Theme.c.quickSetup.borderWidth
            border.color: Theme.c.quickSetup.warning

            RowLayout {
                id: warningRow

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingXl
                spacing: Theme.spacingLg

                IconGlyph {
                    Layout.alignment: Qt.AlignTop
                    glyph: Theme.icons.warning
                    iconSize: 20
                    iconColor: Theme.c.quickSetup.warning
                }

                ThemedLabel {
                    Layout.fillWidth: true
                    size: "sm"
                    tone: "default"
                    text: root.downloads && root.downloads.catalogFailed
                          ? qsTr("Couldn't reach the download store. Until the essentials are installed the editor will be missing basic effects, transitions and fonts. You can install them later from Extras.")
                          : qsTr("Some packs didn't install. Until they are, the editor will be missing basic effects, transitions and fonts. You can install them later from Extras.")
                }
            }
        }

        RowLayout {
            spacing: Theme.spacingMd

            ThemedButton {
                variant: "primary"
                text: qsTr("Retry")
                visible: root.downloads !== null && root.downloads.showWarning
                topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
                bottomPadding: topPadding
                onClicked: root.downloads.retry()
            }

            ThemedButton {
                variant: "ghost"
                text: root.showDetails ? qsTr("Hide details") : qsTr("Show details")
                topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
                bottomPadding: topPadding
                onClicked: root.showDetails = !root.showDetails
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.showDetails

            Repeater {
                model: root.downloads ? root.downloads.requiredEntries : []

                QuickSetupAddonRow {
                    required property var modelData

                    width: parent.width
                    entry: modelData
                    downloads: root.downloads
                    mobile: root.mobile
                }
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: root.downloads !== null && root.downloads.optionalEntries.length > 0

        ThemedLabel {
            width: parent.width
            size: "sm"
            tone: "default"
            font.weight: Font.Medium
            text: qsTr("Optional")
        }

        Repeater {
            model: root.downloads ? root.downloads.optionalEntries : []

            QuickSetupAddonRow {
                required property var modelData

                width: parent.width
                entry: modelData
                downloads: root.downloads
                mobile: root.mobile
                optional: true
                checked: root.downloads.isChecked(modelData)
                onToggled: on => root.downloads.setChecked(modelData.id, on)
            }
        }
    }

    ThemedLabel {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        visible: root.prefs.uiScaleNeedsRestart
        size: "sm"
        text: root.downloads && root.downloads.busy
              ? qsTr("Drift will restart to apply the interface size. Downloads still running will stop and can be resumed from Extras.")
              : qsTr("Drift will restart to apply the interface size.")
    }

    ThemedButton {
        anchors.horizontalCenter: parent.horizontalCenter
        variant: "ghost"
        visible: root.startsProject
        text: qsTr("Just open the editor")
        topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
        bottomPadding: topPadding
        onClicked: Window.window.finish(false)
    }

    Rectangle {
        width: parent.width
        height: Theme.c.quickSetup.borderWidth
        color: Theme.c.quickSetup.divider
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingMd

        ThemedLabel {
            height: discordLink.height
            verticalAlignment: Text.AlignVCenter
            size: "sm"
            text: qsTr("Questions or ideas?")
        }

        ThemedButton {
            id: discordLink

            variant: "ghost"
            glyph: "users"
            text: qsTr("Join our Discord")
            onClicked: Qt.openUrlExternally(root.discordUrl)
        }

        ThemedButton {
            variant: "ghost"
            glyph: "mail"
            text: root.email
            onClicked: Qt.openUrlExternally("mailto:" + root.email)
        }
    }

    Text {
        width: parent.width
        visible: CrashReports.supported
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("If Drift crashes, it sends an error report to help fix it. No project files or media are included. You can turn this off in Settings.")
        color: Theme.c.quickSetup.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeBase * 0.85
    }
}
