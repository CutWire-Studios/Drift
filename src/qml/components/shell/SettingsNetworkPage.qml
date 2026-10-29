import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "network"
    title: qsTr("Network")

    function saveProxy() {
        EditorState.preferences.setNetworkProxy(proxyModeCombo.model[proxyModeCombo.currentIndex].id,
                                                proxyHostField.text, parseInt(proxyPortField.text) || 0,
                                                proxyUserField.text, proxyPasswordField.text)
    }

    readonly property bool proxyNeedsServer: proxyModeCombo.model[proxyModeCombo.currentIndex].id === "http"
        || proxyModeCombo.model[proxyModeCombo.currentIndex].id === "socks5"

    SettingsGroupHeader {
        text: qsTr("Connection")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Proxy")
        searchId: "network-proxy"
        description: qsTr("How Drift reaches the internet for downloads, updates, and online services. Applies immediately.")
        keywords: "proxy http socks5 internet firewall network"
        query: page.query

        ThemedComboBox {
            id: proxyModeCombo
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "system", label: qsTr("System proxy") },
                { id: "none", label: qsTr("No proxy") },
                { id: "http", label: qsTr("HTTP proxy") },
                { id: "socks5", label: qsTr("SOCKS5 proxy") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.networkProxyMode)
                        return i
                }
                return 0
            }
            onActivated: page.saveProxy()
        }
    }

    SettingsRow {
        title: qsTr("Proxy server")
        searchId: "network-proxy-server"
        description: qsTr("Host and port of the proxy. The username and password are optional.")
        keywords: "proxy host port username password login"
        stacked: true
        available: page.proxyNeedsServer
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingMd

            Row {
                width: parent.width
                spacing: Theme.spacingMd

                ThemedTextField {
                    id: proxyHostField
                    width: parent.width - proxyPortField.width - parent.spacing
                    placeholderText: qsTr("Proxy host")
                    text: EditorState.preferences.networkProxyHost
                    onEditingFinished: page.saveProxy()
                }

                ThemedTextField {
                    id: proxyPortField
                    width: 96
                    placeholderText: qsTr("Port")
                    text: EditorState.preferences.networkProxyPort > 0 ? String(EditorState.preferences.networkProxyPort) : ""
                    onEditingFinished: page.saveProxy()
                }
            }

            ThemedTextField {
                id: proxyUserField
                width: parent.width
                placeholderText: qsTr("Username (optional)")
                text: EditorState.preferences.networkProxyUser
                onEditingFinished: page.saveProxy()
            }

            ThemedTextField {
                id: proxyPasswordField
                width: parent.width
                echoMode: TextInput.Password
                placeholderText: qsTr("Password (optional)")
                text: EditorState.preferences.networkProxyPassword
                onEditingFinished: page.saveProxy()
            }
        }
    }
}
