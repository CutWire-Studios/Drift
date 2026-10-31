import QtQuick
import QtQuick.Controls.Basic
import Drift

// Browse, install and remove addons. Hosted by AddonManagerDialog on the desktop and by the
// phone's full-page Extras screen; the host calls refresh() when it comes up.
Item {
    id: root

    implicitHeight: 460

    // importUserPackage()/openForKind() ask the host to show itself first.
    signal showRequested()

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

    // A .driftfx from Drift Forge. Nothing signs these, so the user confirms before it installs.
    property var pendingUserPackage: ({})
    // A .driftpkg or .zip. Installs without asking only when the Drift team signed it.
    property var pendingAddonFile: ({})

    function importUserPackage(url) {
        if (!url || String(url) === "")
            url = FileDialogs.openFile(qsTr("Install Addon"),
                                       [qsTr("Drift addons (*.driftpkg *.driftfx *.zip)")])
        if (!url || String(url) === "")
            return
        const info = Addons.inspectAddonFile(url)
        if (info.error) {
            Toasts.error(info.error)
            return
        }
        if (!root.visible)
            root.showRequested()
        if (info.type === "driftfx") {
            root.pendingUserPackage = info
            confirmUserPackage.open()
            return
        }
        root.pendingAddonFile = info
        Addons.installAddonFile(false)
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
                var lines = [qsTr("%1 is not signed by the Drift team. Only install files you trust.").arg(by)]
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
        onRejected: root.pendingUserPackage = {}
    }

    Connections {
        target: Addons
        function onAddonFileNeedsConfirmation() {
            confirmUnverified.open()
        }
        function onUserPackageInstalled(name, error) {
            if (error.length > 0)
                Toasts.error(qsTr("Could not install “%1”: %2").arg(name).arg(error))
            else
                Toasts.success(qsTr("Installed “%1”").arg(name))
        }
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
                   || kind === "align-model" || kind === "diarize-model"
                   || kind === "depth-model" || kind === "restore-model"
                   || kind === "tts-model" || kind === "audio-tag-model"
                   || kind === "audio-class-model") {
            root.kindFilter = "whisper-model"
            root.kindFilterKinds = ["whisper-model", "denoise-model", "sam2-model", "face-model",
                                    "object-model", "vad-model", "align-model", "diarize-model",
                                    "depth-model", "restore-model", "tts-model",
                                    "audio-tag-model", "audio-class-model"]
        } else if (kind === "effects" || kind === "effect-templates") {
            root.kindFilter = "effects"
            root.kindFilterKinds = ["effects"]
        } else if (kind === "transitions" || kind === "audio-effects"
                   || kind === "fonts" || kind === "stickers" || kind === "looks") {
            root.kindFilter = kind
            root.kindFilterKinds = [kind]
        } else {
            root.kindFilter = "all"
            root.kindFilterKinds = []
        }
        root.highlightId = Addons.firstAddonForKind(kind)
        root.showRequested()
    }

    function formatSize(bytes) {
        if (bytes >= 1e9)
            return (bytes / 1e9).toFixed(1) + " GB"
        if (bytes >= 1e6)
            return Math.round(bytes / 1e6) + " MB"
        return Math.max(1, Math.round(bytes / 1e3)) + " KB"
    }

    function refresh() {
        Addons.rescanCustomAddons()
        Addons.refresh(true)
    }

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
                { id: "looks", label: qsTr("Looks"), kinds: ["looks"] },
                { id: "fonts", label: qsTr("Fonts"), kinds: ["fonts"] },
                { id: "stickers", label: qsTr("Stickers"), kinds: ["stickers"] },
                { id: "whisper-model", label: qsTr("AI tools"),
                  kinds: ["whisper-model", "denoise-model", "sam2-model", "face-model",
                          "object-model", "vad-model", "align-model", "diarize-model",
                          "depth-model", "restore-model", "tts-model",
                          "audio-tag-model", "audio-class-model"] },
                { id: "onnxruntime", label: qsTr("AI engine"),
                  kinds: ["onnxruntime", "onnxruntime-ep"] },
                { id: "custom", label: qsTr("Custom"), kinds: [] }
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

        ThemedButton {
            text: qsTr("Install from file…")
            variant: "ghost"
            onClicked: root.importUserPackage("")
        }

        // Android keeps the folder inside the app's private storage, out of a file manager's reach.
        ThemedButton {
            visible: Qt.platform.os !== "android"
            text: qsTr("Open addons folder")
            variant: "ghost"
            tooltip: qsTr("Put addon folders here, then reopen Extras")
            onClicked: Qt.openUrlExternally(Addons.customAddonsFolderUrl())
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
        color: Addons.refreshing ? Theme.c.addonManagerDialog.statusBusy
                                  : Theme.c.addonManagerDialog.status
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
                  : qsTr("Install an AI Engine below to unlock auto captions, subject cutout, funny face effects, and noise removal.")
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            size: "sm"
            visible: accelerationPanel.problem.length > 0
            color: Theme.c.addonManagerDialog.restartForeground
            tone: "default"
            text: accelerationPanel.problem
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            size: "sm"
            visible: accelerationPanel.restartRequired
            color: Theme.c.addonManagerDialog.restartForeground
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
            if (root.kindFilter === "custom")
                return !!addon.custom
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
                    radius: Theme.c.addonManagerDialog.skeletonRadius
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

        delegate: AddonManagerRow { dialog: root }
    }
}
