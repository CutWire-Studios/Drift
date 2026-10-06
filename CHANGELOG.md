# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.7.5`

---

## ✅ Fixed

- **macOS: AVI, MKV and WebM play in the trim and preview window.** It could only play what Apple's own player supports, so these files showed a first frame and then would not play, even though they played on the timeline.
- **The media bin's hover hint no longer covers the right-click menu.**

## ✨ Added

- **Audio effects made of pedals.** An audio effect can now be a whole pedalboard: filters, a ladder filter, drive, reverb, convolution reverb, delay, pan and gain, alongside the original effects, run in series or side by side in parallel and frequency-band splits. Build them in Drift Forge and import the `.driftfx`.
- **Modulation for audio effects.** LFOs, envelope followers and step sequencers can move any pedal's knobs over time, in step with the clip, so a wobble lands in the same place after you seek.
- **Convolution reverb.** Put a sound in a real space using an impulse response shipped with the effect: Forge has a room, a plate, a hall and a spring tank, or record your own.
- **Keyframe audio effect parameters.** The sliders of an audio effect now take keyframes on the timeline, like a video effect's.

## 🎨 Improved
