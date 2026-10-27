import QtQml

// Controls the global search can only point at, not run: each entry is where it lives and what
// has to be selected for it to show. Keep labels in step with the inspectors they name.
QtObject {
    readonly property string properties: qsTr("Properties")
    readonly property string assets: qsTr("Assets")
    readonly property string settings: qsTr("Settings")
    readonly property string grading: qsTr("Color")
    readonly property string subtitles: qsTr("Subtitles")
    readonly property string sourceLab: qsTr("Source Lab")

    readonly property string anyClip: qsTr("Select a clip")
    readonly property string visualClip: qsTr("Select a video, image, text or shape clip")
    readonly property string videoClip: qsTr("Select a video clip")
    readonly property string soundClip: qsTr("Select a video or audio clip")
    readonly property string textClip: qsTr("Select a text or subtitle clip")
    readonly property string effectsLayer: qsTr("Select a clip's effects layer")
    readonly property string masksLayer: qsTr("Select a clip's mask layer")
    readonly property string audioEffectsLayer: qsTr("Select a clip's audio effects layer")
    readonly property string transitionClip: qsTr("Select the clip before a cut")

    function prop(tab, label, hint, keywords) {
        return { label: label, path: [properties, tab], hint: hint, keywords: keywords || "" }
    }

    function setting(pageId, label, keywords) {
        return { label: label, path: [settings], hint: "", keywords: keywords, page: pageId }
    }

    function color(label, hint, keywords, tab) {
        return { label: label, path: [grading], hint: hint, keywords: keywords, workspace: "color", colorTab: tab }
    }

    function subtitle(label, keywords) {
        return { label: label, path: [subtitles], hint: qsTr("Open the Subtitles workspace"), keywords: keywords, workspace: "subtitles" }
    }

    function sourceEntry(label, keywords) {
        return { label: label, path: [sourceLab], hint: qsTr("Open the Source Lab workspace"), keywords: keywords, workspace: "source-lab" }
    }

    readonly property var entries: [
        prop(qsTr("General"), qsTr("Clip name"), anyClip, "rename"),
        prop(qsTr("General"), qsTr("Crop"), visualClip, "cut frame"),
        prop(qsTr("General"), qsTr("Trim"), anyClip, "in out start end"),
        prop(qsTr("General"), qsTr("Duration"), anyClip, "length"),

        prop(qsTr("Transform"), qsTr("Position"), visualClip, "move x y"),
        prop(qsTr("Transform"), qsTr("Scale"), visualClip, "size zoom resize"),
        prop(qsTr("Transform"), qsTr("Opacity"), visualClip, "transparency alpha fade"),
        prop(qsTr("Transform"), qsTr("Rotation"), visualClip, "angle rotate 90"),
        prop(qsTr("Transform"), qsTr("Flip"), visualClip, "mirror horizontal vertical"),
        prop(qsTr("Transform"), qsTr("Fix orientation"), visualClip, "rotate sideways"),
        prop(qsTr("Transform"), qsTr("Reset position & size"), visualClip, "fit"),

        prop(qsTr("Stabilization"), qsTr("Stabilize"), videoClip, "shake shaky steady"),
        prop(qsTr("Stabilization"), qsTr("Smoothing"), videoClip, "shake steady"),
        prop(qsTr("Stabilization"), qsTr("Tripod mode"), videoClip, "lock still"),

        prop(qsTr("Animation"), qsTr("In animation"), anyClip, "intro enter fade slide pop zoom bounce"),
        prop(qsTr("Animation"), qsTr("Out animation"), anyClip, "outro exit fade slide pop zoom"),
        prop(qsTr("Animation"), qsTr("Custom curve"), anyClip, "easing"),

        prop(qsTr("Audio"), qsTr("Volume"), soundClip, "loud gain level mute"),
        prop(qsTr("Audio"), qsTr("Pan"), soundClip, "balance left right stereo"),
        prop(qsTr("Audio"), qsTr("Remove noise…"), soundClip, "denoise hiss background clean"),
        prop(qsTr("Audio"), qsTr("Auto subtitles"), soundClip, "captions transcribe speech whisper"),
        prop(qsTr("Audio"), qsTr("Audio track"), videoClip, "extract stream"),

        prop(qsTr("Speed"), qsTr("Playback speed"), soundClip, "fast slow motion"),
        prop(qsTr("Speed"), qsTr("Custom speed…"), soundClip, "ramp curve"),
        prop(qsTr("Speed"), qsTr("Reverse"), soundClip, "backwards rewind"),

        prop(qsTr("Blending"), qsTr("Blend mode"), visualClip, "multiply screen overlay add"),

        prop(qsTr("Masks"), qsTr("Feather"), masksLayer, "soft edge blur"),
        prop(qsTr("Masks"), qsTr("Invert"), masksLayer, "reverse"),
        prop(qsTr("Masks"), qsTr("Combine"), masksLayer, "add subtract intersect"),

        prop(qsTr("Effects"), qsTr("Effect settings"), effectsLayer, "parameters"),
        prop(qsTr("Effects"), qsTr("Face tracking"), effectsLayer, "face"),
        prop(qsTr("Effects"), qsTr("Save as preset…"), effectsLayer, "stack"),
        prop(qsTr("Audio FX"), qsTr("Audio effect settings"), audioEffectsLayer, "parameters"),

        prop(qsTr("Transition"), qsTr("Transition duration"), transitionClip, "length"),
        prop(qsTr("Transition"), qsTr("Transition curve"), transitionClip, "easing"),

        prop(qsTr("Text"), qsTr("Font"), textClip, "typeface"),
        prop(qsTr("Text"), qsTr("Text style"), textClip, "color outline shadow"),
        prop(qsTr("Shape"), qsTr("Shape style"), qsTr("Select a shape clip"), "fill stroke"),
        prop(qsTr("Camera"), qsTr("Camera position"), qsTr("Select the camera layer"), "3d"),
        prop(qsTr("PDF"), qsTr("PDF page"), qsTr("Select a PDF clip"), "scroll"),
        prop(qsTr("3D Model"), qsTr("3D model"), qsTr("Select a 3D model clip"), "mesh"),

        color(qsTr("Exposure"), anyClip, "light brightness", 0),
        color(qsTr("Contrast"), anyClip, "light", 0),
        color(qsTr("Highlights"), anyClip, "light bright", 0),
        color(qsTr("Shadows"), anyClip, "light dark", 0),
        color(qsTr("Whites"), anyClip, "light", 0),
        color(qsTr("Blacks"), anyClip, "light", 0),
        color(qsTr("Temperature"), anyClip, "white balance warm cool kelvin", 0),
        color(qsTr("Tint"), anyClip, "white balance green magenta", 0),
        color(qsTr("Saturation"), anyClip, "color intensity", 0),
        color(qsTr("Vibrance"), anyClip, "color saturation", 0),
        color(qsTr("Color wheels"), anyClip, "lift gamma gain shadows midtones highlights", 1),
        color(qsTr("Curves"), anyClip, "rgb curve", 2),
        color(qsTr("Hue curves"), anyClip, "hue vs hue saturation luminance curve", 2),
        color(qsTr("HSL"), anyClip, "hsl hue saturation luminance color band red orange yellow green aqua blue purple magenta", 3),
        color(qsTr("Color grade"), visualClip, "grade color workspace open"),
        color(qsTr("Looks"), anyClip, "lut filter preset cube"),
        color(qsTr("Import looks"), anyClip, "import cube lut look"),
        color(qsTr("Scopes"), qsTr("Open the Color workspace"), "histogram waveform vectorscope parade"),
        color(qsTr("Film Grain"), anyClip, "grain noise film", 4),
        color(qsTr("Glow"), anyClip, "bloom glow halation", 4),
        color(qsTr("Vignette"), anyClip, "dark corners edges", 4),
        color(qsTr("Chromatic Aberration"), anyClip, "color fringe lens", 4),
        color(qsTr("Compare"), anyClip, "before after bypass original"),
        color(qsTr("Apply to all clips"), anyClip, "grade timeline all clips copy"),

        { label: qsTr("Keyframe editor"), path: [qsTr("Animation")], hint: qsTr("Open the Keyframes workspace"),
          keywords: "keyframes animation curves graph editor dope sheet", workspace: "keyframes" },

        { label: qsTr("Masking & Tracking"), path: [qsTr("Masks")], hint: qsTr("Open the Masking & Tracking workspace"),
          keywords: "mask matte rotoscope roto brush polygon tracking tracker motion track planar object follow", workspace: "masking-tracking" },

        subtitle(qsTr("Subtitle lane"), "captions cues timeline drag waveform zoom"),
        subtitle(qsTr("Subtitle timing"), "captions tools shift offset sync frame rate speed gaps durations"),
        subtitle(qsTr("Shift times"), "captions offset delay early late move"),
        subtitle(qsTr("Two-point sync"), "captions resync stretch scale align drift"),
        subtitle(qsTr("Frame rate conversion"), "captions fps 23.976 24 25 29.97 30 speed"),
        subtitle(qsTr("Bridge gaps"), "captions minimum gap lead in out duration overlap"),
        subtitle(qsTr("Subtitle text editor"), "captions srt vtt paste edit import export source"),

        sourceEntry(qsTr("Source Lab"), "source lab workspace cut trim crop reframe ranges subclip favorite reject marker speed denoise stabilize upscale enhance export clip normalize loudness"),
        sourceEntry(qsTr("Trim source clip"), "trim cut in out start end ranges subclip split scene detection favorite reject marker"),
        sourceEntry(qsTr("Crop / reframe source"), "crop reframe reframing aspect ratio 9:16 portrait presets"),
        sourceEntry(qsTr("Speed and loudness"), "speed slow fast motion gain volume loudness normalize"),
        sourceEntry(qsTr("Denoise and stabilize source"), "denoise noise hiss clean stabilize shake steady"),
        sourceEntry(qsTr("Upscale source clip"), "upscale enhance restore resolution sharpen a/b compare"),
        sourceEntry(qsTr("Export trimmed clip"), "export clip file save render container codec quality stream copy replace bin add copy"),

        setting("general", qsTr("General"), "language locale startup autosave recovery cache backup"),
        setting("appearance", qsTr("Appearance"), "theme dark light color workspace layout translucency"),
        setting("workspaces", qsTr("Workspaces"), "workspace layout panel window monitor screen dock preset"),
        setting("timeline", qsTr("Timeline"), "scroll wheel horizontal vertical crossfade transition overview minimap"),
        setting("toolbar", qsTr("Timeline toolbar"), "toolbar buttons customize"),
        setting("keymap", qsTr("Keyboard shortcuts"), "keymap keys hotkeys bindings"),
        setting("preview", qsTr("Preview"), "quality resolution decode proxies guides background graphics gpu"),
        setting("performance", qsTr("Performance"), "advanced tuning cache memory ram vram read-ahead buffer drop frames hardware encoder export pool"),
        setting("audio", qsTr("Audio"), "sound speakers microphone output input"),
        setting("ai", qsTr("AI & Cloud"), "agent mcp claude cursor api key elevenlabs voice scene detection"),
        setting("network", qsTr("Network"), "proxy http socks internet firewall"),
        setting("about", qsTr("About"), "version updates debug info diagnostics"),
    ]

    // Where an installed addon's feature is used. Kinds missing here fall back to Extras.
    readonly property var addonPlaces: ({
        "effects": [assets, qsTr("Effects")],
        "transitions": [assets, qsTr("Transitions")],
        "audio-effects": [assets, qsTr("Audio FX")],
        "effect-templates": [assets, qsTr("Templates")],
        "looks": [qsTr("Color"), qsTr("Looks")],
        "stickers": [assets, qsTr("Stickers")],
        "fonts": [properties, qsTr("Text"), qsTr("Font")],
        "whisper-model": [assets, qsTr("Subtitles")],
        "tts-model": [assets, qsTr("Speech")],
        "sam2-model": [assets, qsTr("Masks"), qsTr("Subject")],
        "object-model": [assets, qsTr("Scenes")],
        "denoise-model": [properties, qsTr("Audio"), qsTr("Remove noise…")],
        "face-model": [properties, qsTr("Effects"), qsTr("Face tracking")],
        "face-props": [properties, qsTr("Effects"), qsTr("Face tracking")],
        "depth-model": [assets, qsTr("Effects")],
        "restore-model": [assets, qsTr("Media"), qsTr("Preview"), qsTr("Upscale…")],
    })
    readonly property var addonFallback: [qsTr("Extras")]
}
