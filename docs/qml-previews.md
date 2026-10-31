# QML component previews

A debug build can open a single QML component in its own window, without the editor around it. The component runs against the app's real singletons (`EditorState`, `Theme`, `Addons`, image providers), so it behaves as it does in the editor. `--preview-shot` saves each preview as a PNG and exits, for scripts and CI.

## Enabling

Previews are behind the `DRIFT_QML_PREVIEWS` CMake option. It defaults to `ON` when `CMAKE_BUILD_TYPE` is `Debug` and `OFF` otherwise. Android and wasm never build it. With the option off, neither `PreviewHost.qml` nor any fixture is compiled in, and `--preview` is ignored.

The option is cached. A build directory configured as Debug keeps it on after switching to Release, unless you pass `-DDRIFT_QML_PREVIEWS=OFF`.

## Opening previews

```sh
build/drift --preview=EasingTile
build/drift --preview=EasingTile,EasingThumb --preview=MediaPill
build/drift --preview=EasingPicker path/to/project.drift
DRIFT_THEME=light build/drift --preview=EasingTile
```

- Names are QML type names in the `Drift` module, which is each file's basename (`components/EasingTile.qml` is `EasingTile`).
- Each name opens one window. Separate names with commas, or repeat `--preview=`.
- The `=` is required. Without it, the name would be read as a project path.
- A project path on the command line is loaded before the windows open, for components that need clips, keyframes or bin items.
- The window starts at the component's implicit size, or 800×600 when it has none.
- `DRIFT_THEME` selects the theme for this run only, as it does for the editor.

If a component fails to load, the window shows the error and the same text goes to stderr.

## Mock data

Some components show nothing without a project: no clip selected, no media, no subtitles. A fixture can fill that in with `PreviewMockData`, which imports the sample media in `tests/data/preview-media/` and puts it on the timeline of the project given on the command line:

- `sunset-waves.mp4` (10 s) on Video 1 at 0 s, selected. The Properties, Color, Looks, Masking and Preview panels all show it.
- `shanghai-skyline.mp4` (12 s) on Video 1, right after the first clip.
- `music.mp3` is imported into the bin but not placed yet. Sources and licenses for all files are in `tests/data/preview-media/README.md`.
- The Source Lab is loaded with the first clip, so the Source Lab panels have a source.
- `subtitles: true` also imports `captions.srt` as a subtitle clip, selected.

The media directory reaches QML as the `PreviewMediaDir` context property, set from `DRIFT_PREVIEW_MEDIA_DIR` (the source tree path, debug previews only).

A fixture using mock data does three things:

```qml
Item {
    implicitWidth: 960
    implicitHeight: 640

    // PreviewHost holds the screenshot until this is true.
    readonly property bool previewReady: mock.ready

    PreviewMockData {
        id: mock
    }

    ColorPanel {
        anchors.fill: parent
        shown: true
    }
}
```

- `previewReady` is the handshake. Placement waits for the probes to finish, which can take a few seconds. If it is still false after about 15 s, the screenshot is taken anyway.
- Panels that refresh only while visible take `shown: true`. The workspace sets it in the app, and a standalone preview has to set it itself. Only set it on components that declare `property bool shown`, or the fixture fails to load.
- The Timeline and Properties panels need nothing beyond `previewReady` and the mock.
- Every mock-data fixture adds clips to the same project, so run each one in its own `drift` process. In one process the previews interfere with each other.

## Fixtures

A component that needs inputs gets a fixture next to it, named `<Name>_Preview.qml`. When one exists, `--preview=<Name>` loads the fixture instead of the component. This is the equivalent of a Compose `@Preview` function.

```qml
// src/qml/components/EasingTile_Preview.qml
import QtQuick
import Drift

Item {
    implicitWidth: 360
    implicitHeight: 480

    Flow {
        anchors.fill: parent
        padding: Theme.spacingMd
        spacing: Theme.spacingXs

        Repeater {
            model: EditorState.easingPresets()

            EasingTile {
                required property var modelData
                required property int index

                width: 80
                preset: modelData
                selected: index === 1
            }
        }
    }
}
```

Use a fixture for any component that:

- has `required property` declarations. Without a fixture, the window reports that the component cannot be created on its own.
- reaches into a parent it expects to be in, such as `appWindow` or a panel id.
- is clearer with several states side by side: selected and unselected, empty and full.

CMake finds fixtures by a recursive glob (`src/qml/**/*_Preview.qml`), so adding one only needs a rebuild; `CMakeLists.txt` does not change. `PreviewHost.qml` and `PreviewMockData.qml` are compiled in the same gated block. Write the root's own `implicitWidth`/`implicitHeight` on a plain `Item`. Layout types such as `Flow` and `Column` make them read-only.

## Screenshots

```sh
QT_QPA_PLATFORM=offscreen build/drift --preview=EasingTile,EasingThumb --preview-shot=/tmp/shots
```

- Each preview is saved as `<dir>/<Name>.png`, and the directory is created if it does not exist.
- The image is taken 250 ms after the first frame, so image providers and nested Loaders have time to finish.
- After saving, each window closes itself, and the app exits when the last one closes.
- The image covers the component's content at its implicit size, theme background included. The window's own size does not matter, so a tiling window manager that ignores the requested size still produces the full image.
- A `ToolWindow` (the clip tools such as Speed curve, Denoise and Segmentation) keeps its content in a window of its own. The shot grabs that window's content and closes it, so these come out like the others.
- `previewReady` (see Mock data) holds the shot until the fixture is set up.

Exit status:

| Code | Meaning |
|------|---------|
| 0 | Every preview loaded and was saved |
| 1 | At least one preview failed to load or could not be written, or the directory could not be created |

A preview that fails to load still writes its PNG, showing the error text, so a run of many previews shows which ones broke.

`QT_QPA_PLATFORM=offscreen` runs without a display. Leave it out to take the images from real windows, which briefly appear on screen.

### Limits

- **GPU-drawn content:** the offscreen platform often has no usable OpenGL context. Plain Qt Quick content renders, but items drawn by the compositor or Skia (the preview viewport, text, shape and look thumbnails) can come out blank. The preview viewport in particular is blank offscreen. Take those images on a machine with a display, without `offscreen`.
- **Timing:** content that keeps loading past the fixture's `previewReady` (network images, thumbnails from a large project) is caught mid-load.
- **Other windows:** a component that opens a `Window` that is not a `ToolWindow` keeps the app running after the preview closes, and its content is not in the shot.
- **No comparison:** previews only produce images. Comparing them against reference images is not implemented.
