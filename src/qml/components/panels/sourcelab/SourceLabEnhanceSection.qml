import QtQuick
import Drift
import "SourceLabTime.js" as Time

SourceLabSection {
    id: root

    readonly property var lab: EditorState.sourceLab
    readonly property var recipe: root.lab.recipe
    property bool runtimeReady: Addons.runtimeAvailable()
    readonly property bool usable: root.lab.enhanceAvailable
    property var decompressModels: []
    property var upscaleModels: []

    readonly property string decompressId: root.recipe.decompressModel ?? ""
    readonly property string upscaleId: root.recipe.upscaleModel ?? ""
    readonly property var upscaleChoice: root.upscaleModels.find(m => m.value === root.upscaleId)
                                         ?? root.upscaleModels[0] ?? ({ value: "", scale: 1 })
    readonly property var decompressChoice: root.decompressModels.find(m => m.value === root.decompressId)
                                            ?? ({ value: "", scale: 1 })
    readonly property real scale: (root.upscaleChoice.scale || 1) * (root.decompressChoice.scale || 1)
    readonly property var crop: root.recipe.crop ?? Qt.rect(0, 0, 1, 1)
    readonly property bool rotated: {
        const index = AssetLibrary.indexOfId(root.lab.currentAssetId)
        return index >= 0 && AssetLibrary.assetAt(index).effectiveRotation % 180 === 90
    }
    readonly property int outWidth: Math.round((root.rotated ? root.lab.height : root.lab.width)
                                               * root.crop.width * root.scale)
    readonly property int outHeight: Math.round((root.rotated ? root.lab.width : root.lab.height)
                                                * root.crop.height * root.scale)
    readonly property real enhanceSeconds: {
        void root.recipe
        return root.dirty ? (root.lab.plan("replace").enhanceSeconds ?? -1) : -1
    }

    title: qsTr("Enhance")
    dirty: root.decompressId !== "" || root.upscaleId !== ""
    onResetRequested: root.lab.resetRecipe("enhance")

    function formatDuration(seconds) {
        const total = Math.round(seconds)
        if (total >= 3600)
            return qsTr("%1 h %2 min").arg(Math.floor(total / 3600)).arg(Math.floor(total / 60) % 60)
        if (total >= 60)
            return qsTr("%1 min").arg(Math.round(total / 60))
        return qsTr("%1 s").arg(total)
    }

    function refreshModels() {
        root.runtimeReady = Addons.runtimeAvailable()
        const decompress = [{ label: qsTr("None"), value: "", scale: 1 }]
        const upscale = [{ label: qsTr("None"), value: "", scale: 1, summary: qsTr("Keep the original size.") }]
        const models = Addons.runtimeAvailable() ? EditorState.restore.models() : []
        for (const m of models) {
            const label = m.custom ? qsTr("%1 (custom, experimental)").arg(m.name) : m.name
            const entry = { label: label, value: m.id, scale: m.scale, summary: m.summary }
            if (m.task === "decompress")
                decompress.push(entry)
            else
                upscale.push(entry)
        }
        root.decompressModels = decompress
        root.upscaleModels = upscale
    }

    Component.onCompleted: root.refreshModels()

    Connections {
        target: Addons
        function onKindChanged(kind) {
            root.refreshModels()
        }
    }

    ThemedLabel {
        width: parent.width
        visible: !root.usable
        wrapMode: Text.WordWrap
        text: qsTr("No enhance models are installed.")
    }

    ThemedButton {
        visible: !root.usable
        variant: "secondary"
        text: root.runtimeReady ? qsTr("Get models") : qsTr("Install AI engine first")
        onClicked: {
            if (root.runtimeReady)
                Qt.openUrlExternally("https://openmodeldb.info")
            else
                ShellBridge.main.openAddonManager("onnxruntime")
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm
        visible: root.usable

        ThemedLabel { text: qsTr("Remove compression") }
        ThemedComboBox {
            width: parent.width
            model: root.decompressModels
            textRole: "label"
            currentIndex: Math.max(0, root.decompressModels.findIndex(m => m.value === root.decompressId))
            onActivated: index => root.lab.setRecipeValue("decompressModel", root.decompressModels[index].value)
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm
        visible: root.usable

        ThemedLabel {
            text: qsTr("Upscale")
            SearchAnchor {
                searchId: "sourcelab-upscale"
                title: qsTr("Upscale source clip")
                keywords: "upscale enhance restore resolution sharpen a/b compare"
            }
        }
        ThemedComboBox {
            width: parent.width
            model: root.upscaleModels
            textRole: "label"
            currentIndex: Math.max(0, root.upscaleModels.findIndex(m => m.value === root.upscaleId))
            onActivated: index => root.lab.setRecipeValue("upscaleModel", root.upscaleModels[index].value)
        }
        ThemedLabel {
            width: parent.width
            visible: (root.upscaleChoice.summary ?? "").length > 0
            wrapMode: Text.WordWrap
            text: root.upscaleChoice.summary ?? ""
        }
    }

    ThemedLabel {
        width: parent.width
        visible: root.usable && root.dirty && root.lab.width > 0
        text: qsTr("Output size: %1 x %2").arg(root.outWidth).arg(root.outHeight)
    }

    ThemedLabel {
        width: parent.width
        visible: root.usable && root.enhanceSeconds > 60
        wrapMode: Text.WordWrap
        color: Theme.c.sourceLabPanel.warning
        text: qsTr("Enhancing this range takes about %1. Trim to the part you need first.")
              .arg(root.formatDuration(root.enhanceSeconds))
    }
}
