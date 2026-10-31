# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.8.0`

---

## ✅ Fixed

## ✨ Added

- **Source Lab replaces Cut / Trim.** A frame-accurate source viewer with JKL, frame stepping and timecode entry, and a trim strip with an overview and a zoomed filmstrip and waveform. Mark ranges as favorite or reject, add markers, and split automatically at scene changes.
- **Source Lab processing and export.** Crop and reframe presets, speed, gain and loudness normalize, denoise, stabilize, and enhance or upscale with A/B compare. Replace in bin rebases timeline clips onto the result, Add as copy keeps the original, and Export file offers container, codec and quality options with a fast stream-copy trim.
- **Tracker shortcuts.** Ctrl+T and Ctrl+Shift+T track forward and backward from the playhead, and Ctrl+Shift+Right and Ctrl+Shift+Left track one frame. All are rebindable.
- **Blur this area.** A mask's detail has a Blur or Pixelate button that moves its shape onto an adjustment layer in one step, keeping any tracker that followed it.
- **Tracking inside mask rows.** Each mask shows Track back, both ways and forward, a confidence strip and what it follows. Dragging a tracked mask on the preview corrects the track at that frame.
- **Follow an object from the text or sticker.** With a text, sticker or image selected, the Tracking panel offers to follow an object in the video below it.
- **Brush fixes on AI cutouts.** Brush strokes on an object or people cutout fix the current frame only, with a button to clear the fix. Projects saved with fixes need Drift with project format 19.
- **Color workspace.** A Color, Looks and Scopes panel side by side. Color grades the selected clip, Looks applies a look to it, and Scopes shows histogram, waveform and vectorscope readings of the frame.
- **Color Grade effect.** Exposure, contrast, highlights, shadows, whites and blacks, temperature, tint, saturation and vibrance, colour wheels for shadows, midtones and highlights, master and per-channel curves, and a look with an intensity slider. Apply a grade to every other clip in one step.
- **Looks and .cube import.** Import `.cube` 3D LUT files as looks, or install a looks addon. Imported looks are kept under your own user looks folder and can be renamed or removed.
- **Film Grain and Chromatic Aberration are built in.** They ship with Drift now, no addon needed.
- **Vignette gains controls.** Amount, feather, roundness, colour and centre, on top of the existing vignette.
- **Glow gains a tint.** Bloom Glow can tint its glow to any colour.

## 🎨 Improved
