# Project folders

A Drift project can live as a folder of small JSON files, `<name>.driftproj/`, instead of one `.drift` bundle. The files are deterministic and pretty-printed, so they diff cleanly in git, and the editor and the folder stay in sync both ways: edit a file with any tool and the open editor applies it as one undo step; edit in the editor and the files change a moment later. The folder is canonical when the two disagree.

This file is also what the MCP tool `project_folder_guide` returns and what the generated `AGENTS.md` in every project folder says, so what you read here is what an agent reads.

Contents: [layout](#layout), [conventions](#conventions), [units and ids](#units-and-ids), [a clip](#a-clip), [what the editor does](#what-the-editor-does), [lint and schemas](#lint-and-schemas), [scripts](#scripts), [templates](#templates), [open, convert and render](#open-convert-and-render).

## Layout

```
MyFilm.driftproj/
  project.json            canvas size, fps, sample rate, background, work area, project info, format version
  bin.json                bin folders and the asset order
  markers.json            timeline flags
  guides.json             preview guide sets
  assets/<id>-<slug>.json one file per media asset
  timeline/tracks/NN-<type>-<name>.json
                          one file per track: its settings and its clips, sorted by start. NN is the stacking order
  sequences/<slug>/sequence.json
  sequences/<slug>/tracks/NN-<type>-<name>.json
                          nested timelines played by composite clips
  sidecars/<clipId>/{keyframes,masks,effects,payload}.json
                          the big parts of a clip, moved out of the track file once they pass ~600 bytes
  transcripts/<assetId>.json
  scripts/*.lua           Lua that generates tracks and clips (see Scripts)
  data/                   CSV and JSON the scripts read
  template.json           makes the folder a template (see Templates)
  AGENTS.md               this guide
  .luarc.json             Lua language server settings
  .drift/                 schemas, Lua type definitions, conflicts and caches; not versioned
```

Files are read in name order where order matters (tracks), and the order of assets comes from `assetOrder` in `bin.json`; an asset file that `assetOrder` does not list is added after the listed ones, so you can create `assets/new.json` without touching anything else. A new track is a new file `timeline/tracks/05-video-b-roll.json`.

## Conventions

- JSON, UTF-8, 2-space indent, one trailing newline. The editor rewrites files in exactly this form, so write it the same way to avoid noise in diffs.
- Keys are written with `$schema` first, then `id`, `type`, `kind`, `name`, then alphabetically. The `$schema` value points at the schema in `.drift/schema/`; keep it, editors use it for completion and validation.
- Clips inside a track file are sorted by `timelineStartUs`.
- A sidecar and the clip's own keys are the same keys: if `sidecars/<clipId>/effects.json` exists, its `effects` replace the clip's. You may also put them inline in the track file; the editor moves them out again when they are big.
- Media paths inside the project folder are relative to it (`"path": "media/intro.mp4"`); anything else is absolute.
- Omitted keys take their defaults, so a hand-written clip needs only the keys that matter (see [a clip](#a-clip)).
- Never edit anything under `.drift/` by hand.

## Units and ids

- All times are integer microseconds, in keys ending `Us`: `1 s = 1000000`. A frame at 30 fps is `33333`. There are no frame counts and no seconds in the files; scripts have `tc` and `sec` helpers for that.
- `timelineStartUs` and `timelineDurationUs` place a clip on the timeline. `srcInUs` and `srcOutUs` choose the part of the source media it plays.
- Keyframe `timeUs` is measured from the start of the clip, not the timeline.
- Positions and sizes are project pixels with the origin at the canvas centre of the clip's frame (`positionX`/`positionY` are the clip's centre, `scaleX`/`scaleY` its width and height in pixels, `rotation` in degrees).
- Ids are free-form strings, unique within their kind: asset ids across `assets/`, track ids across all track files, clip ids across all tracks and sequences. Make them readable (`intro-title`, `bg-music`); a UUID also works. Clips refer to assets by `assetId`, adjustment clips to their clip by `linkedClipId`, composite clips to sequences by `sequenceId`.

## A clip

A title card on a text track, hand-written:

```json
{
  "$schema": "../../.drift/schema/track.schema.json",
  "id": "t-titles",
  "type": "text",
  "name": "Titles",
  "clips": [
    {
      "id": "intro-title",
      "type": "text",
      "name": "Intro",
      "textContent": "Hello",
      "timelineStartUs": 0,
      "timelineDurationUs": 3000000,
      "opacity": {"keyframes": [{"timeUs": 0, "value": 0}, {"timeUs": 500000, "value": 1}]}
    }
  ],
  "transitions": []
}
```

A video clip needs an asset: add `assets/clip1.json` with `{"id": "clip1", "kind": "video", "name": "Clip 1", "path": "media/clip1.mp4", "durationUs": 8000000, "width": 1920, "height": 1080, "fps": 30}` and reference it with `"assetId": "clip1"`, `"srcOutUs"` and the clip's own `"path"`. Fill in the duration and size yourself: a hand-written asset is not probed. (The MCP tool `import_media` writes complete assets; its output shows up in the folder.)

Effects are `{"catalogId": "adjust.contrast", "parameters": {"contrast": 1.2}}` entries in `effects`; `list_effects` over MCP, or the lint, tells you the ids and parameter keys.

## What the editor does

- On open it reads the folder; a file that does not parse stops the open and the error names `file:line:col`.
- While open, every GUI edit is written about 300 ms later, changed files only, atomically. Files you did not change are not touched.
- A change on disk (any tool, `git checkout`, an agent) is read back within a fraction of a second and applied as one undo step named after the files. Undo puts the editor back and writes the folder back to match.
- A file that does not parse, or a project that does not load, leaves the open project exactly as it is and shows a banner with `file:line:col: message`; fix the file and the banner clears.
- If you and the editor change the same file at once, the file on disk wins and the editor's version is saved under `.drift/conflicts/<time>/`.

## Lint and schemas

`.drift/schema/*.schema.json` are JSON Schemas (draft 2020-12), generated from the same description the editor reads the files with. Any JSON language server gives completion and validation through the `$schema` key.

`drift lint <folder>` prints `file:line:col: severity: message`, one per line, and exits 1 if there is an error. The MCP tool `project_lint` returns the same. It checks what a schema cannot: unknown asset, track and clip ids, duplicate ids, clips overlapping on a track, keyframes outside their clip, unknown effects and parameters, sidecars that belong to no clip, missing media, and errors in scripts.

`drift --lsp` is a small language server (stdio) that adds those diagnostics and hover text for every key. Point your editor's LSP client at it for `*.json` in the folder, next to a JSON language server.

## Scripts

`scripts/*.lua` generate tracks and clips with loops and logic. They run when the project opens and whenever a script or a file in `data/` changes, in a sandbox (no `io`, `os` or `require`; deterministic; time and memory budgets).

```lua
-- scripts/titles.lua
local rows = data "titles.csv"            -- data/titles.csv, one table per row
local t = track { name = "Titles", type = "text" }
for i, row in ipairs(rows) do
  t:clip {
    id = "title-" .. i,                   -- ids must be stable so the clip keeps its identity
    type = "text", textContent = row.text,
    start = sec(i - 1) , duration = sec(1.5),
  }
end
```

- `track{...}` makes a track, or returns the existing one when `id` names a track that is already there; `t:clip{...}` adds a clip to it. Keys are the file keys; `start` and `duration` are microsecond aliases, `tc "00:00:01:12"` and `sec(1.5)` convert timecode and seconds, `beats(bpm, n)` gives the time of beat `n`.
- `title{...}` is shorthand for a text clip. `asset{...}` declares an asset. `project` is a read-only table of the declarative project (fps, width, height, tracks, clips) as it was before scripts ran.
- `data "file.csv"` reads a CSV (header row, numbers converted) or JSON from `data/`. `param "name"` reads a template parameter.
- Everything a script produces is stamped `"generatedBy": "scripts/titles.lua"`. The editor shows those clips locked with a badge and replaces exactly those items when the script runs again; your own items are never touched. To take a generated clip over, remove its `generatedBy` key (or use Detach in the editor).
- Script errors appear in the banner and in `drift lint` as `scripts/titles.lua:12: message`. The project keeps its last good result.
- `.drift/lua/drift.lua` holds LuaLS type definitions for all of this and `.luarc.json` points lua-language-server at it.

## Templates

A folder with a `template.json` is a template: a project with parameters and optional rows of data, rendered once per row.

```json
{
  "params": [
    {"name": "headline", "type": "text", "default": "Hello"},
    {"name": "accent", "type": "color", "default": "#ff3366"},
    {"name": "logo", "type": "media", "default": "media/logo.png"},
    {"name": "speed", "type": "number", "default": 1},
    {"name": "style", "type": "choice", "options": ["bold", "light"], "default": "bold"}
  ],
  "data": "data/rows.csv",
  "output": "{headline}-{row}.mp4"
}
```

Scripts read parameters with `param "headline"`; a data row overrides the parameters of the same name for that render. `{row}` and any `{param}` expand in the output pattern.

## Open, convert and render

- Open: File menu → Open project folder, `drift path/to/Name.driftproj`, the MCP `open_project` tool, or the recent list.
- File → Save as project folder writes the open project out as a folder and keeps editing it; Export .drift bundle writes a bundle from a folder.
- `drift lint <folder>`, `drift --lsp`.
- `drift --headless --render <template-folder> [--data rows.csv] [--row N | --all] -o pattern` renders the template once per selected row. MCP: `template_render`.
