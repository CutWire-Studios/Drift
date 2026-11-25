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
- Mouse look in 3D fly mode: the pointer is locked and hidden while flying, with raw deltas on Wayland, X11, Windows, macOS and Android. "Steer toward pointer" stays available in Preview settings for touchpads.
- Gamepads (SDL3 on desktop, Android input devices on phones and tablets): fly the 3D camera with the sticks, triggers and gyro, shuttle and step the timeline, mark in and out, and feel a short rumble on cuts and snaps. Controller buttons are bindable in the keymap, and a new Controllers settings page shows connected pads with a test view, dead zones, response curve and gyro sensitivity.
- Camera recording in the 3D workspace: arm Record and play, and the fly camera (or a selected clip, puppeted with the sticks) is keyed every frame, then thinned by the Recording smoothing setting. A take over existing keys replaces only its range and undoes in one step.
- Control surfaces: bind hardware buttons to keymap actions and faders, knobs and jog wheels to clip, effect, colour, mixer, playhead and zoom targets, with Learn mode, soft takeover and per-device JSON import/export, from a new Control surfaces settings page.
- MIDI controllers, including Mackie Control and HUI motorized faders with mute/solo, transport, jog, LEDs and scribble-strip track names.
- OSC control over UDP for TouchOSC and Open Stage Control, with feedback and an allowed-hosts list. The address space is in `docs/osc.md`.
- Phone remote: pair Drift on Android with the desktop editor through a QR code and use it as a jog wheel, transport, lift/gamma/gain trackballs, an 8-track fader bank and a 3D look pad with gyro steering.
- Project folders (`.driftproj`): a project saved as plain JSON files, one per track and asset with keyframes, masks and effects in sidecars, laid out for small git diffs. The editor keeps it in two-way sync, so an outside edit becomes one undo step. Open Folder, Save as project folder and Export .drift bundle are in the projects menu.
- JSON Schema for every project-folder file, `drift lint <folder>`, a small `drift --lsp` diagnostics server, a generated `AGENTS.md` and `docs/project-folder.md`, plus the `project_lint` and `project_folder_guide` MCP tools.
- Lua scripts in a project folder generate tracks, clips and titles from code and CSV/JSON data. Generated items are locked with a badge until you detach them. Project format 35.
- Templates: a project folder with `template.json` parameters renders once per data row, from "Render with data", `drift --headless --render <folder> --data rows.csv --all -o pattern` or the `template_render` MCP tool.
- Headless rendering without a display server: `drift --headless --render project.drift -o out.mp4` picks a GPU through EGL (`--list-gpus`, `--gpu`), no xvfb needed.
- Render queue: "Add to queue" in the export dialog, a queue window to reorder, hold, retry and watch jobs, rendered in the editor or in a separate background process so editing stays responsive.
- Render farm on the local network: run `drift --headless --farm-coordinator` (or the editor) and `--farm-worker` on other machines with a shared token. Media streams to workers by hash, long exports split into chunks and are joined losslessly. Settings are on a new Render farm page.

## 🎨 Improved
