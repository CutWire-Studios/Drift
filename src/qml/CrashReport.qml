import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift

// Shown before the editor when the last run did not end cleanly; see CrashReporter.
ApplicationWindow {
    id: window

    readonly property bool crashed: CrashReports.startupKind === "crash"

    width: 560
    height: content.implicitHeight + Theme.spacing3xl * 2
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    x: Screen.width / 2 - width / 2
    y: Screen.height / 2 - height / 2
    visible: true
    title: crashed ? qsTr("Drift closed unexpectedly") : qsTr("Drift did not close properly")
    color: Theme.c.quickSetup.background

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacing3xl
        spacing: Theme.spacingLg

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: window.title
            color: Theme.c.quickSetup.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase * 1.5
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: window.crashed
                  ? CrashReports.reasonText
                  : qsTr("Drift did not close properly last time. If it keeps happening, you can start in safe mode, which uses the default graphics backend and loads no add-ons for this session.")
            color: Theme.c.quickSetup.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
        }

        ThemedButton {
            visible: window.crashed && CrashReports.reasonLinkUrl.length > 0
            variant: "ghost"
            text: CrashReports.reasonLinkLabel
            onClicked: Qt.openUrlExternally(CrashReports.reasonLinkUrl)
        }

        Text {
            Layout.fillWidth: true
            visible: window.crashed && CrashReports.reportWillBeSent
            wrapMode: Text.WordWrap
            text: qsTr("A report was sent to help fix this. You can turn this off in Settings.")
            color: Theme.c.quickSetup.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
        }

        Text {
            Layout.fillWidth: true
            visible: CrashReports.saveMessage.length > 0
            wrapMode: Text.WrapAnywhere
            text: CrashReports.saveMessage
            color: Theme.c.quickSetup.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMd
            spacing: Theme.spacingMd

            ThemedButton {
                variant: "secondary"
                text: qsTr("Save crash file…")
                onClicked: CrashReports.saveCrashFile()
            }

            ThemedButton {
                visible: !window.crashed
                variant: "secondary"
                text: qsTr("Start in safe mode")
                onClicked: CrashReports.restartInSafeMode()
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                variant: "primary"
                text: qsTr("Continue")
                onClicked: CrashReports.dismiss()
            }
        }
    }
}
