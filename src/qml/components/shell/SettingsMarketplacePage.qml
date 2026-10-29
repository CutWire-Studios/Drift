import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "marketplace"
    title: qsTr("Marketplace")

    SettingsRow {
        title: qsTr("Account")
        searchId: "marketplace-account"
        description: Market.accountName.length > 0
                     ? qsTr("Account connected (%1)").arg(Market.accountName)
                     : qsTr("Marketplace account connected")
        available: Market.configured && Market.authenticated
        query: page.query

        ThemedButton {
            text: qsTr("Disconnect")
            variant: "ghost"
            tooltip: qsTr("Unlink the marketplace account from this device")
            onClicked: Market.disconnectAccount()
        }
    }
}
