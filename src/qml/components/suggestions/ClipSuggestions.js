.pragma library

// "What to do next" rules for one clip, from facts the editor already holds. A pragma library
// cannot see the QML singletons, so the caller passes them in:
//   env = { editor: EditorState, addons: Addons, tracks: <timeline tracks>,
//           reframe: function(trackIndex, clipIndex),
//           dismissed: function(suggestionId) -> bool }   (dismissed is optional)
//   clip = { clipData, trackType, trackIndex, clipIndex, selected }
// suggest() returns an ordered [{ id, label, run }], at most `limit` of them (0 = all).

function collectFacts(env, clip) {
    const data = clip.clipData
    const kind = data.kind
    const editor = env.editor
    const hasAudio = !data.sequenceId && (kind === "audio"
        || (kind === "video" && editor.canSeparateAudioClip(clip.trackIndex, clip.clipIndex)))
    const turned = Math.abs(data.sourceRotation || 0) % 180 === 90
    const width = turned ? data.sourceHeight : data.sourceWidth
    const height = turned ? data.sourceWidth : data.sourceHeight
    return {
        hasAudio: hasAudio,
        hasSubtitles: env.tracks.some(t => t.type === "subtitle" && (t.clips || []).length > 0),
        whisperReady: env.addons.hasKind("whisper-model"),
        captioning: editor.subtitleGenerating,
        landscape: (kind === "video" || kind === "image") && width > 0 && width > height,
        canvasPortrait: editor.projectPortrait,
        canReframe: typeof env.reframe === "function",
        shaky: kind === "video" && !data.stabilized && !data.stabilizing && !data.sequenceId
               && editor.stabilize.hasAnalysis(clip.trackIndex, clip.clipIndex)
    }
}

function suggest(env, clip, limit) {
    const f = collectFacts(env, clip)
    const out = []
    const select = function() {
        if (!clip.selected)
            env.editor.selectClip(clip.trackIndex, clip.clipIndex)
    }

    if (f.hasAudio && !f.hasSubtitles && f.whisperReady && !f.captioning) {
        out.push({
            id: "captions",
            label: qsTr("Generate captions"),
            run: function() {
                select()
                env.editor.generateSubtitlesForSelection()
            }
        })
    }
    if (f.landscape && f.canvasPortrait && f.canReframe) {
        out.push({
            id: "reframe",
            label: qsTr("Auto reframe…"),
            run: function() { env.reframe(clip.trackIndex, clip.clipIndex) }
        })
    }
    if (f.shaky) {
        out.push({
            id: "stabilize",
            label: qsTr("Stabilize this shaky clip"),
            run: function() {
                select()
                env.editor.stabilize.run(clip.trackIndex, clip.clipIndex)
            }
        })
    }
    const live = typeof env.dismissed === "function" ? out.filter(s => !env.dismissed(s.id)) : out
    return limit > 0 ? live.slice(0, limit) : live
}
