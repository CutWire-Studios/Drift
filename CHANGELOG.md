# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.5.1`

---

## ✅ Fixed

## ✨ Added

- Text rework: captions and titles are now drawn from a layered shading stack (fill, stroke, shadow, glow, extrude; solid, multi-stop gradient with moving offset, image texture or shader effect paints) with a Looks gallery (Shadow, Lift, Hollow, Splice, Outline, Echo, Glitch, Neon, Background, Curve, Gradient, Shine, Chrome, Holographic).
- Text animation engine: After Effects-style animators and range selectors evaluated per character, word or line, with In / Out / Loop preset galleries (type-on blur, rise by word, snap together, zoom out, typewriter with caret, colour wave, wipe, drop, spin, rain, converge, stamp, fade away, fade zoom, remove by word, shrink, fall, pulse, breathe, float, wave, jitter, swing, flicker, colour pulse, tracking drift, slow zoom, karaoke pop/bounce). Kinetic text now honours masks and effects. Lottie / After Effects text animators import as presets.
- Projects saved by this version use format 7; older files migrate on load.

## 🎨 Improved
