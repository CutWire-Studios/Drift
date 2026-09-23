import QtQuick
import QtQuick.Controls.Basic
import Drift

// Browse, install and remove addons. Content that used to be baked into the build — fonts,
// stickers, the Whisper model — is downloaded from here instead.
ThemedDialog {
    id: root

    title: qsTr("Extras")
    preferredWidth: Theme.dialogWidthLg
    showAccept: false
    rejectText: qsTr("Close")

    // Confirms destructive removal, which used to happen instantly on click even
    // for a 670 MB model.
    property string pendingRemovalId: ""
    property string pendingRemovalName: ""

    // Deeper tech notes for a catalogue row — kept out of the list so beginners see the
    // short description, while power users can open this on demand.
    property string detailsName: ""
    property string detailsBody: ""
    property string detailsMeta: ""

    ThemedDialog {
        id: confirmRemoval
        title: qsTr("Remove this pack?")
        acceptText: qsTr("Remove")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        // Enter must not commit a destructive action.
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("“%1” and its downloaded data will be deleted. You can install it again later.")
                  .arg(root.pendingRemovalName)
        }

        onAccepted: {
            Addons.uninstall(root.pendingRemovalId)
            root.pendingRemovalId = ""
        }
        onRejected: root.pendingRemovalId = ""
    }

    ThemedDialog {
        id: detailsDialog
        title: root.detailsName
        showAccept: false
        rejectText: qsTr("Close")
        preferredWidth: Theme.dialogWidthMd

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthMd
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                size: "sm"
                tone: "default"
                text: root.detailsBody
            }

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                size: "sm"
                visible: root.detailsMeta.length > 0
                text: root.detailsMeta
            }
        }
    }

    // Which addon to scroll to and highlight when opened from an empty state.
    property string highlightId: ""
    property string kindFilter: "all"
    // The kinds the selected category covers; empty means every kind. A category is not always one
    // kind — AI Engine spans the core engines and the graphics speed-boost packs.
    property var kindFilterKinds: []

    // id -> { fraction, phase }. Rebuilt wholesale on each signal so the bindings re-evaluate.
    property var transfers: ({})

    function openForKind(kind) {
        if (kind === "onnxruntime" || kind === "onnxruntime-ep") {
            // The runtime has no obvious row to highlight when nothing is installed yet, so land
            // on the category instead — the picker at the top of it is the point.
            root.kindFilter = "onnxruntime"
            root.kindFilterKinds = ["onnxruntime", "onnxruntime-ep"]
        } else if (kind === "whisper-model" || kind === "denoise-model"
                   || kind === "sam2-model" || kind === "face-model"
                   || kind === "object-model" || kind === "vad-model"
                   || kind === "align-model" || kind === "diarize-model") {
            root.kindFilter = "whisper-model"
            root.kindFilterKinds = ["whisper-model", "denoise-model", "sam2-model", "face-model",
                                    "object-model", "vad-model", "align-model", "diarize-model"]
        } else if (kind === "effects" || kind === "effect-templates") {
            root.kindFilter = "effects"
            root.kindFilterKinds = ["effects"]
        } else if (kind === "transitions" || kind === "audio-effects"
                   || kind === "fonts" || kind === "stickers") {
            root.kindFilter = kind
            root.kindFilterKinds = [kind]
        } else {
            root.kindFilter = "all"
            root.kindFilterKinds = []
        }
        root.highlightId = Addons.firstAddonForKind(kind)
        root.open()
    }

    function formatSize(bytes) {
        if (bytes >= 1e9)
            return (bytes / 1e9).toFixed(1) + " GB"
        if (bytes >= 1e6)
            return Math.round(bytes / 1e6) + " MB"
        return Math.max(1, Math.round(bytes / 1e3)) + " KB"
    }

    onOpened: Addons.refresh(true)

    Connections {
        target: Addons
        function onProgressChanged(id, fraction, phase) {
            // A var property does not notify when the same object is assigned back, so
            // mutating in place left every `row.transfer` binding on the empty map.
            var next = Object.assign({}, root.transfers)
            next[id] = { fraction: fraction, phase: phase }
            root.transfers = next
        }
    }

    contentItem: Item {
        implicitHeight: 460

        // Eight category chips do not fit a single row once the dialog is clamped to
        // the window (and Android chips are wider). Wrap so "AI tools" / "AI engine"
        // stay on screen instead of clipping off the right edge.
        Flow {
            id: filters
            spacing: Theme.spacingMd
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            Repeater {
                model: [
                    { id: "all", label: qsTr("All"), kinds: [] },
                    { id: "effects", label: qsTr("Effects"), kinds: ["effects"] },
                    { id: "transitions", label: qsTr("Transitions"), kinds: ["transitions"] },
                    { id: "audio-effects", label: qsTr("Audio FX"), kinds: ["audio-effects"] },
                    { id: "fonts", label: qsTr("Fonts"), kinds: ["fonts"] },
                    { id: "stickers", label: qsTr("Stickers"), kinds: ["stickers"] },
                    { id: "whisper-model", label: qsTr("AI tools"),
                      kinds: ["whisper-model", "denoise-model", "sam2-model", "face-model",
                              "object-model", "vad-model", "align-model", "diarize-model"] },
                    { id: "onnxruntime", label: qsTr("AI engine"),
                      kinds: ["onnxruntime", "onnxruntime-ep"] }
                ]

                ThemedChip {
                    text: modelData.label
                    selected: root.kindFilter === modelData.id
                    onClicked: {
                        root.kindFilter = modelData.id
                        root.kindFilterKinds = modelData.kinds
                    }
                }
            }
        }

        Text {
            id: statusLine
            anchors.top: filters.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            visible: Addons.status.length > 0 || Addons.refreshing
            text: Addons.refreshing ? qsTr("Checking for extras…") : Addons.status
            color: Addons.refreshing ? Theme.mutedForeground : Theme.destructive
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.WordWrap
        }

        // Which installed AI engine the features use. It lives here rather than in a settings
        // page because the thing it chooses between is what this dialog installs.
        Column {
            id: accelerationPanel
            visible: root.kindFilter === "onnxruntime"
            anchors.top: statusLine.visible ? statusLine.bottom : filters.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingSm

            // One-shot answers about what is on disk, not bindings, so they are re-read when an
            // install or removal changes it — the same pattern the feature empty states use.
            property var options: Addons.accelerationOptions()
            property bool runtimeReady: Addons.runtimeAvailable()
            property bool restartRequired: Addons.runtimeRestartRequired()

            Connections {
                target: Addons
                function onKindChanged(kind) {
                    if (kind !== "onnxruntime" && kind !== "onnxruntime-ep")
                        return
                    accelerationPanel.options = Addons.accelerationOptions()
                    accelerationPanel.runtimeReady = Addons.runtimeAvailable()
                    accelerationPanel.restartRequired = Addons.runtimeRestartRequired()
                }
            }

            ThemedLabel {
                text: qsTr("How AI runs")
                size: "sm"
                tone: "default"
            }

            ThemedComboBox {
                id: accelerationBox
                width: 260
                textRole: "label"
                valueRole: "value"
                model: accelerationPanel.options
                currentIndex: {
                    var current = Addons.acceleration()
                    for (var i = 0; i < accelerationPanel.options.length; ++i) {
                        if (accelerationPanel.options[i].value === current)
                            return i
                    }
                    return 0
                }
                onActivated: Addons.setAcceleration(accelerationPanel.options[index].value)
            }

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                size: "sm"
                text: accelerationPanel.runtimeReady
                      ? qsTr("Automatic picks the fastest option you have installed, and uses this computer if the graphics card can't help.")
                      : qsTr("Install an AI Engine below to unlock auto captions, subject cutout, funny face effects, and noise removal.")
            }

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                size: "sm"
                visible: accelerationPanel.restartRequired
                color: Theme.destructive
                tone: "default"
                text: qsTr("Restart Drift for this to take effect.")
            }
        }

        ListView {
            id: list
            anchors.top: accelerationPanel.visible ? accelerationPanel.bottom
                                                   : (statusLine.visible ? statusLine.bottom : filters.bottom)
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            clip: true
            spacing: 8
            model: Addons.catalog.filter(function (addon) {
                if (root.kindFilter === "all")
                    return true
                var wanted = root.kindFilterKinds
                if (!wanted || wanted.length === 0)
                    wanted = [root.kindFilter]
                var have = addon.kinds && addon.kinds.length ? addon.kinds : [addon.kind]
                for (var i = 0; i < wanted.length; ++i) {
                    if (have.indexOf(wanted[i]) !== -1)
                        return true
                }
                return false
            })

            ScrollBar.vertical: AppScrollBar {}

            // Skeleton rows while the catalogue is being fetched. The list area
            // used to be entirely blank during a refresh.
            Column {
                anchors.fill: parent
                spacing: Theme.spacingLg
                visible: Addons.refreshing && list.count === 0

                Repeater {
                    model: 4
                    SkeletonBox {
                        width: parent.width
                        height: 72
                        radius: Theme.radiusMd
                    }
                }
            }

            // The offline case offers a retry now; Addons.refresh() existed but
            // was unreachable once the list was empty.
            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacing3xl, 300)
                visible: list.count === 0 && !Addons.refreshing
                glyph: Addons.status.length > 0 ? Theme.icons.warning : Theme.icons.puzzle
                title: Addons.status.length > 0
                       ? qsTr("Can't reach the download store")
                       : qsTr("Nothing in this category")
                hint: Addons.status.length > 0
                      ? qsTr("Check your connection and try again.")
                      : qsTr("Pick another category above.")
                actionText: Addons.status.length > 0 ? qsTr("Retry") : ""
                onActionTriggered: Addons.refresh(true)
            }

            delegate: Rectangle {
                id: row

                required property var modelData

                readonly property var transfer: root.transfers[modelData.id]
                readonly property bool active: modelData.state === "downloading"
                                               || modelData.state === "installing"

                width: ListView.view.width
                height: body.implicitHeight + Theme.spacing3xl
                radius: Theme.radiusMd
                // Rows had no hover state at all; only the highlighted row
                // differed from the rest.
                color: rowHover.hovered ? Theme.popoverHover : Theme.panelSecondaryBg
                border.width: Theme.borderWidth
                border.color: modelData.id === root.highlightId ? Theme.primary : Theme.panelSecondaryBorder

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                HoverHandler { id: rowHover }

                Column {
                    id: body
                    anchors.left: parent.left
                    anchors.right: actions.left
                    anchors.leftMargin: Theme.spacing2xl - Theme.spacingXs
                    anchors.rightMargin: Theme.spacingXl
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingSm

                    Row {
                        // Bounded so a long addon name elides instead of pushing
                        // the version label out of the row.
                        width: body.width
                        spacing: Theme.spacingLg

                        ThemedLabel {
                            id: nameLabel
                            width: Math.min(implicitWidth,
                                            body.width - versionLabel.width
                                            - (infoButton.visible ? infoButton.width + parent.spacing : 0)
                                            - parent.spacing)
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.name
                            tone: "default"
                            size: "base"
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        IconButton {
                            id: infoButton
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !!(row.modelData.details && row.modelData.details.length > 0)
                            width: visible ? buttonSize : 0
                            height: visible ? buttonSize : 0
                            buttonSize: 22
                            iconSize: Theme.iconSizeSm
                            glyph: Theme.icons.info
                            tooltip: qsTr("Technical details")
                            onClicked: {
                                root.detailsName = row.modelData.name
                                root.detailsBody = row.modelData.details
                                var meta = []
                                if (row.modelData.id)
                                    meta.push(row.modelData.id)
                                if (row.modelData.author)
                                    meta.push(row.modelData.author)
                                if (row.modelData.license)
                                    meta.push(row.modelData.license)
                                if (row.modelData.platform)
                                    meta.push(row.modelData.platform)
                                root.detailsMeta = meta.join(" · ")
                                detailsDialog.open()
                            }
                        }

                        ThemedLabel {
                            id: versionLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.state === "update-available"
                                  ? qsTr("%1 → %2").arg(row.modelData.installedVersion).arg(row.modelData.version)
                                  : row.modelData.version
                        }
                    }

                    ThemedLabel {
                        width: body.width
                        text: row.modelData.description
                        size: "sm"
                    }

                    ThemedLabel {
                        width: body.width
                        text: {
                            if (row.active && row.transfer)
                                return qsTr("%1… %2%").arg(row.transfer.phase)
                                                      .arg(Math.round(row.transfer.fraction * 100))
                            if (row.modelData.state === "failed")
                                return row.modelData.error
                            var parts = [qsTr("%1 download").arg(root.formatSize(row.modelData.downloadSize))]
                            if (row.modelData.items > 0)
                                parts.push(qsTr("%1 items").arg(row.modelData.items))
                            if (row.modelData.license.length > 0)
                                parts.push(row.modelData.license)
                            return parts.join(" · ")
                        }
                        color: row.modelData.state === "failed" ? Theme.destructive : Theme.mutedForeground
                    }
                }

                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    CircularProgress {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.active
                        // Download and extraction each report their own 0..1, so the ring restarts
                        // when the phase changes. Until a fraction arrives it spins
                        // indeterminately rather than sitting at an empty ring that
                        // looks like "not started".
                        indeterminate: !row.transfer || row.transfer.fraction <= 0
                        value: row.transfer ? row.transfer.fraction : 0
                    }

                    // Next to the ring, not in a hover tooltip: touch has no hover, and a
                    // 28px ring cannot fit a readable number inside it.
                    ThemedLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.active && row.transfer && row.transfer.fraction > 0
                        text: row.transfer
                              ? Math.round(row.transfer.fraction * 100) + "%"
                              : ""
                        size: "sm"
                        tone: "default"
                        font.weight: Font.Medium
                    }

                    ThemedButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.active
                        text: qsTr("Cancel")
                        variant: "ghost"
                        onClicked: Addons.cancel(row.modelData.id)
                    }

                    ThemedButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !row.active && row.modelData.state !== "installed"
                        text: row.modelData.state === "update-available" ? qsTr("Update")
                            : row.modelData.state === "failed" ? qsTr("Retry")
                            : qsTr("Install")
                        variant: "primary"
                        onClicked: Addons.install(row.modelData.id)
                    }

                    ThemedButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !row.active && row.modelData.installedVersion.length > 0
                        text: qsTr("Remove")
                        variant: "ghost"
                        tooltip: qsTr("Delete this pack's downloaded data")
                        onClicked: {
                            root.pendingRemovalId = row.modelData.id
                            root.pendingRemovalName = row.modelData.name
                            confirmRemoval.open()
                        }
                    }
                }
            }
        }
    }
}
