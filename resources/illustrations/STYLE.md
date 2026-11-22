# Illustration style

Small looping animations for rich tooltips and Quick Setup pages. The sources are Lottie JSON files in `src/`. `scripts/gen-illustrations.py` turns them into `src/qml/components/illustrations/<Name>Illustration.qml`. The generated QML is committed. There is no build-time step and no runtime QtLottie, so the illustrations behave the same on desktop, Android and wasm.

## Canvas
- Tooltip art is 160×90 (16:9). It appears in RichToolTip only after the extended hover delay.
- Onboarding art is 320×240 (4:3), shown in the Quick Setup page illustration slot.
- Strokes are 1.5 px at the canvas size.

## Palette
Use exactly four colours, written as placeholder hex values. The generator rebinds them to theme tokens, so the art follows the light and dark themes and the accent override.

| Role | Placeholder | Token |
|---|---|---|
| surface | `#FE0001` | `Theme.c.illustration.surface` |
| foreground | `#02FE01` | `Theme.c.illustration.foreground` |
| accent | `#0102FE` | `Theme.c.illustration.accent` |
| muted | `#FDFE02` | `Theme.c.illustration.muted` |

Translucency goes in the colour's alpha, and the generator emits `Qt.alpha(token, a)` for it. Any other colour literal aborts generation.

## Motion
- Each piece shows one idea: before → action → result.
- Loops last 1.2–2.4 s and hold on the key (result) frame before restarting. No cross-fade back to the start.
- Use ease-out for things arriving and ease-in-out for moves. Keep within the Theme easing family (`Theme.easingNamed`).
- No decorative motion.

## Content
- No text or letters, so the art needs no translation. Glyph-like squiggles are fine.
- Reuse the shared vocabulary: rounded clip rectangles on darker track lanes, an accent playhead with a triangle head, the white cursor arrow, accent for the thing being acted on, and muted for context.

## Pipeline constraints
- Animate size through layer scale, not path size. lottietoqml emits `PathInterpolated` for animated rect sizes, and the generator rejects it.
- Output may import only `QtQuick`, `QtQuick.Shapes` and `Drift`.
- `python3 scripts/gen-illustrations.py` regenerates the QML. Add `--render` to also write GIF previews and a contact sheet to `/tmp/drift-illustrations/`; this needs the venv at `~/.venvs/lottie` with `lottie[all]`.
- New illustrations must be added to `CMakeLists.txt` next to the existing `components/illustrations/` entries.
