# Agent access (Drift MCP)

Drift can expose a localhost MCP server so Cursor, Claude Code, or other agents can edit the open project. Enable it in **Settings → Agent access** (off at every launch).

## Connect

**Cursor / Claude (this session):** copy the snippet from Settings after enabling. The bearer token rotates each session.

**One-time stdio attach** (no token in `mcp.json`):

```json
{
  "mcpServers": {
    "drift": {
      "command": "/path/to/drift",
      "args": ["--mcp-stdio"]
    }
  }
}
```

`drift --mcp-stdio` attaches to a running editor with Agent access enabled. It speaks
newline-delimited JSON-RPC, the framing the MCP stdio transport specifies. If the editor
is closed or Agent access is off, each call comes back as a JSON-RPC error and the bridge
keeps running, so enabling Agent access is enough to make it work — no need to restart the
client.

## Headless

`drift --headless` runs the editor with no window and serves MCP itself, for servers and
unattended automation. It needs no running editor, no session token and no open project:

```json
{
  "mcpServers": {
    "drift": {
      "command": "/path/to/drift",
      "args": ["--headless"]
    }
  }
}
```

| Command | Transport |
| --- | --- |
| `drift --headless [project.json]` | MCP on stdin/stdout; quits when stdin closes |
| `drift --headless --mcp-port 4731 [--mcp-token T]` | MCP over HTTP; runs until SIGINT/SIGTERM |
| `drift --headless --mcp-stdio --mcp-port 4731` | both |

On startup it prints what is running — platform plugin, OpenGL, project, transports, and
the HTTP URL, token and session-file path. That banner goes to **stdout**, except when
stdio is a transport, where it goes to stderr instead: the spec allows nothing but MCP
messages on stdout, and a banner there is exactly what breaks clients.

Without `--mcp-token` (or `$DRIFT_MCP_TOKEN`) a token is generated and shown in the
banner. Only the HTTP form writes the session file that `--mcp-stdio` reads, so a headless
instance serving stdio never disturbs a GUI editor running alongside it.

Both forms shut down on SIGINT (Ctrl-C) and SIGTERM.

**Rendering needs a GL context, which is not the same as needing a window.** The
compositor wants OpenGL 3.3 core on an offscreen surface, and Qt's `offscreen` platform
plugin cannot provide one on a host with no `/dev/dri`. On a headless Linux box run:

```bash
QT_QPA_PLATFORM=xcb xvfb-run -a drift --headless
```

Mesa's llvmpipe covers 3.3 in software; a GPU host can use an EGL platform plugin
instead. Started without a usable context, Drift still serves MCP and still edits
projects — it says so on stderr, and only render, capture and export fail.

## Workflow

1. **`catalog`** — toolboxes, per-op “when” hints, endpoints, units, limitations (no schemas).
2. **`toolbox({name})`** — full JSON schemas for ops in that toolbox.
3. **`apply({ops:[{tool, args}, …]})`** — run mutations in order; one undo step for the batch.
4. **`inspect({clips:true, detail:true})`** — project state, clip UUIDs, effects, transitions, bookmarks, mask/fade/speed, subtitle cues, face track, stabilize*, async jobs, `selection`, `undo`.
5. **`capture()`** — JPEG still of the composition (use to verify edits).

Homepage endpoint: `POST /mcp` with `Authorization: Bearer <token>`.

Pinned endpoints (`/mcp/timeline`, `/mcp/project`, …) list that toolbox’s ops directly. `catalog`, `toolbox`, and `apply` are only on `/mcp`; `inspect` and `capture` work on both. Toolbox ops can also be called by name directly on `/mcp` instead of through `apply` — but only `apply` collapses a batch into one undo step.

`apply` takes **toolbox ops only**. `catalog`, `toolbox`, `inspect`, `capture`, and `apply` itself return `unknown_op` inside an `ops` array; call them directly.

## Conventions

| Topic | Rule |
|-------|------|
| Time | Seconds |
| Clip reference | Prefer `clip` UUID from `inspect`; else `track` (0 = top) + `index`. One or the other is **required** — clip ops never fall back to the selection. Read the current selection from `inspect.selection` |
| Selection ops | `separate_audio`, `unlink_audio`, `merge_clips`, `align_clip_left/right`, `copy_selection`, `cut_selection` take no clip argument — call `select_clip` first. `freeze_frame` and `paste_at_playhead` are playhead-based (seek first); they do not use the selection |
| Discovery | Effect stack indices, transition ids, and bookmark indices exist **only** in `inspect({clips:true, detail:true})`. Subtitle cues: `inspect({clips:true, cues:true})` or the `subtitleCues` field of a detail row. Mask/fade/speed/volume/keyframes/`hasFaceTrack`/stabilize* are on the same detail rows |
| Overlap | Off by default — place/move snap to gaps unless `set_overlap` enables overlap; the reply reports `requested` vs `placed`. With overlap off, moving several clips toward zero must be sequenced **back-to-front**. `set_ripple` / `close_gap` close holes after a delete |
| Export | `export_video` is async — poll `inspect().export` or `export_status`. The output path is normalised, so use the `path` echoed back. Named sizes: `list_export_presets` + `export_with_preset` |
| Atomicity | `apply` is **not** atomic: on failure the ops before it stay applied. Check `stopped` / `failed` / `done`. An ops array cannot reference an id produced earlier in the same batch — end the batch after `set_speed_curve` |
| Undo | One batch = one undo step. Linear history (no branches): `list_history` returns `{index, label, hash}` per version (index 0 = Origin). `undo_to({index})` or `undo_to({hash})` jumps; the next edit drops redo. `take_snapshot` writes compact project JSON named `<hash>.json` (file SHA-256 = history hash). Ops outside the stack (and all read-only ops): `import_media`, `import_media_bytes`, `seek`, `play`, `pause`, `undo`, `redo`, `undo_to`, `take_snapshot`, `restore_snapshot`, `set_overlap`, `set_ripple`, `set_snap`, `set_guides`, `set_loop_work_area`, `export_video`, `save_project`, `set_theme`, `set_shortcut`, `reset_shortcuts`, `set_beat_layers`, `detect_beats`, `list_speed_curve`, `list_fade_curve`, `install_addon`, `cancel_addon_install`, `set_acceleration`, `switch_angle`, `end_multicam`. **`set_beat_layers` changes the user's own snapping and cannot be undone** |
| Errors | `{ok:false, error:<code>, detail:<text>}` — `bad_args`, `not_found`, `type_mismatch`, `unknown_op`, `unknown_toolbox`, `wrong_endpoint`, `wrong_toolbox`, `apply_failed`, `import_failed`, `import_timeout`, `export_busy`, `export_failed`, `export_timeout`, `capture_failed`, `conflict` |
| Change detection | Every `inspect` includes `revision`; pass `since:<revision>` to get `{unchanged:true}` when state is current |
| Media import | Absolute paths or `file://`, or `import_media_bytes` (base64 — always pass an explicit `path`). **No directory listing.** If the user gave a fuzzy name (`GX010023.mp4` in Downloads), glob/search with **your own filesystem tools**, then pass the hits to `import_media` and confirm `missing:[]` is empty |

### Async jobs

All return `{started:true}` immediately. Every field below except `export` needs `inspect({detail:true})`.

| Started by | Poll |
|---|---|
| `export_video` | `export.{active, progress}` (or `export_status`) |
| `package_project` | `package.{active, progress}` |
| `generate_subtitles` | `subtitleGen.{active, progress, status}` |
| `set_clip_reverse` (video) | `reverseRender.{active, progress, status}` |
| `detect_scenes` | `sceneDetect.{active, progress, status, clip, scenes}` |
| `run_segmentation`, `segment_clip`, `apply_denoise`, `detect_faces` | No progress field — re-read `inspect({clips:true, detail:true})` and compare |
| `stabilize_clip` | Per-clip `stabilizing` / `stabilizeProgress` / `stabilizeStatus` on the detail clip row |

## Toolbox reference

| Toolbox | When to use |
|---------|-------------|
| `media` | Import (paths/bytes), list, rename, remove, replace, export still |
| `timeline` | Tracks, clips, selection, ripple/gap, bookmarks, copy/paste, A/V link |
| `canvas` | Transform, flip, blend, mask, fade, speed, reverse, animation, stabilisation |
| `playback` | Seek, play, pause, In/Out work area |
| `text` | Title and caption clips, text presets |
| `shapes` | Builtin shapes, stickers, emoji, fonts, text presets |
| `subtitles` | Subtitle clips, cues, import/export, Whisper generation |
| `effects` | Video/audio effects, transitions, templates, effect clipboard |
| `project` | Open/new/save/package, canvas, background, metadata, export |
| `keyframes` | Property animation keys and tangents |
| `speed` | Speed ramps; reading custom fade curves (write them with `set_fade_curve` in `canvas`) |
| `segmentation` | SAM-style cutout (session or one-shot) |
| `ai` | Denoise, face detection, auto-reframe, add-on install |
| `audio` | Waveforms, silence, loudness, ducking, beat detection, beat-synced cuts, clip volume |
| `scene` | Shot detection, what is in each shot, scene-synced cuts |
| `ui` | Theme, shortcuts, editor preferences, guides |
| `multicam` | Multi-camera session: set up, switch at the playhead, save |

### Working to the music

`detect_beats` **blocks** and returns the grid — no polling.

```json
{"name": "detect_beats", "arguments": {"start": 0, "duration": 30}}
```

```json
{"ok": true, "bpm": 128.0, "confidence": 0.81, "beatsPerBar": 4, "firstDownbeat": 2,
 "beats": [0.31, 0.78, 1.25], "onsets": [{"at": 0.31, "s": 0.9}], "cached": false}
```

Then either drive edits from those exact times, or arm the grid and let snapping do it:

| Call | Effect |
|---|---|
| `set_beat_layers({grid:true})` | Beats become snap targets — `place_clip`, `move_clip` and `move_to_track` now magnet to the nearest beat within 150 ms, and so do the user's own drags |
| `split_on_beats({clip, unit:"bar"})` | Cuts the clip at every bar line. One undo step |
| `snap_clips_to_beats({clips, unit:"beat"})` | Quantises clip starts. One undo step |
| `bookmark_beats({unit:"bar"})` | Writes the grid into the project as markers that outlive the analysis |

The analysis is **transient**: any edit that changes the mix drops it (`finishEdit` clears it when the audio fingerprint moves). `inspect({detail:true}).beats` reports `{active, analysed, bpm, confidence, rangeStart, rangeDuration, n, onsets, gridVisible, onsetsVisible, stale}` — check `stale` before trusting a grid you fetched a few ops ago, and re-run `detect_beats` when it is `true`.

### Understanding the footage

`capture` shows one composited frame. The `scene` toolbox instead builds a **structured index
of the source material**: where every shot begins, how active each one is, and — with the
object add-on — what is in it.

`detect_scenes` is **async**. Poll `inspect({detail:true}).sceneDetect` until `active` is false.

```json
{"name": "detect_scenes", "arguments": {"clip": "…uuid…", "with_objects": true}}
```

A clip already scanned at the same settings returns `{"cached": true}` and is ready at once.

| Call | Effect |
|---|---|
| `describe_clip()` | One-call impression: shot count, shortest/longest, mean score, top shots, and labels ranked by **screen time** |
| `list_scenes({sort:"score"})` | Every shot, with `timeline_start`/`timeline_end` already mapped through trim, speed and reverse |
| `find_scenes({label:"person"})` | Searches **every scanned clip** on the timeline, best first — this is how you gather material |
| `split_on_scenes({clip})` | Cuts at every boundary. One undo step |
| `bookmark_scenes({clip})` | Marks the boundaries instead of cutting |

Times come back twice. `start`/`end` are seconds into the **source file**; `timeline_start`/
`timeline_end` are the same moments on the **timeline**. Act on the timeline pair — the mapping
through trim, speed and reverse is already done for you.

Unlike beats, this analysis is **not transient**. It describes the source file and is cached
against that file's timestamp, so it survives edits, undo and reload. Re-trimming a clip changes
the scanned range, so that does scan afresh.

`with_objects` needs the `object-model` add-on. Call `ai_capabilities` to see what is installed
and what each piece unlocks; `list_addons` / `install_addon` can install a missing model.

**Finding files.** Drift does not list directories. When the user names a clip loosely
(`GX010023.mp4` in Downloads, “the wedding file”), glob or search with **your own filesystem
tools**, pass the absolute path to `import_media`, and treat a non-empty `missing:[]` as
“search again”, not as a bin problem.

## Traps

- **`set_transform` writes at the playhead.** If the property is keyframed, or `autoKey` is on, it creates a keyframe there instead of a constant value. Seek first, or mute the animation with `set_property_keyframes_enabled(false)`.
- **`set_mask` replaces the whole mask.** Omitted keys revert to defaults and omitting `shape` turns the mask off. Read the current mask from `inspect({clips:true, detail:true}).tracks[].items[].mask` and send it back merged.
- **`set_subtitle_cues` replaces every cue.** Read `inspect({clips:true, cues:true})` (or a detail row's `subtitleCues`), merge, send.
- **`set_effect_param`, `set_audio_effect_param`, `set_transition_param` do not validate.** A wrong key or index still returns `ok`. Verify with `inspect({clips:true, detail:true})`.
- **`set_speed_curve` returns a new clip id.** The old UUID stops resolving. End the apply batch after it — an ops array cannot reference an id produced earlier in the same batch.
- **`remove_keyframe` deletes the *nearest* key** with no distance limit. Confirm the exact time with `list_keyframes`.
- **`set_keyframe_interpolation` moves the playhead** to `at`, changing the default time of later ops in the same batch.
- **`add_track` shifts every track index** — index 0 is the new track.
- **`import_media_bytes` without `path`** writes to a temp dir that is deleted when the call returns, leaving a broken asset. Always pass `path`.
- **`get_waveform` reads two different things.** With `clip` or `asset` it reads the *source file*, so clip speed, reverse and volume are not applied. Only the timeline form (`start` + `duration`, no clip or asset) is what you would actually hear.
- **`bpm: 0` from `detect_beats` is not an error** — it means no trustworthy tempo, so `beats` is empty. The `onsets` are still valid; pass `unit:"onset"` anywhere a grid is accepted.
- **Beat analysis dies on the next edit.** Read the whole grid out of `detect_beats` before mutating anything, or re-detect. `split_on_beats` and `snap_clips_to_beats` already snapshot it internally.
- **`snap_clips_to_beats` may not land on the beat.** With overlap off, a clip is pushed to the next free gap. Read the `to` values back rather than assuming they equal the beat time.
- **`split_on_beats` keeps your clip id for the *first* piece.** The other pieces are new UUIDs, returned in `clips` in timeline order.
- **There is no track volume.** `set_volume` is per clip; mute a whole lane with `set_track({muted:true})`.
- **`generate_subtitles` after `remove_silence`.** Silence removal shifts the timeline; captions generated before it will be wrong.
- **`apply_denoise` is noise, not reverb.** "Sounds like a bathroom" will not be fixed by denoise.

## Example

An `ops` array is submitted whole, so it cannot reference an id produced earlier in the same batch. Import and place first, then read the new clip's id out of the reply:

```json
{
  "ops": [
    {"tool": "import_media", "args": {"paths": ["/path/to/clip.mp4"]}},
    {"tool": "place_clip", "args": {"asset": "0", "at": 0}}
  ]
}
```

The reply carries each op's result in order — `done[1].result.id` is the new clip's UUID. Use it in the next call:

```json
{
  "ops": [
    {"tool": "set_duration", "args": {"clip": "<done[1].result.id>", "duration": 5}},
    {"tool": "set_transform", "args": {"clip": "<same id>", "x": 0, "y": 0, "w": 1920, "h": 1080}}
  ]
}
```

Then verify visually — `capture` is a homepage tool and returns `unknown_op` inside `ops`, so call it on its own:

```json
{"name": "capture", "arguments": {"at": 2.5}}
```

## Security

- Binds to `127.0.0.1` only.
- Any local process with the session token has full editor access.
- Turn Agent access off when finished.

**Flatpak:** host file import may need `flatpak override --filesystem=home org.cutwire.Drift`.
