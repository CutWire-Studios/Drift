# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.6.0`

---

## ✅ Fixed

- Splitting an animated clip replayed the animation from the start on the second half instead of continuing it, so a cut visibly changed the motion.
- Lottie and SVG clips seeked one frame past the end of the animation, so a clip set to hold its last frame could show the empty frame after it instead.
- Splitting a clip left the second half with no effects: the grade, mask or colour work stayed on the first half only.
- The Duotone effect rendered a black frame instead of tinting the picture, and its shadow and highlight colours could not be changed.

## ✨ Added

## 🎨 Improved
