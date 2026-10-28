# Theming Drift

Drift paints from a JSON theme. `ThemeManager` (a C++ singleton) loads the document, resolves references, and exposes the result as `QQmlPropertyMap`s. `Theme.qml` is the facade the rest of the QML already binds to (`Theme.appBackground`, `Theme.c.themedButton.background`, and so on).

Built-in themes (`dark`, `light`, `graphite`, `frost`) live in `resources/themes/<id>.json`. `graphite` and `frost` extend `dark` and `light` and use a translucent window. The shape is `resources/themes/theme.schema.json`. User themes live in `QStandardPaths::AppDataLocation + "/themes"`.

## Layers

A document is deep-merged in this order, then references are resolved:

1. `palette` — raw colors. No nested objects.
2. `semantic` — roles (`surface.app`, `foreground`, `clip.audio`, …) that point at the palette or at other semantic tokens.
3. `components` — one group per visual component. The group name is the QML file basename in lowerCamel (`ThemedButton.qml` → `themedButton`, `SettingsThemeEntry.qml` → `settingsThemeEntry`). `appWindow` and `androidMain` are the shell exceptions.
4. `metrics`, `typography`, `motion` — numbers and font tokens. Control heights are `{ pointer, touch }`.
5. `window` — translucency request (below).

`extends` names another theme (usually `dark` or `light`). The base is merged underneath this file. Missing keys fall through. `exportTheme` writes the fully resolved document with `extends` omitted, which is the starting point for a theme that should not track future built-in edits.

After `extends` and variants are applied, the built-in theme of the document's variant is merged underneath once more, so a token added in a later Drift release is filled with its default instead of going missing. That is the migration path for older themes; the file on disk is not rewritten.

`schemaVersion` is the theme format the document was written for (currently `1`; omitted means a theme from before versioning). A theme whose version is newer than the running build still loads, with unknown keys ignored, and `ThemeManager.lastWarning` asks the user to update Drift. Bump `ThemeManager::kSchemaVersion` in `ThemeManager.h` when a release adds tokens or changes what a key means. `exportTheme` writes the current version.

References are strings: `$palette.primary`, `$semantic.panel.border`, `$metrics.radius.sm`. Cycles and unknown paths are left as the original string and logged. Colors are `#RGB`, `#RRGGBB`, or `#AARRGGBB`. Eight-digit colors are Qt order: alpha first, then red, green, blue.

## Selection

`ThemeManager.selection` is persisted in `QSettings` group `Theme`, key `selection`.

| selection | what is on screen |
| --- | --- |
| `system` (default) | Built-in dark or light, following `Theme.darkMode` (the appearance preference, otherwise the OS). |
| `dark` / `light` | That built-in, pinned. `Theme.darkMode` follows the pin, so the header Light/Dark checkmarks match the theme on screen. The header toggle moves the pin to the other built-in. `setVariant` does not unpin it. |
| `graphite` / `frost` | That built-in, pinned. `setVariant` does not unpin it. Built-in ids cannot be used by user themes. |
| a user theme id | That theme. |

An unknown saved id is logged and the selection falls back to `system`.

`ThemeManager.effectiveVariant` is `"dark"` or `"light"` for whatever is actually showing.

### Session override

`DRIFT_THEME` is a theme id or a path to a theme file or folder. When it is set, `main` passes it to `ThemeManager::applySessionOverride` after the persisted selection has been loaded. The override is for this process only: it is not written to `Theme/selection`. The next launch without the variable uses the saved selection again.

`Theme.qml` calls `setVariant` when the window opens and whenever light/dark changes. That call does not replace the override with a built-in. A dual-variant override still swaps its dark and light sides. Choosing a theme (Appearance, or the header toggle on a single-variant user theme, which selects `system`) calls `select`, which ends the override and persists the new selection.

### Variants

A user theme may ship both sides:

- inline: `variants: { dark: { …partial… }, light: { …partial… } }`
- or, next to `theme.json` only: `theme.dark.json` and `theme.light.json`

A side file is a partial document (palette, semantic, components, metrics, typography, motion, window). It must not contain `name`, `extends`, `variant`, or nested `variants`. When both sides exist, the header light/dark toggle picks the overlay and the theme stays selected. `setVariant` records the side and reapplies; it does not write back into `Theme.darkMode`.

A theme with only one side is single-variant. `effectiveVariant` is the document's `variant` field (`dark` if it is omitted). `Theme.darkMode` follows that variant so controls which still branch on `Theme.darkMode` match the theme. The header Light/Dark commands call `Theme.setDarkMode`, which leaves the theme and selects `system` on the requested side. The Appearance section says so while such a theme is active.

### Hot reload

`ThemeManager.hotReload` is persisted as `Theme/hotReload` (default false). When it is on, the current user theme's JSON and `qml/` files are watched and reloaded on change. Built-ins are not watched.

## Window translucency

`window` in the theme:

| key | meaning |
| --- | --- |
| `translucent` | The theme wants a translucent window. |
| `backdrop` | Requested backdrop name (`none`, `blur`, `mica`, `acrylic`, …). |
| `backgroundOpacity` | 0..1, how solid the window fill stays. |

The app's `WindowBackdrop` singleton decides what the platform can actually do:

- `translucentWindows` — user switch, persisted, needs a restart (`restartRequired`).
- `translucencyActive` — constant for this process.
- `backdrop` — the effective backdrop.
- `supportedBackdrops` — what this OS build can do.

If the selected theme has `window.translucent: true` and translucency is not active, Appearance asks you to turn the switch on.

Surfaces that should show the backdrop need an alpha below 255. `docs/themes/frosted` does that for the app, panel, and accent (header and popups read those semantic colors) and sets `backdrop` to `blur`.

`WindowBackdrop.windowBackground` multiplies that alpha by `window.backgroundOpacity` only when this process was started with translucent windows. Otherwise it forces the alpha to opaque, so a translucent token cannot clear the window to black. The switch is `Theme/translucentWindows` in `QSettings`. Turning it on or off sets `restartRequired`.

Per platform, something behind the window has to blur or tint:

- **Linux.** A compositor is required. Drift only makes the window translucent and requests no blur itself; backdrop `blur` means a translucent window that the compositor may blur by its own rules. There is no KDE or xcb dependency. The Wayland app id is `org.cutwire.Drift`; on X11 the WM_CLASS instance is the executable name `drift`. **Hyprland:** `windowrulev2 = opacity 0.85, class:^(org\\.cutwire\\.Drift)$` and `windowrulev2 = blur on, class:^(org\\.cutwire\\.Drift)$` (or `decoration:blur = true` plus that window rule). **KWin:** a window rule or a blur script/effect that targets `org.cutwire.Drift`. **GNOME:** Blur my Shell (or a similar extension); GNOME does not blur ordinary client windows by itself. **picom:** `blur-background = true` and a `blur-background-exclude` list that does not exclude `drift`.
- **Windows 11 22H2.** Mica (`mica`) or acrylic (`acrylic`) when `supportedBackdrops` lists them. Windows 10 has no mica; blur and acrylic use the accent-policy fallback (`SetWindowCompositionAttribute`).
- **macOS.** `vibrancy` (system window material) or `blur` (HUD material) behind the window.
- **Android.** The shell is not a composited desktop window. The switch is stored, but a backdrop may be unsupported.

Changing the switch does not rebuild the window. Restart Drift.

## QML overrides

A folder theme may contain `qml/<Slot>.qml`. The file is loaded only when the theme is the current one and its name is in the trusted list (`QSettings` group `Theme`, key `trusted`). Until then, JSON tokens still apply and the slot keeps its built-in control.

`ThemeSlot` passes the host control as `control`. The override should declare `property var control` and read state from it (`control.down`, `control.hovered`, `control.checked`, …). See `docs/themes/override-example`.

Overrides are ordinary QML. They run with Drift's permissions: they can import the app module, touch the filesystem the app can touch, and call into singletons. Trust a theme only after reading every file under its `qml/` directory. Revoke trust from the theme's row in Appearance; the JSON theme stays, the overrides unload. The command palette's per-theme command selects without prompting, which leaves an untrusted theme's overrides off. Appearance is what asks.

`TimelineClipBody` is an additive overlay. Clip bodies are painted natively by `TimelineTrackClips`. Recolor clips with `components.timelineClipItem.*Background`, not with that slot.

Slots (the `slot` string on each `ThemeSlot`):

- AndroidRailButton
- AndroidSheetBackground
- AndroidSheetHandle
- AndroidTopBarBackground
- AndroidTopBarExportButton
- AppScrollBarHandle
- AssetsPanelBackground
- AssetsPanelTabButton
- CollapsibleSectionHeaderBackground
- EditorHeaderBackground
- EditorHeaderExportButtonBackground
- EffectPresetCardBackground
- IconButtonBackground
- KeyframeDiamond
- KeyframeGraphFrame
- MediaAssetsSelectionRing
- NavRailButtonBackground
- PanelFrameBackground
- PreviewLetterbox
- PreviewOsdClusterBackground
- PreviewTransportBarBackground
- PropertiesPanelBackground
- StartScreenBackground
- ThemedButtonBackground
- ThemedCheckBoxIndicator
- ThemedChipBackground
- ThemedComboBoxBackground
- ThemedComboBoxPopupBackground
- ThemedContextMenuBackground
- ThemedDialogBackground
- ThemedMenuItemBackground
- ThemedProgressBarBackground
- ThemedSliderHandle
- ThemedSliderTrack
- ThemedSwitchIndicator
- ThemedTextAreaBackground
- ThemedTextFieldBackground
- ThemedToggleButtonBackground
- ThemedToolTipBackground
- TimelineClipBody
- TimelinePlayheadHead
- TimelineRulerBackground
- TimelineToolbarBackground
- ToastCardBackground
- TrackHeaderRowBackground
- TransformOverlayResizeGrip

## Finding a component's tokens

Group name = lowerCamel of the QML basename (`SettingsAppearanceSection.qml` → `components.settingsAppearanceSection`). Export the current theme (Appearance → Export current theme…) and search that JSON for the group. The full built-in tables are in [theming-tokens.md](theming-tokens.md), regenerated with:

```sh
python3 scripts/gen-theme-tokens.py
```

## Import

Appearance can import:

- a `.json` file (installed as `<name>.json`)
- a folder that contains `theme.json`, optional `theme.dark.json` / `theme.light.json`, and optional `qml/<Slot>.qml`
- a `.zip` of either of those (a single top-level directory is stripped)

The theme is validated before it is copied into the user themes directory. Names `dark`, `light`, and `system` are rejected, as is a name that is already installed.

The archive is rejected outright when any entry is absolute, contains `..`, contains a backslash or a drive colon, or is a unix symlink. Compressed and uncompressed sizes are capped (2 MB per entry, 8 MB for the archive, at most 64 entries). Only `theme.json`, the two side files, and `qml/<Slot>.qml` are written; other names are ignored, including `__MACOSX` and `.DS_Store`. Folder imports refuse symlinks and refuse a `qml` file whose real path is outside that folder.

## Examples

| path | what it shows |
| --- | --- |
| `docs/themes/frosted` | Extends dark, translucent app/panel/accent, `window.translucent` and `backdrop: "blur"`. |
| `docs/themes/high-contrast` | Extends dark with white foreground, white borders, yellow primary. |
| `docs/themes/override-example` | Extends dark and replaces `ThemedButtonBackground` via `control`. |
