<p align="center">
  <img src="Drift_icon.png" alt="Drift icon" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>Create polished videos fast — free, open, and yours.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS-lightgrey" alt="Platform: Linux | Windows | macOS">
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift">GitHub</a> ·
  <a href="https://github.com/CutWire-Studios/Drift/issues">Issues</a> ·
  <a href="LICENSE">License</a>
</p>

Drift is a free, open-source desktop video editor from CutWire Studios. It brings the speed and simplicity of modern creator tools to your computer: drop in clips, add effects and stickers, generate captions, and export — with no subscription, no watermark, and no account required.

Built with **Qt 6**, **QML**, and **FFmpeg**. Preview and export share one compositor, so what you see is what you get.

## Download

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=en" alt="Get it on Flathub" width="240">
  </a>
</p>

**Linux** — install from Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```

Or grab a build for your platform from the [latest release](https://github.com/CutWire-Studios/Drift/releases/latest):

| Platform | Package |
|----------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Installer (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Disk image (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Build from source](#macos) |

See [all releases](https://github.com/CutWire-Studios/Drift/releases) for previous versions and full changelogs.

## Screenshots

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="The Drift editor: media bin on the left, video preview in the centre, clip inspector on the right, timeline below" width="900">
</p>

<p align="center"><em>Media bin, preview, clip inspector, and timeline</em></p>

<p align="center">
  <img src="docs/screenshots/timeline.png" alt="Timeline with four stacked tracks: two image overlays, a title, and a video track showing filmstrip thumbnails" width="900">
</p>

<p align="center"><em>Multi-track timeline — overlays, titles, and video with filmstrip thumbnails</em></p>

<table>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/effects-panel.png" alt="Effects browser showing live thumbnail previews of glitch and distortion presets" width="380"><br>
      <strong>Effects</strong> — every preset previewed on a real frame
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/transitions-panel.png" alt="Transitions browser with animated previews of crossfade, wipe, and push presets" width="380"><br>
      <strong>Transitions</strong> — drop one where two clips overlap
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/stickers-panel.png" alt="Sticker browser with emoji grouped into categories" width="380"><br>
      <strong>Stickers &amp; emoji</strong> — categorised, searchable, drag onto the canvas
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/text-styles.png" alt="Text style presets including neon, karaoke pop, word background, and handwritten" width="380"><br>
      <strong>Title styles</strong> — neon, karaoke, highlights, one-word colour
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/templates-panel.png" alt="Template browser showing multi-effect looks such as Beat Drop and Glitch Cut" width="380"><br>
      <strong>Look templates</strong> — music-synced effect stacks in one click
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/audio-effects.png" alt="Audio effect browser with EQ, compressor, limiter, noise gate, de-esser, and voice leveler" width="380"><br>
      <strong>Audio</strong> — EQ, compressor, gate, de-esser, voice changers
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/speed-fade.png" alt="Speed and fade inspector with speed presets, reverse, and fade in/out sliders" width="380"><br>
      <strong>Speed &amp; fades</strong> — presets, custom ramps, reverse
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/subtitle-editor.png" alt="Subtitle editor listing timed caption lines, with the line at the playhead highlighted" width="380"><br>
      <strong>Auto captions</strong> — from the speech, editable line by line
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/subject-cutout.png" alt="Subject cutout dialog with a click marker on the person and the isolated subject highlighted" width="380"><br>
      <strong>Subject cutout</strong> — click the subject, get it on its own clip
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/export-dialog.png" alt="Export dialog with downscale presets and advanced encoder settings" width="420"><br>
      <strong>Export</strong> — presets up front, encoder settings when you want them
    </td>
  </tr>
</table>

## Features

- **Multi-track timeline** — trim, split, snap, ripple, mute/hide tracks, and full undo/redo
- **Effects & transitions** — GPU effects, stylish transitions, and reusable look templates
- **Stickers, emoji, titles & shapes** — finish the look without leaving the editor
- **Auto captions** — speech-to-text captions you can edit on the timeline
- **Cutouts & masks** — isolate subjects, mask clips, and key out green screens
- **Speed & motion** — speed changes, reverse, fades, and animate to the beat of your music
- **Audio tools** — mixing, effect chain, and background noise cleanup
- **Addons** — optional fonts, stickers, effects, and speech models on demand
- **Project bundles** — package a project with its media for easy sharing and backup
- **Export** — MP4 (H.264 + AAC) that matches the preview, with quality presets

## Requirements

| Dependency | Version |
|---|---|
| CMake | ≥ 3.21 |
| C++ compiler | C++20 |
| Qt | 6.5+ (Quick, QuickControls2, Multimedia, Test, Concurrent, Widgets, OpenGL, Network) |
| FFmpeg | 8.x (libavformat, libavcodec, libavutil, libswscale, libswresample, libavfilter) |
| libzstd | any (addon package decompression) |
| OpenSSL | 3.x, libcrypto only (addon signature verification) |
| SoundTouch | any (pitch shifting behind the voice effects) |

ONNX Runtime powers auto-subtitles (and related ML features). Drift does not link it — only its headers are needed to build, and the library itself is an addon the user installs from the Acceleration category, which is what makes the CPU / CUDA / WebGPU choice theirs rather than the packager's. The headers are downloaded automatically at configure time; pass `-DDRIFT_FETCH_ONNXRUNTIME=OFF` to use a system install instead. A development build also stages a CPU runtime into `<build>/onnxruntime` so it works before anything is installed — `-DDRIFT_BUNDLE_ONNXRUNTIME=OFF` (what the Flatpak manifests use) turns that off, and `DRIFT_ONNXRUNTIME_DIR` points at an extracted release instead.

On Debian/Ubuntu install `libzstd-dev`, `libssl-dev` and `libsoundtouch-dev`; on Arch, `zstd`, `openssl` and `soundtouch`; on macOS, `brew install qt ffmpeg zstd openssl@3 sound-touch` (see [macOS](#macos)). None of them has a download fallback — configure fails with a pkg-config error if the development headers are missing.

Optional: OpenCV for experimental background-removal builds (`-DWITH_BGREMOVAL=ON`). Only `core`, `imgproc`, and `imgcodecs` are linked.

**Nothing has to be placed by hand.** Fonts, emoji stickers, and speech models are addons (see below), so a clone builds and runs with no bundled assets.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Optional OpenCV background removal:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DWITH_BGREMOVAL=ON
cmake --build build -j$(nproc)
```

## Run

```bash
./build/drift
```

## Test

```bash
cd build
ctest --output-on-failure
```

Test targets: `Core`, `EditorState`, `Playback`, `Engine`, `MediaProbe`, `AddonPackage`.

On a headless machine — CI, a container, a build server with no GPU — run it under Xvfb instead:

```bash
QT_QPA_PLATFORM=xcb xvfb-run -a -s "-screen 0 1280x1024x24" ctest --output-on-failure
```

`QT_QPA_PLATFORM=offscreen` will not work: that plugin cannot create an OpenGL context without a
`/dev/dri` device, and the compositor tests then compare against a null frame. Software rendering
is not the issue — llvmpipe passes the whole suite once there is an X server.

`AddonPackage` verifies against a signed fixture in `tests/data/`, so that file has to be checked out with the repo.

## macOS

Everything above applies, with Homebrew supplying the dependencies:

```bash
brew install cmake ninja qt ffmpeg zstd openssl@3 sound-touch

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix openssl@3);$(brew --prefix)"
cmake --build build -j$(sysctl -n hw.ncpu)
```

`openssl@3` needs naming explicitly because Homebrew keeps it keg-only, and `sysctl -n hw.ncpu` stands in for `nproc`. Qt from [qt.io](https://www.qt.io/download-open-source) works too — point `CMAKE_PREFIX_PATH` at `.../6.x.x/macos` instead.

The build produces an application bundle rather than a bare executable, so run it with:

```bash
open build/Drift.app          # or: ./build/Drift.app/Contents/MacOS/Drift to see stderr
```

The bundle is not cosmetic: macOS treats a loose binary as a background process, with no Dock tile, no menu bar and no way to raise the window, and only `Info.plist` can set `NSHighResolutionCapable`, without which the UI and the preview render at 1x on Retina displays.

Effects, transitions, templates and audio effects are staged into `Drift.app/Contents/Resources`, which `GpuPackageParse::defaultSearchPaths` adds as a search root next to the directory holding the executable. The `DRIFT_*_DIR` overrides behave as they do elsewhere.

### Disk image

```bash
scripts/package-macos.sh
```

Builds Release, runs `macdeployqt` to copy Qt, FFmpeg, OpenSSL, zstd and SoundTouch into `Contents/Frameworks`, drops the build machine's `LC_RPATH` entries, signs, and writes `dist/Drift-<version>-<arch>.dmg`. The rpath step matters: dyld searches the executable's rpaths before the `@loader_path` entries in the nested frameworks, so a bundle still listing `/opt/homebrew/opt/qt6/lib` loads the host's Qt on any Mac that has one.

Signing is ad-hoc by default — enough to launch locally, since Apple Silicon will not run unsigned binaries, but it still shows the unidentified-developer prompt elsewhere. Pass `--identity "Developer ID Application: …"` for a distributable signature and notarise the `.dmg` with `notarytool`. Without notarisation, opening it needs right-click → Open, or `xattr -dr com.apple.quarantine /Applications/Drift.app`.

Homebrew ships single-architecture bottles, so the image is Apple Silicon only and Intel Macs build from source. Its deployment floor also follows Homebrew's rather than the macOS 12 a source build targets — build against Qt from qt.io to reach 12.

## Addons

Fonts, emoji stickers, and speech models download at runtime rather than shipping in the binary. That keeps the install small and lets you take only what you need. Open the Addon Manager from the header (layers icon), or follow the install prompt in the font picker, stickers tab, or auto-subtitle panel.

<p align="center">
  <img src="docs/screenshots/addon-manager.png" alt="Addon Manager listing installable packs with size, item count, and licence" width="620">
</p>

Packages are `.driftpkg` archives — zstd-compressed, Ed25519-signed, and verified before install — under `<AppDataLocation>/addons/`. Format, registry, and installer live in `src/engine/AddonPackage.*`, `src/engine/AddonRegistry.*`, and `src/models/AddonManager.*`.

**Effects and transitions are bundled *and* addons.** They ship next to the binary so the editor works out of the box; `effects.core` / `transitions.core` addons can ship shader fixes without an app release. Content resolves highest-priority-first:

```
1. $DRIFT_*_DIR          developer override
2. installed addon       downloaded updates
3. <appDir>/<kind>       bundled with the build
4. <AppDataLocation>     hand-placed
```

Catalogs resolve duplicate ids first-root-wins, so an installed `builtin.effects.gaussian_blur` supersedes the bundled one. An addon cannot *remove* a bundled package — the bundled copy reappears when the addon no longer defines that id.

Opening a project that uses an effect or transition with no catalog entry reports it rather than silently dropping it from the render.

To work against local content instead of downloading:

```bash
DRIFT_EFFECTS_DIR=/path/to/effects \
DRIFT_TRANSITIONS_DIR=/path/to/transitions \
DRIFT_FONTS_DIR=/path/to/fonts \
DRIFT_STICKERS_DIR=/path/to/stickers \
DRIFT_WHISPER_MODEL_DIR=/path/to/whisper-small \
  ./build/drift
```

Building and publishing addons lives in a separate repository, along with the Cloudflare Worker that serves them.

### Pointing at a different service

The endpoint and client token are defined in `CMakeLists.txt` and injected as compile definitions — `src/models/AddonEndpoint.h` only reads them.

```bash
cmake -B build -DDRIFT_ADDON_INDEX_URL=https://addons.example.com/v1/index \
               -DDRIFT_ADDON_CLIENT_TOKEN=your-token

cmake -B build -DDRIFT_ADDON_INDEX_URL=      # build with no addon service at all
```

With the service disabled the manager lists and installs nothing; already-installed, side-loaded, and `DRIFT_*_DIR` content still work.

The token is not a secret — it ships in every binary. It exists so the bucket cannot be crawled or hotlinked.

These are CMake *cache* variables: changing the default in `CMakeLists.txt` does not affect an existing build directory, so pass `-D...` again or reconfigure from scratch.

## CLI tools

Built under `build/tools/`:

```bash
# Probe a media file
./build/tools/probe /path/to/video.mp4

# Render one composited frame from a saved project
./build/tools/renderframe project.dcut.json 1000000 out.png
```

Arguments for `renderframe`: `<project.json> <time_us> <output.png>`.

## Project layout

```
src/
  core/           Domain model (Project, Track, Clip, Keyframe, Effect) — no GUI
  engine/         FFmpeg: ClipReader, FrameCompositor, AudioMixer, EffectProcessor, Exporter
  models/         QML-facing models: AppController, AssetLibrary, TimelineModel, ClipListModel
  playback/       PlaybackEngine, PlaybackClock, CompositorService
  preview/        PreviewItem (QQuickItem → QSGTexture)
  qml/            UI panels and components
tests/            Unit tests (ctest) + tests/data (signed addon fixture)
tools/            Headless probe + renderframe
flatpak/          Flatpak / Flathub packaging
packaging/arch/   PKGBUILD for the Arch package
installer/windows/Inno Setup script for the Windows installer
scripts/          Release-notes extraction and asset sync helpers
cmake/            FindFFmpeg.cmake
```

## Releasing

Pushing a `vX.Y.Z` tag runs [`.github/workflows/release.yml`](.github/workflows/release.yml), which
builds the AppImage, Windows installer, Arch package, and Flatpak bundle, then publishes them as a
GitHub release. Before tagging:

1. Bump `project(Drift VERSION ...)` in `CMakeLists.txt` and `pkgver` in `packaging/arch/PKGBUILD`
   — the workflow refuses to publish if either disagrees with the tag.
2. Add a `<release version="X.Y.Z">` entry to `flatpak/org.cutwire.Drift.metainfo.xml`. Its notes
   become the GitHub release body via `scripts/extract_release_notes.py`, so the software centre
   and the release page can never say different things.
3. Run the **Build** workflow manually (`workflow_dispatch`) to prove each platform green — a tag
   is public the moment the release job finishes.

Flathub is submitted separately from `flatpak/org.cutwire.Drift.flathub.yml`; pin its `commit:` to
the tagged commit first.

### CMake targets

| Target | Role |
|---|---|
| `driftcore` | Core domain + JSON persistence |
| `driftengine` | FFmpeg decode, compositing, effects, export |
| `drift` | Qt Quick application |

## Architecture (summary)

**Unified frame server** — Preview and export share `FrameCompositor`:

> “Give me the composited RGBA frame + mixed audio at timeline time T (µs).”

**Time model** — Core timeline positions are `int64_t` microseconds (`drift::TimeUs`). QML uses seconds at the boundary via `AppController`.

**Threading**

| Thread | Responsibility |
|---|---|
| Main (GUI) | QML, models, undo stack, playhead UI |
| Decode workers | `ClipReaderPool` — one thread per active media path |
| Compositor | `CompositorService` — frames off the GUI thread |
| Audio (pull) | `QAudioSink` → `PlaybackClock` (audio-master) |

**Data flow (video)**

```
Media file → ClipReader → EffectProcessor
          → FrameCompositor (transforms, blending, text, masks)
          → PreviewItem (QSGTexture)  |  Exporter
```

**Data flow (audio)**

```
Media file → ClipReader → AudioMixer (volume, fades, audio effects)
          → QAudioSink  |  Exporter
```


## QML entry points

Singletons registered in `main.cpp`:

- `EditorState` / `AppController` — timeline controller
- `AssetLibrary` — media bin

## License

GPLv3 — see [LICENSE](LICENSE).
