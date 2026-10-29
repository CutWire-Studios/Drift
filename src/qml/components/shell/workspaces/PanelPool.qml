import QtQuick
import Drift

// Every panel wrapper lives here for the whole session. A layout rebuild only moves
// them between slots and this hidden holder. Preview is a plain instance (destroying it would
// take its video sink and GL renderer along); every other panel sits inside a LazyPanel that
// creates it on demand and destroys it a few seconds after it leaves the screen.
Item {
    id: root
    visible: false

    property var assetsPanel: null
    property var propertiesPanel: null
    property var previewPanel: null
    property var timelinePanel: null
    property var audioMixerPanel: null
    property var sourceViewerPanel: null
    property var denoisePanel: null
    property var colorPanel: null
    property var looksPanel: null
    property var scopesPanel: null
    property var keyframesPanel: null
    property var maskingPanel: null
    property var trackingPanel: null
    property var subtitlesPanel: null
    property var subtitleLanePanel: null
    property var subtitleTextPanel: null
    property var subtitleTimingPanel: null

    property var _created: ({})

    // Parked wrappers sit under the hidden holder, so parent is part of the test.
    function _shownOf(type) {
        const w = root._created[type]
        return !!w && w.visible && w.parent !== root
    }

    readonly property bool denoiseInLayout: !!root._created["denoise"] && root._created["denoise"].parent !== root

    readonly property bool audioMixerShown: root._shownOf("audioMixer")

    readonly property bool sourceViewerShown: root._shownOf("sourceViewer")

    readonly property bool sourceLabShown: root._shownOf("sourceLab.bin") || root._shownOf("sourceLab.viewer") || root._shownOf("sourceLab.trim")
        || root._shownOf("sourceLab.ranges") || root._shownOf("sourceLab.process") || root._shownOf("sourceLab.output")

    readonly property bool denoiseShown: root._shownOf("denoise")

    readonly property bool colorShown: root._shownOf("color")

    readonly property bool looksShown: root._shownOf("looks")

    readonly property bool scopesShown: root._shownOf("scopes")

    readonly property bool keyframesShown: root._shownOf("keyframes")

    readonly property bool maskingShown: root._shownOf("masking")

    readonly property bool trackingShown: root._shownOf("tracking")

    readonly property bool subtitlesShown: root._shownOf("subtitles")

    readonly property bool subtitleLaneShown: root._shownOf("subtitleLane")

    readonly property bool subtitleTextShown: root._shownOf("subtitleText")

    readonly property bool subtitleTimingShown: root._shownOf("subtitleTiming")

    // One line per panel type. A type missing from here gets a placeholder.
    // "assets.<tab>" and "properties.<tab>" are handled by prefix in panel(): the full panels
    // pinned to one tab.
    readonly property var factories: ({
        "assets": assetsC,
        "properties": propertiesC,
        "preview": previewC,
        "timeline": timelineC,
        "audioMixer": audioMixerC,
        "subtitles": subtitlesC,
        "subtitleLane": subtitleLaneC,
        "subtitleText": subtitleTextC,
        "subtitleTiming": subtitleTimingC,
        "recorder": recorderC,
        "denoise": denoiseC,
        "color": colorC,
        "looks": looksC,
        "scopes": scopesC,
        "keyframes": keyframesC,
        "masking": maskingC,
        "tracking": trackingC,
        "undoHistory": historyC,
        "sourceViewer": sourceViewerC,
        "sourceLab.bin": sourceLabBinC,
        "sourceLab.viewer": sourceLabViewerC,
        "sourceLab.trim": sourceLabTrimC,
        "sourceLab.ranges": sourceLabRangesC,
        "sourceLab.process": sourceLabProcessC,
        "sourceLab.output": sourceLabOutputC,
        "agentLog": agentLogC
    })

    Component { id: assetsC; LazyPanel { panel: AssetsPanel {} } }
    Component { id: propertiesC; LazyPanel { panel: PropertiesPanel {} } }
    Component { id: previewC; PreviewPanel {} }
    Component { id: timelineC; LazyPanel { panel: TimelinePanel {} } }
    Component { id: audioMixerC; LazyPanel { panel: AudioMixerPanel {} } }
    Component { id: subtitlesC; LazyPanel { panel: SubtitlesPanel {} } }
    Component { id: subtitleLaneC; LazyPanel { panel: SubtitleLanePanel {} } }
    Component { id: subtitleTextC; LazyPanel { panel: SubtitleTextPanel {} } }
    Component { id: subtitleTimingC; LazyPanel { panel: SubtitleTimingPanel {} } }
    Component { id: recorderC; LazyPanel { panel: RecorderPanel {} } }
    Component { id: denoiseC; LazyPanel { panel: DenoisePanel {} } }
    Component { id: colorC; LazyPanel { panel: ColorPanel {} } }
    Component { id: looksC; LazyPanel { panel: LooksPanel {} } }
    Component { id: scopesC; LazyPanel { panel: ScopesPanel {} } }
    Component { id: keyframesC; LazyPanel { panel: KeyframesPanel {} } }
    Component { id: maskingC; LazyPanel { panel: MaskingPanel {} } }
    Component { id: trackingC; LazyPanel { panel: TrackingPanel {} } }
    Component { id: historyC; LazyPanel { panel: HistoryPanel {} } }
    Component { id: sourceViewerC; LazyPanel { panel: SourceViewerPanel {} } }
    Component { id: sourceLabBinC; LazyPanel { panel: SourceLabBinPanel {} } }
    Component { id: sourceLabViewerC; LazyPanel { panel: SourceLabViewerPanel {} } }
    Component { id: sourceLabTrimC; LazyPanel { panel: SourceLabTrimPanel {} } }
    Component { id: sourceLabRangesC; LazyPanel { panel: SourceLabRangesPanel {} } }
    Component { id: sourceLabProcessC; LazyPanel { panel: SourceLabProcessPanel {} } }
    Component { id: sourceLabOutputC; LazyPanel { panel: SourceLabOutputPanel {} } }
    Component { id: agentLogC; LazyPanel { panel: AgentLogPanel {} } }
    Component { id: placeholderC; WorkspacePlaceholderPanel {} }

    // Stays in the pool like any panel, but the panel inside only exists while shown (plus a
    // grace period): a parked or background-tab panel keeps no bindings alive.
    component LazyPanel: Item {
        id: lazy

        property bool shown: false
        property Component panel: null
        property var props: ({})
        property string tab: ""
        property var setup: null
        property var _item: null
        readonly property var item: lazy._item

        function refresh() {
            const wanted = lazy.shown || grace.running
            if (wanted && !lazy._item) {
                const inner = lazy.panel.createObject(lazy, lazy.props)
                inner.anchors.fill = lazy
                if ("shown" in inner)
                    inner.shown = Qt.binding(() => lazy.shown)
                lazy.applyTab(inner)
                if (lazy.setup)
                    lazy.setup(inner)
                lazy._item = inner
            } else if (!wanted && lazy._item) {
                const old = lazy._item
                lazy._item = null
                old.destroy()
            }
        }

        function applyTab(inner) {
            if (!lazy.tab)
                return
            if (typeof inner.showTab === "function") {
                inner.showTab(lazy.tab)
            } else if (typeof inner.tabIndexOf === "function") {
                const index = inner.tabIndexOf(lazy.tab)
                if (index >= 0)
                    inner.activeTab = index
            }
        }

        onShownChanged: {
            if (lazy.shown)
                grace.stop()
            else
                grace.restart()
            lazy.refresh()
        }

        onTabChanged: {
            if (lazy._item)
                lazy.applyTab(lazy._item)
        }

        Timer {
            id: grace
            interval: 5000
            onTriggered: lazy.refresh()
        }
    }

    function inLayout(type) {
        return !!root._created[type] && root._created[type].parent !== root
    }

    // Brings a panel's tab to the front when it sits behind another one.
    function reveal(type) {
        if (!root.inLayout(type))
            return false
        root._created[type].parent?.parent?.activate?.(type)
        return true
    }

    function label(type) {
        const types = Workspaces.panelTypes
        for (let i = 0; i < types.length; ++i) {
            if (types[i].type === type)
                return types[i].fullLabel
        }
        return type
    }

    function create(type) {
        if (type.startsWith("assets."))
            return assetsC.createObject(root, { props: { lockedTab: type.slice(7) } })
        if (type.startsWith("properties.")) {
            const wrapper = propertiesC.createObject(root, { props: { lockedTab: type.slice(11) } })
            wrapper.setup = pane => {
                pane.browseEffectsRequested.connect(root.browseEffects)
                pane.browseAudioEffectsRequested.connect(root.browseAudioEffects)
            }
            return wrapper
        }
        const factory = root.factories[type]
        return factory ? factory.createObject(root)
                       : placeholderC.createObject(root, { label: root.label(type) })
    }

    function showAssetsTab(tab) {
        if (root.assetsPanel)
            root.assetsPanel.showTab(tab)
        else
            root.panel("assets").tab = tab
    }

    function browseEffects() { root.showAssetsTab("effects") }
    function browseAudioEffects() { root.showAssetsTab("sounds") }

    function panel(type) {
        if (root._created[type])
            return root._created[type]
        const w = root.create(type)
        root._created = Object.assign({}, root._created, { [type]: w })
        if ("setup" in w)
            w.shown = Qt.binding(() => root._shownOf(type))
        switch (type) {
        case "assets":
            root.assetsPanel = Qt.binding(() => w.item)
            break
        case "properties":
            root.propertiesPanel = Qt.binding(() => w.item)
            break
        case "preview":
            root.previewPanel = w
            w.previewFullscreen = Qt.binding(() => ShellBridge.main?.previewFullscreen ?? false)
            break
        case "timeline":
            root.timelinePanel = Qt.binding(() => w.item)
            w.setup = item => {
                item.keyframesTab = Qt.binding(() => (root.propertiesPanel?.keyframesTab ?? false) && !root.keyframesShown)
                item.externalMixerShown = Qt.binding(() => root.audioMixerShown)
            }
            break
        case "audioMixer":
            root.audioMixerPanel = Qt.binding(() => w.item)
            break
        case "sourceViewer":
            root.sourceViewerPanel = Qt.binding(() => w.item)
            break
        case "denoise":
            root.denoisePanel = Qt.binding(() => w.item)
            break
        case "color":
            root.colorPanel = Qt.binding(() => w.item)
            break
        case "looks":
            root.looksPanel = Qt.binding(() => w.item)
            break
        case "scopes":
            root.scopesPanel = Qt.binding(() => w.item)
            break
        case "keyframes":
            root.keyframesPanel = Qt.binding(() => w.item)
            break
        case "masking":
            root.maskingPanel = Qt.binding(() => w.item)
            break
        case "tracking":
            root.trackingPanel = Qt.binding(() => w.item)
            break
        case "subtitles":
            root.subtitlesPanel = Qt.binding(() => w.item)
            break
        case "subtitleLane":
            root.subtitleLanePanel = Qt.binding(() => w.item)
            break
        case "subtitleText":
            root.subtitleTextPanel = Qt.binding(() => w.item)
            break
        case "subtitleTiming":
            root.subtitleTimingPanel = Qt.binding(() => w.item)
            break
        }
        return w
    }

    // Turns on the timeline's cue lane (SubtitleCueLane) while any subtitle panel is on screen.
    Binding {
        target: EditorState
        property: "subtitleEditing"
        value: true
        when: root.subtitlesShown || root.subtitleLaneShown || root.subtitleTextShown || root.subtitleTimingShown
        restoreMode: Binding.RestoreBindingOrValue
    }

    function parkAll() {
        for (const type in root._created) {
            const item = root._created[type]
            item.anchors.fill = null
            item.parent = root
        }
    }

    Component.onCompleted: root.panel("preview")

    Connections {
        target: root.propertiesPanel
        // Empty-state browse CTAs jump the assets panel to the matching library tab.
        function onBrowseEffectsRequested() { root.browseEffects() }
        function onBrowseAudioEffectsRequested() { root.browseAudioEffects() }
    }

    Connections {
        target: root.previewPanel
        function onFullscreenRequested() { ShellBridge.main.togglePreviewFullscreen() }
    }
}
