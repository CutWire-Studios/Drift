import QtQuick
import Drift

// Installs an addon, .driftfx or Drift Forge zip picked by the user, asking and reporting
// failures in the window that hosts this item. The Addons signals reach every importer, so
// only the one that started the current import answers them.
Item {
    id: root

    property bool active: false
    property var pendingUserPackage: ({})
    property var pendingAddonFile: ({})

    // An empty url asks with a file dialog.
    function importFile(url) {
        if (!url || String(url) === "")
            url = FileDialogs.openFile(qsTr("Install Addon"),
                                       [qsTr("Drift addons and effects (*.driftpkg *.driftfx *.zip)")])
        if (!url || String(url) === "")
            return
        const info = Addons.inspectAddonFile(url)
        if (info.error) {
            root.showFailure(qsTr("Could not install this file"), info.error)
            return
        }
        root.active = true
        if (info.type === "driftfx") {
            root.pendingUserPackage = info
            confirmUserPackage.open()
            return
        }
        root.pendingAddonFile = info
        Addons.installAddonFile(false)
    }

    function showFailure(title, message) {
        failure.title = title
        failure.message = message
        failure.open()
    }

    Connections {
        target: Addons
        enabled: root.active
        function onAddonFileNeedsConfirmation() {
            confirmUnverified.open()
        }
        function onUserPackageInstalled(name, error) {
            root.active = false
            if (error.length > 0)
                root.showFailure(qsTr("Could not install “%1”").arg(name), error)
            else
                Toasts.success(qsTr("Installed “%1”").arg(name))
        }
    }

    ThemedDialog {
        id: confirmUnverified
        title: qsTr("Install an unofficial addon?")
        acceptText: qsTr("Install anyway")
        acceptVariant: root.pendingAddonFile.nativeCode ? "destructive" : "primary"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: {
                const p = root.pendingAddonFile
                const by = p.author ? qsTr("“%1” by %2").arg(p.name).arg(p.author) : qsTr("“%1”").arg(p.name)
                const lines = [qsTr("%1 is not signed by the Drift team. Only install files you trust.").arg(by)]
                if (p.nativeCode)
                    lines.push(qsTr("It contains code that runs on your computer."))
                if (p.replaces)
                    lines.push(qsTr("It will replace “%1”.").arg(p.replaces))
                return lines.join("\n\n")
            }
        }

        onAccepted: Addons.installAddonFile(true)
        onRejected: {
            Addons.discardAddonFile()
            root.pendingAddonFile = {}
            root.active = false
        }
    }

    ThemedDialog {
        id: confirmUserPackage
        title: root.pendingUserPackage.kind === "transitions" ? qsTr("Install this transition?")
               : root.pendingUserPackage.kind === "audio-effects" ? qsTr("Install this audio effect?")
                                                                   : qsTr("Install this effect?")
        acceptText: qsTr("Install")
        preferredWidth: Theme.dialogWidthSm

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: {
                const p = root.pendingUserPackage
                const by = p.author ? qsTr("“%1” by %2").arg(p.name).arg(p.author) : qsTr("“%1”").arg(p.name)
                return qsTr("%1 was made by a user, not the Drift team, and nothing has checked it. Only install files you trust.").arg(by)
            }
        }

        onAccepted: {
            Addons.installUserPackage()
            root.pendingUserPackage = {}
        }
        onRejected: {
            root.pendingUserPackage = {}
            root.active = false
        }
    }

    ThemedDialog {
        id: failure

        property string message: ""

        acceptText: qsTr("OK")
        showReject: false
        preferredWidth: Theme.dialogWidthSm

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: failure.message
        }
    }
}
