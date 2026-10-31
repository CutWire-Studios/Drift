import QtQuick
import QtQuick.Layouts
import Drift

QuickSetupPage {
    id: root

    readonly property string optionalId: "stickers.harmonyos"

    property int revision: 0
    property var transfers: ({})
    property var requested: ({})
    property bool optionalChecked: false
    property bool failureSeen: false
    property bool confirmingSkip: false

    readonly property bool catalogReady: {
        const _r = revision
        return Addons.catalog.length > 0
    }
    readonly property var entries: {
        const _r = revision
        const out = Addons.quickSetupAddons()
        for (let i = 0; i < out.length; ++i)
            out[i].missing = catalogReady && out[i].state === "available" && !inCatalog(out[i].id)
        return out
    }
    readonly property var requiredEntries: entries.filter(e => e.required)
    readonly property var optionalEntry: entries.find(e => e.id === optionalId)
    readonly property var wantedEntries: entries.filter(e => isWanted(e))
    readonly property int doneCount: wantedEntries.filter(e => e.installed).length
    readonly property bool allDone: doneCount === wantedEntries.length
    readonly property bool catalogFailed: !Addons.refreshing && !catalogReady && !allDone
    readonly property bool anyFailed: wantedEntries.some(e => rowState(e) === "failed")
    readonly property bool showWarning: catalogFailed || anyFailed
    readonly property real overall: {
        let sum = 0
        for (const e of wantedEntries) {
            if (e.installed)
                sum += 1
            else if (e.state === "downloading" && transfers[e.id])
                sum += transfers[e.id].fraction * 0.95
            else if (e.state === "installing")
                sum += 0.95
        }
        return wantedEntries.length > 0 ? sum / wantedEntries.length : 1
    }

    title: qsTr("Install the essentials")
    subtitle: qsTr("Drift's basic effects, transitions, audio effects, fonts and looks come as add-ons. Without them the editor will be missing most of what you expect, so they are installed now.")
    glyph: Theme.icons.package
    canAdvance: allDone
    contentMaxWidth: 760

    signal skipRequested()

    function isWanted(e) {
        return e.required || (e.id === optionalId && (optionalChecked || e.installed))
    }

    function inCatalog(id) {
        const rows = Addons.catalog
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === id)
                return true
        }
        return false
    }

    function rowState(e) {
        if (e.installed)
            return "installed"
        if (e.state === "downloading" || e.state === "installing")
            return e.state
        if (e.state === "failed" || e.state === "needs-newer-app" || e.missing)
            return "failed"
        return "queued"
    }

    function rowError(e) {
        if (e.state === "needs-newer-app")
            return qsTr("Needs a newer version of Drift.")
        if (e.missing)
            return qsTr("Not available in the download store.")
        return e.error.length > 0 ? e.error : qsTr("The download failed.")
    }

    function install(id) {
        const next = requested
        next[id] = true
        requested = next
        Addons.install(id)
    }

    function startMissing() {
        if (!catalogReady)
            return
        for (const e of wantedEntries) {
            if (!e.installed && !e.missing && e.state === "available" && !requested[e.id])
                install(e.id)
        }
    }

    function setOptional(on) {
        optionalChecked = on
        const e = optionalEntry
        if (!e || e.installed)
            return
        if (on) {
            if (catalogReady && !e.missing)
                install(e.id)
        } else {
            const next = requested
            next[e.id] = false
            requested = next
            if (e.state === "downloading" || e.state === "installing")
                Addons.cancel(e.id)
        }
    }

    function retry() {
        confirmingSkip = false
        const next = requested
        for (const e of wantedEntries) {
            if (rowState(e) === "failed")
                next[e.id] = false
        }
        requested = next
        if (!catalogReady)
            Addons.refresh(true)
        else
            startMissing()
    }

    onRevisionChanged: startMissing()
    onShowWarningChanged: {
        if (showWarning)
            failureSeen = true
    }

    Component.onCompleted: {
        Addons.refresh()
        revision++
    }

    Connections {
        target: Addons
        function onCatalogChanged() { root.revision++ }
        function onStatusChanged() { root.revision++ }
        function onRefreshingChanged() { root.revision++ }
        function onProgressChanged(id, fraction, phase) {
            const next = root.transfers
            next[id] = { fraction: fraction, phase: phase }
            root.transfers = next
        }
        function onTransferSucceeded(id) { root.revision++ }
        function onTransferFailed(id, reason) { root.revision++ }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd

        ThemedLabel {
            width: parent.width
            size: "sm"
            tone: "default"
            text: qsTr("%1 of %2 installed").arg(root.doneCount).arg(root.wantedEntries.length)
        }

        ThemedProgressBar {
            width: parent.width
            barHeight: root.mobile ? Theme.spacingMd : Theme.spacingSm
            value: root.overall
        }
    }

    Rectangle {
        width: parent.width
        height: warningRow.implicitHeight + Theme.spacingXl * 2
        visible: root.showWarning
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
                text: root.catalogFailed
                      ? qsTr("Couldn't reach the download store. Until the essentials are installed the editor will be missing basic effects, transitions and fonts. You can install them later from Extras.")
                      : qsTr("Some packs didn't install. Until they are, the editor will be missing basic effects, transitions and fonts. You can install them later from Extras.")
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd

        Repeater {
            model: root.requiredEntries

            QuickSetupAddonRow {
                required property var modelData

                width: parent.width
                entry: modelData
                page: root
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: root.optionalEntry !== undefined

        ThemedLabel {
            width: parent.width
            size: "sm"
            tone: "default"
            font.weight: Font.Medium
            text: qsTr("Optional")
        }

        QuickSetupAddonRow {
            width: parent.width
            entry: root.optionalEntry
            page: root
            optional: true
            checked: root.optionalChecked || (root.optionalEntry !== undefined && root.optionalEntry.installed)
            onToggled: on => root.setOptional(on)
        }
    }

    RowLayout {
        spacing: Theme.spacingMd
        visible: root.failureSeen && !root.allDone

        ThemedButton {
            variant: "primary"
            text: qsTr("Retry")
            topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
            bottomPadding: topPadding
            onClicked: root.retry()
        }

        ThemedButton {
            variant: "ghost"
            text: qsTr("Skip for now")
            visible: !root.confirmingSkip
            topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
            bottomPadding: topPadding
            onClicked: root.confirmingSkip = true
        }

        ThemedLabel {
            visible: root.confirmingSkip
            Layout.alignment: Qt.AlignVCenter
            size: "sm"
            tone: "default"
            wrapMode: Text.NoWrap
            text: qsTr("Continue without the essentials?")
        }

        ThemedButton {
            variant: "destructive"
            text: qsTr("Continue")
            visible: root.confirmingSkip
            topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
            bottomPadding: topPadding
            onClicked: root.skipRequested()
        }

        ThemedButton {
            variant: "ghost"
            text: qsTr("Cancel")
            visible: root.confirmingSkip
            topPadding: root.mobile ? Theme.spacingXl : Theme.spacingMd
            bottomPadding: topPadding
            onClicked: root.confirmingSkip = false
        }
    }

    ThemedLabel {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("You can find more effects, models and stickers later in Extras.")
    }
}
