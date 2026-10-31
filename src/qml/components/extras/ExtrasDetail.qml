import QtQuick
import QtQuick.Controls.Basic
import Drift

Rectangle {
    id: root

    required property var store
    property string addonId: ""

    property var row: ({})
    property var plan: ({})
    property bool detailsOpen: false

    readonly property string state: row.state || ""
    readonly property bool active: state === "downloading" || state === "installing"
    readonly property bool busy: active || state === "queued"
    readonly property bool installable: state === "available" || state === "failed" || state === "update-available"
    readonly property bool installed: state === "installed" || state === "update-available"
    readonly property var transfer: store.transfers[addonId]
    readonly property var alsoInstalls: (plan.added || []).filter(a => a.id !== addonId)
    readonly property bool hasRuntimeKind: {
        const kinds = row.kinds && row.kinds.length ? row.kinds : [row.kind]
        return kinds.indexOf("onnxruntime") !== -1 || kinds.indexOf("onnxruntime-ep") !== -1
    }
    readonly property real sidePadding: store.compact ? Theme.androidPagePadding : Theme.pagePadding

    function reload() {
        row = root.addonId.length > 0 ? Addons.entry(root.addonId) : ({})
        plan = root.installable ? Addons.planInstall([root.addonId]) : ({})
    }

    function isSafeUrl(url) {
        return /^https:\/\//i.test(String(url || ""))
    }

    onAddonIdChanged: {
        detailsOpen = false
        reload()
    }
    Component.onCompleted: reload()

    Connections {
        target: Addons
        function onCatalogChanged() { root.reload() }
    }

    color: Theme.c.extrasStore.detailBackground

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true }
    }

    IconButton {
        id: backButton
        x: root.sidePadding
        y: Theme.spacingSm
        visible: !root.store.compact
        glyph: Theme.icons.chevronLeft
        tooltip: qsTr("Back")
        onClicked: root.store.handleBack()
    }

    Flickable {
        id: flick
        anchors.top: backButton.visible ? backButton.bottom : parent.top
        anchors.topMargin: backButton.visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentHeight: body.implicitHeight + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: body
            x: root.sidePadding
            width: flick.width - x * 2
            spacing: Theme.spacing2xl
            topPadding: root.store.compact ? Theme.spacingSm : 0

            component Section: Column {
                property string heading: ""
                default property alias content: holder.data
                width: parent.width
                spacing: Theme.spacingMd

                ThemedLabel {
                    text: parent.heading
                    tone: "default"
                    size: "base"
                    font.weight: Font.Medium
                }

                Column {
                    id: holder
                    width: parent.width
                    spacing: Theme.spacingMd
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingXl

                ExtrasIconTile {
                    id: headIcon
                    tileSize: root.store.compact ? 56 : 72
                    icon: root.row.icon || ""
                    accent: root.row.accent || ""
                    label: root.row.name || ""
                }

                Column {
                    width: parent.width - headIcon.width - parent.spacing
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        width: parent.width
                        text: root.row.name || ""
                        tone: "default"
                        size: "base"
                        font.pixelSize: Theme.fontSizeBase + 4
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    ThemedLabel {
                        width: parent.width
                        visible: text.length > 0
                        text: root.row.tagline || ""
                        size: "sm"
                        wrapMode: Text.WordWrap
                    }

                    ThemedLabel {
                        width: parent.width
                        text: {
                            const parts = []
                            if (root.row.unofficial)
                                parts.push(qsTr("Unofficial"))
                            if (root.row.author)
                                parts.push(root.row.author)
                            if (root.row.license)
                                parts.push(root.row.license)
                            if (root.row.version)
                                parts.push(root.state === "update-available"
                                           ? qsTr("%1 → %2").arg(root.row.installedVersion).arg(root.row.version)
                                           : root.row.version)
                            const size = root.installed && !root.installable
                                         ? DownloadFormat.formatBytes(root.row.installedSize)
                                         : DownloadFormat.formatBytes(root.row.downloadSize)
                            if (size.length > 0)
                                parts.push(size)
                            return parts.join(" · ")
                        }
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd

                Flow {
                    width: parent.width
                    spacing: Theme.spacingLg

                    ThemedButton {
                        visible: root.installable || root.busy || root.state === "needs-newer-app"
                        enabled: root.state !== "needs-newer-app"
                        variant: root.busy ? "secondary" : "primary"
                        text: {
                            if (root.state === "needs-newer-app")
                                return qsTr("Needs a newer Drift")
                            if (root.busy)
                                return qsTr("Cancel")
                            if (root.state === "update-available")
                                return qsTr("Update")
                            if (root.state === "failed")
                                return qsTr("Retry")
                            return qsTr("Install")
                        }
                        onClicked: {
                            if (root.busy)
                                Addons.cancel(root.addonId)
                            else
                                Addons.installMany([root.addonId])
                        }
                    }

                    Row {
                        visible: root.state === "installed"
                        height: Theme.controlHeight
                        spacing: Theme.spacingSm

                        IconGlyph {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.success
                            iconColor: Theme.c.extrasStore.stateInstalled
                        }
                        ThemedLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Installed")
                            tone: "default"
                            size: "sm"
                        }
                    }

                    ThemedButton {
                        visible: root.installed && !root.busy
                        variant: "ghost"
                        text: qsTr("Remove")
                        tooltip: qsTr("Delete this pack's downloaded data")
                        onClicked: root.store.requestRemoval(root.addonId, root.row.name || "")
                    }
                }

                ThemedProgressBar {
                    width: Math.min(parent.width, 360)
                    visible: root.active
                    value: root.transfer ? root.transfer.fraction : 0
                }

                ThemedLabel {
                    width: parent.width
                    visible: text.length > 0
                    wrapMode: Text.WordWrap
                    size: "sm"
                    color: root.state === "failed" || root.state === "needs-newer-app"
                           ? Theme.c.extrasStore.stateFailed : Theme.c.extrasStore.foregroundMuted
                    text: {
                        if (root.state === "queued")
                            return qsTr("Waiting for other downloads to finish…")
                        if (root.active)
                            return root.transfer ? qsTr("%1… %2%").arg(root.transfer.phase).arg(Math.round(root.transfer.fraction * 100)) : ""
                        if (root.state === "failed")
                            return root.row.error || ""
                        if (root.state === "needs-newer-app")
                            return qsTr("Requires Drift %1 or newer").arg(root.row.minAppVersion)
                        if (root.installable && root.alsoInstalls.length > 0)
                            return qsTr("Also installs: %1").arg(root.alsoInstalls.map(a => {
                                const size = DownloadFormat.formatBytes(a.downloadSize)
                                return size.length > 0 ? qsTr("%1 (%2)").arg(a.name).arg(size) : a.name
                            }).join(", "))
                        return ""
                    }
                }
            }

            Section {
                heading: qsTr("About")
                visible: (root.row.summary || root.row.description || "").length > 0
                ThemedLabel {
                    width: parent.width
                    text: root.row.summary || root.row.description || ""
                    tone: "default"
                    size: "sm"
                    wrapMode: Text.WordWrap
                }
            }

            Section {
                heading: qsTr("Images")
                visible: !!root.row.images && root.row.images.length > 0
                ListView {
                    width: parent.width
                    height: 220
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingLg
                    clip: true
                    model: root.row.images || []
                    ScrollBar.horizontal: AppScrollBar {}

                    delegate: Column {
                        id: shot
                        required property var modelData
                        spacing: Theme.spacingXs

                        Image {
                            id: shotImage
                            height: 190
                            width: shot.modelData.width > 0 && shot.modelData.height > 0
                                   ? height * shot.modelData.width / shot.modelData.height : 320
                            source: shot.modelData.src
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        ThemedLabel {
                            width: shotImage.width
                            visible: text.length > 0
                            text: shot.modelData.caption || ""
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Section {
                heading: qsTr("What it's for")
                visible: !!root.row.useCases && root.row.useCases.length > 0
                Repeater {
                    model: root.row.useCases || []
                    delegate: ThemedLabel {
                        required property string modelData
                        width: parent.width
                        text: "•  " + modelData
                        tone: "default"
                        size: "sm"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Section {
                heading: qsTr("How to use")
                visible: !!root.row.howTo && root.row.howTo.length > 0
                Repeater {
                    model: root.row.howTo || []
                    delegate: ThemedLabel {
                        required property int index
                        required property string modelData
                        width: parent.width
                        text: (index + 1) + ".  " + modelData
                        tone: "default"
                        size: "sm"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Section {
                heading: qsTr("Unlocks")
                visible: !!root.row.unlocks && root.row.unlocks.length > 0
                Repeater {
                    model: root.row.unlocks || []
                    delegate: ThemedLabel {
                        required property var modelData
                        width: parent.width
                        text: modelData.location && modelData.location.length > 0
                              ? qsTr("%1 — %2").arg(modelData.label).arg(modelData.location)
                              : modelData.label
                        tone: "default"
                        size: "sm"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Column {
                id: accelerationPanel
                width: parent.width
                visible: root.hasRuntimeKind
                spacing: Theme.spacingSm

                property var options: Addons.accelerationOptions()
                property bool runtimeReady: Addons.runtimeAvailable()
                property bool restartRequired: Addons.runtimeRestartRequired()
                property string problem: Addons.accelerationProblem()

                Connections {
                    target: Addons
                    function onKindChanged(kind) {
                        if (kind !== "onnxruntime" && kind !== "onnxruntime-ep")
                            return
                        accelerationPanel.options = Addons.accelerationOptions()
                        accelerationPanel.runtimeReady = Addons.runtimeAvailable()
                        accelerationPanel.restartRequired = Addons.runtimeRestartRequired()
                        accelerationPanel.problem = Addons.accelerationProblem()
                    }
                }

                ThemedLabel {
                    text: qsTr("How AI runs")
                    tone: "default"
                    size: "base"
                    font.weight: Font.Medium
                }

                ThemedComboBox {
                    width: 260
                    textRole: "label"
                    valueRole: "value"
                    model: accelerationPanel.options
                    currentIndex: {
                        const current = Addons.acceleration()
                        for (let i = 0; i < accelerationPanel.options.length; ++i) {
                            if (accelerationPanel.options[i].value === current)
                                return i
                        }
                        return 0
                    }
                    onActivated: {
                        Addons.setAcceleration(accelerationPanel.options[index].value)
                        accelerationPanel.problem = Addons.accelerationProblem()
                    }
                }

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    size: "sm"
                    text: accelerationPanel.runtimeReady
                          ? qsTr("Automatic picks the fastest option you have installed, and uses this computer if the graphics card can't help.")
                          : qsTr("Install an AI engine to unlock auto captions, subject cutout, funny face effects, and noise removal.")
                }

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    size: "sm"
                    visible: accelerationPanel.problem.length > 0
                    color: Theme.c.extrasStore.stateFailed
                    text: accelerationPanel.problem
                }

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    size: "sm"
                    visible: accelerationPanel.restartRequired
                    color: Theme.c.extrasStore.stateFailed
                    text: qsTr("Restart Drift for this to take effect.")
                }
            }

            Section {
                heading: qsTr("Needs")
                visible: !!root.row.requires && root.row.requires.length > 0
                Repeater {
                    model: root.row.requires || []
                    delegate: Rectangle {
                        id: need
                        required property var modelData
                        width: parent.width
                        height: needCol.height + Theme.spacingLg * 2
                        radius: Theme.c.extrasStore.chipRadius
                        color: Theme.c.extrasStore.chipBackground

                        TapHandler {
                            enabled: need.modelData.id && need.modelData.id.length > 0
                            onTapped: root.store.openDetail(need.modelData.id)
                        }

                        Column {
                            id: needCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.spacingXl
                            spacing: Theme.spacingXs

                            Row {
                                spacing: Theme.spacingSm
                                IconGlyph {
                                    anchors.verticalCenter: parent.verticalCenter
                                    glyph: need.modelData.installed ? Theme.icons.success : Theme.icons.clock
                                    iconSize: Theme.iconSizeSm
                                    iconColor: need.modelData.installed ? Theme.c.extrasStore.stateInstalled
                                                                        : Theme.c.extrasStore.foregroundMuted
                                }
                                ThemedLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: need.modelData.installed
                                          ? qsTr("%1 · Installed").arg(need.modelData.name)
                                          : qsTr("%1 · Will be installed").arg(need.modelData.name)
                                    tone: "default"
                                    size: "sm"
                                }
                            }
                            ThemedLabel {
                                width: parent.width
                                visible: text.length > 0
                                text: need.modelData.reason || ""
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            Section {
                heading: qsTr("Works well with")
                visible: !!root.row.recommends && root.row.recommends.length > 0
                Repeater {
                    model: root.row.recommends || []
                    delegate: Rectangle {
                        id: rec
                        required property var modelData
                        width: parent.width
                        height: recCol.height + Theme.spacingLg * 2
                        radius: Theme.c.extrasStore.chipRadius
                        color: Theme.c.extrasStore.chipBackground

                        TapHandler {
                            enabled: rec.modelData.id && rec.modelData.id.length > 0
                            onTapped: root.store.openDetail(rec.modelData.id)
                        }

                        Column {
                            id: recCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.spacingXl
                            spacing: Theme.spacingXs

                            ThemedLabel {
                                width: parent.width
                                text: rec.modelData.installed ? qsTr("%1 · Installed").arg(rec.modelData.name)
                                                              : rec.modelData.name
                                tone: "default"
                                size: "sm"
                                elide: Text.ElideRight
                            }
                            ThemedLabel {
                                width: parent.width
                                visible: text.length > 0
                                text: rec.modelData.reason || ""
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: (root.row.details || "").length > 0

                Row {
                    spacing: Theme.spacingSm
                    TapHandler { onTapped: root.detailsOpen = !root.detailsOpen }

                    ThemedLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Technical details")
                        tone: "default"
                        size: "base"
                        font.weight: Font.Medium
                    }
                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: root.detailsOpen ? Theme.icons.chevronUp : Theme.icons.chevronDown
                    }
                }

                ThemedLabel {
                    width: parent.width
                    visible: root.detailsOpen
                    text: root.row.details || ""
                    tone: "default"
                    size: "sm"
                    wrapMode: Text.WordWrap
                }

                ThemedLabel {
                    width: parent.width
                    visible: root.detailsOpen
                    wrapMode: Text.WordWrap
                    text: [root.row.id, root.row.platform].filter(p => !!p).join(" · ")
                }
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingMd
                visible: !!root.row.links && root.row.links.length > 0

                Repeater {
                    model: root.row.links || []
                    delegate: ThemedButton {
                        required property var modelData
                        visible: root.isSafeUrl(modelData.url)
                        variant: "ghost"
                        text: modelData.label
                        onClicked: Qt.openUrlExternally(modelData.url)
                    }
                }
            }
        }
    }
}
