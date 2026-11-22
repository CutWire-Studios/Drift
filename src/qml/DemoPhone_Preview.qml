import QtQuick
import QtQuick.Controls.Basic
import Drift

// The phone editor at phone size, for checking the mobile layout on desktop.
//   drift --preview=DemoPhone [--phone-tool=<toolSpecs id>[:mode]] [--phone-media=<file>] [--phone-canvas=WxH]
// Without --phone-media the 9:16 project gets the landscape mock clip.
//   --phone-select=none      no clip selected
//   --phone-empty=1          empty project (no clip added)
//   --phone-sheet=add|more|project|options   open that sheet / the preview options strip
//   (a sheet also saves the editor as DemoPhone_base.png + DemoPhone_overlay.png (the sheet
//                            alone; sheets live in the overlay, composite the two at the bottom))
//   --phone-dialog=<name>[:arg]   open a popup in the sheet window and save DemoPhone_dialog.png (whole window, popups included):
//                            export[:audio|gif|advanced], exportprogress, notifications[:toasts], layout, easing, transition, link,
//                            composer, textedit, layout, share, assets:<media|sounds|effects|...>, scroll with --phone-dscroll=N|end
//   --phone-screen=settings[:<pageId>]|extras   host the Settings / Extras sub page instead of the editor
//   --phone-screen=home[:0|1|2]   host AndroidHome (tab index) instead of the editor
//   --phone-vector=<.svg|.json>   a Lottie/SVG clip instead of media (the media import path does not make a clip for these)
//   --phone-clips=2         a second clip (shanghai-skyline.mp4) right after the first
//   --phone-transition=<kind>   a transition of that kind on the cut (needs --phone-clips=2) and selects it
//   --phone-effect=<id|first>   apply a video effect to the clip (first = first catalogue entry)
//   --phone-audiofx=<id|first>  apply an audio effect (use with --phone-media=<audio file>)
//   --phone-subtitles=1     import captions.srt and select the subtitle clip
//   --phone-prop=key=value  extra property for the opened tool page (true/false become booleans)
//   --phone-pick=N          on an add page (sticker/shape/text/mask) pick the Nth card in the library (0-based)
//   --phone-add=text|shape|sticker   add such a clip (selected) before the tool opens, for the style pages
//   --phone-mask=<shape>    add that mask (e.g. rect) to the clip and select the mask clip, before the tool opens
//   --phone-masktool=<id>   set the preview's masking tool (rect, ellipse, polygon, brush, ai, tracker) once the tool opens
//   --phone-stickers=<dir>   import that folder of images as a sticker pack first
//   --phone-expand=1        expand every shading layer in the open text/shape style inspector
//   --phone-scroll=N         scroll the open tool page's controls by N px (a number, or "end")
Item {
    id: root
    implicitWidth: 393
    implicitHeight: 800

    function arg(name, fallback) {
        const prefix = "--" + name + "="
        const a = Qt.application.arguments.find(s => s.startsWith(prefix))
        return a ? a.substring(prefix.length) : fallback
    }

    readonly property string tool: arg("phone-tool", "")
    readonly property string media: arg("phone-media", PreviewMediaDir + "/sunset-waves.mp4")
    readonly property var canvas: arg("phone-canvas", "1080x1920").split("x").map(Number)

    property bool started: false
    property bool settled: false
    readonly property bool previewReady: settled

    function idx() {
        return AssetLibrary.indexOfId(AssetLibrary.assetIdForPath(root.media))
    }

    Component.onCompleted: {
        Theme.inputModeOverride = "touch"
        Theme.sizeClassOverride = "compact"
        EditorState.projectFile.setProjectResolution(root.canvas[0], root.canvas[1])
        if (root.arg("phone-stickers", "") !== "")
            EditorState.importStickerPack("file://" + root.arg("phone-stickers", ""))
        if (root.arg("phone-vector", "") !== "") {
            EditorState.addVectorClip(root.arg("phone-vector", ""), -1, 0, {})
            EditorState.selectClip(0, 0)
            EditorState.playheadSeconds = 1.0
            root.started = true
            openTimer.start()
        } else if (root.arg("phone-empty", "") !== "1" && screen[0] !== "home")
            AssetLibrary.importUrlsAsync(["file://" + encodeURI(root.media)].concat(
                root.arg("phone-clips", "") === "2" ? ["file://" + PreviewMediaDir + "/shanghai-skyline.mp4"] : []))
        else {
            root.started = true
            openTimer.start()
        }
    }

    readonly property var dialog: arg("phone-dialog", "").split(":")
    readonly property bool dialogMode: dialog[0] !== ""
    readonly property bool sheetMode: dialogMode || (arg("phone-sheet", "") !== "" && arg("phone-sheet", "") !== "options")
    readonly property var editor: sheetMode ? sheetLoader.item : mainLoader.item
    readonly property var screen: arg("phone-screen", "editor").split(":")

    Timer {
        interval: 100
        repeat: true
        running: !root.started
        onTriggered: {
            const i = root.idx()
            if (AssetLibrary.importing || i < 0 || AssetLibrary.isImportPending(AssetLibrary.assetIdAt(i))
                    || !(AssetLibrary.assetAt(i).durationSeconds > 0))
                return
            root.started = true
            let track = -1
            for (let t = 0; t < EditorState.trackCount && track < 0; ++t) {
                if (EditorState.trackAcceptsAsset(t, i))
                    track = t
            }
            EditorState.addClipFromAssetAt(i, track, 0)
            if (root.arg("phone-clips", "") === "2") {
                const j = AssetLibrary.indexOfId(AssetLibrary.assetIdForPath(PreviewMediaDir + "/shanghai-skyline.mp4"))
                EditorState.addClipFromAssetAt(j, track, AssetLibrary.assetAt(i).durationSeconds)
            }
            const effect = root.arg("phone-effect", "")
            if (effect !== "")
                EditorState.addEffect(track, 0, effect === "first" ? EditorState.effectCatalog()[0].id : effect)
            const audioFx = root.arg("phone-audiofx", "")
            if (audioFx !== "")
                EditorState.addAudioEffect(track, 0, audioFx === "first" ? EditorState.audioEffectCatalog()[0].id : audioFx)
            if (root.arg("phone-select", "") !== "none")
                EditorState.selectClip(track, 0)
            const add = root.arg("phone-add", "")
            if (add === "text") EditorState.addTextClip("Hello world", -1)
            else if (add === "shape") EditorState.addShapeClip("star", -1)
            else if (add === "sticker") EditorState.addStickerClip(EditorState.builtinStickers()[0].id, -1)
            if (root.arg("phone-mask", "") !== "")
                EditorState.addMaskToClip(track, 0, root.arg("phone-mask", ""))
            const transition = root.arg("phone-transition", "")
            if (transition !== "") {
                EditorState.addTransition(track, 0, transition, 0.5)
                EditorState.selectTransition(track, 0)
            }
            if (root.arg("phone-subtitles", "") === "1") {
                EditorState.importSubtitleFile("file://" + PreviewMediaDir + "/captions.srt", 0)
            }
            EditorState.playheadSeconds = 1.0
            openTimer.start()
        }
    }

    Timer {
        id: openTimer
        interval: 800
        onTriggered: {
            const sheet = root.arg("phone-sheet", "")
            if (sheet === "add") root.editor.openAddMenu()
            else if (sheet === "more") root.editor.openMoreTools()
            else if (sheet === "project") root.editor.projectSheetItem.open()
            else if (sheet === "options") root.editor.previewItem.optionsOpen = true
            if (root.dialogMode)
                root.openDialog()
            // "crop:video" opens a tool with {mode: "video"}.
            const [tool, mode] = root.tool.split(":")
            const props = mode ? { mode: mode } : {}
            const [propKey, propValue] = root.arg("phone-prop", "").split("=")
            if (propKey)
                props[propKey] = propValue === "true" ? true : propValue === "false" ? false : propValue
            if (tool.length > 0 && root.editor)
                root.editor.openTool(tool, Object.keys(props).length > 0 ? props : undefined)
            if (root.arg("phone-masktool", "") !== "")
                Qt.callLater(() => EditorState.preview.maskingTool = root.arg("phone-masktool", ""))
            settleTimer.start()
            if (root.arg("phone-scroll", "") !== "")
                scrollTimer.start()
            if (root.arg("phone-pick", "") !== "")
                pickTimer.start()
            if (root.arg("phone-expand", "") === "1")
                expandTimer.start()
        }
    }

    function findCards(item, out) {
        if (item.visible && ((typeof item.tapped === "function" && item.payload !== undefined)
                || (typeof item.applyRequested === "function" && item.modelData !== undefined)))
            out.push(item)
        for (const c of item.children)
            findCards(c, out)
        return out
    }

    function findInspector(item) {
        if (typeof item.setLayerExpanded === "function" && item.layers !== undefined)
            return item
        for (const c of item.children) {
            const f = findInspector(c)
            if (f) return f
        }
        return null
    }

    function openDialog() {
        const e = root.editor
        const [name, a] = root.dialog
        if (name === "export") {
            const d = e.exportDialogItem
            a === "audio" ? d.openAudioOnly() : d.openDialog()
            if (a === "gif") { d.exportMode = "gif"; d.applyGifDefaults(); d.syncComboIndices() }
            if (a === "advanced") d.advancedOpen = true
        }
        else if (name === "exportprogress") e.exportProgressItem.openDialog()
        else if (name === "notifications") {
            if (a === "toasts") {
                Toasts.info(qsTr("Tap a clip to edit it"))
                Toasts.success(qsTr("Export finished"))
                Toasts.warning(qsTr("Proxy generation is slow on this device"))
                Toasts.error(qsTr("Could not open the file"))
            }
            e.notificationsItem.open()
        }
        else if (name === "layout") layoutDemo.openSheet()
        else if (name === "share") shareDemo.openFor && shareDemo.openFor(["file://" + root.media])
        else if (name === "easing") {
            EditorState.setClipKeyframe(0, 0, "opacity", 0.5, 1)
            EditorState.setClipKeyframe(0, 0, "opacity", 1.5, 0.3)
            e.openEasingSheet(0, 0, "opacity", qsTr("Opacity"), 0.5)
        }
        else if (name === "transition") e.openTransitionPicker(0, 0, "", "")
        else if (name === "link") e.startLinkImport("https://example.com/watch?v=abc")
        else if (name === "composer") e.openCueComposer(0)
        else if (name === "textedit") e.startInlineTextEdit(0, 0, true)
        else if (name === "assets") e.openAssetsTab(a || "media")
        if (root.arg("phone-dscroll", "") !== "")
            dscrollTimer.start()
    }

    Timer {
        id: dscrollTimer
        interval: 1500
        onTriggered: {
            const f = root.findFlickAny(sheetLoader.Overlay.overlay)
            const v = root.arg("phone-dscroll", "")
            if (f) f.contentY = v === "end" ? f.contentHeight - f.height : Number(v)
        }
    }

    function findFlickAny(item) {
        if (item.contentHeight !== undefined && item.contentY !== undefined && item.visible
                && item.contentHeight > item.height && item.height > 100)
            return item
        for (const c of item.children) {
            const f = findFlickAny(c)
            if (f) return f
        }
        return null
    }

    Timer {
        id: expandTimer
        interval: 1200
        onTriggered: {
            const insp = root.findInspector(root.editor)
            if (!insp)
                return console.warn("phone-expand: no inspector")
            for (const l of insp.layers)
                insp.setLayerExpanded(l.id, true)
        }
    }

    Timer {
        id: pickTimer
        interval: 1200
        onTriggered: {
            const cards = root.findCards(root.editor, [])
            const card = cards[Number(root.arg("phone-pick", "0"))]
            if (!card)
                console.warn("phone-pick: no card")
            else if (typeof card.applyRequested === "function")
                card.applyRequested(card.modelData.id)
            else
                card.tapped(1)
        }
    }

    function findFlick(item) {
        if (item.contentHeight !== undefined && item.contentY !== undefined && item.visible
                && item.contentHeight > item.height && item.height > 100 && item.mapToItem(root, 0, 0).y > 300)
            return item
        for (const c of item.children) {
            const f = findFlick(c)
            if (f) return f
        }
        return null
    }

    Timer {
        id: scrollTimer
        interval: 1500
        onTriggered: {
            const f = root.findFlick(root.editor)
            const v = root.arg("phone-scroll", "")
            if (f) f.contentY = v === "end" ? f.contentHeight - f.height : Number(v)
        }
    }

    Timer {
        id: settleTimer
        interval: 2500
        onTriggered: {
            // Popups draw in the window overlay, which the stage grab misses.
            const dir = root.arg("preview-shot", "")
            if (root.dialogMode && dir !== "") {
                sheetLoader.grabToImage(r => r.saveToFile(dir + "/DemoPhone_base.png"))
                {
                    const items = sheetLoader.Overlay.overlay.children.filter(c => c.visible && c.width > 0)
                    items.forEach((c, k) => c.grabToImage(r => r.saveToFile(dir + "/DemoPhone_dialog" + k + "_" + Math.round(c.x) + "_" + Math.round(c.y) + ".png")))
                }
            }
            else if (root.sheetMode && dir !== "") {
                sheetLoader.grabToImage(r => r.saveToFile(dir + "/DemoPhone_base.png"))
                const e = root.editor
                const sheet = root.arg("phone-sheet", "")
                const popup = sheet === "add" ? e.addMenuItem : sheet === "more" ? e.moreToolsItem : e.projectSheetItem
                popup.background.parent.grabToImage(r => r.saveToFile(dir + "/DemoPhone_overlay.png"))
            }
            root.settled = true
        }
    }

    Component {
        id: editorComp
        AndroidEditor { anchors.fill: parent }
    }

    Loader {
        id: mainLoader
        anchors.fill: parent
        active: root.screen[0] === "editor" && !root.sheetMode
        sourceComponent: editorComp
    }

    ApplicationWindow {
        id: sheetWin
        width: 393
        height: 800
        visible: root.sheetMode
        AndroidLayoutSheet { id: layoutDemo }
        AndroidShareTargetSheet { id: shareDemo }
        Loader {
            id: sheetLoader
            anchors.fill: parent
            active: root.sheetMode
            sourceComponent: editorComp
        }
    }

    Loader {
        anchors.fill: parent
        active: root.screen[0] === "home"
        sourceComponent: AndroidHome {
            currentTab: Number(root.screen[1] || 0)
        }
    }

    Loader {
        anchors.fill: parent
        active: root.screen[0] === "settings"
        sourceComponent: AndroidSubPage {
            title: qsTr("Settings")
            SettingsPane {
                id: pane
                anchors.fill: parent
                mobile: true
                Component.onCompleted: if (root.screen[1]) selectPage(root.screen[1])
            }
        }
    }

    Timer {
        running: root.screen[0] === "settings" || root.screen[0] === "extras"
        interval: 1500
        onTriggered: {
            if (root.arg("phone-dscroll", "") === "")
                return
            const f = root.findFlickAny(root)
            const v = root.arg("phone-dscroll", "")
            if (f) f.contentY = v === "end" ? f.contentHeight - f.height : Number(v)
        }
    }

    Loader {
        anchors.fill: parent
        active: root.screen[0] === "extras"
        sourceComponent: AndroidExtrasPage {}
    }
}
