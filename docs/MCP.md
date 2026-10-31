# Agent access (Drift MCP)

Drift can expose a localhost MCP server so Cursor, Claude Code, or other agents can edit the open project. Enable it in **Settings → Agent access** (off at every launch by default; a "Start agent on startup" switch there opts into starting it automatically instead, and resets itself the next time access is turned off).

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

### Several instances

Any number of editors (and headless instances) can run at once. The first takes port 4731;
the rest bind a free port. Each one with Agent access on registers itself with a file in
`$XDG_RUNTIME_DIR/drift/mcp-sessions/<pid>.json` (the temp directory where there is no
runtime directory; `DRIFT_MCP_SESSION_DIR` moves it) holding its port, token, pid, start
time, whether it is headless, and the open project's path and name. The file is
owner-only, rewritten when the token rotates or the project changes, and removed when
Agent access is turned off. Records left by a process that has died are ignored and
deleted the next time anything reads the registry.

The stdio bridge adds four tools of its own to the homepage, and answers `initialize`,
`ping` and `tools/list` itself, so they work before any editor is reachable:

| Tool | Does |
| --- | --- |
| `instances()` | Lists running instances: `id` (pid), `name` (project name), `project` (path, when saved), `headless`, `isolated` (started by `launch_instance`), `url`, `started`, `attached`. Never the token |
| `use_instance({id})` | Attaches to that instance; every later call goes to it |
| `launch_instance({project?, headless?, wait?})` | Starts a new Drift with Agent access on and attaches to it once it is up **and** the requested project has finished opening (up to `wait` seconds, default 60). A failed open comes back as `load_failed` with the reason, still attached to the instance. A window by default; `headless:true` runs `--headless --mcp-port 4731` instead, which keeps running until `close_instance` or a signal stops it |
| `close_instance({id, discard?})` | Quits an instance that `launch_instance` started (`isolated:true` in `instances()`) and waits for it to exit; its registry record and instance marker go with it. Never the user's own editor (`not_isolated`). Refuses `busy` during a save or load, and `unsaved_changes` when the project is dirty unless `discard:true`. The instance-side op is `quit_instance`, which applies the same checks |

Launched instances run with `--isolated-session` (the GUI also gets `--agent-access`, which
turns Agent access on for that run only). An isolated instance shares the user's settings
but not their session: it never reopens the last project or offers crash recovery, opens
straight into the editor without the start screen, and does not write the recovery file,
`lastSessionPath` or the recent-projects list, so the user's own editor reopens exactly what
it would have otherwise.

Every instance, with Agent access or without, also leaves a marker in
`<AppData>/instances/<pid>` while it runs. The startup sweep that reclaims unpacked project
media under `<AppData>/projects` only knows what this instance's recents and the recovery
file name, so it holds back entirely while any other instance is alive.

With exactly one instance running, the bridge attaches to it on the first call. With
several it refuses to guess: tool calls come back `no_instance` with the list until
`use_instance` picks one. `drift --mcp-stdio --mcp-instance <pid>` pins it up front.

Once attached, a bridge stays on that instance and follows its token rotations. Opening
another editor never takes over its next call; if the instance turns Agent access off or
closes, calls fail with a message naming it until access returns or `use_instance` moves
the bridge elsewhere.

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
banner. Only the HTTP form registers a session file for `--mcp-stdio` to find, so a
headless instance serving only stdio stays out of the registry.

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

The `initialize` reply carries a short `instructions` string with the essentials; the eight homepage tools are:

1. **`catalog`** — toolboxes and their ops as `"name — when"` strings, plus limitations (~3k tokens). `catalog({brief:true})` is names only; `catalog({guide:true})` adds the long prose guide; `catalog({endpoints:true})` adds the HTTP endpoints.
2. **`search({q})`** — find ops by keyword across names, when-hints, descriptions and argument names. Returns `hits:[{name, toolbox, when, args, required}]`; `schema:true` inlines the full schema when there are ≤3 hits. Use this instead of loading a whole toolbox.
3. **`toolbox({name})`** or **`toolbox({ops:[…]})`** — full JSON schemas for a toolbox, or for just the named ops (any toolbox).
4. **`apply({ops:[{tool, args}, …]})`** — run mutations in order; one undo step for the batch.
5. **`inspect()`** — project summary; `clips:true` adds clip rows, `detail:true` expands them, `clip:<uuid>` / `track:<n>` filter, `since:<revision>` short-circuits.
6. **`activity()`** — cheap text profile of visual change, motion and loudness over a range; tells you *where* to look.
7. **`frames()`** — one labelled contact-sheet image of many moments; tells you *what* is there.
8. **`capture({at, view})`** — one full-size JPEG still of the composition (use to verify edits). `view:"editor3d"` shows the preview's free 3D viewpoint instead (see [3D viewport](#3d-viewport)).

Homepage endpoint: `POST /mcp` with `Authorization: Bearer <token>`.

Pinned endpoints (`/mcp/timeline`, `/mcp/project`, …) list that toolbox’s ops directly. `catalog`, `search`, `toolbox`, and `apply` are only on `/mcp`; `inspect`, `capture`, `frames` and `activity` work on both. Toolbox ops can also be called by name directly on `/mcp` instead of through `apply` — but only `apply` collapses a batch into one undo step.

`apply` takes **toolbox ops only**. The eight homepage tools return `unknown_op` inside an `ops` array; call them directly. `get_waveform({image:true})` is also refused inside `apply`.

## Conventions

| Topic | Rule |
|-------|------|
| Time | Seconds |
| Clip reference | Prefer `clip` UUID from `inspect`; else `track` (0 = top) + `index`. One or the other is **required** — clip ops never fall back to the selection. Read the current selection from `inspect.selection` |
| Selection ops | `separate_audio`, `unlink_audio`, `merge_clips`, `align_clip_left/right`, `copy_selection`, `cut_selection` take no clip argument — call `select_clip` first. `merge_clips` joins two abutting cuts of the same media, or two or more subtitle clips on one track (`select_clips`; gaps allowed, the earliest clip's style and transform win); `merge_clips({track})` merges every subtitle clip on that track without a selection. `freeze_frame` and `paste_at_playhead` are playhead-based (seek first); they do not use the selection |
| Discovery | Effect stack indices, transition ids, and bookmark indices exist **only** in `inspect({clips:true, detail:true})`. Subtitle cues: `inspect({clips:true, cues:true})` or the `subtitleCues` field of a detail row. Mask/fade/speed/volume/keyframes/`hasFaceTrack`/stabilize* are on the same detail rows. **Detail rows omit what does not apply**: caption styling only on text/subtitle clips, shape styling only on shapes, `vector` (document metadata, slot overrides — never the document text) only on Lottie/SVG clips, `mask` only when one is set, `keyframes` only for animated properties (listed in `animated`), stabilize/animation blocks only when in use, fade fields only when a fade is set, empty arrays, false booleans and other defaults dropped (absent = default: no fade, `speed` 1, `volume` 1, unlinked). Every row carries `transform:{x,y,w,h,rotation,opacity}` at the playhead, plus `layer3d`, `rotationX`, `rotationY`, `z` and `perspective` on a clip switched to a 3D layer (see [3D clip transforms](#3d-clip-transforms)). `verbose:true` returns the raw untrimmed map |
| Numbers | Every number in a reply is rounded to 3 decimals, except `fps` and speed-curve `pos` which keep 6 |
| Validation | Toolbox op args are checked against the op's schema before it runs (the homepage tools `inspect`, `capture`, `frames`, `activity` are not): a missing required key → `bad_args` naming it; a wrong JSON type → `type_mismatch` (numeric strings like `"1"` are still accepted); an enum or declared min/max violation → `bad_args` listing the allowed values or range. Unknown keys are not errors; they come back as `ignored:[…]` on success. A clip-ref op with neither `clip` nor `track`+`index` gets `bad_args` saying so; a stale uuid gets `not_found` with a hint to re-read `inspect` |
| Effects | `list_effects` / `list_audio_effects` / `list_transitions` are compact by default (`cats:{<cat>:[{id,label}]}`); pass `id`, `cat`, or `q` to get parameters. Ids are accepted with `.` or `_` interchangeably; an unknown id returns `not_found` with the closest matches. Effect stacks live on an **adjustment clip linked to the target**, created on its own lane the first time; `add_effect`/`add_audio_effect` report it as `host:{track,index,clip}`. Keep addressing the original clip in every effect op, and expect that extra lane in `inspect` |
| Images | `capture`, `frames`, and `get_waveform({image:true})` return a text block (JSON meta) followed by an image block. Times are always in the text block; never rely on burned-in labels alone. Sheets fit one vision image (≤1456 px long edge) |
| Overlap | Off by default — place/move snap to gaps unless `set_overlap` enables overlap; the reply reports `requested` vs `placed`. With overlap off, moving several clips toward zero must be sequenced **back-to-front**. `set_ripple` / `close_gap` close holes after a delete |
| Composites | `make_composite` (selection-based) moves the selected clips into a nested timeline and leaves one `composite` clip plus a bin item. `open_composite({clip})` / `({sequence})` makes that timeline the one every op edits; `open_composite({main:true})` returns. Adding content past a composite's end never lengthens placed instances, and shortening the content never shortens them (or the adjustment clips hosted on them) — trim them with `set_duration`; their trim limit follows the content. `set_clip_speed` retimes a composite as a unit: on an untrimmed instance, `speed = currentDuration / target` makes it end at `target`. `set_speed_curve` also works on composites. One level only: no composites inside composites. `flatten_composite` renders the clip to a video file (async, poll `export_status`) and swaps it in. `export_video` always renders the main timeline |
| Export | `export_video` is async — poll `inspect().export` or `export_status`. The output path is normalised, so use the `path` echoed back. Named sizes: `list_export_presets` + `export_with_preset` |
| Atomicity | `apply` is **not** atomic: on failure the ops before it stay applied. The failure reply is `{ok:false, error:"apply_failed", stopped:<index>, tool, failed:<that op's error>, done:[results of the ops that ran]}` — `done` never contains the failed op. An ops array cannot reference an id produced earlier in the same batch — end the batch after `set_speed_curve` |
| Undo | One batch = one undo step. Linear history (no branches): `list_history({limit})` returns the newest `limit` (default 20) versions as `{index, label, short}` plus the HEAD `hash`/`short` and `total` (index 0 = Origin). `undo_to({index})` or `undo_to({hash})` jumps (a ≥8-char prefix such as `short` works); the next edit drops redo. `take_snapshot` writes compact project JSON named `<hash>.json` (file SHA-256 = history hash). Ops outside the stack (and all read-only ops): `import_media`, `import_media_bytes`, `seek`, `play`, `pause`, `undo`, `redo`, `undo_to`, `take_snapshot`, `restore_snapshot`, `set_overlap`, `set_ripple`, `set_snap`, `set_loop_work_area`, `export_video`, `save_project`, `quit_instance`, `set_theme`, `export_theme`, `import_theme`, `select_theme`, `set_preview_view`, `set_shortcut`, `reset_shortcuts`, `set_beat_layers`, `detect_beats`, `list_speed_curve`, `list_fade_curve`, `install_addon`, `cancel_addon_install`, `set_acceleration`, `switch_angle`, `end_multicam`, `market_install` (alias `market_download`), `market_cancel_download`, `market_retry_download`, `market_open`, and the preset-store ops `rename/delete/export/import_user_text_preset`, `rename/delete/export_text_animation_preset`, `import_effect`, `rename/delete/export/import_effect_preset`. **`set_beat_layers` changes the user's own snapping and cannot be undone** |
| Errors | `{ok:false, error:<code>, detail:<text>}` — `bad_args`, `not_found`, `type_mismatch`, `unknown_op` (the detail suggests the nearest op names and their toolbox), `unknown_toolbox`, `wrong_endpoint`, `wrong_toolbox`, `apply_failed`, `import_failed`, `import_timeout`, `export_busy`, `export_failed`, `export_timeout`, `capture_failed`, `conflict`, `unsupported` (a build without the vector renderer), and from the market toolbox `market_unavailable`, `consent_required`, `market_error`, `rate_limited`, `app_too_old`, `quota_exceeded`, `install_timeout`, `download_failed`. `type_mismatch` is also the answer when an op gets the wrong **kind** of clip (a shape to `apply_text_preset`, a title to `set_shape_style`, an image to `set_clip_orientation`) |
| UI themes | No op edits theme tokens. `export_theme({path, name, theme?, mode?})` writes a copy (`copy`: every resolved token; `starter`: `extends` + core palette, metrics, typography, motion, window) to a file; edit it with your own file tools, then `import_theme({path, replace:true, select:true})`. Import errors name the bad key; fix and re-import. `list_themes` / `select_theme` switch between installed themes. Built-in ids (`dark`, `light`, `graphite`, `frost`) and `system` are reserved and cannot be imported or removed. Format: `docs/theming.md` |
| Change detection | Every `inspect` includes `revision`; pass `since:<revision>` to get `{unchanged:true}` when state is current |
| Media import | Absolute paths or `file://`, or `import_media_bytes` (base64 — always pass an explicit `path`). **No directory listing.** If the user gave a fuzzy name (`GX010023.mp4` in Downloads), glob/search with **your own filesystem tools**, then pass the hits to `import_media` and confirm `missing:[]` is empty |

### Async jobs

All return `{started:true}` immediately. Every field below except `export` lives under `inspect({detail:true}).jobs`, which lists only the jobs that are running (the whole `jobs` key is absent when nothing is).

| Started by | Poll |
|---|---|
| `export_video` | `export.{active, progress}` (or `export_status`) |
| `package_project` | `jobs.package.{active, progress}` |
| `generate_subtitles` | `jobs.subtitleGen.{active, progress, status}` |
| `set_clip_reverse` (video) | `jobs.reverseRender.{active, progress, status}` |
| `market_install` | `jobs.downloads.{active, byKind}` (every running download, add-ons and updates included); per-job status, progress, error and result from `market_downloads` |
| `detect_scenes` | `jobs.sceneDetect.{active, progress, status, clip, scenes}` (also present, inactive, while a scan result is loaded) |
| `run_segmentation`, `segment_clip`, `apply_denoise`, `detect_faces` | No progress field — re-read `inspect({clips:true, detail:true})` and compare |
| `stabilize_clip` | Per-clip `stabilizing` / `stabilizeProgress` / `stabilizeStatus` on the detail clip row |
| `transcribe`, `diarize`, `tts_generate`, `sfx_generate`, `add_voice`, `voiceover_from_subtitles`, `estimate_depth` | Return `{job_id}` instead: poll `get_job({id})` until `active` is false, then read `ok` plus `result` or `error`. `cancel_job({id})` stops one. `inspect({detail:true}).jobs.list` keeps recent jobs, finished ones included |

## Toolbox reference

| Toolbox | When to use |
|---------|-------------|
| `media` | Import (paths/bytes, optionally into a bin `folder`), list, rename, remove, replace, export still, bin folders (`list_folders`, `create_folder`, `move_asset_to_folder`, `rename_folder`, `delete_folder`), `set_asset_rotation` (lossless orientation override for a sideways video), PDF clips (`inspect_pdf`, `set_pdf_options`) — see [PDF documents](#pdf-documents) |
| `timeline` | Tracks, clips, selection, ripple/gap, bookmarks, copy/paste, A/V link |
| `canvas` | Transform (including 3D tilt, depth and perspective), flip, blend, mask, fade, speed, reverse, animation, stabilisation, `set_clip_orientation` (lossless 0/90/180/270 for a video clip), shape styling (`set_shape_style`: the same shading-layer stack captions have — `layers` / `layer` patches, `add_shape_layer` … — plus geometry knobs; the legacy flat `fill`/`stroke` keys still land on the `fill`/`stroke` layers) — see [Shapes](#shapes) |
| `playback` | Seek, play, pause, In/Out work area |
| `text` | Title and caption clips, style packs (`list_text_presets`, user presets), fonts, shading layers (fill/stroke/shadow/glow/extrude), gradient presets and shader effects, looks, In/Out/Loop animation presets, Lottie preset import |
| `shapes` | Builtin shapes, stickers, emoji |
| `motion` | Lottie animations and SVG drawings as vector clips: add, inspect, swap the document, re-theme through slots or the `svg.*` element overrides |
| `model3d` | 3D models (glTF binary `.glb`) as model clips: add, inspect, pick the animation, pose and light them — see [3D models](#3d-models) |
| `subtitles` | Subtitle clips, cues, import/export, Whisper generation |
| `effects` | Video/audio effects, transitions, templates, effect clipboard, writing new ones into My Effects — see [Custom effects](#custom-effects) |
| `project` | Open/new/save/package, canvas, background, metadata, export |
| `keyframes` | Property animation keys and tangents — clip transform, `fx.<i>.<param>`, `mask.<key>`, on text/subtitle clips `text.<key>` (pixelSize, letterSpacing, lineHeight, boxPadding, pathBend) or `text.layer.<id>.<field>` (opacity, offsetX, offsetY, blur, width, spread, trimStart, trimEnd, dashOffset, sketchLength, sketchDeviation, color.r/g/b/a, gradient.angle/offset/scale/center.x/y, gradient.stop.n.pos, effect.<param> for an effect paint's scalar params); the old names outlineWidth, shadowBlur, glowRadius, gradientAngle, color.r… still resolve onto the stroke/shadow/glow/fill layers. On shape clips the same layer fields as `shape.layer.<id>.<field>` (a fresh shape's layers are `fill` and `stroke`) plus `shape.<cornerRadius|points|innerRatio|headSize|thickness|tailX|tailSize>`; on SVG clips `vector.svg.…` (see Motion); on PDF clips `pdf.scrollX`, `pdf.scrollY` (page units) and `pdf.zoom` (0.1..10) (see PDF documents); on 3D model clips `model3d.ambient` and `model3d.light<n>_<x\|y\|z\|intensity\|radius\|cone\|aimX\|aimY>` (n 1..4), with the model placed by the clip transform (see 3D models). `set_keyframe` on a property the clip does not have fails `bad_args` |
| `speed` | Speed ramps; reading custom fade curves (write them with `set_fade_curve` in `canvas`) |
| `segmentation` | SAM-style cutout (session or one-shot) |
| `ai` | Denoise, face detection, auto-reframe, depth estimation for the depth effects (`estimate_depth`, `sample_depth`, `clear_depth`) — see [Depth effects](#depth-effects), add-on install |
| `audio` | Waveforms, silence, loudness, ducking, beat detection, beat-synced cuts, clip volume |
| `scene` | Shot detection, what is in each shot, scene-synced cuts |
| `ui` | Theme, shortcuts, editor preferences, guides |
| `multicam` | Multi-camera session: set up, switch at the playhead, save |
| `market` | The Cutwire marketplace: stock media, Drift assets, sound effects and community add-ons; browse, preview, install (optionally onto the timeline), manage downloads |
| `transcript` | Word-level transcripts stored on each asset, reading them (`get_transcript`), cutting by words (`cut_words`) or source ranges (`keep_ranges`), building a sequence from an edit list (`assemble`), speaker labels (`diarize`) — see [Editing speech](#editing-speech) |
| `voice` | ElevenLabs / Fish Audio voiceover (`tts_generate`), ElevenLabs sound effects (`sfx_generate`), `list_voices`, `cloud_provider_status` — see [Cloud voices](#cloud-voices); local Chatterbox TTS, voice library and subtitle voiceovers — see [Local voices](#local-voices) |

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

### Seeing the footage

Agents cannot watch video, so the three read tools give a text-then-image path from “where does
something happen” to “what is it”:

1. **`activity({start, end})`** — text only. Samples the composition (default 200 frames across the
   range, tiny render) and returns three arrays plus the strongest peaks:

   ```json
   {"ok":true,"space":"timeline","start":0,"end":12,"step":0.06,"n":200,"scan":[64,36],
    "content":[0,1.2,0.9,…],"motion":[0,0.02,…],"audio":[0.41,…],
    "peaks":[{"t":3,"content":69.8},{"t":6,"content":67}],"max":{"content":69.8,"motion":0.6,"audio":0.9}}
   ```

   `content` is the mean HSV change between consecutive samples on a 0..255 scale — the same
   metric and scale as `detect_scenes` (27 is a cut at full frame rate). `motion` is the fraction
   of pixels that moved, `audio` the mixed peak, both 0..1. At coarse steps a fast pan scores like
   a cut, so these are *peaks*, not cuts.
2. **`frames({…})`** — one JPEG contact sheet, about the cost of a single `capture`. Each tile has
   its index and time burned in, and the same times come back in `frames[].t`.

   | Mode | What it renders |
   |---|---|
   | `sample:"changes"` (default) | Up to `n` (12) visually distinct frames. Candidates are hashed (dHash) at ~4 per second, capped at 120; a frame is kept only if it differs from the last four kept by more than `min_change` bits (default 12; in-shot motion scores ~5–10, a cut 30+). `skipped` says how many looked the same; `next:{start,end}` appears when more distinct frames remained than `n` allowed |
   | `sample:"uniform"` | `n` evenly spaced frames |
   | `sample:"scenes"` | The `detect_scenes` thumbnail of every scanned shot in the range (`frames[].clip`, `frames[].scene`); unscanned clips contribute their first frame and are listed in `unscanned` |
   | `at:[…]` | Exactly those times (max 20) |

   Add `clip` (or `track`+`index`) to render one clip's **source** frames instead of the
   composition: `t` is then source seconds and `tl` the timeline time. `return:"path"` writes the
   sheet next to the project's frame captures instead of inlining it. `diff` on each frame is the
   dHash distance to the previous kept tile. `dur` is the length of the material; a tile past it
   renders black and carries `beyond_end:true` (the count is repeated at the top level).
3. **`capture({at})`** — one 1280 px still for detail. `beyond_end:true` flags a time past the
   timeline end.

For audio, **`get_waveform({image:true, …})`** returns a PNG with a mixed-peaks lane, a
speech-band lane, silent stretches shaded, onset ticks when `detect_beats` is current, a lane
of the transcript's words when the media is transcribed (indices in the reply's `words`), an
optional `spectrogram:true` lane, and a time axis — together with a `summary_buckets` (50)
numeric summary and the `silence` ranges it found. In `clip` mode the image form is
timeline-space (through the clip's volume and fades), unlike the numeric clip form.

Typical recipe: `activity()` → `frames({at: peaks})` or `frames()` → edit → `capture({at})`.

### Editing speech

The cheapest way for an agent to understand talking footage is to read it, not watch it. Transcribe
each source once, read the words with their times, cut between words, and look at pictures only
at the cuts.

1. **`transcribe({clip})`** (or `{asset}`, or `{clips:[…]}`) transcribes the **whole source file**
   behind the clip and stores the result on the asset, in source time. It survives every later
   trim, split, reorder and undo, and is saved with the project; calling it again returns
   `cached` unless `force:true`. Async — poll `get_job`.

   | `engine` | What you get | Needs |
   |---|---|---|
   | `local` (default) | Whisper words, each placed by a CTC forced aligner (real word start/end), Silero VAD keeping Whisper off silence, speaker labels with `diarize:true` | `whisper-model`; `align-model` for the language (word timings, else interpolated and `aligned:false`); `vad-model` and `diarize-model` optional |
   | `elevenlabs` | Scribe: verbatim (fillers like "um" kept and tagged), audio events like `(laughter)`, speakers with `diarize:true`, `keyterms` to bias names | The user's key and consent (Settings → Cloud providers). **Billable** |

   Whisper tends to drop fillers, so a local transcript tags only the ones it kept.
2. **`get_transcript`** reads it three ways:
   - `{asset}`: the whole take in **source seconds**, which is what `keep_ranges` and `assemble` take.
   - `{clip}`: only what that clip plays, in **timeline seconds**.
   - `{start, end}`: everything audible on the timeline in that range.

   The default `view:"phrases"` breaks lines on a pause of at least `break_on_silence` (0.7 s) or
   a speaker change, and adds a `compact` string, `[12.40-15.10] S1 text` per line, which is the
   cheapest read of a long take. `view:"words"` lists every word with a stable index `i` into the
   asset's transcript, plus `type` (filler/event), `speaker`, `conf` and `estimated` (the aligner
   could not place it). Page through with `offset`/`limit`/`next_offset`.
3. Cut:
   - **`cut_words`** removes word runs (`words:[[i,j]]`) or text (`text:"um"`, or `match:"phrase"`
     for a phrase). `snap:"boundary"` also takes the pause around the removed words, keeping
     `padding` (0.05 s) of air beside the words that stay. `snap:"silence"` cuts at the quietest
     point of each gap instead. `dry_run` reports the plan without editing.
   - **`keep_ranges`** rebuilds one clip from source ranges, in any order: its edit decision list.
   - **`assemble`** places `[{asset, start, end}]` from several sources back to back (the best take
     of each beat), appended to a track.
   - **`remove_silence`** with `method:"vad"` cuts only where nobody speaks; energy thresholds
     mistake music and room noise for speech.

   All of these:
   - cut linked video and audio together;
   - ripple what follows;
   - give every cut edge a short audio-only de-click ramp (`declick`, 0.03 s; it never fades the
     picture);
   - leave one undo step.
4. Check each cut. `get_waveform({image:true})` draws a lane of the transcript's words under the
   waveform (the reply's `words` carry their indices), and `frames({at:[…]})` shows the picture on
   both sides.
5. **`generate_subtitles`** on transcribed media builds captions straight from the stored words,
   instantly and on the exact word times, however the clips were cut. On media without a
   transcript it runs Whisper as before.

`diarize({clip})` labels speakers on its own (and relabels an existing transcript's words). A
butt cut between two clips' audio can take a real crossfade: `add_transition` works on audio
tracks. With media past the cut on both sides it is a true crossfade; a side with no media dips
through silence instead.

### Cloud voices

The `voice` toolbox needs the user's own ElevenLabs or Fish Audio account. Keys live in Settings →
Cloud providers, or in `ELEVENLABS_API_KEY` / `FISH_API_KEY` for headless runs. Nothing is sent
until the user also turns on that provider's consent switch.

- **`cloud_provider_status`** says what is ready. Keys are never returned.
- **`list_voices({provider})`** lists voice ids.
- **`tts_generate({provider, text, voice})`** and **`sfx_generate({prompt, duration})`** write an
  MP3 under the app data folder, import it into the bin (the asset records what made it), and
  with `place` put it on a free audio lane.
- Every call is billable. `not_configured` and `consent_required` errors say what to ask the user.

### Local voices

`tts_generate({provider:"local"})` speaks with Chatterbox on this machine: free, no key, no consent
switch. It needs the `tts-model` addon (`addon_missing` otherwise), and the first run loads the
model, which takes about a minute.

- **`local_tts_status({warm_up})`** returns `{available, model_loaded, loading, running, addon_id,
  languages, last_voice, last_language, last_exaggeration}`. `warm_up:true` starts loading the model
  without waiting.
- **`tts_generate({provider:"local", text, voice, language, exaggeration, place})`** takes a
  `list_local_voices` id (default: the last one used), a language code from `local_tts_status`, and
  an exaggeration of 0.25 to 2. Unlike the cloud providers, `place` defaults to off, so the speech
  lands in the bin only; `place:true` or `{at, track}` also puts it on the timeline. One local job
  runs at a time (`busy`). The job result is `{asset, clip?, duration, path}`.
- **`list_local_voices`**, **`add_voice({name, path | clip, in, out})`**, **`rename_voice`**,
  **`remove_voice`** manage the voice library. A sample is 3 to 20 seconds of clean speech; `in` and
  `out` are seconds in the source file, and a clip defaults to its trimmed range. `add_voice` is
  async and its job result is `{voice, name}`. The built-in `default` voice can't be renamed or removed.
- **`voiceover_from_subtitles({clip, voice, language, exaggeration})`** speaks every caption of a
  subtitle clip at its cue time on new audio tracks, as one undo step. Async; the job result is
  `{placed, longer}`.

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
| `describe_clip({clip})` | One-call impression: shot count, shortest/longest, mean score, top shots, and labels ranked by **screen time**. `clip` (or `track`+`index`) reads any scanned clip's cache; omit it for the last scanned clip |
| `list_scenes({clip, sort:"score"})` | Every shot, with `timeline_start`/`timeline_end` already mapped through trim, speed and reverse, and `thumb`/`timeline_thumb` for its representative frame. Takes the same optional clip ref as `describe_clip` |
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

### Motion graphics

The `motion` toolbox puts Lottie (Bodymovin JSON) animations and SVG drawings on a graphic
track as **vector clips**: they are drawn by Skia at whatever size the clip box has, follow the
clip's speed/reverse, and take effects, masks and transitions like any other clip. Builds
without the vector renderer fail every `add_*` op with `unsupported`.

| Call | Effect |
|---|---|
| `import_media({paths})` | Also takes Lottie `.json` files and `.lottie` bundles (unpacked into app data, one asset per animation); the asset then places like any other with `place_clip` |
| `add_lottie({json, at, track, duration, fit, loop, offset, slots, name})` | `json` is the document text (inline, ≤ 8 MB) or an absolute `.json` path. Plays once at its own length unless `duration` is set; `loop` (`hold` default, `loop`, `pingpong`, `hide`) decides what happens past the end; `fit` (`contain` default, `cover`, `stretch`) how it fills the box. Returns `{id, track, index}` plus the inspect summary |
| `add_svg({svg, at, track, duration, fit, slots, name})` | Same for an SVG still (default 5 s). SMIL animation and scripts are ignored and reported in `unsupported`. Returns `elements:[{id, tag, classes, inDefs, fill, stroke, strokeWidth, opacity}]`, the ids the `svg.<id>.*` overrides address |
| `inspect_lottie({json \| svg \| path \| clip})` | Read-only. `{version, fps, durationSec, width, height, layers, slots, namedProperties, fonts, markers, expressions, unsupported, hints}`; for an SVG also `elements` |
| `set_lottie_source({clip, json \| svg \| path})` | Swap the document; position, length, fit and loop stay, slot overrides survive only where the new document declares the same slot with the same type |
| `set_lottie_options({clip, fit, loop, offset, name})` | Playback options; only supplied keys change |
| `set_lottie_slot({clip, name, value})` | Override one declared slot; `value:null` clears (and drops its keyframes). Typed: color `"#rrggbb"`/`"#aarrggbb"`/name/`[r,g,b,a]` in 0..1, scalar number, vec2 `[x,y]`, text string, image path. On an SVG the names are the reserved override keys below |
| `list_lottie_slots({clip})` | Declared slots with current overrides; for an SVG the four drawing-wide keys plus the element overrides that are set |
| `get_lottie_source({clip})` | The document text (can be large) — edit it and send it back with `set_lottie_source` when the animation has no slots |

Slots are the templating mechanism: an animation exported with slots (Lottie ≥ 5.10, After
Effects "Essential Properties") can be re-coloured or re-worded per clip without touching its
JSON. `capture` / `frames` render vector clips like everything else, so check the result at two
times before relying on an animation.

An SVG declares no slots; it is restyled through reserved keys instead. `svg.fill`, `svg.stroke`
(colour), `svg.strokeWidth`, `svg.opacity` (number) act on the whole drawing — drawing-wide colours
replace paints the file already has, so a `fill="none"` outline stays hollow and no stroke is
added where the file drew none. `svg.<elementId>.<fill|stroke|strokeWidth|opacity|visible>` act on
one element from `elements` (ids are case-sensitive; `visible` is 0/1). Everything but `visible`
keyframes as `vector.svg.…` — scalars directly, colours per channel (`vector.svg.logo.fill.r`,
one `set_keyframe` per channel, 0..1). An `.svg` dropped in the bin (`import_media`) is a vector
asset and places as a vector clip.

### 3D clip transforms

Every visual clip except a 3D model clip can be made a **3D layer**, then tilted and pushed in
depth. The switch is per clip (`layer3d`, off by default; the inspector's "3D layer" switch).
Four more transform properties sit beside `x`/`y`/`w`/`h`/`rotation`. Writing any of them, through
`set_transform` or `set_keyframe`, turns the switch on, and a clip loaded with any of them set comes
up switched on. `set_transform({layer3d:false})` flattens the clip: all four go back to their
defaults (keys included; `undo` brings them back). `set_transform` writes them at the playhead (the same keyframe rules apply),
`set_keyframe` animates them under the same names, and `reset_transform` clears them and switches
3D off:

| Property | Default | Meaning |
|---|---|---|
| `rotationX` | 0 | Degrees; + tips the top edge away from the viewer |
| `rotationY` | 0 | Degrees; + swings the right edge away from the viewer |
| `z` | 0 | Project pixels; − pushes the clip away (smaller), + pulls it toward the viewer |
| `perspective` | 2000 | Distance of the eye from the canvas, in project pixels; smaller = stronger foreshortening |

The clip turns about its own centre. Rotations are intrinsic: X, then Y, then the existing
`rotation`, so `rotation` spins the clip within its tilted plane. The eye sits `perspective` px in
front of the **canvas centre**, so every clip is seen by the same virtual camera. As a result, a clip
off-centre drifts toward the centre as `z` decreases. At `z = −perspective` it is half size, and at
`z ≥ perspective` it is behind the eye and draws nothing.

Masks and effects are applied in the clip's own frame before the tilt, so they follow it. Clip
intro/outro animations and text animations stay flat, on top of the tilt. A clip whose 3D values
are all zero renders exactly as before. `inspect` and
`set_transform` readouts include `layer3d` and the four fields only on a 3D layer.

### Transform layers

A **transform layer** moves, scales, rotates, tilts and fades several tracks as one without nesting
them in a composite. It is an adjustment track with scope `range`: it covers every video, text,
subtitle and graphic track from just below it down to its **span end**, and each clip on it is a
transform clip (`adjustmentKind: "transform"`) whose ordinary transform is applied as a parent to
every clip it covers. Per-clip transforms stay as they are; a covered clip renders as
parent × its own transform, so its `x`/`y`/`w`/`h` are still measured inside the canvas and the
layer carries them.

- `make_transform_layer({clips:[ids]})` adds a layer directly above the topmost of those clips'
  tracks, spanning to the lowest, with one clip over their time range. `add_track({type:"transform",
  span_end?})` adds one at the top instead (default span: every visual track below), with an identity
  clip over the whole timeline.
- `set_transform` on the transform clip moves the group. Its box defaults to the full canvas, which is
  the identity; shrink `w`/`h` to scale the group, set `rotation`, or `layer3d` with `rotationX`/
  `rotationY`/`z` to tilt it as one flat card. `opacity` multiplies into every covered clip.
  `set_keyframe` and clip intro/outro animations work on it like on any clip.
- `set_transform_span({track, end | end_clip})` changes the span. The end must be a visual track
  below the layer; it may not cut through a nested layer's span or reach past an enclosing one.
- Layers nest: a layer inside another's span is itself moved, and composes outer × inner.
- `set_track({hidden:true})` on a layer bypasses it: the covered tracks stay visible, unmoved.
- `inspect` rows: adjustment tracks report `scope`, lanes `parent`, a layer `span_end` and `covers`,
  and any covered track `transformedBy` (layer indexes, outermost first). `set_transform` and
  `reset_transform` on a covered clip report `parented`.

Limits: opacity is multiplied per clip, so overlapping covered clips show through each other (a true
group fade still needs a composite); standalone effect adjustments inside the span, and their
masks, are not moved; a transition mixes after both of its sides have been moved; and content a clip
places off-canvas becomes visible when the group is scaled down.

### 3D models

The `model3d` toolbox puts a glTF binary (`.glb` only — a `.gltf` with sidecar files would not
survive bundling) on a graphic track as a **model clip**. A model clip is a 3D layer like any
other: it sits in the same world as tilted clips, is seen through the scene camera when there is
one, and takes opacity, fades, blend modes, effects, masks and transitions. Track order decides
stacking unless its depth occlusion is on (the Transform tab's "Occlude by depth"), in which case
it sorts by depth against the other occluding 3D layers.

| Call | Effect |
|---|---|
| `import_media({paths})` | Also takes `.glb` files; the asset places with `place_clip` like any other (its duration is the first animation's length, 5 s for a static model) |
| `add_model3d({path, at, track, duration, animation, loop, offset, scale, depth, rotX, rotY, rotZ, ambient, lights, name})` | Absolute `.glb` path. Returns `{id, track, index, animations:[{name, durationSec}], vertexCount, warning?, model3d:{…}}` |
| `inspect_model3d({path \| clip})` | Read-only. `{animations, vertexCount, primitiveCount, materialCount, textureCount, warning}` — `warning` says what the loader skipped (Draco compression, extra material textures, morph targets) |
| `set_model3d_source({clip, path})` | Swap the file; transform, length, lighting and keyframes stay, the animation index clamps to the new file |
| `set_model3d_options({clip, animation, loop, offset, scale, depth, rotX, rotY, rotZ, ambient, lights, name})` | Plain (non-keyed) values; only supplied keys change |

Placement: the clip's rect (`x`, `y`, `w`, `h`) is the front face of the model's bounding box,
`z` pushes it in depth, and `rotationX`/`rotationY`/`rotation` turn it about its centre exactly
as they turn a tilted clip (intrinsic, X then Y then the in-plane spin, canvas axes: +y down).
`perspective` is the eye it is seen through when no scene camera is running. Set them with
`set_transform` or keyframe them like any clip. A new model starts centred with its largest
extent half the canvas height. `scale`, `depth` and `rotX`/`rotY`/`rotZ` are shorthands kept from
before models were 3D layers: `scale` resizes the box about its centre to that fraction of the
canvas height, `depth` 0..1 sets `perspective`, and `rotX`/`rotY`/`rotZ` (degrees about the
model's glTF axes, +y up) write `rotationX = -rotX`, `rotationY = rotY`, `rotation = -rotZ`; as
keyframes, `model3d.rotY` and friends key those clip properties. Lighting is `ambient` 0..1
plus up to four point or spot lights, `lights: [{enabled, x, y, z, color, intensity, radius,
cone, aimX, aimY}, …]` by index (give only the fields to change). They are placed like 3D
Relight's: `x`/`y` across the clip's rect (0..1 spans it), `z` in clip heights from the model's
centre (negative is nearer the viewer), `radius` the reach in clip heights, and a spot (`cone`
under 180) points at `aimX`/`aimY` on the plane through the model's centre. They follow the
clip's position and size but not its rotation. The numbers keyframe as `model3d.ambient` and
`model3d.light<n>_<field>`; the 3D preview draws the lights and drags them. Projects from before
format 15 had one directional key light, which loads as light 1. Shading is a simple Blinn-Phong on the base colour — normal, roughness and
occlusion maps are ignored, so a model reads flatter than in a PBR viewer.

An animated file lists its clips in `animations`; `animation` picks one by index, the clip's
speed/reverse and `offset` remap into it, and `loop` (`loop` default, `hold`, `pingpong`, `hide`)
decides what happens past its end. Node (rigid) animation and skeletal skinning play; morph
targets do not (reported in `warning`). `capture` / `frames` render model clips like everything
else.

### 3D viewport

The preview panel has a 2D mode (the camera's picture) and a 3D mode: the world from a free
viewpoint, with the scene camera body and the stage drawn in it. The viewpoint is editor
session state, never saved or exported, and its changes are not undoable. `set_preview_view`
(ui toolbox) drives it; only supplied keys act, in this order:

| Key | Effect |
|---|---|
| `mode` | `2d` or `3d` |
| `reset` | back to the overview viewpoint |
| `axis` | `front`, `back`, `left`, `right`, `top` or `bottom` view of the current target (leaves look-through) |
| `frame_selection` | frame the selected clip, or the whole stage when nothing is selected |
| `orbit` | `[dx, dy]` degrees |
| `pan` | `[dx, dy]` canvas px |
| `dolly` | wheel steps; + moves toward the target |
| `look_through` | `true` views through the scene camera (what exports), `false` returns to the free viewpoint |

It returns `{mode, look_through, target:[x,y,z], yaw, pitch, distance, fov, eye:[x,y,z]}`; a bad
`mode`, `axis` or array shape is `bad_args`. `inspect` reports `preview_mode` (and `look_through`
while in 3d). `capture({view:"editor3d"})` renders that viewpoint, or the overview if the view was
never placed, at the same size rules as a normal capture; the reply adds `view`. `frames` has no
`view` option, and the selected clip's box is not drawn in the capture.

### Scene camera

`add_camera` puts a Camera clip on the camera lane (the whole sequence, or one clip at `at` for a
cut); a camera at rest looks exactly like no camera. It is posed by the clip's transform props, all
keyframable: `x`/`y`/`z` translate it in its own frame (canvas px; +x right so the picture goes
left, +y down, +z back from the scene), `rotationX`/`rotationY` pitch and yaw (degrees; -`rotationX`
looks up, +`rotationY` looks left), `rotation` rolls (degrees, clockwise), `perspective` is the
eye distance (px). A bare rotation orbits the eye about the canvas centre; the tools below turn
the camera in place by re-solving `x`/`y`/`z` so the eye stays put. World space is canvas px about
the canvas centre, y down, +z toward the viewer.

| Call | Effect |
|---|---|
| `get_camera_state({at?, clip?})` | Read-only. `{active, at, clip, track, x, y, z, rotationX, rotationY, rotation, perspective, eye:[x,y,z]}`; `active:false` when no camera covers `at` (default the playhead) |
| `reset_camera({clip?})` | Clears pan, dolly, pitch, yaw, roll and their keys; the lens stays. One undo step; `not_found` without a camera |
| `camera_look_at({target, at?, keyframe?, clip?})` | Aims the camera at a clip id (its centre, with `z`; transform-layer parents ignored) or a world point `{x,y,z}`. Writes `rotationX`/`rotationY` and `x`/`y`/`z` (eye held); keys at `at` with `keyframe:true` or on properties that already animate |
| `camera_move({move, start, end, amount?, easing?, target?, clip?})` | Start and end keyframes from the pose at `start`; one undo step |

`camera_move` moves and the default `amount`: `dolly_in`/`dolly_out` 300 px along the view;
`truck_left`/`truck_right` 200 px; `crane_up`/`crane_down` 200 px; `pan_left`/`pan_right` 20 and
`tilt_up`/`tilt_down` 15 degrees, in place; `orbit_left`/`orbit_right` 45 degrees around `target`
(clip id or `{x,y,z}`, default the canvas centre) keeping it centred; `push_in`/`pull_out` scale
`perspective` by `1-amount` / `1+amount` (fraction 0..0.9, default 0.35), which changes perspective
strength, not framing. `easing` is `ease` (default) or `linear`. Orbit interpolates the pose
linearly, so a sweep beyond about 90 degrees cuts a chord; split it into several moves. `start`..`end`
must lie inside the camera clip, and without a camera the tools fail `not_found` ("call add_camera").

### PDF documents

A `.pdf` goes through `import_media` like any file and places with `place_clip` as a **PDF clip**
on a graphic track (5 s by default). The clip's frame is a fixed viewport; the pages are scrolled
and zoomed through it. Rendering needs the `pdfium` addon: without it the clip still places but
`pageCount` is 0 and nothing draws. `inspect_pdf` reports `addonAvailable` and `addonId`; install it
with `install_addon({id:"pdfium"})` (find it with `list_addons`), after which clips fill in their
page list on their own.

| Call | Effect |
|---|---|
| `inspect_pdf({path \| clip})` | Read-only. `{pageCount, addonAvailable, addonId}`; with `clip` also `firstPage, lastPage, layout, gridColumns, gap, scrollX, scrollY, zoom, keyframes` |
| `set_pdf_options({clip, firstPage, lastPage, layout, gridColumns, gap})` | Only supplied keys change. `firstPage` 1-based, `lastPage` 0 = through the end (both clamp to the document once probed), `layout` `column` \| `row` \| `grid`, `gridColumns` 1..20, `gap` 0..200 PDF points |

Scroll and zoom are keyframes: `pdf.scrollX`, `pdf.scrollY` and `pdf.zoom` (0.1..10, 1 fits the
cross axis of the layout). Scroll counts pages (column and row layouts) or rows/columns (grid):
`pdf.scrollY = 2.5` in a column puts the top of the third page half-scrolled at the viewport top.

### Depth effects

Five effects in the `depth` category read a clip's estimated depth (relative, 0 = farthest thing in
the clip, 1 = nearest, consistent across the clip) and pass the frame through until it exists:

| Effect | What it does | Handy parameters |
|---|---|---|
| `depth.relight` | Up to four 3D point/spot lights that fall on the scene, with shadows | `ambient` (how much of the original light remains), `light<N>_enabled/x/y/z/color/intensity/radius/cone/aimX/aimY` — x/y in 0..1 frame units (may sit off-frame), z −1 (towards camera) … 1 (at the back) |
| `depth.focus` | Depth of field | `focusDepth`, `focusRange`, `blur` (px at 1080p), `autoFocus` + `focusX/focusY` to track whatever is at a point |
| `depth.fog` | Haze thickening with distance | `fogColor`, `density`, `start`, `ground` |
| `depth.view` | Shows the depth itself (greyscale or colour map) | `colorize` |
| `depth.occlude` | Goes on a layer *above* the clip (text, sticker, 3D model): places it at `depth` inside that clip, so nearer things pass in front | `target` (the clip to sit inside, by id — a string for `set_effect_param`; `""` = the nearest video/image clip beneath), `depth`, `softness`, `cutoutEdges` |

1. `ai_capabilities` — `depth-model` must be installed (`install_addon` otherwise).
2. `estimate_depth({clip, quality})` → `{job_id}`; poll `get_job` (it is slow: about 0.5 s per
   frame on CPU at `draft`). Re-estimating unchanged pixels returns the cached map at once.
3. `add_effect` with one of the ids above on the clip (or, for `depth.occlude`, on the layer above
   it — and `estimate_depth` the clip it sits inside, not that layer) and tune with `set_effect_param` (`set_effect_color_param` for light colours) or `fx.<i>.<param>` keyframes.
4. `sample_depth({clip, x, y, time?})` → `{depth}` reads what is at a spot — use it to set
   `focusDepth` on the subject, or `depth` on `depth.occlude` just behind them.

`inspect({clips:true,detail:true})` reports `hasDepth`. The map is cleared when the clip's pixels
change (replace source, switch angle, orientation); `clear_depth` removes it by hand. Standalone
adjustment tracks have no depth of their own, so depth effects there pass through.

### Custom effects

Agents can write new GPU video effects, transitions and audio effects. Each one is an ordinary user
package under `<AppData>/<effects|transitions|audio-effects>/<slug>/`, the same place an imported
`.driftfx` lands, so it appears under **My Effects** and works with `add_effect`, `add_transition`
and `add_audio_effect` as soon as it is created. The format is in
[custom-effects.md](custom-effects.md).

1. `effect_authoring_guide({kind})` returns the manifest grammar, the parameter types and their
   inspector controls, and a working example (for audio, also the processors and their params).
2. `get_effect_source({kind, id})` reads any package, bundled ones included, to fork.
3. `create_effect({kind, slug, manifest, files})` parses the package and compiles its shaders on
   the GL thread before installing anything. It fails with `bad_manifest` or
   `shader_compile_failed` (`errors[]` holds the driver log per pass) and leaves nothing on disk.
   The id is always `user.<slug>`. Without OpenGL (some headless setups) it installs anyway and
   reports `shaderCheck:"skipped"`.
4. `update_effect({kind, id, …})` replaces a user package in place, `delete_effect` removes one
   (`in_use` unless `force:true` when the project still uses it), and `export_effect({kind, id, path?})`
   writes a shareable `.driftfx`. Inside flatpak or snap, or without `path`, the file goes to
   `<AppData>/exports/` and the reply says so with `fellBack` and `reason`.

5. `import_effect({path, overwrite?})` installs a `.driftfx` (from `export_effect` or Drift Forge) into
   My Effects and returns `{ids:[…], kind, name}`; the id works with `add_effect` / `add_transition` /
   `add_audio_effect` straight away. Errors: `not_found` (no such file), `bad_package` (not a Drift
   package), `exists` (that package folder is already installed; pass `overwrite:true` to replace it).
   It needs the running editor's add-on manager.

`get_effect_source` adds `uses` (how many times the project uses it) for user packages, and
`list_effects` / `list_transitions` / `list_audio_effects` rows carry `user:true` when the package is
in My Effects (the key is absent otherwise).

Saved effect stacks (`list_user_effect_presets`, `apply_effect_preset`) are managed like text presets:
`save_effect_preset({clip, label, effect_index?, audio?})` saves the whole stack, or just one effect
(`effect_index`, with `audio:true` for the audio stack; a bad index fails `bad_args`),
`rename_effect_preset({preset, label})`, `delete_effect_preset({preset})`,
`export_effect_preset({preset, path})` and `import_effect_preset({path})` (returns `{id, label}`). Unknown
ids fail `not_found`.

Bundled and add-on effects are read-only to these ops (`not_user_effect`). None of them are undoable:
they change files on disk, not the project.

### Text looks and animation

A caption's look is an ordered **shading stack** (`textStyle.layers`, `layers[0]` drawn first):
each layer is a `fill`, `stroke`, `shadow`, `glow` or `extrude` painted from a `solid` colour, a
`gradient` (multi-stop; `space` block|line|word|glyph|accentRun; `offset` keyframable and
`offsetSpeed` for a moving gradient), a `texture` (image path) or a shader `effect` (`shine`,
`shimmer`, `neon-pulse`, `glitch`, `chrome`, `dissolve`). Motion is three **animation slots** —
`in`, `out`, `loop` — each a preset id plus params, or an inline After Effects-style animator
tree for experts.

| Call | Effect |
|---|---|
| `list_text_presets({q})` / `add_text({text, preset})` / `apply_text_preset({clip, preset})` | The 33 built-in style packs (`title`, `subtitle`, `lower-third`, `caption`, `quote`, `impact`, `pop`, `neon`, `handwritten`, `hormozi`, `one-word-color`, `word-background`, `sentence-background`, `karaoke-pop`, `karaoke-highlight`, `mirage`, `underline`, `bulky`, `word-outline`, `gold-luxe`, `chrome`, `retro-3d`, `glitch`, `comic`, `fire`, `ice`, `candy`, `sticker`, `rainbow`, `cinematic`, `holo-shimmer`, `sketch`, `editorial`). The list carries each pack's `font`, `accent` rule and `anim:{in,out,loop}` preset ids so you can choose without applying. A pack replaces the **whole** style — layers and all three animation slots — and is remembered as `packId`. Unknown ids fail `not_found` |
| `save_text_preset({clip, label})` / `list_user_text_presets` / `apply_user_text_preset` / `rename_user_text_preset` / `delete_user_text_preset` / `export_user_text_preset({preset, path})` / `import_user_text_preset({path})` | User packs (`user:` ids) on disk; `apply_text_preset` accepts them too. Not project edits, so outside undo |
| `list_gradient_presets` / `list_text_effects` | The ready-made paints: 12 gradient stop sets (`sunset`, `ocean`, `candy`, `gold`, `chrome`, `rainbow`, `fire`, `ice`, `mono`, `holo`, `mint`, `berry` — copy `stops`/`kind`/`angle` into a layer's `paint.gradient`) and the six shader effects with their typed params (`shine`, `shimmer`, `neon-pulse`, `glitch`, `chrome`, `dissolve` — `paint:{kind:"effect", effect:{id, params:{<id>:{type, value}}}}`) |
| `set_text({clip, style})` | Partial patch. `layers` replaces the stack, `layer:{id\|index,…}` patches one, `color` edits the front-most fill, `animation:{in\|out\|loop:{preset, params, duration, stagger, unit, order, ease, period}}`. The flat v6 keys (`outline*`, `shadow*`, `glow*`, `fillKind`, `animIn`…) still work and land on the well-known layers |
| `add_text_layer({clip, kind, at})` / `set_text_layer` / `remove_text_layer` / `move_text_layer` / `duplicate_text_layer` | Edit the stack one layer at a time; `add_text_layer` returns `{layerId}`. Strokes take `strokeAlign` center\|outside\|inside, `dash` solid\|dash\|dot\|dashdot with `dashOffset`, `trimStart/trimEnd` for a write-on and `sketchLength/sketchDeviation/sketchSeed` for a hand-drawn wobble (fills take the sketch too). The same four tools also edit a shape clip's stack (`add_shape_layer` … are aliases) |
| `list_text_looks` / `apply_text_look({clip, look, params})` | One-click recipes (Plain, Shadow, Lift, Hollow, Splice, Outline, Echo, Glitch, Neon, Background, Curve, Gradient, Shine, Chrome, Holographic) that rewrite the stack; the style remembers the look, and re-applying the **same** look with `params` adjusts just those params (a different look starts from its defaults) |
| `list_text_animations({which, q})` | The In / Out / Loop presets with their typed params and `flags` (`unitLocked`, `orderLocked`, `easeLocked`, `durationLocked` say which controls the preset ignores; `mirrorForOut`, `mode`). Every reveal preset takes `duration`, `stagger`, `unit` (block\|character\|word\|line), `order`, `ease`; loops take `period` (0 = one pass over the clip: hold motion such as `tracking-drift`) and `amount` |
| `set_text_animation({clip, which, preset, …})` / `clear_text_animation` | Set or clear a slot. Switching to a different preset resets its params to that preset's defaults unless `keepControls:true`; `durationOverride` sets the slot's total length; `animators:[…]` installs an inline animator tree instead |
| `import_text_animation({path, which})` | A Lottie / `.lottie` text layer's animators (After Effects export) or a Drift preset file, saved as a user preset under the slot's `imported` category; `unsupported` lists what was dropped |
| `apply_text_style_to_all({clip, scope})` | Copy a caption clip's style to the other subtitle clips on its track (or `project`) |

Reel-style typography, for example: `set_text_animation({clip, which:"in", preset:"type-on-blur", stagger:0.05})`,
`set_text_animation({clip, which:"loop", preset:"tracking-drift"})`,
`apply_text_look({clip, look:"gradient", params:{preset:"berry", space:"word"}})`.

Every text op fails `type_mismatch` on a clip that is not a text or subtitle clip.

### Shapes

`list_shapes` → `add_shape({shape, at, track})` → `set_shape_style({clip, style})`. The 29 catalog
ids (`list_shapes` rows carry `kind` and the default box `aspect`): basic `rectangle`,
`rounded-rectangle`, `square`, `circle` (an `ellipse` with a square box), `ellipse`, `triangle`,
`right-triangle`, `diamond`, `pentagon`, `hexagon`, `octagon`, `parallelogram`, `trapezoid`; arrows
`arrow`, `double-arrow`, `block-arrow`, `curved-arrow`, `chevron`; bubbles `speech-bubble`,
`speech-bubble-rect`, `thought-bubble`, `callout`; fun `star`, `lightning-bolt`, `cloud`, `heart`,
`cross`, `burst`, `banner`. An unknown id fails `not_found`.

| `style` key | Read by |
|---|---|
| `kind` | any — swaps the geometry, keeps the layers |
| `layers` / `layer` | the shading stack, exactly as on captions (a fresh shape has `fill` under `stroke`; shape strokes default to `strokeAlign:"inside"`); `scope` and `gradient.space` are text-only and do nothing here |
| `cornerRadius` | native on `rounded-rectangle`, `speech-bubble-rect`, `callout`; rounds the corners of every other kind |
| `points`, `innerRatio` | `star`, `burst` |
| `headSize` | `arrow`, `double-arrow`, `block-arrow`, `chevron`, the `banner` notch |
| `thickness` | `arrow`, `double-arrow`, `chevron`, `curved-arrow`, `cross` |
| `tailX`, `tailSize` | the four bubbles |

`add_shape_layer` / `set_shape_layer` / `remove_shape_layer` / `move_shape_layer` /
`duplicate_shape_layer` are the text layer tools under another name. Animate with
`set_keyframe({prop:"shape.cornerRadius"})` or `shape.layer.<id>.<field>`. Looks and style packs
are text-only; build a shape's look from `list_gradient_presets` / `list_text_effects`.

### The marketplace

The `market` toolbox drives the same server-defined marketplace the app's Marketplace window shows.
The build must ship a marketplace service (`market_status.configured`); every market op fails
`market_unavailable` otherwise. Sources are `stock.<type>` (video, photo, audio, music, greenscreen),
`drift-assets`, `sfx` and `community.<kind>` (effect, transition, audio-effect, look, theme,
effect-template). Stock installs spend a per-machine quota; the rest are free.

| Call | Effect |
|---|---|
| `market_status({source, layout, refresh})` | `{configured, offline, revision, features, sources:[{id, kind, label, mediaKind, searchable, canResolve, categories, providers?, sorts?}]}`; `source` returns one source in full, `layout:true` adds `pages:[{id, title, sections:[{id, type, title, source}]}]`, `refresh:true` re-reads the manifest (waits up to 15 s) |
| `market_search({source \| section, q, category, sort, period, provider, filters, limit, cursor, more})` | `{source, items:[{id, title, kind, dur?, w?, h?, by?, tags, category?, thumb, preview?, variants, installed}], next_cursor?, has_more, quota?, warnings?}`. `section:"page/section"` runs that section's own binding (sort, category, page size). `provider:"*"` asks every provider. `more:true` fetches the next page of the previous search of that source. `type` is an alias for `source:"stock.<type>"`. Blocks until the service answers |
| `market_item({source, id})` | One item in full: description, license, `variants:[{id, label, w, h}]`, `web_url`, `min_app_version`, `installed`, `install_state`, `extra` |
| `market_resolve({url, wait})` | For sources that resolve pasted page links; blocks up to `wait` seconds (default 90) and returns one `item` |
| `market_preview({source, id, what, image, max})` | Downscaled JPEG of the thumbnail (`what:"thumb"`, default) or preview as image content, meta `{path, w, h, mime}`. Video and audio previews return meta `{path, mime, dur}` only. `image:false` returns just the meta. Cannot be batched in `apply` |
| `market_install({source, id, variant, dir, wait, place})` | Returns `{job}` at once, or after `wait` seconds (max 600) `{ok, job, result}` with `result` one of `{asset}`, `{prop}`, `{effect}`, `{theme}`, `{look}`, `{template}`. `dir` is an absolute folder and only applies to stock. `place:{at, track, new_track}` needs `wait > 0` and a bin asset; it places the clip through `place_clip` (one undo step) and adds `{clip, placed, requested, reason?}`. A failed or cancelled install is `{ok:false, error, detail, job}`; an install still running is `conflict`. `market_download` is a hidden alias |
| `market_downloads({kind, clear})` | Every transfer the app runs, add-ons and updates included: `{active, jobs:[{id, kind, title, status, phase, progress, bytes, total, speed, error?, error_code?, retryable, cancellable, result?}]}` |
| `market_cancel_download({id})` / `market_retry_download({id})` | Cancel a running job or retry a failed/cancelled one, any kind; returns `{job}`. A finished or non-cancellable job is `conflict` |
| `market_open({page, tab, item})` | Asks the app window to open the Marketplace on a page, the downloads tab (`tab:"downloads"`) or one item (`item:{source, id}`); `{opened:true}` |

Errors: `market_unavailable`, `market_error`, `install_timeout`, and the service's own codes
(`rate_limited`, `app_too_old`, `quota_exceeded`, `provider_unavailable`, `not_found`,
`download_failed`).

`DRIFT_MARKET_API_URL` in the environment points the client at another service (tests use a
local fake) without rebuilding.

## Traps

- **`set_transform` can write at the playhead.** If the property is already animated (two or more keys), or `autoKey` is on, it creates a keyframe there instead of a constant value. Seek first, or mute the animation with `set_property_keyframes_enabled(false)`.
- **`export_video` inherits the settings you leave out** from the last export made through MCP (agent exports keep their own memory, separate from the export dialog's). Mode switches are the exception — `audio_only` and `gif` never carry over — but a codec, a bitrate or a scale does. Pass every setting you care about.
- **A single key is moved, not added to.** Visual clips are born with one key at their start on x/y/width/height; with auto-key off `set_transform` moves that key, so the value holds for the whole clip. Only a property that already has two or more keys (or any property with `autoKey` on) gets a key at the playhead. Writes are clamped into the clip's own span.
- **`auto_reframe` cannot make a crop sharper than the canvas.** The decode is bounded by the canvas, not by the layout rect, so a crop achieved with an oversized transform resolves at canvas resolution at best. The reply's `scale` says how far the source is being pushed; `upscaled:true` means it is past its own resolution.
- **`set_mask` replaces the whole mask.** Omitted keys revert to defaults and omitting `shape` turns the mask off. Read the current mask from `inspect({clips:true, detail:true}).tracks[].items[].mask` and send it back merged (its `points` come as `[{x,y}]`, which the schema accepts alongside `[[x,y]]`; drop the read-only `animated`/`keyframes`).
- **`set_subtitle_cues` replaces every cue.** Read `inspect({clips:true, cues:true})` (or a detail row's `subtitleCues`), merge, send.
- **`set_effect_param`, `set_audio_effect_param` and `set_transition_param` fail `not_found`** on a key the effect does not declare or a stack index that is not there. They used to accept anything and store it silently; a batch that relied on that now stops at the bad op.
- **`set_speed_curve` returns a new clip id.** The old UUID stops resolving. End the apply batch after it — an ops array cannot reference an id produced earlier in the same batch.
- **Keyframe property names are not the transform names**: `width`/`height`, not `w`/`h` (`set_transform` uses w/h, `set_keyframe` uses width/height). The 3D names `rotationX`, `rotationY`, `z` and `perspective` are the same in both.
- **A clip's `z` does not change stacking.** Track order alone decides what draws on top, and tilted clips never intersect. A clip tilted past 90° shows its mirror image; there is no back face.
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
- **`generate_subtitles` makes a caption clip with fixed times.** Cut first, caption last. From a transcribed asset it is instant and word-exact.
- **Word indices belong to the asset, not the clip.** `cut_words({words})` takes indices from `get_transcript` (any view) and they stay valid across cuts; a word the clip no longer plays is skipped.
- **`transcribe` always covers the whole source file**, however short the clip: an hour-long recording is an hour of transcription (and of Scribe billing).
- **`keep_ranges` refuses speed-ramped clips** — a ramp is shaped over the clip's own range. Flatten it (`clear_speed_curve`) first.
- **One caption clip for a cut-up track.** `generate_subtitles({clips:[…]})` or `generate_subtitles({track, start, end})` transcribes several video/audio clips into a single subtitle clip; the sources must not overlap in time. `export_subtitle_file({clip, path, timeline_times:true})` offsets cues by the clip's start so the file matches the exported video.
- **`apply_denoise` is noise, not reverb.** "Sounds like a bathroom" will not be fixed by denoise.
- **`activity.content` at coarse steps reads pans as cuts.** Confirm a peak with `frames({at:[…]})` before cutting on it.
- **`frames({clip})` times are source seconds.** Use `tl` for the timeline position.
- **`get_waveform({image:true})` in clip mode is timeline-space**, while the numeric clip form reads the raw source file.
- **Lottie expressions are not evaluated.** Skottie renders an expression-driven property at its static value. `inspect_lottie` (and every `add_lottie` reply) lists them under `expressions`; bake them to keyframes in the authoring tool before relying on the motion.
- **`set_lottie_slot` is typed against the document.** An undeclared slot or a value of the wrong type fails `bad_args` — read the `slots` array from `add_lottie` or `list_lottie_slots` first. Documents with no slots can only be changed by editing the JSON (`get_lottie_source` → `set_lottie_source`).
- **`add_lottie` with a `slots` map does not fail on a bad slot** — it places the clip and reports the rejected ones in `slotErrors`.
- **`set_text.layers` replaces, `set_text.layer` patches.** Send the whole array only when you mean to rebuild the stack; layer ids are lowercase and keyframes on a removed layer are dropped.
- **Switching a text animation preset resets its params.** Send `preset` and the tweaks in one `set_text_animation` call, or add `keepControls:true` (in `set_text_animation`, or inside the slot object of `set_text.style.animation`) to keep duration/stagger/unit/order/ease/period/amount across presets. Re-sending the current preset id keeps everything.
- **`apply_text_look` with a different look starts from that look's defaults.** Only re-applying the clip's current look merges `params`.
- **Stagger `duration` is per unit.** A word-by-word reveal takes `duration + stagger × (words − 1)`; short subtitle cues can cut it off.
- **`list_emoji` needs `q` or `group`** — the catalog is ~1900 entries; `add_emoji` takes the `id` (the character).

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

The reply carries each op's result in order — `done[1].result.id` is the new clip's UUID. On failure `done` holds only the ops that ran and `failed` the error of the one at `stopped`. Use the id in the next call:

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

## Measuring the surface

`scripts/mcp-probe.py` starts `build/drift --headless`, synthesises a four-shot clip with ffmpeg,
calls every read tool plus a sample of ops, and prints the reply size of each in characters
(≈ tokens × 4). `--out DIR` saves the replies and returned images; `--calls FILE` runs your own
list. Run it after touching the MCP layer to see what an agent will pay.

## Security

- Binds to `127.0.0.1` only.
- Any local process with the session token has full editor access.
- Turn Agent access off when finished.

**Flatpak:** host file import may need `flatpak override --filesystem=home org.cutwire.Drift`.
