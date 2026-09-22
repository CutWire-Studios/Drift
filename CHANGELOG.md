# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.6.0`

---

## ✅ Fixed

- Adding a transition could bind it to a clip that overlapped almost all of the outgoing one instead of the clip at the cut, and a transition loaded from an older project could claim a span far longer than the clips it joins.
- Auto-reframe stretched the picture and zoomed far past what was asked for — a 9:16 crop of a 4K clip came out around 3x magnified and distorted. It now keeps the source's shape, fits the requested aspect inside the canvas, and reports how much it magnified the source so an upscale is visible before it reaches a render.
- Adding two graphics at the same moment — an emoji and a Lottie, say — pushed the second one down the timeline instead of stacking it on its own lane, so it appeared at the wrong time.
- Keyframes written while the playhead sat outside a clip landed outside it too, where they could never play but still bent the animation.
- Setting an effect, audio-effect or transition parameter that does not exist now fails instead of quietly storing a value nothing reads. The colour-parameter schema no longer advertises alpha it cannot carry.
- Beat detection reported half the tempo on music with a strong downbeat — 60 BPM on a 120 BPM track — and said it was certain. It now checks whether the faster tempo explains the music just as well, and reports lower confidence when the two are genuinely close.
- Ducking music under speech flattened any volume envelope the music already had, and running it again stacked new keyframes on top of the old ones so the music pumped between words. Each dip now returns to the level the music was actually at, and re-running replaces the previous pass.
- Setting a value at a keyframe's own time could add a second keyframe a fraction of a millisecond away instead of updating the one already there.
- The Limiter made audio louder instead of limiting it, and lowering its ceiling added more gain rather than less. It is now a real ceiling limiter: the output never exceeds the ceiling, and a signal already below it is left alone.
- The true-peak reading was always 0.0 dBFS: it was taken after the master soft clipper and used an interpolation that could not see between samples. It is now a real 4x oversampled measurement of the unclipped mix.
- Loudness was measured 3 dB below the standard, so Normalise applied 3 dB too much gain and pushed audio into clipping. Normalising now also builds on the clip's existing volume instead of replacing it, and warns when the target would clip.
- Splitting an animated clip replayed the animation from the start on the second half instead of continuing it, so a cut visibly changed the motion.
- Lottie and SVG clips seeked one frame past the end of the animation, so a clip set to hold its last frame could show the empty frame after it instead.
- Splitting a clip left the second half with no effects: the grade, mask or colour work stayed on the first half only.
- The Duotone effect rendered a black frame instead of tinting the picture, and its shadow and highlight colours could not be changed.

## ✨ Added

## 🎨 Improved
