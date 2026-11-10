# Procedural 3D

A Procedural clip is a whole 3D scene built from code. A Lua script builds meshes, lights and optionally a camera for every frame, and shaders you write in GLSL draw them. There is no default material: a package always carries its own shaders. The clip goes on a graphic (Shape) track and behaves like any 3D layer: it occludes and is occluded by other 3D clips, and the orbit view in the 3D workspace shows it as world geometry.

This file is also what the MCP tool `procedural_authoring_guide` returns, so what you read here is what an agent reads.

Contents: [package layout](#package-layout), [the manifest](#the-manifest), [Lua API](#lua-api-v1), [budgets](#budgets), [determinism](#determinism), [performance](#performance), [the shader contract](#shader-contract), [raymarching](#raymarching-with-gl_fragdepth), [worked example](#worked-example), [MCP workflow](#mcp-workflow).

## Package layout

A package is a folder, normally `<AppData>/procedurals/<slug>/` (this is where `create_procedural` and the Procedural workspace write):

```
procedural.json   manifest (required)
main.lua          the script, run once per frame (required)
<name>.vert       vertex shader   } a pair: both halves are required;
<name>.frag       fragment shader }   <name> is what scene:add refers to
thumbnail.webp    optional card image
```

At least one shader pair is required, at most 32. Limits: manifest 256 KiB, `main.lua` 1 MiB, each shader 256 KiB. A half without its partner makes the whole package invalid. Package ids made by the tools are always `user.<slug>`.

`DRIFT_PROCEDURALS_DIR` (a path list) adds extra search roots.

## The manifest

`procedural.json`:

```json
{
  "displayName": "Orb Field",
  "description": "A grid of lumpy spheres lit by two orbiting lights.",
  "category": "demo",
  "order": 900,
  "maxVertices": 400000,
  "maxInstances": 4096,
  "parameters": [
    {"identifier": "count", "displayName": "Grid size", "type": "int", "minValue": 2, "maxValue": 16, "defaultValue": 8}
  ]
}
```

- `id` is set for you by `create_procedural` (`user.<slug>`). `displayName`, `description`, `category` (a free slug) and `order` are presentation.
- `maxVertices` and `maxInstances` are optional. Each is clamped to the platform budget (see [Budgets](#budgets)); use them to make a package fail early on a mistake instead of grinding.
- `parameters` use the same schema as custom effects (see `custom-effects.md`, or the `effect_authoring_guide` tool). Common fields: `identifier`, `displayName`, `type`, `defaultValue`, `minValue`, `maxValue`, `group`.

| type | inspector control | in Lua `params.<id>` | in GLSL |
|---|---|---|---|
| `float` (default) | slider | number | `uniform float` |
| `int` | slider on whole numbers | number | `uniform float` (rounded) |
| `bool` | switch | boolean | `uniform float` 0/1 |
| `enum` / `choice` | dropdown (`options`) | number (option index) | `uniform float` |
| `color` | swatch | `{x, y, z}` = r, g, b in 0..1 (also `[1]..[3]`); with `"alpha": true` a fourth `w` / `[4]` | `uniform vec3` / `vec4` |
| `vec2` / `point` | 2D pad | `{x, y}` (also `[1]`, `[2]`) | `uniform vec2` |
| `region` | box | `{x, y, w, h}` | `uniform vec4` |

Every parameter is also bound as a shader uniform under its own `identifier`, whenever the shader declares one. Names that clash with the shader contract (`u_model`, `u_time`, `u_light*`, ...) are rejected. Sampler-style parameters (`image`, `gradient`, `curve`, `lut`, `track`, `audio`) are not bound in v1.

Numeric, bool, enum and colour-channel parameters can be keyframed. Over MCP the property is `proc.<identifier>`; a colour animates per channel (`proc.tint.r`, `.g`, `.b`, `.a`) and a vec2 as `proc.origin.x`, `proc.origin.y`.

## Lua API (v1)

Lua 5.4, sandboxed. Available libraries: base, `table`, `string`, `math`, `utf8`. Removed: `io`, `os`, `package`, `debug`, `require`, `dofile`, `loadfile`, `collectgarbage`, `string.dump`, `math.random` / `math.randomseed` (use `rand.new`). `load` accepts source text only. `print` goes to the console (and to the MCP result's `log`). `main.lua` runs from the top on a fresh state every frame; nothing carries over between frames.

### Conventions

- Units are project pixels. The origin is the canvas top-left, +X right, **+Y down**, **+Z away from the viewer**. A right-handed system.
- Angles are radians everywhere: `mat.rotate`, `fov`, a spot light's `cone`.
- `mat.rotate(rx, ry, rz)` is Euler: X, then Y, then Z.
- `mat.lookAt` builds a view matrix whose camera looks down +Z, with +X right and +Y up in view space. The default `up` is `(0, -1, 0)` (up on screen is -Y in world).
- `geo.grid` faces -Z, toward the default viewer. The outward normal of a triangle is `(v1 - v0) x (v2 - v0)`.
- Mesh methods change the mesh in place and return it, so they chain. Use `mesh:copy()` for an independent copy.
- Buffer methods `:add :mul :lerp :map :slice :noise` return a **new** buffer; `:fill` changes the buffer in place.
- A vec3 argument is a table `{x, y, z}` or `{x = , y = , z = }`. A mat4 is a `mat` userdata; two matrices multiply with `*`.

### Globals

| name | contents |
|---|---|
| `time` | clip source time in seconds. Trimming the head by 2 s starts the script at `time = 2` |
| `frame` | frame index at the project frame rate |
| `params` | the resolved parameter values, typed as in the table above |
| `seed` | the clip's integer seed |
| `canvas` | `{w, h}`, the project size in pixels |
| `camera` | read-only: `eye`, `right`, `up`, `forward` (vec3 tables), `fov` (radians, vertical), `near`, `far`, `view`, `proj` (`mat`). It is the Camera clip covering the procedural clip, or the default flat 2D camera |
| `scene` | the output; see below |
| `geo`, `buf`, `mat`, `noise`, `rand` | the libraries below |

Reading anything from `camera` marks the scene as camera-dependent: it is re-evaluated when the camera moves. A script that never touches `camera` is evaluated once per time value.

### `geo` primitives

| call | result |
|---|---|
| `geo.grid(w, h, nx, ny)` | flat grid in the XY plane centred on the origin, facing -Z. `nx`, `ny` default to 1 |
| `geo.box(w, h, d)` | box centred on the origin, 24 vertices |
| `geo.sphere(r, seg, rings)` | UV sphere; defaults 32 and 16 |
| `geo.cylinder(r, h, seg)` | capped cylinder; default 32 segments |
| `geo.torus(R, r, seg, tube)` | torus around the Y axis; defaults 32 and 16 |
| `geo.line(points)` | line strip through a table of vec3 or a vec3 buffer (2 or more points) |
| `geo.points(n_or_buffer)` | `n` points at the origin, or one per element of a vec3 buffer |
| `geo.merge(a, b, ...)` | one mesh from several, with the same topology; meshes that have instances cannot be merged |

Lines draw as 1 px GL lines and points as GL points (set `gl_PointSize` in the vertex shader).

### Mesh methods

| call | effect |
|---|---|
| `m:transform(mat)` | transforms positions and normals (flipping winding if the matrix mirrors) |
| `m:set(name, buf)` | `"position"` (vec3), `"normal"` (vec3), `"uv"` (vec2), `"color"` (vec4), or a custom attribute name (float, vec2, vec3 or vec4; up to 8 per mesh). The buffer length must equal the vertex count |
| `m:get(name)` | a copy as a buffer, or `nil` when the mesh has none |
| `m:displace(spec, amount)` | moves each vertex along its normal by `noise * amount`. `spec` is a frequency number or `{freq=, octaves=, offset={x,y,z}, kind="perlin"\|"simplex"}` |
| `m:recomputeNormals()` | smooth normals; vertices that share a position are welded for shading |
| `m:bounds()` | `min, max` as two vec3 tables, or `nil` when empty |
| `m:instances(buf)` | draws the mesh once per element: a mat4 buffer, or a vec4 buffer of `(x, y, z, uniformScale)`. Without an argument returns the instance count |
| `m:copy()` | independent copy |
| `m:count()` | vertex count, triangle (or line) count |

### Buffers (`buf`)

A buffer is a typed array of `float`, `vec2`, `vec3`, `vec4` or `mat4` elements, 1-based, readable and writable as `b[i]` (a number for `float`, a table otherwise) with `#b` as the length.

- `buf.new(kind, n)` zero-filled; `buf.from(table [, kind])` from numbers, vectors or matrices. With `kind` given, the table is a flat list of numbers.
- `b:fill(value)` sets every element (a number fills all components of a vector).
- `b:add(x)`, `b:mul(x)` with a number, a buffer of the same kind, or a float buffer (one scalar per element). `b:lerp(other, t)` with `t` a number or a float buffer. `b:slice(first, last)`.
- `b:noise(freq, octaves, ox, oy, oz)` and `noise.field(b, ...)`: a float buffer of fractal noise sampled at each element treated as a position (float, vec2 or vec3 buffers).
- `b:map(fn)` calls `fn(value, index)` per element and returns a new buffer. The value is a number for float buffers and a table otherwise; `fn` must return the same shape. Slow and counted against the work budget; see [Performance](#performance).
- `b:kind()` returns the kind name.

### `mat`, `noise`, `rand`

- `mat.identity()`, `mat.translate(x, y, z)`, `mat.rotate(rx, ry, rz)`, `mat.scale(sx [, sy, sz])` (one argument scales uniformly), `mat.compose(translation, rotation, scale)` (each a vec3; scale may be a number), `mat.lookAt(eye, target [, up])`, `mat.perspective(fovy, aspect, near, far)` (z in [-1, 1]).
- Matrix methods: `m:apply(x, y, z)` returns the transformed point as three numbers, `m:inverse()`, `m:toTable()` (16 numbers, column-major).
- `noise.perlin(x, y, z)`, `noise.simplex(x, y, z)`: about -1..1. `noise.fbm(x, y, z, octaves, lacunarity, gain)`. Unused coordinates default to 0.
- `rand.new(seed)` returns a generator `r` with `r:float()` (0..1), `r:range(a, b)`, `r:int(a, b)` inclusive, `r:normal()`, `r:unit()` (three numbers, a random unit vector). A new generator with the same seed repeats the same sequence.

### `scene`

- `scene:add(mesh, "shaderName", {uniform = value, ...})` adds a mesh drawn with the shader pair `shaderName.vert` / `shaderName.frag`. Uniform values are a number, boolean (sent as 0/1), a table of 2 to 4 numbers (vec2 to vec4) or a `mat`. The shader declares the matching GLSL type. The mesh is copied at that moment, so changing it later does not affect the scene. A shader name the package does not have is an error caught by `create_procedural`.
- `scene:light{type = "point"|"directional"|"spot", pos = , dir = , color = , intensity = , range = , cone = }`. Defaults: point, white, intensity 1, range 0 (unlimited), cone 0.5 rad. `dir` is the direction the light shines (normalised for you). Lights belong to this clip only and reach only its shaders. The renderer uses the first 8.
- `scene:camera{eye = , target = , up = , fov = , near = , far = }`: the script's own camera. See [camera modes](#camera-modes).

## Budgets

Going over a budget gives an error scene: the clip renders nothing, and the error appears in the console, in `evaluate_procedural` and in `create_procedural` / `update_procedural` results.

| platform | vertices | instances | Lua memory |
|---|---|---|---|
| desktop | 1,000,000 | 100,000 | 64 MB |
| Android | 300,000 | 30,000 | 24 MB |
| wasm | 150,000 | 15,000 | 24 MB |

- Vertices and instances are summed over the whole scene. The manifest's `maxVertices` / `maxInstances` can only lower them.
- Time limit: 250 ms per evaluation in preview, 5 s in export. The check runs every 1000 VM instructions, so the limit cannot be caught with `pcall`. A pathological `string.find` pattern is the one thing it cannot interrupt.
- Work limit: the elements touched by native operations and `:map` callbacks are counted against a per-frame allowance of 16 times the vertex budget. A loop that creates many individually legal meshes still stops.
- Budget, deadline and memory errors abort the script and cannot be swallowed by `pcall`.
- Lights: up to 32 may be created, 8 are drawn. `print` keeps the last 200 lines.

## Determinism

The same inputs must give the same scene, because frames are cached, evaluated on several threads (preview, editor view, export) and compared against exports.

- Use `rand.new(seed)` for randomness. Mix in `seed` (the clip's seed) so clips differ, for example `rand.new(seed * 1000 + i)`. There is no clock; `math.random` is removed.
- `time` and `frame` are the only things that vary.
- `pairs()` order is **unspecified**. Iterate arrays with `ipairs` or a numeric `for`, and sort keys before using them. (The bundled Lua fixes its hash seed, but Flatpak builds use the system Lua where the order can vary between runs.)
- Do not depend on floating point formatting in `tostring`.

## Performance

The script runs every frame, on the compositor thread, within 250 ms in preview.

- Prefer bulk native operations to loops: `buf:mul`, `buf:add`, `buf:lerp`, `buf:noise`, `m:displace`, `m:transform`, `m:instances`, `geo.merge`. They run in C++.
- `buf:map(fn)` and per-element Lua loops call back into the interpreter for every element. They are fine for hundreds of elements and a trap for hundreds of thousands. When you need per-element variation, build a float buffer with `:noise` or arithmetic and use it as a mask, or move the variation into the shader (a custom per-vertex attribute via `m:set`, and the time in `u_time`).
- Draw many copies of one mesh with `m:instances(...)` rather than `geo.merge` of copies: instances share one vertex buffer and count against a much larger budget.
- Expensive work that does not depend on `time` is still redone each frame; keep resolutions modest (a 24 x 12 sphere is usually enough) and let the shader do the shading.
- GPU buffers are cached by mesh content, so a mesh that is identical from one frame to the next costs nothing to upload.

## Shader contract

Write `#version 330 core`; Drift translates to GLSL ES 3.00 on mobile and in the browser. No compute or geometry shaders. Each `<name>.vert` + `<name>.frag` pair is one material. Output is **straight** (not premultiplied) alpha.

### Vertex attributes

| location | name | type | when the mesh has none |
|---|---|---|---|
| 0 | `a_position` | vec3 | always present |
| 1 | `a_normal` | vec3 | 0 |
| 2 | `a_uv` | vec2 | 0 |
| 3 | `a_color` | vec4 | white |
| 4 to 11 | `a_<name>` | float to vec4 | 0 |
| 12 to 15 | `a_instanceMatrix` | mat4 | identity |

Declare attributes with plain `in`. The custom ones come from `m:set("<name>", buf)` and are matched by name (`a_<name>`); they take slots 4 to 11 in the order the vertex shader declares them, or at the slot you give with `layout(location = N)`. A shader that declares more than 8 custom attributes fails to compile.

`a_instanceMatrix` is a per-instance attribute (divisor 1). The renderer does **not** apply it for you: multiply it in yourself, `u_model * a_instanceMatrix * vec4(a_position, 1.0)`. For an uninstanced mesh it is the identity, so the same shader works for both. Lines and points use the same attributes.

### Uniforms

| uniform | type | meaning |
|---|---|---|
| `u_model` | mat4 | script space to world; carries the clip's own position, rotation and tilt |
| `u_view`, `u_proj` | mat4 | see [camera modes](#camera-modes) |
| `u_normalMatrix` | mat3 | inverse-transpose of `u_model`'s upper 3x3 |
| `u_cameraPos` | vec3 | the eye in world space |
| `u_time` | float | clip source time in seconds (same as `time`) |
| `u_timeUs` | float | the same in microseconds |
| `u_frameIndex` | int | frame number |
| `u_frameRate` | float | project frame rate |
| `u_resolution` | vec2 | the project canvas in pixels |
| `u_renderResolution` | vec2 | the size actually being rendered (preview may be smaller) |
| `u_audioLevel`, `u_audioBass`, `u_audioBeat` | float | the timeline mix, as for effects |
| `u_lightCount` | int | number of lights, 0 to 8 |
| `u_lightPos[8]` | vec3 | positions in world space |
| `u_lightDir[8]` | vec3 | unit directions the light shines (spot and directional) |
| `u_lightColor[8]` | vec3 | colour already multiplied by intensity |
| `u_lightParams[8]` | vec3 | `x` type (0 point, 1 directional, 2 spot), `y` range (0 = unlimited), `z` cone half-angle in radians |

Lights are written in script space and mapped through `u_model` to world space, so they follow the clip when it moves. Every manifest parameter is also a uniform under its own name, and so is each key of the uniforms table passed to `scene:add`. Uniforms the shader does not declare are ignored.

### Camera modes

The mode is decided per frame by the script.

**World mode** (the script never calls `scene:camera`). Geometry is in project world space and shares depth with the other 3D clips, seen through the scene camera (the covering Camera clip, or the default flat camera). In the 3D workspace's orbit view only the drawing view changes; `camera` in Lua still reports the scene camera. Here `u_view * world` is **not** a rigid view matrix: it takes world pixels to homogeneous canvas pixels and `u_proj` takes those to clip space, so write `gl_Position = u_proj * u_view * world` and nothing cleverer. Do not rely on `u_view` for eye-space depth or on `u_proj` being a perspective matrix. Use `u_cameraPos` and world positions for lighting and ray directions.

**Own-camera mode** (the script calls `scene:camera{...}`). The clip becomes a flat full-canvas layer: `u_view` is the `lookAt` view matrix, `u_proj` is a perspective projection (y-flipped so the picture is upright), `u_model` is the identity, and the result does not take part in shared depth. The editor view draws a frustum guide for the camera. `gl_Position = u_proj * u_view * world` is again the right formula.

### Faces, depth and blending

- There is no back-face culling. The renderer sets `glFrontFace(GL_CW)` so that `gl_FrontFacing` is true for a mesh's outward (counter-clockwise) winding as the generators produce it. For a closed mesh `if (!gl_FrontFacing) n = -n;` lights the inside correctly; discard back faces yourself if you want culling.
- The depth test is `GL_LESS`. **Fragments with alpha still write depth**, so use `discard` for cutouts rather than relying on alpha to hide a pixel. Within one clip, draw order is the order of `scene:add`; there is no sorting, so soft transparency between overlapping meshes depends on that order.
- Output straight alpha: `fragColor = vec4(rgb, alpha)` with `rgb` not multiplied by `alpha`.
- `gl_FragDepth` is respected, which is what makes raymarching possible.
- Supersampling (2x at 1080p and below) is applied to the clip, so edges are smooth without MSAA code in the shader.

## Raymarching with `gl_FragDepth`

Ray tracing is not built in. A fragment shader that marches a signed distance field and writes `gl_FragDepth` gives the same look, and the result depth-sorts against other 3D clips in world mode.

1. In `main.lua` add a quad that covers the canvas at some depth, once, and pass anything animated as uniforms:

```lua
local quad = geo.grid(canvas.w, canvas.h, 1, 1):transform(mat.translate(canvas.w / 2, canvas.h / 2, 0))
scene:add(quad, "march", {radius = 120 + 20 * math.sin(time)})
```

2. In the vertex shader, pass the world position through (`v_world`), and in the fragment shader build a ray from the eye through it:

```glsl
vec3 ro = u_cameraPos;
vec3 rd = normalize(v_world - u_cameraPos);
float t = 0.0;
bool hit = false;
for (int i = 0; i < 96; ++i) {
    vec3 p = ro + rd * t;
    float d = length(p - vec3(u_resolution * 0.5, 0.0)) - radius;   // your signed distance function
    if (d < 0.5) { hit = true; break; }
    t += d;
    if (t > 20000.0) break;
}
if (!hit) discard;
vec3 p = ro + rd * t;
vec4 clip = u_proj * u_view * vec4(p, 1.0);
gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
```

3. Shade `p` with a normal from the SDF gradient and the `u_light*` uniforms.

Always `discard` the misses (alpha fragments would still write depth and cut a hole in what is behind). Keep the loop count bounded (mobile GPUs are slow here) and the step cheap.

## Worked example

A grid of lumpy spheres (one displaced sphere mesh drawn as instances) lit by two orbiting point lights, with one shader pair. The three files below are the whole package; they are the ones the tests create and validate.

### procedural.json

```json
{
  "displayName": "Orb Field",
  "description": "A grid of lumpy spheres lit by two orbiting lights.",
  "category": "demo",
  "maxVertices": 100000,
  "maxInstances": 1024,
  "parameters": [
    {"identifier": "count", "displayName": "Grid size", "type": "int", "minValue": 2, "maxValue": 16, "defaultValue": 6},
    {"identifier": "spacing", "displayName": "Spacing", "type": "float", "minValue": 40, "maxValue": 300, "defaultValue": 130},
    {"identifier": "radius", "displayName": "Radius", "type": "float", "minValue": 10, "maxValue": 120, "defaultValue": 45},
    {"identifier": "bump", "displayName": "Bumpiness", "type": "float", "minValue": 0, "maxValue": 0.6, "defaultValue": 0.25},
    {"identifier": "speed", "displayName": "Speed", "type": "float", "minValue": 0, "maxValue": 4, "defaultValue": 1},
    {"identifier": "tint", "displayName": "Tint", "type": "color", "defaultValue": "#4fa3ff"}
  ]
}
```

### main.lua

```lua
local n = math.floor(params.count)
local t = time * params.speed

-- One lumpy unit sphere; the noise drifts with time. Instances scale it to `radius`.
local orb = geo.sphere(1, 24, 12)
orb:displace({freq = 1.4, octaves = 2, offset = {t * 0.5, 0, 0}}, params.bump)

-- (x, y, z, scale) per instance, centred on the canvas.
local cx, cy = canvas.w * 0.5, canvas.h * 0.5
local half = (n - 1) * 0.5
local inst = buf.new("vec4", n * n)
for j = 0, n - 1 do
    for i = 0, n - 1 do
        local k = j * n + i
        local wave = math.sin(t * 2 + (i + j) * 0.7)
        inst[k + 1] = {
            cx + (i - half) * params.spacing,
            cy + (j - half) * params.spacing,
            wave * 40,
            params.radius * (0.85 + 0.15 * wave),
        }
    end
end
orb:instances(inst)

scene:add(orb, "lit", {shininess = 28})

-- Two lights orbiting the grid in opposite directions.
local reach = n * params.spacing * 0.5
scene:light{type = "point", pos = {cx + math.cos(t) * reach, cy + math.sin(t) * reach * 0.6, -220},
            color = {1.0, 0.55, 0.3}, intensity = 1.3, range = reach * 3}
scene:light{type = "point", pos = {cx - math.cos(t) * reach, cy - math.sin(t) * reach * 0.6, -220},
            color = {0.3, 0.8, 1.0}, intensity = 1.1, range = reach * 3}
```

### lit.vert

```glsl
#version 330 core
in vec3 a_position;
in vec3 a_normal;
in mat4 a_instanceMatrix;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normalMatrix;

out vec3 v_world;
out vec3 v_normal;

void main()
{
    vec4 world = u_model * a_instanceMatrix * vec4(a_position, 1.0);
    v_world = world.xyz;
    v_normal = normalize(u_normalMatrix * mat3(a_instanceMatrix) * a_normal);
    gl_Position = u_proj * u_view * world;
}
```

### lit.frag

```glsl
#version 330 core
in vec3 v_world;
in vec3 v_normal;
out vec4 fragColor;

uniform vec3 u_cameraPos;
uniform int u_lightCount;
uniform vec3 u_lightPos[8];
uniform vec3 u_lightColor[8];
uniform vec3 u_lightParams[8];

uniform vec3 tint;
uniform float shininess;

void main()
{
    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing)
        n = -n;
    vec3 v = normalize(u_cameraPos - v_world);
    vec3 color = tint * 0.12;
    for (int i = 0; i < 8; ++i) {
        if (i >= u_lightCount)
            break;
        vec3 toLight = u_lightPos[i] - v_world;
        float dist = length(toLight);
        vec3 l = toLight / max(dist, 1e-4);
        float range = u_lightParams[i].y;
        float falloff = range > 0.0 ? clamp(1.0 - dist / range, 0.0, 1.0) : 1.0;
        float diffuse = max(dot(n, l), 0.0);
        float specular = pow(max(dot(n, normalize(l + v)), 0.0), shininess);
        color += (tint * diffuse + vec3(specular)) * u_lightColor[i] * falloff;
    }
    fragColor = vec4(color, 1.0);
}
```

`tint` is the manifest colour parameter (bound as a `vec3`); `shininess` comes from the uniforms table in `scene:add`. This script never reads `camera`, so it is evaluated once per time value regardless of camera moves.

## MCP workflow

1. `procedural_authoring_guide` returns this file.
2. `create_procedural({slug, manifest, files})` with `files` = `{"main.lua": ..., "<name>.vert": ..., "<name>.frag": ...}`. The package is parsed, `main.lua` is run at `t = 0` with default parameters and camera, and every shader pair is compiled on the GL thread, all before anything is installed. The reply has `stats` (counts, `elapsedMs`), `log` (your `print` output), `shaders` (`{name: {ok, log}}`) and, on failure, `errors[]` with the Lua error or the driver log prefixed by the shader name. When OpenGL is unavailable the shaders are not compiled and `shaderCheck` is `"skipped"`.
3. `evaluate_procedural({id, t, params})` runs the script without rendering and reports `stats`, `bounds` (world-space AABB over all instances), `meshes`, `lights`, `cameraMode` (`world` or `own`, with the camera) and `errors`. Use it to see how big the scene is and whether it sits on the canvas, for example the bounds against `canvas`, before placing it.
4. `add_procedural({id, at, duration, params, seed})` places the clip. Change parameters over time with `set_keyframe({prop: "proc.<identifier>", ...})`.
5. `update_procedural({id, files, manifest, remove_files})` iterates; only the files you pass change. `get_procedural_source` reads any package, bundled ones included, to fork; `delete_procedural` removes a user package.

When the script reports an error the clip renders nothing, and the message is shown in the Procedural workspace console and in `evaluate_procedural`. A clip whose package is missing keeps its data and shows a warning badge.
