# File-based GPU effects

All effect presets are GPU packages under `effects/` (`backend: "gpu"`). Preview and export share `FrameCompositor` → `EffectProcessor` → `GpuEffectExecutor`.

## Package layout

```
effects/
└── adjust_contrast/
    ├── effect.json
    ├── thumbnail.png   # optional browser preview (auto-picked if present)
    └── main.frag
```

Search order: `DRIFT_EFFECTS_DIR`, `<applicationDir>/effects`, `<AppDataLocation>/effects`.

## effect.json

| Field | Meaning |
|---|---|
| `id` / `displayName` / `category` / `order` | Catalog metadata |
| `thumbnail` | Optional image path (relative to package, or absolute). Defaults to `thumbnail.png` when that file exists |
| `backend` | `"gpu"` or `"model3d"` |
| `parameters[]` | User-facing uniforms — see the parameter types below |
| `fixedParams` | Hidden uniforms (colors as `#rrggbb`, enums as strings) |
| `requires` | `"face"` to receive the baked face anchors, `"depth"` to receive the clip's depth map (see below), or an array of both. Any other value is a parse error. `"depth"` is `gpu` backend only |
| `history` | Whole number 1–32: the effect samples that many of the clip's earlier source frames (see "Time effects" below). `gpu` backend only |
| `historyExact` | Whole number 1–2, no more than `history`: the engine decodes that many earlier source frames on demand when its ring lacks them (see "Time effects"). `gpu` backend only; `nextFeatures` name `history:exact` |
| `historyUnits` | `"project"` (default) or `"source"`. With `"source"`, `historyExact` and `future` count the media's own frames (up to 4 each) instead of project frames; `history` is ignored. `gpu` backend only; `nextFeatures` name `history:source-units` |
| `future` | Whole number 1–2 (1–4 with `historyUnits: "source"`): the engine decodes that many of the clip's following source frames for each composite (see "Time effects"). `gpu` backend only; `nextFeatures` name `history:future` |
| `previousOutput` | `true`: the effect can read its own output from the previous frame (see "Time effects"). `gpu` backend only |
| `pipeline` | `intermediateBuffers` + `passes`. A buffer is `{"id", "scale", "format"}`; `format` is optional: `"rgba8"` (default), `"rgba16f"` or `"rg16f"` (half-float: values outside 0..1 and negatives survive between passes). Any other value is a parse error. A package using a half-float format lists `buffer:float` in `nextFeatures`. See "Float intermediate buffers" |

### Parameter types

| `type` | Binds as | Notes |
|---|---|---|
| `float` (default) | `float` | `minValue`/`maxValue`/`defaultValue`; keyframable |
| `bool` / `boolean` | `float` (0 or 1) | Rendered as a switch; keyframable (the value rounds at 0.5) |
| `color` / `colour` | `vec3`, or `vec4` with `"alpha": true` | `defaultValue` is a `"#rrggbb"` string, or `"#rrggbbaa"` with `alpha`; rendered as a swatch (plus an opacity slider); keyframable |
| `point` / `vec2` | `vec2` | `defaultValue` is `[x, y]`; `minValue`/`maxValue` are `[lo, lo]`/`[hi, hi]` (one shared range); a 2D pad; keyframable per axis |
| `choice` / `enum` | `float` (option index) | `options[]` (at least two); rendered as a dropdown |
| `int` / `integer` | `float` (rounded) | `minValue`/`maxValue`/`defaultValue`; the slider snaps to whole numbers; keyframable. A `float` may also set `step` (or `ui.step`) to snap |
| `file` | *(not bound)* | Absolute path string; `fileFilters` for the picker. Used by `model3d` |
| `curve` | `sampler2D` (256×1) | `defaultValue` is a key array `[{"x", "y", "ease"}]`; keys are baked to a 256-texel curve. Ease `"spline"` (see below) |
| `lut` | `sampler2D` (1089×33) | `defaultValue` is a look id, `""` for none. The look's `.cube` file, resampled to 33³ and tiled (see below). An empty or unresolved id binds the identity |

A `lut` parameter's value is a look id from the looks catalog, not a file path. The engine bakes the
look's `look.cube` to a 33×33×33 lattice laid out as 33 tiles of 33×33 side by side: texel
`(b * 33 + r, g)` holds the output for input `(r, g, b)`, so the texture is 1089 wide and 33 high.
A shader samples it with a trilinear lookup across the two neighbouring blue tiles:

```glsl
vec3 lutSample(sampler2D t, vec3 c) {
    float bz = c.b * 32.0;
    float b0 = floor(bz);
    float b1 = min(b0 + 1.0, 32.0);
    float f = bz - b0;
    float v = (c.g * 32.0 + 0.5) / 33.0;
    float u0 = (b0 * 33.0 + c.r * 32.0 + 0.5) / 1089.0;
    float u1 = (b1 * 33.0 + c.r * 32.0 + 0.5) / 1089.0;
    return mix(texture(t, vec2(u0, v)).rgb, texture(t, vec2(u1, v)).rgb, f);
}
```

A `curve` key's `"ease"` shapes the segment to its right. `"spline"` is a monotone cubic
(Fritsch–Carlson) through all of the curve's keys, so the curve passes through every key and never
overshoots between them. The other eases are `linear`, `smooth`, `in`, `out` and `hold`.

Features a package needs from the engine go in `nextFeatures` (`"param:lut"`, `"curve:spline"`,
…). A feature this build does not know rejects the package with "needs a newer Drift", so a
package that uses a new param type never loads silently on an older release.

A colour parameter may list `"swatches": ["#rrggbb", …]`, preset shades the inspector shows as a
grid under the picker, and `"enables": "<bool key>"`, a bool parameter that picking any colour
switches on in the same undo step (Face Retouch's Lip colour turns on Custom lip colour). An
invalid swatch, or `enables` naming anything but a bool parameter, is a parse error.

A float parameter named `faceIndex` on a face effect is shown as numbered face buttons rather than
a slider, one per face slot the clip's track uses.

Any parameter may set `group` to fold into a named inspector section. Only the first group starts
open; `"groupCollapsed": true` on a group's parameters keeps it folded even when it comes first
(Face Retouch's Advanced section).

Colour parameters bind as **`vec3`** unless they declare `"alpha": true`, which makes them a
**`vec4`** and adds an opacity slider. Without `alpha`, any alpha in `defaultValue` is discarded at
parse time and the value is normalized to `#rrggbb`; with it, the value is kept as `#rrggbbaa`
(CSS order, alpha last). A colour keyframes as four channel tracks, `<key>.r/.g/.b/.a` in 0..1,
folded back into the hex before binding. File paths are **not keyframable**.

Pass inputs: `source_texture` (+ optional `index`), `buffer` (+ `id`), or `texture` (+ `id`). Multiple inputs bind as `u_currentTexture` (unit 0) and `u_texture1`…  
Pass outputs: `buffer` or `canvas`.

`pipeline.textures[]` declares static image assets loaded once from the package dir:

```json
"textures": [{ "id": "glyphs", "file": "glyphs.png" }]
```

Static textures upload unflipped and **wrap** (`GL_REPEAT`), without mipmaps. A shader that must
not tile one — a makeup template — has to zero it outside [0, 1] itself.

Bundled packages ride inside the Android binary as Qt resources, and only files matching the glob
in `CMakeLists.txt` (`effect.json`, `*.frag`, `*.png`, `*.bin`, `NOTICE`, `LICENSE*`) make it in.
A texture in any other format loads on desktop and fails the package on Android.

### Mesh passes (`"geometry": "face111"`)

A pass may set `"geometry": "face111"` with `"templateBounds": [x, y, w, h]`. Instead of a
full-screen quad, the engine copies the pass's input 0 into its output, then draws GPUPixel's
111-point face mesh over it: each vertex sits on the tracked point from `u_faceLandmarks111`, and
carries the same point on GPUPixel's reference face. The fragment shader gets:

| Varying | Meaning |
|---|---|
| `v_texCoord` | Screen uv, exactly what a quad pass sees at that pixel |
| `v_templateCoord` | uv in the template image. `templateBounds` is where the image sits on the reference face, in its 1280-pixel frame |

`u_templateBounds` and `u_meshAspect` are engine-bound. Outside the mesh, and in the whole frame
when the clip has no mesh, the output is the plain copy. Rules, all enforced at load time:

- `"requires": "face"` effects only — not transitions.
- Input 0 must be a `source_texture` or `buffer` (it is what gets copied).
- `templateBounds` needs four numbers with a positive width and height.

See `effects/face_retouch` for lipstick and blush templates drawn this way, and
`src/engine/face/tracking/Face111.h` for the point order.

## GLSL

- `#version 330 core`
- Reserved: `u_currentTexture`, `u_textureN`, `u_resolution`, `u_time`, `u_timeUs`, `u_frameIndex`, `u_frameRate`, `u_progress`, `u_fromTexture`, `u_toTexture`, `u_depth*`, `u_hasDepth`, `u_history*`, `u_future*`, `u_sourceFps`, `u_sourcePhase`, `u_sourceStep`, `u_projectFps`, `u_prevOutput`, `u_templateBounds`, `u_meshAspect`, `u_face*`

**Grace mode:** compile/GL failure → passthrough.

## Face effects

A package with `"requires": "face"` receives the clip's baked landmarks. It should also declare a
`faceIndex` float 0–3, which selects which tracked person to follow — the engine **consumes** that
parameter rather than binding it.

Two coordinate conventions are in play, and mixing them up produces elliptical warps:

- **Positions** (`u_faceLeftEyeX`, …) are in **uv**: 0–1, top-left origin.
- **Lengths, angles and every contour loop** are **width-normalized**: uv with y scaled by the frame
  aspect, so a radius means the same thing along both axes. Rebuild that space from `u_resolution`
  with `vec2 toLocal(vec2 uv, float aspect) { return vec2(uv.x, uv.y * aspect); }`.

### Uniforms

| Uniform | Type | Notes |
|---|---|---|
| `u_faceValid` | `float` | **`< 0.5` means pass the frame through untouched.** Every face shader must honour this |
| `u_faceLeftEye`/`RightEye`/`Nose`/`Mouth`/`MouthLeft`/`MouthRight`/`Chin`/`Forehead`/`Center` `X`,`Y` | `float` | uv. Left/right are **image**-side, not the subject's |
| `u_faceRx`, `u_faceRy`, `u_faceAngle`, `u_faceEyeRadius` | `float` | Face oval half-axes, eye-line tilt, iris radius |
| `u_faceHasContours` | `float` | 0 for a sidecar baked before contours existed. Treat like `u_faceValid` |
| `u_faceOval[36]`, `u_faceLipOuter[20]`, `u_faceLipInner[20]`, `u_faceEyeLeft[16]`, `u_faceEyeRight[16]`, `u_faceBrowLeft[10]`, `u_faceBrowRight[10]` | `vec2[]` | Closed loops. Eye rings run inner corner → **upper** lid → outer corner, so indices 0–8 are the lash line |
| `u_faceCheekLeftX/Y`, `u_faceCheekRightX/Y` | `float` | uv |
| `u_facePoseValid` | `float` | 0 when the pose could not be derived |
| `u_facePoseRight`/`Up`/`Fwd` `X`,`Y`,`Z` | `float` | Orthonormal head basis. `Fwd` points **out of the face toward the viewer** |
| `u_facePoseOriginX/Y/Z`, `u_facePoseScale` | `float` | Eye midpoint and interocular distance |
| `u_faceYaw`, `u_facePitch`, `u_faceRoll` | `float` | Radians, derived from the basis for shaders that only want an angle |
| `u_faceHasMesh` | `float` | 0 for a sidecar baked before the mesh existed |
| `u_faceLandmarks111[111]` | `vec2[]` | Width-normalized. The 468-point mesh reduced to GPUPixel's 111-point layout (`src/engine/face/tracking/Face111.h`). Only set when `u_faceHasMesh` is 1 |

Both `u_faceValid` and `u_faceHasContours` must be checked by anything using the loops:

```glsl
if (u_faceValid < 0.5 || u_faceHasContours < 0.5) { fragColor = texture(u_currentTexture, v_texCoord); return; }
```

**Uniform budget.** The seven loops together are 256 components. GL 3.3 core guarantees at least
1024 fragment default-block components, and no shipping package declares more than about 220 — but
this is why the full 468-point mesh is not delivered this way. `u_faceLandmarks111` is 222
components, but many drivers give every array element its own vec4 slot, so count it as 444 and do
not declare it in the same pass as the seven loops. Face Retouch keeps them in separate passes.

**No `#include`.** The package loader materializes each `.frag` verbatim. The polygon SDF helper is
duplicated into every beauty package on purpose, which is also what keeps a package self-contained
enough to redistribute as an addon. If that becomes a burden, the fix is an `"includes"` array
concatenated at parse time in `loadGpuPipeline` — the program cache keys off the materialized
source, so `GlRuntime` would need no change.

**Sidecar compatibility.** Face tracks are format v2; v1 files still load, with `hasContours`,
`hasPose`, and `hasMesh` false. Optional v2 blobs: `"c"` (contours), `"p"` (pose), `"m"` (468×3
mesh, uint16 packed like contours). Missing `"m"` is not an error — the 3D Face Mesh effect
pass-throughs until the clip is re-detected. Format version stays 2; do not bump for the mesh blob.

## Depth effects

A package with `"requires": "depth"` receives the depth estimated for its clip (the Depth addon,
Video Depth Anything; see `VdaDepth` and `DepthSidecar`). The engine compiles a prelude into every
pass — **do not declare these yourself**:

| Name | Kind | Notes |
|---|---|---|
| `u_depthTexture` | `sampler2D` | Single channel, bound on unit 8. Lower resolution than the frame (short side 392 or 518) |
| `u_depthResolution` | `vec2` | Its size in texels |
| `u_hasDepth` | `float` | **`< 0.5` means there is no depth: pass the frame through.** A clip that has not been estimated, and standalone adjustment tracks, render this way |
| `float driftDepth(vec2 uv)` | helper | 0 is the farthest thing in the clip, 1 the nearest. Normalised over the **whole clip**, so a value means the same place from frame to frame |
| `float driftDepthGuided(vec2 uv, sampler2D guide)` | helper | `driftDepth` snapped to the colour edges of `guide` (normally `u_currentTexture`) by a 3×3 joint-bilateral filter. Use it wherever a depth edge meets a visible edge |
| `vec3 driftNormal(vec2 uv, float strength)` | helper | Surface normal from depth gradients, in uv space (x right, **y down**) with z toward the viewer |
| `vec2 packDepth(float)` / `float unpackDepth(vec2)` | helpers | 16-bit depth through two 8-bit channels of an intermediate buffer |

Depth uv matches the frame's: `(0, 0)` is the top-left. The map is relative, not metres — distances
in a shader are perceptual, so expose a scale parameter rather than assuming units.

### Behind Subject (`depth.occlude`)

A compositor-backend package, not a shader: it places the layer carrying it (text, a sticker, a 3D
model) at `depth` inside a video or image clip beneath it, and wherever that clip is nearer the
layer gives way. Its `target` parameter is of type `clip` (a clip id, picked in the inspector from
`AppController::effectClipCandidates`); empty, or naming a clip that no longer exists, means the
nearest video or image clip beneath — whether or not it has depth yet, so the inspector can name it
and offer to estimate it. A chosen clip that is not on screen at that instant occludes nothing. `FrameCompositor::buildGpuScene` marks the occluder
(`GpuLayer::emitDepthCanvas`) and the occluded layer (`occluderItem`); `composeOnGlThread` lays the
occluder's depth out on a canvas-sized target once it is drawn (`kDepthPlaceFragShader`: depth in
r/g, cutout matte in b, coverage in a), and both layer shaders test against it. With
`cutoutEdges`, an occluder with a single media mask (a Subject/People Cutout matte) supplies the
silhouette and depth only decides in front or behind. The occluder is looked for within the same
scene, so inside a composite clip it stays inside it. Not applied in transitions or the CPU
compositor, and a 3D model counts as one flat plane at `depth`.

## Mask effects

A package with `"requires": "mask"` reads the clip's masks: every Mask-kind adjustment pinned to
the clip (Cut out subject, People Cutout, SAM, rectangle, ellipse, freeform), folded exactly as the
compositor would cut with them — feather, invert and the add/subtract/intersect ops included. The
package **consumes** the masks: while it is enabled the clip is drawn whole, so a person matte can
drive an outline, a glow or a background effect without the background disappearing. The cutout's
decontaminated foreground is not used either. The engine compiles a prelude into every pass — **do
not declare these yourself**:

| Name | Kind | Notes |
|---|---|---|
| `u_clipMask` | `sampler2D` | The folded coverage in `.r`, bound on unit 9, in the source's uv space |
| `u_hasClipMask` | `float` | **`< 0.5` means the clip has no mask** |
| `float driftMask(vec2 uv)` | helper | 1 inside the mask stack, 0 outside; 0 everywhere without a mask |

A transition with `"requires": "mask"` reads the **outgoing** clip's masks, laid out on the canvas
where that clip is drawn, and that side is drawn whole for the transition. Neither is applied on the
CPU compositor, where the package runs with `u_hasClipMask = 0` and the clip is still cut out.

## Time effects (`history`, `previousOutput`, `future`)

An effect can look back in time, and with `future` ahead too. Independent top-level fields in `effect.json`
say how (`history` and `previousOutput` below; `future` after them), and a package may set several. Each also has a `nextFeatures` name (`history:source`,
`history:previous-output`) so an older Drift rejects the package with "needs a newer Drift" instead
of failing to compile it.

```json
"history": 32,
"previousOutput": true,
"nextFeatures": ["history:source", "history:previous-output"]
```

**Source history** (`"history": N`, 1–32). The engine keeps the clip's last N source frames — the
picture before the clip's effect chain runs, one per *project* frame. The prelude, compiled into
every pass, gives the shader (do not declare these yourself):

| Name | Kind | Notes |
|---|---|---|
| `vec4 historyFrame(float age, vec2 uv)` | helper | The source frame `age` project frames ago at `uv` (1 = the previous frame, same uv convention as the source). Ages are rounded and clamped to what is recorded: past the oldest it reads the oldest, and with nothing recorded it reads the effect's own input |
| `float historyCount()` | helper | How many past frames are recorded so far: 0 on the first frame and after a seek, up to N |
| `u_historyTexture`, `u_historyGrid`, `u_historyAtlas`, `u_historyTile` | engine-bound | The atlas behind `historyFrame`, bound on unit 14. Reserved (`u_history*`) |

**Previous output** (`"previousOutput": true`). `sampler2D u_prevOutput` (unit 15, declared by the
prelude) is what this effect — its last pass's canvas — produced for the previous project frame, at
the same size and uv convention as the source. It is transparent on the first frame and after a
seek, so a feedback shader that blends `u_prevOutput` over the live picture starts as the plain
picture. Feeding it back (zoom/rotate it a little, fade it by `decay`) makes tunnels and trails.

Behaviour, shared by both:

- **A history is per clip instance and per compositor.** The preview, an export and a thumbnail
  pass each keep their own, so scrubbing the preview during an export changes nothing in the file.
- **Stepping.** Each composite looks at the clip's time in project frames
  (`round(clipTime * fps)`). The same frame again (a paused re-render after an edit) replaces what
  was recorded for it; the next frame — or the one after, when playback dropped a frame — appends;
  anything else (a seek, a scrub, a loop, a trim) clears the history (unless the package sets
  `historyExact`, below). Until it fills an effect
  shows something sensible: `historyFrame` clamps to the oldest frame recorded and a transparent
  `u_prevOutput` leaves the live picture alone.
- **`"historyExact": N` (1–2, `history` must be at least N, `nextFeatures`: `history:exact`).** When
  the ring does not already hold the previous N frames (after a seek, on a paused frame the ring
  never saw, at the first frame of an export range) the compositor decodes frames `t - k` for
  k = N..1 from the clip's own reader and seeds the ring, so the effect sees them from the first
  frame. The times go through the clip's speed, ramp and reverse mapping, so "previous" is the
  temporal previous frame. Normal playback already has them and decodes nothing extra. Not done
  while scrubbing (the approximate-seek read), for frames before the clip's start, for clips of
  composite, still-image, procedural or cutout layers, which keep the ring behaviour, or by the
  single-image executor. The seeded frames are cropped and straightened like the live one. Each
  extra frame is one more held decoder surface until the frame has rendered, which is why
  `kHwExtraFrames` has two spare.
- **Export is deterministic.** It composes frame after frame in order, so each frame is built from
  exactly the frames before it, and the history is empty at the start of the file or range.
  Starting an export mid-clip begins with an empty history at the range's first frame, except for
  `historyExact` packages, which decode the frames before it.
- **Memory is bounded.** The source ring is one atlas texture per clip, scaled down in steps when
  N full-size frames would not fit (the frames sampled by `historyFrame` can therefore be softer
  than the live one on long rings and large canvases), and the whole store is capped (768 MiB
  desktop, 256 MiB Android), dropping the least recently used history first. When a ring cannot
  be afforded the effect sees an empty history. A clip that stops being drawn loses its history.
- Standalone (unlinked) adjustment clips record the canvas below them. Transition packages have no
  history, and the single-image executor (`GpuEffectExecutor`: effect thumbnails and the CPU
  compositor) always runs with an empty one, so there a time effect shows the current frame.

**Future frames** (`"future": N`, 1–2, `nextFeatures`: `history:future`). After reading the clip's current
frame the compositor decodes the N frames after it (`t + k` project frames, `k = 1..N`, through the clip's
speed, ramp and reverse mapping, from the clip's own reader) and uploads them for the effect every composite.
They are the clip's source frames before its effect chain, cropped and straightened like the live one. The
prelude gives every pass:

| Name | Kind | Notes |
|---|---|---|
| `vec4 futureFrame(float ahead, vec2 uv)` | helper | The source frame `ahead` project frames later at `uv` (1 = the next frame). Rounded and clamped to what is available; with none available it reads the effect's own input |
| `float futureCount()` | helper | How many following frames are available: 0 at the end of the clip, when a decode fails, and while scrubbing (the approximate-seek read decodes nothing extra) |
| `u_futureTexture1`, `u_futureTexture2`, `u_futureCount` | engine-bound | The frames behind `futureFrame`, on units 13 and 12 (so a `future` package keeps at most two sampler params clear of them). Reserved (`u_future*`) |

Nothing is kept between composites, so a frame rendered cold, paused or in sequence sees the same future
frames. During playback the frame decoded as `t + 1` is the next composite's current frame and comes from
the reader's cache. Only video clips with a decoded source frame get them: nested, still-image, procedural
and cutout layers, the single-image executor and standalone adjustment clips see `futureCount() == 0`.
Each future frame is one more decoder surface held until the frame has rendered, which is why `kHwExtraFrames`
has two more spare.

**Source-unit neighbours** (`"historyUnits": "source"`, `nextFeatures`: `history:source-units`). Project frames are
the wrong unit for footage whose rate differs from the timeline's: 120 fps footage on a 30 fps timeline skips
three of every four frames, and 24 fps footage on a 60 fps one repeats them. With this field `historyExact: N` and
`future: N` (1–4 each) mean the N media frames before and after the one shown, whatever the project rate. The
ring is bypassed entirely, because it records one frame per project frame (a package with this field records
nothing and `history` is ignored): the frames are decoded from the clip's own reader by stepping the shown frame's
pts by the stream's frame duration (`PreviewVideoFrame::ptsUs` / `frameDurUs`), through the same speed, reverse
and reverse-proxy mapping as the shown frame, and are cache hits during playback. Cold, paused, sequential and
export renders therefore agree. They are not decoded while scrubbing, and not beyond the clip's trim. To bound the
cost only as many are decoded each side as one project frame spans, `max(1, ceil(sourceStep()))`, so 30 fps footage
on a 30 fps timeline gets one each side even for a package asking for four; `historyCount()` and `futureCount()`
report what was decoded. The frames share one atlas (past slots first, then future), sampled by the unchanged
`historyFrame(age, uv)` and a `futureFrame(ahead, uv)` that reads the slots after them, so a package keeps its
sampler units (`historyFrame` on 14, no unit 12 or 13); tiles shrink with the same plan as the ring when they
would not fit. Each decoded frame is one decoder surface held until the frame renders: `kHwExtraFrames` covers all
eight. The prelude adds:

| Name | Kind | Notes |
|---|---|---|
| `float sourceFrameRate()` | helper | The media's frame rate (the stream's), in frames per second; the project's when unknown |
| `float projectFrameRate()` | helper | The project's frame rate |
| `float sourceStep()` | helper | How many media frames one project frame advances: media rate / project rate times the clip's speed (0 for a frozen frame) |
| `float sourcePhase()` | helper | 0..1: how far this instant is between the shown media frame and the next one in playback order. Frame `j` (0 = shown) holds from `j - sourcePhase()` to `j + 1 - sourcePhase()` source frames from the instant |
| `u_sourceFps`, `u_sourcePhase`, `u_sourceStep`, `u_projectFps`, `u_futureCount`, `u_futureSlot` | engine-bound | Behind the helpers. Reserved |

`effects/motion_blur` (`motion.blur`) is a `historyUnits: "source"`, `historyExact: 4`, `future: 4` package. Its
`mode` choice (Auto, Optical flow, Frame blend) picks between two methods; Auto blends when `sourceStep() >= 2`
(at least two real frames per project frame, counting the clip's speed) and uses flow otherwise. Frame blend
averages the media's frames over a shutter centred on the instant and `shutter / 360 * strength` project frames
long: each frame counts by how much of its one-frame hold lies inside, and frames that were not decoded count
nothing. In blend mode `luma4.frag` writes zeros, so the matching passes find nothing to search and stay cheap,
and `blur.frag` does the average. Flow mode runs on the adjacent media frames and scales the flow by
`sourceStep()` to a project frame. Flow, in either unit, works like this: it estimates
optical flow from the current frame to both neighbours (`historyFrame(1.0, uv)` and `futureFrame(1.0, uv)`) on
a three-level luma pyramid (1/64, 1/16 and 1/4 size; the three frames' luma share one buffer's R, G and B;
block matching with each level refining the coarser one, sub-pixel at the finest, and a relative accept test
so flat-ish areas keep their motion), then a flow-aware smoothing pass per direction that also fills the flat
inside of a moving object. A tile-max pass (1/16 size, over both directions' magnitudes) and a 3x3
neighbour-max pass give each pixel the longest velocity near it, and the final pass samples after McGuire et
al.'s reconstruction filter, weighting each tap by whether its own velocity reaches the pixel, so a moving
object smears past its outline onto the background. The shutter is centred on the frame: its earlier half
follows the flow towards the previous frame and its later half the flow towards the next, so accelerating
or curving motion bends the smear. Where the two flows disagree by more than acceleration explains
(an occlusion), the direction that matches the footage better is mirrored for the whole shutter; with a frame
missing on one side (the clip's first or last frame, a scrub) the other direction is mirrored. Taps are jittered
per pixel. The smear length is the flow scaled by `shutter / 360 * strength * sourceStep()`, capped by `maxBlur`; `strength`
above 1 exaggerates beyond a real shutter. It passes the picture through only when neither neighbour exists.
It runs 14 passes over 13 buffers (`luma4/16/64`, `flow64/16/4` and `flow4s` for backward flow, the same with
an `f` suffix for forward flow, `tile16`, `nbr16`; flows are `rg16f`).

`effects/motion_smooth` (`motion.smooth`, "Motion Smoothing") is a `historyUnits: "source"`, `future: 1` package that
invents the frames between the media's own. Its `multiplier` choice (x2, x3, x4, x5, x8) makes the output behave like
footage at the media rate times the multiplier: `sourcePhase()` is quantised down to a multiple of 1/multiplier, and
phase 0 (the shown frame itself), the clip's last frame and scrubbing pass the shown frame through untouched. The
frames interpolated are the shown frame `u_currentTexture` (A) and `futureFrame(1.0, uv)` (B), the media's next
frame; B is the raw source, so effects above Motion Smoothing in the stack are not in it and it belongs first. Flow
is measured both ways (A to B and B to A) on the Motion Blur pyramid (`luma4/16/64` hold A in R and B in G;
`match*` and `match*r` are the two directions; `smooth` as there; flows are `rg16f`), 12 passes. `interpolate.frag`
then looks x up in A at the `a` solving `a + p * AB(a) = x` and in B at the `b` solving `b + (1 - p) * BA(b) = x`
(three fixed-point steps), and blends them by `(1 - p)` and `p` times a confidence: the flow's round trip
(A to B and back) must land within a pixel or two of where it started and the two ends must agree in colour, so a
side that is being covered or uncovered counts for little. With both sides unreliable the nearer frame is shown
unwarped. It only helps when the media's frame rate times the clip's speed is below the project's; the
description says so and the Effects inspector shows a notice (`framesCoverProject` in the clip map, from
`drift::framesAlreadyCoverProject`) when it is not. The reader's cache answers with the nearest frame, so
`fillGpuLayerSourceNeighbours` steps back to the frame at or before the instant when the cache returned the one
after it; without that the phase would be clamped to 0 and the effect would do nothing on half the frames.

Compositor code that wants the previous source frame of a clip sets `GpuLayer::sourceHistoryFrames` and, after `buildLayerTarget` has run for the layer, calls
`drift::gl::pastSourceFrame(rt, layer.historyScope, layer.historyKey, age)` (FrameHistory.h). The
result is a texture plus the `uv` rectangle of that frame inside it.

The Time effects in the Trending Effects addon (Feedback, Trails, Time Echo, Frame Delay, Slit
Scan) use the `time` category. The catalog has no fixed list beyond its five built-ins: any other
category slug appears as an extra group, labelled with the capitalised slug.

## Float intermediate buffers

`"format": "rgba16f" | "rg16f"` on an `intermediateBuffers` entry allocates it as RGBA16F, so a pass can
store signed or large values (flow vectors, accumulators) without packing them into 8 bits. `rg16f` is
allocated as RGBA16F too (Qt's framebuffer object has no two-channel half-float), so it saves no memory.
The buffer pool keys on the format, so 8-bit and half-float targets never mix.

Where the context cannot render to half float (a GLES 3.0/3.1 or WebGL2 context without
`EXT_color_buffer_float` or `EXT_color_buffer_half_float`, or an allocation that fails), every
buffer of the pipeline is allocated as RGBA8 instead and a single `qInfo` is logged. A shader
must therefore work either way. A package with a half-float buffer gets this prelude:

- `uniform float u_floatBuffers`: 1 when the buffers are half float, 0 on the fallback.
- `vec4 packFlow(vec2 v)` / `vec2 unpackFlow(vec4 texel)`: store and read a signed vec2 in
  [-0.5, 0.5]. Half float keeps it as is in `.rg`; the fallback keeps 16 bits per component in the
  `.rg` and `.ba` byte pairs. Use them for every read and write of such a buffer and the same shader
  runs on both paths. A shader that needs a different range tests `u_floatBuffers` itself.

`effects/motion_blur` keeps all its flow, tile-max and neighbour-max buffers as `rg16f` through these helpers.

## 3D face mesh (`backend: "model3d"`)

Not a fragment pipeline — there is no `.frag`. `face_mesh_3d` declares parameters only; the engine
warps the rest-pose Surrey Face Model (`sfm_face.bin`) onto the baked 468-point track and composites
it over the frame.

- Orthographic MVP from `(faceCenter, faceRx, pose, user params, aspect)` — never a pixel size, so
  preview at `renderScale 0.5` and export at 1.0 produce a bit-identical matrix. Depth maps as
  `z_ndc = −z_wn / 4` so `+forward` (toward the viewer) is nearer in the GL depth buffer.
- Lighting is **screen-space** (fixed relative to the frame) and approximate Blinn-Phong in sRGB —
  the rest of the compositor is untagged 8-bit, so linearizing the mesh alone would look foreign.
- Translucent fill plus a barycentric wireframe (`fillOpacity` / `wireframe` / `wireColor` /
  `wireWidth`). Fill+wire uses a geometry shader.
- `warpMesh` is a fixed param. The overlay writes depth (clip-z flipped so MediaPipe nearer-is-smaller
  wins `GL_LESS`; no in-buffer blend) so the nearest surface wins. Draw is two-sided: a real frontal
  pose has `forward.z < 0` (half-turn about X), which reverses winding versus an identity quaternion,
  and `GL_BACK` would cull the whole face. Without `hasMesh`, the effect skips.
- Failure mode: skip the effect, keep the frame. Empty rest-mesh path is silent.
