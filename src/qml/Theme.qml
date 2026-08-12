pragma Singleton
import QtQuick
import Drift

// Design tokens for the app shell and panel surfaces (dark + light), plus
// shared layout/typography/iconography constants used across the UI.
//
// Control chrome (buttons, inputs, chips, dialogs) lives in
// src/qml/components/Themed*.qml — use those; do not re-skin Qt controls inline.
QtObject {
    id: theme

    property FontLoader _interLoader: FontLoader { source: "qrc:/qt/qml/Drift/resources/fonts/Inter.ttf" }

    readonly property string fontFamily: _interLoader.name || "sans-serif"
    readonly property string monoFontFamily: "monospace"

    // --- Light/dark mode: follows the OS until the user picks a side -----------
    // Qt.styleHints.colorScheme is live-updated by the platform theme (Qt 6.5+).
    // Once toggled, the choice lives in QSettings via EditorState and survives
    // restarts; it is app-wide, not stored per project.
    readonly property bool systemPrefersDark: Qt.styleHints.colorScheme !== Qt.Light
    readonly property bool darkMode: EditorState.darkModeOverridden ? EditorState.darkModePreferred
                                                                    : systemPrefersDark

    function toggleDarkMode() {
        EditorState.setDarkModePreference(!darkMode);
    }

    function setDarkMode(enabled) {
        EditorState.setDarkModePreference(enabled);
    }

    // --- Color palettes: app shell vs. panel surfaces, light and dark ------------
    readonly property var _dark: ({
        appBackground: "#0d0d0d",
        foreground: "#dedede",
        border: "#292929",
        accent: "#242424",
        accentForeground: "#f2f2f2",
        mutedForeground: "#808080",
        popoverHover: "#212121",
        panelBackground: "#1a1a1a",
        panelForeground: "#d9d9d9",
        panelBorder: "#2e2e2e",
        panelAccent: "#262626",
        panelAccentForeground: "#ededed",
        panelMuted: "#383838",
        panelSecondaryBg: "#26200a",
        panelSecondaryBorder: "#4a3d00",
        panelSecondaryForeground: "#ffcf4a"
    })
    readonly property var _light: ({
        appBackground: "#ffffff",
        foreground: "#1c1c1c",
        border: "#e8e8e8",
        accent: "#f5f5f5",
        accentForeground: "#050505",
        mutedForeground: "#7a7a7a",
        popoverHover: "#f5f5f5",
        panelBackground: "#f9fafb",
        panelForeground: "#212121",
        panelBorder: "#dedede",
        panelAccent: "#ededed",
        panelAccentForeground: "#0d0d0d",
        panelMuted: "#d4d4d4",
        panelSecondaryBg: "#fff6da",
        panelSecondaryBorder: "#ffe7a3",
        panelSecondaryForeground: "#9a6f00"
    })
    readonly property var _palette: darkMode ? _dark : _light

    // --- Colors: app shell ---------------------------------------------------
    readonly property color appBackground: _palette.appBackground
    readonly property color foreground: _palette.foreground
    readonly property color border: _palette.border
    readonly property color accent: _palette.accent
    readonly property color accentForeground: _palette.accentForeground
    readonly property color mutedForeground: _palette.mutedForeground
    readonly property color popoverHover: _palette.popoverHover

    // --- Colors: panel surfaces ------------------------------------------------
    readonly property color panelBackground: _palette.panelBackground
    readonly property color panelForeground: _palette.panelForeground
    readonly property color panelBorder: _palette.panelBorder
    readonly property color panelAccent: _palette.panelAccent
    readonly property color panelAccentForeground: _palette.panelAccentForeground
    readonly property color panelMuted: _palette.panelMuted
    // Slider groove — lighter than panelMuted in dark mode so the track reads at rest.
    readonly property color sliderTrack: darkMode ? "#505050" : panelMuted
    // Scrollbar track + handle (timeline horizontal bar, panel flickables).
    readonly property color scrollbarTrack: darkMode ? "#2a2a2a" : panelBorder
    readonly property color scrollbarHandle: darkMode ? "#6a6a6a" : panelMuted
    readonly property color scrollbarHandleHover: darkMode ? "#888888" : mutedForeground
    readonly property color scrollbarHandlePressed: darkMode ? "#b8b8b8" : foreground
    readonly property color panelSecondaryBg: _palette.panelSecondaryBg
    readonly property color panelSecondaryBorder: _palette.panelSecondaryBorder
    readonly property color panelSecondaryForeground: _palette.panelSecondaryForeground

    // --- Colors: shared semantic (identical in both themes) -----------------------
    readonly property color primary: "#F8B81C"
    readonly property color primaryForeground: "#221900"
    readonly property color destructive: "#e91616"
    readonly property color constructive: "#23d160"
    readonly property color warning: "#f97316"

    // Keyboard focus indicator. Shared by every focusable control so a Tab pass
    // reads as one system regardless of which control has focus.
    readonly property color focusRing: primary

    // Export CTA gradient stops (the documented inline-color exception, sourced
    // from here so the button still tracks the token system).
    readonly property color exportGradientTop: "#ffcf4a"
    readonly property color exportGradientBottom: "#f59e0b"
    readonly property color exportGlow: "#fbbf24"

    // Scrims/overlays drawn over media (clip name bands, preview letterbox,
    // thumbnail duration badges). Fixed regardless of app theme because they sit
    // on photographic content, not on panel surfaces.
    readonly property color scrimColor: "#00000066"
    readonly property color scrimStrong: "#000000b3"
    readonly property color overlayColor: "#000000"
    // Guides and handles drawn on top of preview media.
    readonly property color guideStrong: "#99ffffff"
    readonly property color guideMedium: "#80ffffff"
    readonly property color guideWeak: "#66ffffff"
    readonly property color onMedia: "#ffffff"
    // Timeline snap indicator.
    readonly property color snapGuide: "#f5c542"
    // Async placeholder fill for thumbnails, filmstrips and waveforms.
    readonly property color skeletonColor: darkMode ? "#242424" : "#e8e8e8"
    readonly property color skeletonHighlight: darkMode ? "#333333" : "#f5f5f5"

    // --- Colors: timeline clip types (fixed regardless of app theme) ---------------
    readonly property color clipText: "#5DBAA0"
    readonly property color clipSubtitle: "#4A9FD4"
    readonly property color clipAudio: "#8F5DBA"
    readonly property color clipGraphic: "#BA5D7A"
    readonly property color clipEffect: "#5d93ba"
    readonly property color transitionOverlap: "#9B5DE5"
    readonly property color waveformColor: "#ffffffb3" // rgba(255,255,255,0.7) — on dark clip chrome
    // Waveform drawn on panel surfaces (subtitle cue lane, etc.): follows light/dark FG.
    readonly property color panelWaveformColor: darkMode ? "#ffffffb3" : "#212121b3"
    // Detected beat grid in the keyframe strip. Bar lines get the stronger alpha; the
    // beats between them the weaker one, so the metre reads at a glance without the grid
    // competing with the keyframe curves drawn on top of it.
    readonly property color beatBarColor: darkMode ? "#7ac8ff8c" : "#1f6fb24d"
    readonly property color beatGridColor: darkMode ? "#7ac8ff47" : "#1f6fb230"
    // Transients that do not land on the grid — drawn as short vertically-centered bars.
    readonly property color beatOnsetColor: darkMode ? "#ffffff8c" : "#2121218c"
    // Video clips normally show real thumbnails (always photographic/dark-ish); until
    // thumbnail generation exists, use a fixed dark placeholder so the white filename
    // scrim stays legible in light mode too instead of following panelAccent.
    readonly property color clipVideoPlaceholder: "#2b2b2b"
    // Style-pack thumbnails: most packs use white/light glyphs (and sit on video), so the
    // card canvas stays dark in both themes — panelSecondaryBg washes them out in light mode.
    readonly property color textStylePreviewBg: "#1c1c1c"
    readonly property color textStylePreviewBorder: darkMode ? "#3a3a3a" : "#2a2a2a"

    // --- Colors: keyframe curves (fixed regardless of app theme) -------------
    // One hue per animatable property so overlaid curves, their key diamonds and
    // their gutter chips all read as the same series. Chosen for separation at
    // 1.5px stroke width on both panel backgrounds.
    readonly property var keyframeCurveColors: ({
        "x": "#16a9f3",
        "y": "#f59e0b",
        "width": "#23d160",
        "height": "#e879f9",
        "rotation": "#f43f5e",
        "opacity": "#a78bfa",
        "volume": "#2dd4bf"
    })
    // Effect parameters are open-ended ("fx.<index>.<key>"), so they can't have named entries
    // above. Hashing the prop into this ramp keeps each one a stable, distinct color across
    // sessions instead of drawing every effect curve in the same accent.
    readonly property var keyframeCurveRamp: [
        "#38bdf8", "#fb923c", "#4ade80", "#c084fc",
        "#fb7185", "#facc15", "#2dd4bf", "#818cf8"
    ]
    function keyframeCurveColor(prop) {
        if (keyframeCurveColors[prop])
            return keyframeCurveColors[prop]
        let hash = 0
        for (let i = 0; i < prop.length; ++i)
            hash = (hash * 31 + prop.charCodeAt(i)) & 0x7fffffff
        return keyframeCurveRamp[hash % keyframeCurveRamp.length]
    }

    // --- Radius --------------------------------------------------------------
    readonly property real radiusSm: 5.6
    readonly property real radiusMd: 10.4
    readonly property real radiusLg: 13.12
    readonly property real radiusXs: 3    // badges, tick marks, hairline chrome
    readonly property real radiusPill: 999

    // --- Spacing scale ---------------------------------------------------------
    // Every gap/margin/padding in the app should come from here. Values are the
    // ones already in de-facto use, deduplicated into a scale.
    readonly property real spacingXs: 2
    readonly property real spacingSm: 4
    readonly property real spacingMd: 6
    readonly property real spacingLg: 8
    readonly property real spacingXl: 12
    readonly property real spacing2xl: 16
    readonly property real spacing3xl: 24

    // --- Control metrics -------------------------------------------------------
    // controlHeight is the alignment baseline: text fields, combo boxes and text
    // buttons all share it so adjacent controls in a Row line up.
    readonly property real controlHeight: 30
    readonly property real controlHeightSm: 26   // chips, segmented toggles
    readonly property real iconButtonSize: 28
    readonly property real borderWidth: 1
    readonly property real borderWidthFocus: 2

    // --- Icon sizes ------------------------------------------------------------
    readonly property real iconSizeSm: 12
    readonly property real iconSizeMd: 14
    readonly property real iconSizeBase: 16
    readonly property real iconSizeLg: 18
    readonly property real iconSizeXl: 22

    // --- Motion ----------------------------------------------------------------
    // durationFast: hover/press tints. durationBase: dialogs, tab crossfades.
    // durationSlow: layout-affecting reveals. durationPress: press feedback.
    readonly property int durationFast: 90
    readonly property int durationBase: 140
    readonly property int durationSlow: 220
    readonly property int durationPress: 120
    // Strong ease-out (~cubic-bezier(0.22, 1, 0.36, 1)) for anything entering or
    // leaving: it starts fast, so the interface answers in the frame the user is
    // watching. OutCubic was too weak to read as deliberate.
    readonly property int easing: Easing.OutQuint
    // Strong ease-in-out (~cubic-bezier(0.76, 0, 0.24, 1)) for things already on
    // screen that move or morph, where a fast start reads as a jerk.
    readonly property int easingInOut: Easing.InOutQuart
    // Press feedback: pressable chrome dips to this scale while held.
    readonly property real pressScale: 0.97
    readonly property int tooltipDelay: 400

    // --- Dialog widths ---------------------------------------------------------
    readonly property real dialogWidthSm: 340
    readonly property real dialogWidthMd: 420
    readonly property real dialogWidthLg: 660
    // Minimum breathing room between a dialog and the window edge.
    readonly property real dialogMargin: 32

    // --- Typography ------------------------------------------------------------
    readonly property real fontSizeXs: 11.52
    readonly property real fontSizeSm: 12.64
    readonly property real fontSizeBase: 14.72
    readonly property real fontSizeTiny: 9.6   // 0.6rem clip captions
    readonly property real fontSizeTick: 10    // literal 10px ruler tick labels
    readonly property real fontSizeCard: 11.2  // 0.7rem asset card filenames

    // --- Layout: chrome ------------------------------------------------------
    readonly property real headerHeight: 54.4
    readonly property real panelGap: 3
    readonly property real pagePadding: 12

    // --- Layout: assets panel -----------------------------------------------
    readonly property real panelHeaderHeight: 44
    readonly property real tabRailWidth: 40
    readonly property real assetCardWidth: 112
    readonly property real assetCardGap: 16

    // Tint for category chips in asset browser tabs.
    readonly property var categoryColors: [
        "#f59e0b", "#ef4444", "#8b5cf6", "#3b82f6", "#10b981",
        "#ec4899", "#06b6d4", "#84cc16", "#f97316", "#6366f1"
    ]

    function categoryColor(index) {
        const colors = categoryColors
        if (!colors || colors.length === 0)
            return primary
        return colors[Math.abs(index) % colors.length]
    }

    // --- Layout: preview panel -----------------------------------------------
    readonly property real previewToolbarPaddingTop: 20
    readonly property real previewToolbarPaddingBottom: 12

    // --- Layout: timeline ------------------------------------------------------
    readonly property real timelineToolbarHeight: 40
    // Tall enough to be an easy seek/scrub hit target (CapCut/Premiere-style).
    readonly property real timelineRulerHeight: 28
    readonly property real timelineBookmarkRowHeight: 18
    readonly property real trackHeightVideo: 65
    readonly property real trackHeightAudio: 50
    readonly property real trackHeightText: 25
    readonly property real trackHeightSubtitle: 25
    readonly property real trackHeightShape: 50
    readonly property real trackGap: 6
    // Invisible hit area above tracks (no visible UI) for new-track drops when timeline has clips.
    readonly property real newTrackHitSlop: 24
    readonly property real trackLabelsWidth: 130
    readonly property real pixelsPerSecondBase: 50
    // Empty runway after the last clip: fixed in pixels at every zoom (not a
    // fixed number of seconds). Sized as a fraction of the timeline viewport.
    readonly property real timelineEndPadFraction: 0.25
    readonly property real timelineEndPadMinPx: 120
    readonly property real playheadLineWidth: 2
    // Top scrubber head — sized to sit in the seek strip and stay easy to grab.
    readonly property real playheadHandleSize: 16
    readonly property real playheadSeekGrabWidth: 18
    readonly property real clipSelectionRingWidth: 1.5
    // Name band across the top of a clip. Clamped against track height at the use
    // site so it never swallows a short (25px text/subtitle) row.
    readonly property real clipHeaderBandHeight: 20
    readonly property real clipTrimHandleWidth: 12
    // Floor for a selected clip's on-timeline width so it never shrinks to a
    // sliver. Sized for two trim handles plus a move strip between them.
    readonly property real clipMinInteractiveWidth: 28
    readonly property real clipMinWidth: clipMinInteractiveWidth * 2
    // Matches drift::kMinClipDurationUs (0.1s). Effective min duration is the
    // larger of this and clipMinWidth / pxPerSecond at the current zoom.
    readonly property real clipMinDurationSeconds: 0.1

    // --- Layout: window --------------------------------------------------------
    // Floor for ApplicationWindow. Below this the split minimums cannot all be
    // satisfied and panels start overlapping.
    readonly property real windowMinimumWidth: 900
    readonly property real windowMinimumHeight: 560

    // --- Iconography (Lucide SVGs in resources/icons/; ISC-licensed, see
    // resources/licenses/LICENSE-lucide.txt) ------------------------------------
    // Values are Lucide icon file names (without .svg).
    readonly property var icons: ({
        scissors: "scissors",
        chevronsLeft: "chevrons-left",
        undo: "undo",
        redo: "redo",
        clipboardPaste: "clipboard-paste",
        copyPlus: "copy-plus",
        copy: "copy",
        trash: "trash-2",
        snowflake: "snowflake",
        bookmark: "bookmark",
        repeat: "repeat",
        star: "star",
        layers: "layers",
        magnet: "magnet",
        linkTwo: "link-2",
        unlink: "unlink-2",
        foldHorizontal: "fold-horizontal",
        zoomOut: "zoom-out",
        zoomIn: "zoom-in",
        zoomFit: "chevrons-left-right-ellipsis",
        gauge: "gauge",
        play: "play",
        pause: "pause",
        stepBack: "step-back",
        stepForward: "step-forward",
        rewind: "rewind",
        fastForward: "fast-forward",
        maximize: "maximize",
        minimize: "minimize",
        folder: "folder",
        headphones: "headphones",
        type: "type",
        smile: "smile",
        wand: "wand-sparkles",
        sparkles: "sparkles",
        sliders: "sliders-horizontal",
        settings: "settings",
        upload: "upload",
        plus: "plus",
        volumeHigh: "volume-2",
        volumeOff: "volume-off",
        eye: "eye",
        eyeOff: "eye-off",
        film: "film",
        video: "video",
        music: "music",
        audioLines: "audio-lines",
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
        list: "list",
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
        contrast: "contrast",
        penTool: "pen-tool",
        imagePlay: "image-play",
        puzzle: "puzzle",
        info: "info",
        package: "package",
        fileText: "file-text",

        // Status / feedback
        warning: "triangle-alert",
        success: "circle-check",
        error: "circle-x",
        spinner: "loader-circle",
        refresh: "refresh-cw",
        download: "download",

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
