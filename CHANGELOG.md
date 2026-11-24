# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.9.4`

---

## ✅ Fixed

## ✨ Added

- Generator clips: solid colour, gradient (linear, radial, sweep, up to five stops), animated noise, colour bars, grid, checkerboard and a film-leader countdown, from a new Generators tab in the assets panel. Colours, gradient angle and offset, noise offset and speed are keyframable.
- A particle emitter clip with point, line, rectangle and circle emitters, gravity, wind, drag, turbulence, size, opacity and colour over life, built-in or image/emoji sprites and eight presets (confetti, fireworks, sparks, snow, rain, smoke, magic dust, bubbles). The simulation is deterministic, so scrubbing and export match.
- Scripted text: a Lua script on a text clip produces its words for every frame, with presets for timecode burn-in, frame counter, number ticker, stopwatch, date and clock and percentage loader. Matching MCP tools: `list_generators`, `add_generator`, `set_generator`, `list_text_scripts`, `set_text_script`. Project format 34.
- Scrolling credits and tickers: a text clip can roll up like end credits or crawl left or right, at a speed that follows its length, from the new Scroll section of the Text inspector, with a credits roll template.
- Title templates: thirteen ready-made lower thirds, titles, a chapter card, a subscribe button, a quote card and a credits roll, from the Templates chip of the Text assets tab and the `insert_title_template` MCP tool. Every piece stays an ordinary text or shape clip.
- Auto-cut to beat: choose the music clip and the clips to cut together and Drift places them on a new track so each cut lands on a beat or bar line. "Add beat markers" marks a clip's beats every beat, every 2 beats or every bar. Both are in the Audio submenu of the clip menu and in the MCP (`auto_cut_to_beat`, `add_beat_markers`).
- Auto ducking lowers music under speech on other tracks, with presets and voice detection through the Voice activity (silero.vad) addon, from the Audio Mixer and the clip menu; re-running replaces the previous pass. Closes #555.

## 🎨 Improved
