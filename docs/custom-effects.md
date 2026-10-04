# Custom effects

A custom effect is a folder holding a JSON manifest, plus shaders for the GPU kinds. Drift loads it
the same way it loads the bundled packages. Folders under `<AppData>/effects`,
`<AppData>/transitions` and `<AppData>/audio-effects` appear under **My Effects** in the browsers.
They get there in one of three ways: imported from a `.driftfx`, dropped in by hand, or written by an
agent over MCP (see [MCP.md](MCP.md#custom-effects)).

| Kind | Manifest | Code | Reference |
|---|---|---|---|
| Video effect | `effect.json` | GLSL fragment shaders | [gpu-effects.md](gpu-effects.md) |
| Transition | `transition.json` | GLSL fragment shaders | [gpu-transitions.md](gpu-transitions.md) |
| Audio effect | `audio-effect.json` | none: wraps a built-in processor | below |

The MCP op `effect_authoring_guide` returns the same material, written for agents. Keep the two in
step: its text lives in `src/mcp/McpEffectAuthoring.cpp`.

## Parameters → inspector controls

Every entry in `parameters[]` becomes one control in the inspector. For GPU kinds it also becomes a
uniform named by its `identifier`.

| `type` | Control | GLSL | Fields |
|---|---|---|---|
| `float` (default) | Slider | `float` | `minValue`, `maxValue`, `defaultValue`; `step` snaps the slider. Keyframable |
| `int` | Slider snapping to whole numbers | `float` (rounded) | `minValue`, `maxValue`, `defaultValue`. Keyframable |
| `bool` | Switch | `float` 0/1 | `defaultValue` 0 or 1. Keyframable (rounds at 0.5) |
| `color` | Swatch and picker | `vec3`, or `vec4` with `"alpha": true` (adds an opacity slider) | `defaultValue` `"#rrggbb"` / `"#rrggbbaa"`; `swatches`: preset shades; `enables`: a bool parameter switched on when a colour is picked. Keyframable |
| `vec2` / `point` | 2D pad | `vec2` | `defaultValue` `[x, y]`; `minValue` / `maxValue` as `[lo, lo]` / `[hi, hi]` (one range shared by both axes). Keyframable per axis |
| `enum` / `choice` | Dropdown | `float` (option index) | `options`: at least two labels; `defaultValue` is an index |

Any parameter can set:
- `group`, to fold it into a named inspector section,
- `groupCollapsed`, to keep that section closed.

`fixedParams` binds hidden uniforms that the user never sees.

The engine also knows `file` and `clip` types, but they serve specific built-in effects (3D props,
depth occlusion). They are not for general packages.

## Video effects and transitions

These use the pipeline grammar from [gpu-effects.md](gpu-effects.md): intermediate buffers, static
textures, and passes that end in `"output": {"type": "canvas"}`.

Shaders are written as `#version 330 core`. They read `in vec2 v_texCoord` and write
`out vec4 fragColor`, where `v_texCoord.y == 0` is the top of the frame.

The engine binds:

| Uniform | Type | Meaning |
|---|---|---|
| `u_currentTexture`, `u_texture1`, … | sampler | Pass inputs, in order |
| `u_resolution` | `vec2` | Pixels |
| `u_time`, `u_timeUs` | `float` | Seconds and microseconds |
| `u_frameIndex` | `int` | Frame index |

Transitions also get `u_fromTexture`, `u_toTexture` and `u_progress`, and they must follow the
[determinism rule](gpu-transitions.md#the-determinism-rule).

An effect created over MCP must use `"backend": "gpu"`. `create_effect` and `update_effect` compile
every pass on the GL thread before installing, and return the driver log if compilation fails. At
render time, a shader that fails to compile leaves the frame unchanged.

## Audio effects

An audio effect has no DSP of its own. It names one of the processors compiled into Drift
(`src/engine/audio/AudioEffectFactory.cpp`) and sets the parameters that processor reads:

```json
{
  "id": "user.long_hall",
  "displayName": "Long Hall Echo",
  "category": "space",
  "backend": "juce",
  "processor": "echo",
  "prerollMs": 2000,
  "parameters": [ … identifiers the processor reads … ]
}
```

To find the right identifiers, copy them from a bundled effect that uses the same processor. The
`processors` table in `effect_authoring_guide` lists those effects. Then change `displayName`, the
ranges and the defaults to suit.

`prerollMs` is how much earlier audio the processor needs to produce a correct block from an
arbitrary start. Use 0 for stateless processors and the tail length for echoes.

## Sharing

A `.driftfx` file is the folder packed with zstd. It has a SHA-256 digest but no signature (see
`src/engine/AddonPackage.h`). Users import one through the Import button in the effect browsers.
`export_effect` writes one for any My Effects package.
