pragma Singleton
import QtQuick
import Drift

// Design tokens for the app shell and panel surfaces, plus shared layout,
// typography and iconography constants. Color, metric, type and motion values
// are loaded from JSON by ThemeManager; the property names below are the ones
// the rest of the app already binds to.
//
// Control chrome (buttons, inputs, chips, dialogs) lives in
// src/qml/components/Themed*.qml — use those; do not re-skin Qt controls inline.
QtObject {
    id: theme

    // --- Platform keyboard shortcuts -----------------------------------------
    // Shortcut strings stored by EditorState use a portable/canonical notation:
    //
    //   Ctrl  = primary application accelerator
    //           Command on macOS, Control on Windows/Linux
    //
    // This keeps project/settings data portable while the UI and actual Shortcut
    // objects follow the conventions of the operating system.
    readonly property bool isMacOS: Qt.platform.os === "osx"
    readonly property bool isWindows: Qt.platform.os === "windows"
    readonly property bool isLinux: Qt.platform.os === "linux"

    function nativeShortcutSequence(sequence) {
        // Qt already performs the native Apple modifier mapping:
        //
        //   Ctrl -> Command (⌘)
        //   Meta -> physical Control (⌃)
        //   Alt  -> Option (⌥)
        //
        // Therefore the canonical shortcut string must be passed through
        // unchanged. Converting Ctrl to Meta here would incorrectly turn
        // Command shortcuts into physical Control shortcuts on macOS.
        return sequence
    }

    // Arrow chords are dispatched from the editor FocusScope after the focused
    // item, not as ApplicationShortcut — that matcher misses Left/Right on some
    // Wayland compositors, and would steal the keys from text fields if it did fire.
    function shortcutSequenceUsesArrowKey(sequence) {
        if (!sequence || sequence.length === 0)
            return false
        const parts = sequence.split("+")
        const key = parts[parts.length - 1]
        return key === "Left" || key === "Right" || key === "Up" || key === "Down"
    }

    function shortcutDisplay(sequence) {
        if (!sequence || sequence.length === 0)
            return ""

        if (!isMacOS)
            return sequence

        const parts = sequence.split("+")

        let control = false
        let option = false
        let shift = false
        let command = false
        let key = ""

        for (let i = 0; i < parts.length; ++i) {
            const part = parts[i]

            // Qt's native Apple mapping:
            // Ctrl = Command, Meta = physical Control.
            if (part === "Ctrl")
                command = true
            else if (part === "Meta")
                control = true
            else if (part === "Alt")
                option = true
            else if (part === "Shift")
                shift = true
            else
                key = part
        }

        const keySymbols = {
            "Left": "←",
            "Right": "→",
            "Up": "↑",
            "Down": "↓",
            "Backspace": "⌫",
            "Delete": "⌦",
            "Escape": "⎋",
            "Return": "↩",
            "Enter": "⌅"
        }

        if (keySymbols[key] !== undefined)
            key = keySymbols[key]

        // Ordem visual tradicional da Apple.
        return (control ? "⌃" : "")
             + (option ? "⌥" : "")
             + (shift ? "⇧" : "")
             + (command ? "⌘" : "")
             + key
    }

    function platformShortcutText(text) {
        if (!text || !isMacOS)
            return text

        // Used for prose/tooltips such as "Ctrl+scroll" and
        // "Shift for 5s", preserving the translated sentence itself.
        return text
            .replace(/Ctrl/g, "⌘")
            .replace(/Alt/g, "⌥")
            .replace(/Shift/g, "⇧")
            .replace(/Meta/g, "⌘")
    }

    function primaryModifierPressed(modifiers) {
        // On macOS Qt maps Command to ControlModifier by default.
        // On Windows/Linux this is the physical Control key.
        return (modifiers & Qt.ControlModifier) !== 0
    }


    // One loader per file: the UI ships static Inter instances under the family "Inter UI",
    // so the Essential Fonts addon's "Inter" cannot shadow them whichever registers last.
    property FontLoader _interRegular: FontLoader { source: "qrc:/qt/qml/Drift/resources/fonts/InterUI-Regular.ttf" }
    property FontLoader _interMedium: FontLoader { source: "qrc:/qt/qml/Drift/resources/fonts/InterUI-Medium.ttf" }
    property FontLoader _interSemiBold: FontLoader { source: "qrc:/qt/qml/Drift/resources/fonts/InterUI-SemiBold.ttf" }
    property FontLoader _interBold: FontLoader { source: "qrc:/qt/qml/Drift/resources/fonts/InterUI-Bold.ttf" }
    property FontLoader _interItalic: FontLoader { source: "qrc:/qt/qml/Drift/resources/fonts/InterUI-Italic.ttf" }

    readonly property string fontFamily: _interRegular.name || "sans-serif"
    readonly property string monoFontFamily: ThemeManager.typography.monoFontFamily

    // --- Light/dark mode: follows the OS until the user picks a side -----------
    // Qt.styleHints.colorScheme is live-updated by the platform theme (Qt 6.5+).
    // Once toggled, the choice lives in QSettings via EditorState.preferences and survives
    // restarts; it is app-wide, not stored per project.
    //
    // A single-variant user theme reports its own variant here. That binding reads
    // ThemeManager.effectiveVariant and must not be written back by setVariant, or the
    // two sides chase each other. The toggle records the requested side and selects
    // the system theme, which is what actually loads a built-in.
    readonly property bool systemPrefersDark: Qt.styleHints.colorScheme !== Qt.Light
    // Single-variant user themes report their own side. A pinned built-in does
    // too: setVariant will not unpin it, and the header checkmarks read darkMode.
    // Selection "system" follows the appearance preference, otherwise the OS.
    // setVariant at startup must not be what loads a built-in over DRIFT_THEME;
    // ThemeManager keeps that override until select().
    readonly property bool darkMode: {
        if (ThemeManager.appearanceFollowsTheme)
            return ThemeManager.effectiveVariant === "dark"
        if (ThemeManager.selection === "dark" || ThemeManager.selection === "light")
            return ThemeManager.selection === "dark"
        return EditorState.preferences.darkModeOverridden
               ? EditorState.preferences.darkModePreferred
               : systemPrefersDark
    }

    function toggleDarkMode() {
        setDarkMode(!darkMode)
    }

    function setDarkMode(enabled) {
        const side = enabled ? "dark" : "light"
        EditorState.preferences.setDarkModePreference(enabled)
        if (ThemeManager.appearanceFollowsTheme) {
            // setVariant only stores the side while a single-variant theme is up.
            ThemeManager.setVariant(side)
            ThemeManager.select("system")
            return
        }
        if (ThemeManager.selection === "dark" || ThemeManager.selection === "light")
            ThemeManager.select(side)
    }

    function syncVariant() {
        ThemeManager.setVariant(darkMode ? "dark" : "light")
    }

    // Startup stays synchronous so the matching built-in is on screen before the
    // first frame. A deferred swap would paint the constructor's dark theme and
    // flash light sessions. Later changes wait until darkMode has finished
    // evaluating: setVariant loads a theme, and doing that mid-evaluation is a
    // binding loop.
    Component.onCompleted: syncVariant()
    onDarkModeChanged: Qt.callLater(syncVariant)

    // Per-component tokens. Each group is lowerCamel of the QML type name.
    readonly property var c: ThemeManager.components
    readonly property var tokens: ThemeManager

    // Shell palettes for both variants, not just the one currently applied.
    readonly property var _dark: ({
        appBackground: ThemeManager.builtinToken("dark", "semantic.surface.app"),
        foreground: ThemeManager.builtinToken("dark", "semantic.foreground"),
        border: ThemeManager.builtinToken("dark", "semantic.border"),
        accent: ThemeManager.builtinToken("dark", "semantic.accent"),
        accentForeground: ThemeManager.builtinToken("dark", "semantic.accentForeground"),
        mutedForeground: ThemeManager.builtinToken("dark", "semantic.mutedForeground"),
        popoverHover: ThemeManager.builtinToken("dark", "semantic.popoverHover"),
        panelBackground: ThemeManager.builtinToken("dark", "semantic.surface.panel"),
        panelForeground: ThemeManager.builtinToken("dark", "semantic.panel.foreground"),
        panelBorder: ThemeManager.builtinToken("dark", "semantic.panel.border"),
        panelAccent: ThemeManager.builtinToken("dark", "semantic.panel.accent"),
        panelAccentForeground: ThemeManager.builtinToken("dark", "semantic.panel.accentForeground"),
        panelMuted: ThemeManager.builtinToken("dark", "semantic.panel.muted"),
        panelSecondaryBg: ThemeManager.builtinToken("dark", "semantic.panel.secondary.background"),
        panelSecondaryBorder: ThemeManager.builtinToken("dark", "semantic.panel.secondary.border"),
        panelSecondaryForeground: ThemeManager.builtinToken("dark", "semantic.panel.secondary.foreground")
    })
    readonly property var _light: ({
        appBackground: ThemeManager.builtinToken("light", "semantic.surface.app"),
        foreground: ThemeManager.builtinToken("light", "semantic.foreground"),
        border: ThemeManager.builtinToken("light", "semantic.border"),
        accent: ThemeManager.builtinToken("light", "semantic.accent"),
        accentForeground: ThemeManager.builtinToken("light", "semantic.accentForeground"),
        mutedForeground: ThemeManager.builtinToken("light", "semantic.mutedForeground"),
        popoverHover: ThemeManager.builtinToken("light", "semantic.popoverHover"),
        panelBackground: ThemeManager.builtinToken("light", "semantic.surface.panel"),
        panelForeground: ThemeManager.builtinToken("light", "semantic.panel.foreground"),
        panelBorder: ThemeManager.builtinToken("light", "semantic.panel.border"),
        panelAccent: ThemeManager.builtinToken("light", "semantic.panel.accent"),
        panelAccentForeground: ThemeManager.builtinToken("light", "semantic.panel.accentForeground"),
        panelMuted: ThemeManager.builtinToken("light", "semantic.panel.muted"),
        panelSecondaryBg: ThemeManager.builtinToken("light", "semantic.panel.secondary.background"),
        panelSecondaryBorder: ThemeManager.builtinToken("light", "semantic.panel.secondary.border"),
        panelSecondaryForeground: ThemeManager.builtinToken("light", "semantic.panel.secondary.foreground")
    })
    readonly property var _palette: darkMode ? _dark : _light

    // --- Colors: app shell ---------------------------------------------------
    readonly property color appBackground: ThemeManager.semantic.surface.app
    readonly property color foreground: ThemeManager.semantic.foreground
    readonly property color border: ThemeManager.semantic.border
    readonly property color accent: ThemeManager.semantic.accent
    readonly property color accentForeground: ThemeManager.semantic.accentForeground
    readonly property color mutedForeground: ThemeManager.semantic.mutedForeground
    readonly property color popoverHover: ThemeManager.semantic.popoverHover

    // --- Colors: panel surfaces ------------------------------------------------
    readonly property color panelBackground: ThemeManager.semantic.surface.panel
    readonly property color panelForeground: ThemeManager.semantic.panel.foreground
    readonly property color panelBorder: ThemeManager.semantic.panel.border
    readonly property color panelAccent: ThemeManager.semantic.panel.accent
    readonly property color panelAccentForeground: ThemeManager.semantic.panel.accentForeground
    readonly property color panelMuted: ThemeManager.semantic.panel.muted
    // Slider groove — lighter than panelMuted in dark mode so the track reads at rest.
    readonly property color sliderTrack: ThemeManager.semantic.panel.sliderTrack
    // Scrollbar track + handle (timeline horizontal bar, panel flickables).
    readonly property color scrollbarTrack: ThemeManager.semantic.panel.scrollbar.track
    readonly property color scrollbarHandle: ThemeManager.semantic.panel.scrollbar.handle
    readonly property color scrollbarHandleHover: ThemeManager.semantic.panel.scrollbar.handleHover
    readonly property color scrollbarHandlePressed: ThemeManager.semantic.panel.scrollbar.handlePressed
    // Bottom-sheet drag pill. panelBorder sat under the 3:1 non-text floor in both themes.
    readonly property color sheetHandle: ThemeManager.semantic.panel.sheetHandle
    readonly property color panelSecondaryBg: ThemeManager.semantic.panel.secondary.background
    readonly property color panelSecondaryBorder: ThemeManager.semantic.panel.secondary.border
    readonly property color panelSecondaryForeground: ThemeManager.semantic.panel.secondary.foreground

    // --- Colors: shared semantic ------------------------------------------------
    readonly property color primary: ThemeManager.semantic.primary
    readonly property color primaryForeground: ThemeManager.semantic.primaryForeground
    // `primary` as a foreground on a panel. The brand amber fails contrast on the
    // light panel; dark mode keeps the amber. Fills still use `primary`.
    readonly property color accentOnPanel: ThemeManager.semantic.accentOnPanel
    readonly property color destructive: ThemeManager.semantic.destructive
    readonly property color constructive: ThemeManager.semantic.constructive
    readonly property color warning: ThemeManager.semantic.warning
    readonly property color focusRing: ThemeManager.semantic.focusRing

    readonly property color exportGradientTop: ThemeManager.semantic.export.gradientTop
    readonly property color exportGradientBottom: ThemeManager.semantic.export.gradientBottom
    readonly property color exportGlow: ThemeManager.semantic.export.glow

    // Scrims sit on photographic content, so they do not follow the app theme.
    readonly property color scrimColor: ThemeManager.semantic.scrim
    readonly property color scrimStrong: ThemeManager.semantic.scrimStrong
    readonly property color overlayColor: ThemeManager.semantic.overlay
    readonly property color guideStrong: ThemeManager.semantic.guide.strong
    readonly property color guideMedium: ThemeManager.semantic.guide.medium
    readonly property color guideWeak: ThemeManager.semantic.guide.weak
    readonly property color onMedia: ThemeManager.semantic.onMedia
    readonly property color snapGuide: ThemeManager.semantic.timeline.snapGuide
    readonly property color skeletonColor: ThemeManager.semantic.skeleton
    readonly property color skeletonHighlight: ThemeManager.semantic.skeletonHighlight

    // --- Colors: timeline clip types -------------------------------------------
    readonly property color clipText: ThemeManager.semantic.clip.text
    readonly property color clipSubtitle: ThemeManager.semantic.clip.subtitle
    readonly property color clipAudio: ThemeManager.semantic.clip.audio
    readonly property color clipGraphic: ThemeManager.semantic.clip.graphic
    readonly property color clipEffect: ThemeManager.semantic.clip.effect
    readonly property color clipComposite: ThemeManager.semantic.clip.composite
    readonly property color clipProxy: ThemeManager.semantic.clip.proxy
    readonly property color clipProxyForeground: ThemeManager.semantic.clip.proxyForeground
    readonly property color clipProxyBand: ThemeManager.semantic.clip.proxyBand
    readonly property color clipEditFriendly: ThemeManager.semantic.clip.editFriendly
    readonly property color clipEditFriendlyForeground: ThemeManager.semantic.clip.editFriendlyForeground
    readonly property color clipAdjustmentVideo: ThemeManager.semantic.clip.adjustment.video
    readonly property color clipAdjustmentAudio: ThemeManager.semantic.clip.adjustment.audio
    readonly property color clipAdjustmentMask: ThemeManager.semantic.clip.adjustment.mask
    readonly property color clipTransform: ThemeManager.semantic.clip.transform
    readonly property color clipVideoOverview: ThemeManager.semantic.clip.videoOverview
    readonly property color transitionOverlap: ThemeManager.semantic.clip.transitionOverlap
    readonly property color waveformColor: ThemeManager.semantic.clip.waveform
    readonly property color panelWaveformColor: ThemeManager.semantic.clip.panelWaveform
    readonly property color beatBarColor: ThemeManager.semantic.clip.beat.bar
    readonly property color beatGridColor: ThemeManager.semantic.clip.beat.grid
    readonly property color beatOnsetColor: ThemeManager.semantic.clip.beat.onset
    readonly property color clipVideoPlaceholder: ThemeManager.semantic.clip.videoPlaceholder
    // Fixed dark in both themes: a white surround in light mode left the video floating.
    readonly property color previewLetterbox: ThemeManager.semantic.preview.letterbox
    readonly property color textStylePreviewBg: ThemeManager.semantic.preview.textStyleBackground
    readonly property color textStylePreviewBorder: ThemeManager.semantic.preview.textStyleBorder

    // --- Colors: 3D transform gizmo --------------------------------------------
    readonly property color gizmoX: ThemeManager.semantic.gizmo.x
    readonly property color gizmoY: ThemeManager.semantic.gizmo.y
    readonly property color gizmoZ: ThemeManager.semantic.gizmo.z
    readonly property color gizmoUniform: ThemeManager.semantic.gizmo.uniform
    readonly property color gizmoHot: ThemeManager.semantic.gizmo.hot

    // A token group as a plain object, for C++ items whose style property is a QVariantMap.
    function plainMap(group) {
        const out = {}
        for (const key of group.keys())
            out[key] = group[key]
        return out
    }

    // --- Colors: keyframe curves ------------------------------------------------
    readonly property var keyframeCurveColors: ThemeManager.semantic.keyframe.curves
    readonly property var keyframeCurveRamp: ThemeManager.semantic.keyframe.ramp
    function keyframeCurveColor(prop) {
        if (!prop)
            return keyframeCurveRamp[0]
        if (keyframeCurveColors[prop])
            return keyframeCurveColors[prop]
        let hash = 0
        for (let i = 0; i < prop.length; ++i)
            hash = (hash * 31 + prop.charCodeAt(i)) & 0x7fffffff
        return keyframeCurveRamp[hash % keyframeCurveRamp.length]
    }

    // --- Radius --------------------------------------------------------------
    readonly property real radiusSm: ThemeManager.metrics.radius.sm
    readonly property real radiusMd: ThemeManager.metrics.radius.md
    readonly property real radiusLg: ThemeManager.metrics.radius.lg
    readonly property real radiusXs: ThemeManager.metrics.radius.xs
    readonly property real radiusPill: ThemeManager.metrics.radius.pill

    // --- Spacing scale ---------------------------------------------------------
    readonly property real spacingXs: ThemeManager.metrics.spacing.xs
    readonly property real spacingSm: ThemeManager.metrics.spacing.sm
    readonly property real spacingMd: ThemeManager.metrics.spacing.md
    readonly property real spacingLg: ThemeManager.metrics.spacing.lg
    readonly property real spacingXl: ThemeManager.metrics.spacing.xl
    readonly property real spacing2xl: ThemeManager.metrics.spacing.xxl
    readonly property real spacing3xl: ThemeManager.metrics.spacing.xxxl

    // --- Control metrics -------------------------------------------------------
    // controlHeight is the alignment baseline: text fields, combo boxes and text
    // buttons all share it so adjacent controls in a Row line up.
    //
    // Two independent axes, deliberately not one boolean.
    //
    //   sizeClass  — how much room there is. Drives which shell runs and whether a layout
    //                collapses to a single pane. A narrow window on a desktop is "compact".
    //   touchInput — what is pointing at it. Drives hit-target growth. A touchscreen laptop
    //                at 1400dp is touch but not compact; a 500dp mouse-driven window is the
    //                reverse.
    //
    // liftDragRequired is derived rather than a third axis: a platform DragHandler cannot
    // leave a bottom sheet, and the sheet is a consequence of being compact, not of being
    // touched — so a mouse user in the compact shell needs the lift gesture too.
    property real windowWidth: 0                        // pushed by the root window
    property real keyboardInset: 0                      // soft-keyboard overlap with the window bottom, pushed by the root window
    property string sizeClassOverride: ""               // "compact" | "medium" | "expanded"
    property string inputModeOverride: ""               // "touch" | "pointer"

    readonly property string sizeClass: sizeClassOverride.length > 0 ? sizeClassOverride
                                      : windowWidth <= 0 ? (Qt.platform.os === "android" ? "compact" : "expanded")
                                      : windowWidth < 600 ? "compact"
                                      : windowWidth < 840 ? "medium"
                                      : "expanded"
    readonly property bool compact: sizeClass === "compact"

    readonly property bool pointerPrimary: inputModeOverride.length > 0
                                         ? inputModeOverride === "pointer"
                                         : !(Qt.platform.os === "android" || Qt.platform.os === "ios")
    readonly property bool touchInput: !pointerPrimary
    readonly property bool liftDragRequired: touchInput || compact

    // Retained as an alias so existing call sites keep compiling.
    readonly property bool touchUi: touchInput

    // Reads touchUi, so the binding follows both the theme document and the input mode.
    function inputMetric(metric) {
        return touchUi ? metric.touch : metric.pointer
    }

    readonly property real controlHeight: inputMetric(ThemeManager.metrics.controlHeight)
    readonly property real controlHeightSm: inputMetric(ThemeManager.metrics.controlHeightSm)
    readonly property real iconButtonSize: inputMetric(ThemeManager.metrics.iconButtonSize)
    readonly property real borderWidth: ThemeManager.metrics.borderWidth
    readonly property real borderWidthFocus: ThemeManager.metrics.borderWidthFocus

    // --- Icon sizes ------------------------------------------------------------
    readonly property real iconSizeSm: ThemeManager.metrics.iconSize.sm
    readonly property real iconSizeMd: ThemeManager.metrics.iconSize.md
    readonly property real iconSizeBase: ThemeManager.metrics.iconSize.base
    readonly property real iconSizeLg: ThemeManager.metrics.iconSize.lg
    readonly property real iconSizeXl: ThemeManager.metrics.iconSize.xl

    // --- Motion ----------------------------------------------------------------
    function easingNamed(name) {
        switch (name) {
        case "Linear": return Easing.Linear
        case "InQuad": return Easing.InQuad
        case "OutQuad": return Easing.OutQuad
        case "InOutQuad": return Easing.InOutQuad
        case "InCubic": return Easing.InCubic
        case "OutCubic": return Easing.OutCubic
        case "InOutCubic": return Easing.InOutCubic
        case "InQuart": return Easing.InQuart
        case "OutQuart": return Easing.OutQuart
        case "InOutQuart": return Easing.InOutQuart
        case "InQuint": return Easing.InQuint
        case "OutQuint": return Easing.OutQuint
        case "InOutQuint": return Easing.InOutQuint
        case "OutExpo": return Easing.OutExpo
        case "InOutExpo": return Easing.InOutExpo
        default: return Easing.OutQuint
        }
    }

    readonly property int durationFast: ThemeManager.motion.durationFast
    readonly property int durationBase: ThemeManager.motion.durationBase
    readonly property int durationSlow: ThemeManager.motion.durationSlow
    readonly property int durationPress: ThemeManager.motion.durationPress
    readonly property int easing: easingNamed(ThemeManager.motion.easing)
    readonly property int easingInOut: easingNamed(ThemeManager.motion.easingInOut)
    readonly property real pressScale: ThemeManager.motion.pressScale
    readonly property int tooltipDelay: ThemeManager.motion.tooltipDelay

    // --- Dialog widths ---------------------------------------------------------
    readonly property real dialogWidthSm: ThemeManager.metrics.dialog.widthSm
    readonly property real dialogWidthMd: ThemeManager.metrics.dialog.widthMd
    readonly property real dialogWidthLg: ThemeManager.metrics.dialog.widthLg
    readonly property real dialogMargin: ThemeManager.metrics.dialog.margin

    // --- Typography ------------------------------------------------------------
    readonly property real fontSizeXs: ThemeManager.typography.fontSize.xs
    readonly property real fontSizeSm: ThemeManager.typography.fontSize.sm
    readonly property real fontSizeBase: ThemeManager.typography.fontSize.base
    readonly property real fontSizeTiny: ThemeManager.typography.fontSize.tiny
    readonly property real fontSizeTick: ThemeManager.typography.fontSize.tick
    readonly property real fontSizeCard: ThemeManager.typography.fontSize.card

    // --- Layout: chrome ------------------------------------------------------
    readonly property real headerHeight: ThemeManager.metrics.headerHeight
    readonly property real panelGap: ThemeManager.metrics.panelGap
    readonly property real pagePadding: ThemeManager.metrics.pagePadding

    // --- Layout: assets panel -----------------------------------------------
    readonly property real panelHeaderHeight: inputMetric(ThemeManager.metrics.panelHeaderHeight)
    readonly property real tabRailWidth: inputMetric(ThemeManager.metrics.tabRailWidth)
    readonly property real assetCardWidth: ThemeManager.metrics.assetCardWidth
    readonly property real assetCardGap: ThemeManager.metrics.assetCardGap

    readonly property var categoryColors: ThemeManager.semantic.category
    function categoryColor(index) {
        const colors = categoryColors
        if (!colors || colors.length === 0)
            return primary
        return colors[Math.abs(index) % colors.length]
    }

    // --- Layout: preview panel -----------------------------------------------
    readonly property real previewTransportPadding: ThemeManager.metrics.previewTransportPadding

    // --- Layout: timeline ------------------------------------------------------
    readonly property real timelineToolbarHeight: ThemeManager.metrics.timeline.toolbarHeight
    readonly property real timelineOverviewHeight: ThemeManager.metrics.timeline.overviewHeight
    readonly property real timelineRulerHeight: ThemeManager.metrics.timeline.rulerHeight
    readonly property real timelineBookmarkRowHeight: ThemeManager.metrics.timeline.bookmarkRowHeight
    readonly property real trackHeightVideo: ThemeManager.metrics.timeline.trackHeightVideo
    readonly property real trackHeightAudio: ThemeManager.metrics.timeline.trackHeightAudio
    readonly property real trackHeightText: inputMetric(ThemeManager.metrics.timeline.trackHeightText)
    readonly property real trackHeightSubtitle: inputMetric(ThemeManager.metrics.timeline.trackHeightSubtitle)
    readonly property real trackHeightShape: ThemeManager.metrics.timeline.trackHeightShape
    readonly property real trackHeightAdjustment: inputMetric(ThemeManager.metrics.timeline.trackHeightAdjustment)
    readonly property real adjustmentLaneHeight: inputMetric(ThemeManager.metrics.timeline.adjustmentLaneHeight)
    readonly property real trackGap: ThemeManager.metrics.timeline.trackGap
    readonly property real newTrackHitSlop: ThemeManager.metrics.timeline.newTrackHitSlop
    readonly property real trackLabelsWidth: ThemeManager.metrics.timeline.trackLabelsWidth
    readonly property real pixelsPerSecondBase: ThemeManager.metrics.timeline.pixelsPerSecondBase
    readonly property real timelineEndPadFraction: ThemeManager.metrics.timeline.endPadFraction
    readonly property real timelineEndPadMinPx: ThemeManager.metrics.timeline.endPadMinPx
    readonly property real playheadLineWidth: ThemeManager.metrics.timeline.playheadLineWidth
    readonly property real playheadHandleSize: ThemeManager.metrics.timeline.playheadHandleSize
    readonly property real playheadSeekGrabWidth: ThemeManager.metrics.timeline.playheadSeekGrabWidth
    readonly property real clipSelectionRingWidth: ThemeManager.metrics.timeline.clipSelectionRingWidth
    readonly property real clipHeaderBandHeight: ThemeManager.metrics.timeline.clipHeaderBandHeight
    readonly property real clipTrimHandleWidth: ThemeManager.metrics.timeline.clipTrimHandleWidth
    readonly property real clipMinInteractiveWidth: ThemeManager.metrics.timeline.clipMinInteractiveWidth
    readonly property real clipMinWidth: clipMinInteractiveWidth * 2
    readonly property real clipMinDurationSeconds: ThemeManager.metrics.timeline.clipMinDurationSeconds

    // --- Layout: Android / touch -----------------------------------------------
    readonly property real androidIconButtonSize: ThemeManager.metrics.android.iconButtonSize
    readonly property real androidTimelineToolbarHeight: ThemeManager.metrics.android.timelineToolbarHeight
    readonly property real androidPreviewTransportHeight: ThemeManager.metrics.android.previewTransportHeight
    readonly property real androidTopBarHeight: ThemeManager.metrics.android.topBarHeight
    readonly property real androidBottomRailHeight: ThemeManager.metrics.android.bottomRailHeight
    readonly property real androidClipToolbarHeight: ThemeManager.metrics.android.clipToolbarHeight
    readonly property real androidSplitterHeight: ThemeManager.metrics.android.splitterHeight
    readonly property real androidSheetHeightFraction: ThemeManager.metrics.android.sheetHeightFraction
    readonly property real androidSheetExpandedFraction: ThemeManager.metrics.android.sheetExpandedFraction
    readonly property real androidSheetHeaderHeight: ThemeManager.metrics.android.sheetHeaderHeight
    readonly property real androidSheetDismissFraction: ThemeManager.metrics.android.sheetDismissFraction
    readonly property int touchLiftInterval: ThemeManager.motion.touchLiftInterval
    readonly property real touchLiftStepAsideDistance: ThemeManager.metrics.android.liftStepAsideDistance
    readonly property real androidRailFabSize: ThemeManager.metrics.android.railFabSize
    readonly property real androidAddRowHeight: ThemeManager.metrics.android.addRowHeight
    readonly property real androidTrackLabelsWidth: ThemeManager.metrics.android.trackLabelsWidth
    readonly property real androidPlayheadCentreWidth: ThemeManager.metrics.android.playheadCentreWidth
    readonly property real androidClipTrimHandleWidth: ThemeManager.metrics.android.clipTrimHandleWidth
    readonly property real androidClipEdgeMargin: ThemeManager.metrics.android.clipEdgeMargin
    readonly property real androidTrimHotspotExtra: ThemeManager.metrics.android.trimHotspotExtra
    readonly property real androidPreviewMaxScreenFraction: ThemeManager.metrics.android.previewMaxScreenFraction
    readonly property real androidPreviewMinFraction: ThemeManager.metrics.android.previewMinFraction
    readonly property real androidPagePadding: ThemeManager.metrics.android.pagePadding
    readonly property real androidMinTouchTarget: ThemeManager.metrics.android.minTouchTarget
    readonly property real androidTouchGap: ThemeManager.metrics.android.touchGap
    readonly property real androidLayoutCardWidth: ThemeManager.metrics.android.layoutCardWidth
    readonly property real androidLayoutCardHeight: ThemeManager.metrics.android.layoutCardHeight
    readonly property real androidHomeTileHeight: ThemeManager.metrics.android.homeTileHeight
    readonly property real androidHomeRecentCardWidth: ThemeManager.metrics.android.homeRecentCardWidth
    readonly property real androidHomeRecentCardHeight: ThemeManager.metrics.android.homeRecentCardHeight

    // --- Layout: window --------------------------------------------------------
    readonly property real windowMinimumWidth: ThemeManager.metrics.windowMinimumWidth
    readonly property real windowMinimumHeight: ThemeManager.metrics.windowMinimumHeight

    // --- Iconography (Lucide SVGs in resources/icons/; ISC-licensed, see
    // resources/licenses/LICENSE-lucide.txt) ------------------------------------
    // Values are Lucide icon file names (without .svg).
    readonly property var icons: ({
        scissors: "scissors",
        chevronsLeft: "chevrons-left",
        chevronsRightLeft: "chevrons-right-left",
        undo: "undo",
        redo: "redo",
        clipboardPaste: "clipboard-paste",
        copyPlus: "copy-plus",
        copy: "copy",
        trash: "trash-2",
        pipette: "pipette",
        snowflake: "snowflake",
        bookmark: "bookmark",
        repeat: "repeat",
        star: "star",
        layers: "layers",
        box: "box",
        split: "split",
        magnet: "magnet",
        link: "link",
        linkTwo: "link-2",
        unlink: "unlink-2",
        foldHorizontal: "fold-horizontal",
        zoomOut: "zoom-out",
        zoomIn: "zoom-in",
        zoomFit: "chevrons-left-right-ellipsis",
        gauge: "gauge",
        filePlay: "file-play",
        rabbit: "rabbit",
        play: "play",
        pause: "pause",
        stepBack: "step-back",
        stepForward: "step-forward",
        rewind: "rewind",
        fastForward: "fast-forward",
        skipBack: "skip-back",
        skipForward: "skip-forward",
        maximize: "maximize",
        group: "group",
        locateFixed: "locate-fixed",
        minimize: "minimize",
        folder: "folder",
        folderInput: "folder-input",
        folderOutput: "folder-output",
        headphones: "headphones",
        type: "type",
        smile: "smile",
        wand: "wand-sparkles",
        sparkles: "sparkles",
        sliders: "sliders-horizontal",
        slidersVertical: "sliders-vertical",
        settings: "settings",
        upload: "upload",
        plus: "plus",
        volumeHigh: "volume-2",
        volumeOff: "volume-off",
        eye: "eye",
        eyeOff: "eye-off",
        film: "film",
        panelBottomDashed: "panel-bottom-dashed",
        panelTop: "panel-top",
        video: "video",
        music: "music",
        audioLines: "audio-lines",
        mic: "mic",
        micOff: "mic-off",
        micVocal: "mic-vocal",
        image: "image",
        shapes: "shapes",
        chevronDown: "chevron-down",
        chevronUp: "chevron-up",
        chevronsRight: "chevrons-right",
        x: "x",
        messageSquare: "message-square",
        moon: "moon",
        sun: "sun",
        grid: "grid-3x3",
        // The media bin's grid toggle; grid-3x3 above still marks the layout
        // pickers and the preview overlay's guide grid.
        layoutGrid: "layout-grid",
        list: "list",
        listTree: "list-tree",
        sortByName: "arrow-down-a-z",
        sortByKind: "tags",
        gripVertical: "grip-vertical",
        ellipsis: "ellipsis",
        save: "save",
        setStart: "arrow-left-to-line",
        setEnd: "arrow-right-to-line",
        // Timeline trim tools — vertical align marks read as “keep from here”.
        trimStart: "align-start-vertical",
        trimEnd: "align-end-vertical",
        blend: "blend",
        option: "option",
        keyboard: "keyboard",
        crop: "crop",
        diamondPlus: "diamond-plus",
        diamondMinus: "diamond-minus",
        mask: "square-dashed",
        puzzle: "puzzle",
        bug: "bug",
        bot: "bot",
        languages: "languages",
        shuffle: "shuffle",
        info: "info",
        package: "package",
        store: "store",
        gem: "gem",
        fileText: "file-text",

        // Status / feedback
        warning: "triangle-alert",
        success: "circle-check",
        error: "circle-x",
        spinner: "loader-circle",
        refresh: "refresh-cw",
        download: "download",
        bell: "bell",

        // Affordances
        reset: "rotate-ccw",
        search: "search",
        chevronRight: "chevron-right",
        chevronLeft: "chevron-left",
        check: "check",
        pencil: "pencil",
        clock: "clock",
        lock: "lock",
        lockOpen: "lock-open",
        moveHorizontal: "move-horizontal",
        move3d: "move-3d",
        rotate3d: "rotate-3d",
        scale3d: "scale-3d",
        fly: "helicopter",
        // CapCut-style select/pointer tool (exit cut modes)
        mousePointer: "mouse-pointer",

        // Text alignment
        alignLeft: "text-align-start",
        alignCenter: "text-align-center",
        alignRight: "text-align-end",
        // Lucide names these *-horizontal, but they are the correct valign glyphs.
        alignTop: "align-start-horizontal",
        alignMiddle: "align-center-horizontal",
        alignBottom: "align-end-horizontal",

        // Media
        captions: "captions",
        listVideo: "list-video",
        // Lane height on the phone edit strip: list + chevrons collapsing /
        // expanding, not foldVertical (those keys were never registered).
        listChevronsDownUp: "list-chevrons-down-up",
        listChevronsUpDown: "list-chevrons-up-down",
        smartphone: "smartphone",
        monitor: "monitor",
        square: "square",
        ratio: "ratio",

        // Brand marks (layout chooser)
        brandYoutube: "brand-youtube",
        brandInstagram: "brand-instagram",
        brandFacebook: "brand-facebook",
        brandTiktok: "brand-tiktok",
        brandSnapchat: "brand-snapchat",
        brandX: "brand-x",
        brandLinkedin: "brand-linkedin"
    })
}
