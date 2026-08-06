# Face-prop place markers

`markers.glb` is the default model for **3D Face Prop**. It draws coloured dots at the
landmark positions Drift uses to pin props to a tracked face, so you can line up your own
`.glb` in a modeller against the same frame of reference.

## Head space

After load, Drift normalises every model so it is **one head-width wide** at `scale = 1`.
This file is authored in that space already:

| Axis | Direction |
|------|-----------|
| Origin | Eye midpoint (prop pivot) |
| +X | Image-right |
| +Y | Toward forehead |
| +Z | Toward the viewer |
| Width ±0.5 | Face silhouette left / right |

## Marker colours

| Dot | Colour | Notes |
|-----|--------|--------|
| `origin` | white | Eye midpoint — prop pivot; keep sunglasses / crowns relative to this |
| `leftEye` / `rightEye` | blue | Image-left / image-right |
| `noseTip` | orange | Slightly forward (+Z) |
| `mouthCenter` / corners | pink | |
| `chin` | green | Anatomical chin (below mouth) |
| `forehead` | yellow | |
| `crown` | pale | Top of head — hat / crown alignment |
| `back` | dark grey | Behind the head; balances AABB depth so load centering does not pull markers into the face |
| `cheekLeft` / `cheekRight` | purple | |
| `faceLeft` / `faceRight` | grey | Width rails (±0.5); do not delete when editing |

The cloud is symmetric about the eye midpoint on X/Y/Z so Drift's load-time AABB
centering leaves the authored pivot in place. Author props against these dots, export
as `.glb` without Draco, then swap the Model file in the effect.
