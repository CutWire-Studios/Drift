import QtQuick
import QtQuick.Layouts
import Drift

SettingsPage {
    id: page

    pageId: "farm"
    title: qsTr("Render farm")

    SettingsGroupHeader {
        text: qsTr("Coordinator")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Let other computers render for this one")
        searchId: "farm-coordinator"
        description: qsTr("Listens for render workers on your network. Jobs sent to the farm from the Export dialog are split up between them. Only turn this on in a network you trust.")
        keywords: "render farm coordinator workers network lan distributed cluster"
        query: page.query
        available: Qt.platform.os !== "android" && Qt.platform.os !== "wasm"

        ThemedSwitch {
            checked: RenderFarm.enabled
            onToggled: RenderFarm.enabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Port")
        searchId: "farm-port"
        description: RenderFarm.lastError.length > 0 ? RenderFarm.lastError
                                                     : qsTr("The TCP port workers connect to.")
        keywords: "render farm port tcp"
        query: page.query
        available: Qt.platform.os !== "android" && Qt.platform.os !== "wasm"

        ThemedNumberField {
            width: Theme.c.settingsRow.controlWidth
            from: 1
            to: 65535
            value: RenderFarm.listenPort
            onEdited: (v) => RenderFarm.listenPort = Math.round(v)
        }
    }

    SettingsRow {
        title: qsTr("Token")
        searchId: "farm-token"
        description: RenderFarm.running ? qsTr("Workers need this to join %1. Paste it on the worker with --token.").arg(RenderFarm.address)
                                        : qsTr("Workers need this to join. Paste it on the worker with --token.")
        keywords: "render farm token password secret authentication"
        query: page.query
        available: Qt.platform.os !== "android" && Qt.platform.os !== "wasm"
        stacked: true

        RowLayout {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                text: RenderFarm.token
            }
            ThemedButton {
                text: qsTr("Copy")
                onClicked: EditorState.copyText(RenderFarm.token)
            }
            ThemedButton {
                text: qsTr("New token")
                tooltip: qsTr("Workers using the old token are turned away the next time they connect")
                onClicked: RenderFarm.regenerateToken()
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Worker")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Media cache size")
        searchId: "farm-cache"
        description: qsTr("How much of a coordinator's media a worker started on this computer keeps for the next job, in GB. The least recently used files go first.")
        keywords: "render farm worker cache media disk size gb"
        query: page.query
        available: Qt.platform.os !== "android" && Qt.platform.os !== "wasm"

        ThemedNumberField {
            width: Theme.c.settingsRow.controlWidth
            from: 1
            to: 100000
            unit: qsTr("GB")
            value: RenderFarm.cacheGb
            onEdited: (v) => RenderFarm.cacheGb = Math.round(v)
        }
    }
}
