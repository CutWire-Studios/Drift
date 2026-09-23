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
| `pipeline` | `intermediateBuffers` + `passes` |

### Parameter types

| `type` | Binds as | Notes |
|---|---|---|
| `float` (default) | `float` | `minValue`/`maxValue`/`defaultValue`; keyframable |
| `bool` / `boolean` | `float` (0 or 1) | Rendered as a switch; not keyframable |
| `color` / `colour` | `vec3` | `defaultValue` is a `"#rrggbb"` string; rendered as a swatch |
| `file` | *(not bound)* | Absolute path string; `fileFilters` for the picker. Used by `model3d` |

Colour parameters bind as **`vec3`** — alpha is dropped, so declare a separate opacity float if you
need one. Any alpha in `defaultValue` is discarded at parse time and the value is normalized to six
digits. Colours and file paths are **not keyframable**: the whole keyframe stack is typed `double`.

Pass inputs: `source_texture` (+ optional `index`), `buffer` (+ `id`), or `texture` (+ `id`). Multiple inputs bind as `u_currentTexture` (unit 0) and `u_texture1`…  
Pass outputs: `buffer` or `canvas`.

`pipeline.textures[]` declares static image assets loaded once from the package dir:

```json
"textures": [{ "id": "glyphs", "file": "glyphs.png" }]
```

## GLSL

- `#version 330 core`
- Reserved: `u_currentTexture`, `u_textureN`, `u_resolution`, `u_time`, `u_timeUs`, `u_frameIndex`, `u_progress`, `u_fromTexture`, `u_toTexture`, `u_depth*`, `u_hasDepth`, `u_face*`

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

Both `u_faceValid` and `u_faceHasContours` must be checked by anything using the loops:

```glsl
if (u_faceValid < 0.5 || u_faceHasContours < 0.5) { fragColor = texture(u_currentTexture, v_texCoord); return; }
```

**Uniform budget.** The seven loops together are 256 components. GL 3.3 core guarantees at least
1024 fragment default-block components, and no shipping package declares more than about 220 — but
this is why the full 468-point mesh is not delivered this way.

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

## Special case: time_echo

History frames are still decoded in `FrameCompositor`; blending runs on the GPU via `GpuEffectExecutor::blendTimeEcho` (CPU fallback if GL is unavailable).

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
