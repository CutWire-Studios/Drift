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

The `initialize` reply carries a short `instructions` string with the essentials; the eight homepage tools are:

1. **`catalog`** — toolboxes and their ops as `"name — when"` strings, plus limitations (~3k tokens). `catalog({brief:true})` is names only; `catalog({guide:true})` adds the long prose guide; `catalog({endpoints:true})` adds the HTTP endpoints.
2. **`search({q})`** — find ops by keyword across names, when-hints, descriptions and argument names. Returns `hits:[{name, toolbox, when, args, required}]`; `schema:true` inlines the full schema when there are ≤3 hits. Use this instead of loading a whole toolbox.
3. **`toolbox({name})`** or **`toolbox({ops:[…]})`** — full JSON schemas for a toolbox, or for just the named ops (any toolbox).
4. **`apply({ops:[{tool, args}, …]})`** — run mutations in order; one undo step for the batch.
5. **`inspect()`** — project summary; `clips:true` adds clip rows, `detail:true` expands them, `clip:<uuid>` / `track:<n>` filter, `since:<revision>` short-circuits.
6. **`activity()`** — cheap text profile of visual change, motion and loudness over a range; tells you *where* to look.
7. **`frames()`** — one labelled contact-sheet image of many moments; tells you *what* is there.
8. **`capture({at})`** — one full-size JPEG still of the composition (use to verify edits).

Homepage endpoint: `POST /mcp` with `Authorization: Bearer <token>`.

Pinned endpoints (`/mcp/timeline`, `/mcp/project`, …) list that toolbox’s ops directly. `catalog`, `search`, `toolbox`, and `apply` are only on `/mcp`; `inspect`, `capture`, `frames` and `activity` work on both. Toolbox ops can also be called by name directly on `/mcp` instead of through `apply` — but only `apply` collapses a batch into one undo step.

`apply` takes **toolbox ops only**. The eight homepage tools return `unknown_op` inside an `ops` array; call them directly. `get_waveform({image:true})` is also refused inside `apply`.

## Conventions

| Topic | Rule |
|-------|------|
| Time | Seconds |
| Clip reference | Prefer `clip` UUID from `inspect`; else `track` (0 = top) + `index`. One or the other is **required** — clip ops never fall back to the selection. Read the current selection from `inspect.selection` |
| Selection ops | `separate_audio`, `unlink_audio`, `merge_clips`, `align_clip_left/right`, `copy_selection`, `cut_selection` take no clip argument — call `select_clip` first. `freeze_frame` and `paste_at_playhead` are playhead-based (seek first); they do not use the selection |
| Discovery | Effect stack indices, transition ids, and bookmark indices exist **only** in `inspect({clips:true, detail:true})`. Subtitle cues: `inspect({clips:true, cues:true})` or the `subtitleCues` field of a detail row. Mask/fade/speed/volume/keyframes/`hasFaceTrack`/stabilize* are on the same detail rows. **Detail rows omit what does not apply**: caption styling only on text/subtitle clips, shape styling only on shapes, `vector` (document metadata, slot overrides — never the document text) only on Lottie/SVG clips, `mask` only when one is set, `keyframes` only for animated properties (listed in `animated`), stabilize/animation blocks only when in use, fade fields only when a fade is set, empty arrays, false booleans and other defaults dropped (absent = default: no fade, `speed` 1, `volume` 1, unlinked). Every row carries `transform:{x,y,w,h,rotation,opacity}` at the playhead. `verbose:true` returns the raw untrimmed map |
| Numbers | Every number in a reply is rounded to 3 decimals, except `fps` and speed-curve `pos` which keep 6 |
| Validation | Args are checked against the op's schema before it runs: a missing required key → `bad_args` naming it; a wrong JSON type → `type_mismatch` (numeric strings like `"1"` are still accepted); an enum or declared min/max violation → `bad_args` listing the allowed values or range. Unknown keys are not errors; they come back as `ignored:[…]` on success. A clip-ref op with neither `clip` nor `track`+`index` gets `bad_args` saying so; a stale uuid gets `not_found` with a hint to re-read `inspect` |
| Effects | `list_effects` / `list_audio_effects` / `list_transitions` are compact by default (`cats:{<cat>:[{id,label}]}`); pass `id`, `cat`, or `q` to get parameters. Ids are accepted with `.` or `_` interchangeably; an unknown id returns `not_found` with the closest matches. Effect stacks live on an **adjustment clip linked to the target**, created on its own lane the first time; `add_effect`/`add_audio_effect` report it as `host:{track,index,clip}`. Keep addressing the original clip in every effect op, and expect that extra lane in `inspect` |
| Images | `capture`, `frames`, and `get_waveform({image:true})` return a text block (JSON meta) followed by an image block. Times are always in the text block; never rely on burned-in labels alone. Sheets fit one vision image (≤1456 px long edge) |
| Overlap | Off by default — place/move snap to gaps unless `set_overlap` enables overlap; the reply reports `requested` vs `placed`. With overlap off, moving several clips toward zero must be sequenced **back-to-front**. `set_ripple` / `close_gap` close holes after a delete |
| Export | `export_video` is async — poll `inspect().export` or `export_status`. The output path is normalised, so use the `path` echoed back. Named sizes: `list_export_presets` + `export_with_preset` |
| Atomicity | `apply` is **not** atomic: on failure the ops before it stay applied. The failure reply is `{ok:false, error:"apply_failed", stopped:<index>, tool, failed:<that op's error>, done:[results of the ops that ran]}` — `done` never contains the failed op. An ops array cannot reference an id produced earlier in the same batch — end the batch after `set_speed_curve` |
| Undo | One batch = one undo step. Linear history (no branches): `list_history({limit})` returns the newest `limit` (default 20) versions as `{index, label, short}` plus the HEAD `hash`/`short` and `total` (index 0 = Origin). `undo_to({index})` or `undo_to({hash})` jumps (a ≥8-char prefix such as `short` works); the next edit drops redo. `take_snapshot` writes compact project JSON named `<hash>.json` (file SHA-256 = history hash). Ops outside the stack (and all read-only ops): `import_media`, `import_media_bytes`, `seek`, `play`, `pause`, `undo`, `redo`, `undo_to`, `take_snapshot`, `restore_snapshot`, `set_overlap`, `set_ripple`, `set_snap`, `set_guides`, `set_loop_work_area`, `export_video`, `save_project`, `set_theme`, `set_shortcut`, `reset_shortcuts`, `set_beat_layers`, `detect_beats`, `list_speed_curve`, `list_fade_curve`, `install_addon`, `cancel_addon_install`, `set_acceleration`, `switch_angle`, `end_multicam`. **`set_beat_layers` changes the user's own snapping and cannot be undone** |
| Errors | `{ok:false, error:<code>, detail:<text>}` — `bad_args`, `not_found`, `type_mismatch`, `unknown_op` (the detail suggests the nearest op names and their toolbox), `unknown_toolbox`, `wrong_endpoint`, `wrong_toolbox`, `apply_failed`, `import_failed`, `import_timeout`, `export_busy`, `export_failed`, `export_timeout`, `capture_failed`, `conflict` |
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
| `detect_scenes` | `jobs.sceneDetect.{active, progress, status, clip, scenes}` (also present, inactive, while a scan result is loaded) |
| `run_segmentation`, `segment_clip`, `apply_denoise`, `detect_faces` | No progress field — re-read `inspect({clips:true, detail:true})` and compare |
| `stabilize_clip` | Per-clip `stabilizing` / `stabilizeProgress` / `stabilizeStatus` on the detail clip row |

## Toolbox reference

| Toolbox | When to use |
|---------|-------------|
| `media` | Import (paths/bytes), list, rename, remove, replace, export still |
| `timeline` | Tracks, clips, selection, ripple/gap, bookmarks, copy/paste, A/V link |
| `canvas` | Transform, flip, blend, mask, fade, speed, reverse, animation, stabilisation, shape styling (`set_shape_style`: the same shading-layer stack captions have — `layers` / `layer` patches, `add_shape_layer` … — plus geometry knobs; the legacy flat `fill`/`stroke` keys still land on the `fill`/`stroke` layers) |
| `playback` | Seek, play, pause, In/Out work area |
| `text` | Title and caption clips, style packs, shading layers (fill/stroke/shadow/glow/extrude), looks, In/Out/Loop animation presets, Lottie preset import |
| `shapes` | Builtin shapes, stickers, emoji, fonts, text presets |
| `motion` | Lottie animations and SVG drawings as vector clips: add, inspect, swap the document, re-theme through slots or the `svg.*` element overrides |
| `subtitles` | Subtitle clips, cues, import/export, Whisper generation |
| `effects` | Video/audio effects, transitions, templates, effect clipboard |
| `project` | Open/new/save/package, canvas, background, metadata, export |
| `keyframes` | Property animation keys and tangents — clip transform, `fx.<i>.<param>`, `mask.<key>`, on text/subtitle clips `text.<key>` (pixelSize, letterSpacing, lineHeight, boxPadding, pathBend) or `text.layer.<id>.<field>` (opacity, offsetX, offsetY, blur, width, spread, trimStart, trimEnd, dashOffset, sketchLength, sketchDeviation, color.r/g/b/a, gradient.angle/offset/scale/center.x/y, gradient.stop.n.pos); the old names outlineWidth, shadowBlur, glowRadius, gradientAngle, color.r… still resolve onto the stroke/shadow/glow/fill layers. On shape clips the same layer fields as `shape.layer.<id>.<field>` (a fresh shape's layers are `fill` and `stroke`) plus `shape.<cornerRadius|points|innerRatio|headSize|thickness|tailX|tailSize>`; on SVG clips `vector.svg.…` (see Motion) |
| `speed` | Speed ramps; reading custom fade curves (write them with `set_fade_curve` in `canvas`) |
| `segmentation` | SAM-style cutout (session or one-shot) |
| `ai` | Denoise, face detection, auto-reframe, add-on install |
| `audio` | Waveforms, silence, loudness, ducking, beat detection, beat-synced cuts, clip volume |
| `scene` | Shot detection, what is in each shot, scene-synced cuts |
| `ui` | Theme, shortcuts, editor preferences, guides |
| `multicam` | Multi-camera session: set up, switch at the playhead, save |
| `market` | Stock media from the Cutwire marketplace: status/consent, search, resolve a link, download into the bin |

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
speech-band lane, silent stretches shaded, onset ticks when `detect_beats` is current, an
optional `spectrogram:true` lane, and a time axis — together with a `summary_buckets` (50)
numeric summary and the `silence` ranges it found. In `clip` mode the image form is
timeline-space (through the clip's volume and fades), unlike the numeric clip form.

Typical recipe: `activity()` → `frames({at: peaks})` or `frames()` → edit → `capture({at})`.

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
keyframes as `vector.svg.…` — scalars directly, colours per channel (`vector.svg.logo.fill.r`),
or through the colour helpers. An `.svg` dropped in the bin (`import_media`) is a vector asset
and places as a vector clip.

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
| `set_text({clip, style})` | Partial patch. `layers` replaces the stack, `layer:{id\|index,…}` patches one, `color` edits the front-most fill, `animation:{in\|out\|loop:{preset, params, duration, stagger, unit, order, ease, period}}`. The flat v6 keys (`outline*`, `shadow*`, `glow*`, `fillKind`, `animIn`…) still work and land on the well-known layers |
| `add_text_layer({clip, kind, at})` / `set_text_layer` / `remove_text_layer` / `move_text_layer` | Edit the stack one layer at a time; `add_text_layer` returns `{layerId}`. Strokes take `strokeAlign` center\|outside\|inside, `dash` solid\|dash\|dot\|dashdot with `dashOffset`, `trimStart/trimEnd` for a write-on and `sketchLength/sketchDeviation/sketchSeed` for a hand-drawn wobble (fills take the sketch too). The same four tools also edit a shape clip's stack (`add_shape_layer` … are aliases) |
| `list_text_looks` / `apply_text_look({clip, look, params})` | One-click recipes (Shadow, Lift, Hollow, Splice, Outline, Echo, Glitch, Neon, Background, Curve, Gradient, Shine, Chrome, Holographic) that rewrite the stack; the style remembers the look so its params stay adjustable |
| `list_text_animations({which, q})` | The In / Out / Loop presets with their typed params. Every reveal preset takes `duration`, `stagger`, `unit` (block\|character\|word\|line), `order`, `ease`; loops take `period` (0 = one pass over the clip: hold motion such as `tracking-drift`) |
| `set_text_animation({clip, which, preset, …})` / `clear_text_animation` | Set or clear a slot. Picking a preset resets its params to the preset defaults; `animators:[…]` installs an inline animator tree instead |
| `import_text_animation({path, which})` | A Lottie / `.lottie` text layer's animators (After Effects export) or a Drift preset file, saved as a user preset under the slot's `imported` category; `unsupported` lists what was dropped |
| `apply_text_style_to_all({clip, scope})` | Copy a caption clip's style to the other subtitle clips on its track (or `project`) |

Reel-style typography, for example: `set_text_animation({clip, which:"in", preset:"type-on-blur", stagger:0.05})`,
`set_text_animation({clip, which:"loop", preset:"tracking-drift"})`,
`apply_text_look({clip, look:"gradient", params:{preset:"berry", space:"word"}})`.

### Stock media from the marketplace

The `market` toolbox wraps the same service the Assets → Market tab uses (`docs/marketplace`).
It is gated twice: the build must ship a marketplace service (`market_status.configured`), and
**the user must have accepted the marketplace terms in the app** (`market_status.consented`).
Every other market op fails `consent_required` until then; there is deliberately no op to
accept on the user's behalf, because downloads spend a per-machine quota.

| Call | Effect |
|---|---|
| `market_status()` | Types → providers with capabilities (`search`, `featured`, `resolve`), filters (with option ids) and quota, plus account/coins when connected |
| `market_search({q, type, provider, filters, limit})` | One page of listings `{id, title, type, provider, dur, w, h, coins?, by?, thumb?, variants?}`; `more:true` fetches the next page; `thumb` is a URL you can fetch with your own tools to look at the item |
| `market_resolve({url})` | For resolve-only providers (pasted page links); blocks up to 90 s and returns one `item` |
| `market_item({id})` | Variants, preview and license of a listing from the last search/resolve |
| `market_download({id, variant, dir, wait})` | Starts the download, **spends quota**, imports the file into the bin when done and reports its `asset` id. `wait:<seconds>` blocks for completion; otherwise poll `market_downloads()` or `inspect({detail:true}).jobs.market` |
| `market_downloads({clear})` / `market_cancel_download({id})` | Job list with status/progress/error/path/asset; cancel a running one |

Errors: `market_unavailable`, `consent_required`, `market_error`, and the service's own codes
(`rate_limited`, `auth_required`, `payment_required`, `provider_unavailable`, `not_found`,
`download_failed`). Coin prices appear only when non-zero; nothing in the reply says "free".

`DRIFT_MARKET_API_URL` in the environment points the client at another service (tests use a
local fake) without rebuilding.

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
- **`activity.content` at coarse steps reads pans as cuts.** Confirm a peak with `frames({at:[…]})` before cutting on it.
- **`frames({clip})` times are source seconds.** Use `tl` for the timeline position.
- **`get_waveform({image:true})` in clip mode is timeline-space**, while the numeric clip form reads the raw source file.
- **Lottie expressions are not evaluated.** Skottie renders an expression-driven property at its static value. `inspect_lottie` (and every `add_lottie` reply) lists them under `expressions`; bake them to keyframes in the authoring tool before relying on the motion.
- **`set_lottie_slot` is typed against the document.** An undeclared slot or a value of the wrong type fails `bad_args` — read the `slots` array from `add_lottie` or `list_lottie_slots` first. Documents with no slots can only be changed by editing the JSON (`get_lottie_source` → `set_lottie_source`).
- **`add_lottie` with a `slots` map does not fail on a bad slot** — it places the clip and reports the rejected ones in `slotErrors`.
- **`set_text.layers` replaces, `set_text.layer` patches.** Send the whole array only when you mean to rebuild the stack; layer ids are lowercase and keyframes on a removed layer are dropped.
- **Picking a text animation preset resets its params.** Send `preset` and the tweaks in one `set_text_animation` call, or pass `keepControls:true` in `set_text` to keep duration/stagger/unit/order/ease across presets.
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
