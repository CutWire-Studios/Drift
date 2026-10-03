# Face Retouch

One beauty effect built from GPUPixel's algorithms: skin smoothing with sharpening, whitening,
slim face, big eyes, lipstick and blush, plus a skin tone shift of Drift's own. GPUPixel itself is not linked — it runs its own GL
context, only takes CPU uploads, and its face detector is a closed binary — so its GLSL is
ported into Drift's package format and driven by Drift's own 468-point face track.

Every pass passes the frame through untouched when the clip has no face, when the face track
predates contours or the mesh, or when `Face` names a slot the frame does not have.

## Requirements

- The **face-model** addon and the ONNX Runtime addon (same as every other face effect).
- Face tracking baked on the clip with the mesh — Effects → Detect faces. A track from before
  the mesh was stored has to be rescanned.

## Parameters

| Parameter | Effect |
|---|---|
| Smoothing | GPUPixel's edge-preserving mix toward the local mean, on face skin only. |
| Sharpen | 3×3 high-pass over the face, eyes included. |
| Whitening | GPUPixel's levels, gray curve and LUT chain, on face skin only. |
| Skin tone / Tone | Pulls face skin toward the picked tone (swatch grid from porcelain to espresso), lighter or darker. A per-channel gain against the colour measured at the cheeks, so shading and texture survive. Drift's own, not GPUPixel's; masked by the contours only, so it works on every complexion. |
| Slim face | Pulls nine jaw points toward the nose line (GPUPixel's 0–0.05). |
| Big eyes | Magnifies around each eye centre (GPUPixel's 0–0.15). |
| Lipstick | `mouth.png` multiplied over the lips. |
| Custom lip colour / Lip colour | Replaces the template's colour, keeping its shading. The swatch grid holds preset shades; picking one or using the picker turns Custom lip colour on. |
| Blush | `blusher.png` multiplied over the cheeks. |
| Face | Which tracked face to retouch when the clip has more than one (numbered buttons). |
| Skin feather *(Advanced)* | How soft the edge of the skin mask is at the jaw and around the features. |
| Custom blush colour / Blush colour *(Advanced)* | Replaces the template's colour, keeping its shading. |

Skin is the face oval minus the eyes, brows and lips, from the contour loops, times GPUPixel's
own red-channel skin test. Whitening uses twice the feather of smoothing.

## Order

smooth and whiten → lipstick → blush → reshape. The warp runs last so the makeup stays where
it was drawn.

## Makeup templates

`mouth.png` and `blusher.png` are GPUPixel's templates converted by
`tools/convert_makeup_templates.py`. GPUPixel paints them opaque, as distance from white under a
multiply; the script splits each pixel into a tint and a straight alpha that reproduce the
original exactly, so the default look is unchanged and a custom colour reuses the painted alpha
instead of staining the template's white margin.

## Mesh passes

Lipstick and blush are `"geometry": "face111"` passes: the engine copies the input into the
output, then draws GPUPixel's 111-point face mesh over it with each vertex at the tracked point
and its template coordinate taken from GPUPixel's reference face. `templateBounds` is where the
PNG sits on that reference face, in its 1280-pixel frame — the same numbers GPUPixel's
`LipstickFilter` and `BlusherFilter` use. See `docs/gpu-effects.md`.

The 468 → 111 mapping lives in `src/engine/Face111.cpp`;
`face111MappingMatchesGpuPixelReference` in `tests/tst_engine.cpp` keeps it honest.

## License

GPUPixel is Copyright (c) 2021 PixPark, Apache License 2.0. See `NOTICE` and
`LICENSE-APACHE-2.0`.
