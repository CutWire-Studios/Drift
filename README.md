<p align="center">
  <img src="Drift_icon.png" alt="Drift icon" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>The free desktop editor that makes your videos look finished — not “good enough.”</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-lightgrey" alt="Platform: Linux | Windows | macOS | Android">
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift">GitHub</a> ·
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest">Download</a> ·
  <a href="https://github.com/CutWire-Studios/Drift/issues">Issues</a> ·
  <a href="LICENSE">License</a>
</p>

Drift is a desktop video editor from CutWire Studios. Drop in clips, add effects, captions, stickers,
and music, then export a polished video — with **no subscription, no watermark, and no account**.

It is built for the edits people actually make: Reels and Shorts, game clips, school projects,
tutorials, product demos, memes, and anything you want to look sharp without living in a browser
or paying a monthly fee.

What you see in the preview is what you export. One compositor, one look, no surprises.

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
**macOS** — install via Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **First launch on macOS:** Drift is signed ad-hoc. If Gatekeeper blocks opening it, run:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Or grab a build for your platform from the
[latest release](https://github.com/CutWire-Studios/Drift/releases/latest):

| Platform | Package |
|----------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Installer (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Portable zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Disk image (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

On a phone, grab `Drift-*-arm64-v8a.apk` from the latest release and install it (or `adb install Drift-*-arm64-v8a.apk`). Use `x86_64` for emulators.

See [all releases](https://github.com/CutWire-Studios/Drift/releases) for previous versions and full changelogs.

## Screenshots

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="The Drift editor: media bin on the left, video preview in the centre, clip inspector on the right, timeline below" width="900">
</p>

<p align="center"><em>Everything in one window — media, preview, inspector, and timeline</em></p>

<p align="center">
  <img src="docs/screenshots/timeline.png" alt="Timeline with four stacked tracks: two image overlays, a title, and a video track showing filmstrip thumbnails" width="900">
</p>

<p align="center"><em>A real multi-track timeline, with overlays, titles, and filmstrip thumbnails</em></p>

<table>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/effects-panel.png" alt="Effects browser showing live thumbnail previews of glitch and distortion presets" width="380"><br>
      <strong>Effects that sell the look</strong><br>
      Every preset is previewed on a real frame
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/transitions-panel.png" alt="Transitions browser with animated previews of crossfade, wipe, and push presets" width="380"><br>
      <strong>Transitions that feel expensive</strong><br>
      Drop one where two clips overlap
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/stickers-panel.png" alt="Sticker browser with emoji grouped into categories" width="380"><br>
      <strong>Stickers and emoji on demand</strong><br>
      Search, drag, and drop them onto the canvas
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/text-styles.png" alt="Text style presets including neon, karaoke pop, word background, and handwritten" width="380"><br>
      <strong>Titles that actually get watched</strong><br>
      Neon, karaoke, highlights, and punchy word styles
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/templates-panel.png" alt="Template browser showing multi-effect looks such as Beat Drop and Glitch Cut" width="380"><br>
      <strong>Look templates in one click</strong><br>
      Music-synced stacks like Beat Drop and Glitch Cut
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/audio-effects.png" alt="Audio effect browser with EQ, compressor, limiter, noise gate, de-esser, and voice leveler" width="380"><br>
      <strong>Audio that sounds intentional</strong><br>
      EQ, compressor, gate, de-esser, and voice tools
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/speed-fade.png" alt="Speed and fade inspector with speed presets, reverse, and fade in/out sliders" width="380"><br>
      <strong>Speed, reverse, and fades</strong><br>
      Slow-mo, ramps, reverse, and clean in/out
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/subtitle-editor.png" alt="Subtitle editor listing timed caption lines, with the line at the playhead highlighted" width="380"><br>
      <strong>Captions from the speech itself</strong><br>
      Generate them, then edit every line
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/subject-cutout.png" alt="Subject cutout dialog with a click marker on the person and the isolated subject highlighted" width="380"><br>
      <strong>Click the subject. Keep only that.</strong><br>
      Isolate a person or object onto its own clip
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/export-dialog.png" alt="Export dialog with downscale presets and advanced encoder settings" width="420"><br>
      <strong>Export that matches the preview</strong><br>
      Simple presets up front, extra control when you want it
    </td>
  </tr>
</table>

## Features that actually ship in the edit

**A timeline that behaves like a real editor.** Trim, split, snap, ripple, mute or hide tracks, and
undo anything. Stack overlays, titles, and B-roll instead of fighting a one-track toy.

**Looks in seconds, not hours.** GPU effects, stylish transitions, and reusable look templates — so
a clip can go from raw footage to a finished vibe without opening another app.

**Text, stickers, emoji, and shapes on the canvas.** Neon titles, karaoke-style captions, reaction
stickers, and callouts stay in the same editor as the cut.

**Auto captions you can actually fix.** Speech becomes timed subtitle lines. Edit the wording, tweak
the timing, and export with captions that match how people watch on mute.

**Cutouts, masks, and green screen.** Click a subject and lift it onto its own clip. Mask parts of a
shot, or key out a green screen when you need a cleaner composite.

**Motion that hits the beat.** Speed ramps, reverse, fades, and edits that snap to the music — the
kind of pacing that makes a clip feel designed, not dumped.

**Audio tools that clean up the mix.** Volume, fades, EQ, compression, noise cleanup, and voice
effects, so narration and music sit together instead of fighting.

**Multicam when one camera is not enough.** Watch every angle at once, punch between cameras, and
save the take as a clean cut — without rebuilding the timeline by hand.

**Project bundles for sharing and backup.** Package a project with its media so the whole edit moves
with you, instead of breaking the moment a file path changes.

**Export that looks like the preview.** MP4 with quality presets, GIF loops, and ranged export from
an In/Out work area. What you signed off on is what you get.

## Agent access — let an AI edit with you

Drift has a built-in **MCP server** for local AI tools. Turn on Agent access and Cursor, Claude Code,
or another compatible agent can work in the open project: import media, place and trim clips, change
effects, capture a still of the composition, and export.

This is a real editor hook, not a chatbot bolted onto a webpage. The agent sees the timeline and can
make edits you can undo.

Agent access stays **off until you enable it**, and it only listens on your own computer. Full setup
and safety notes live in the [MCP guide](docs/MCP.md).

## Addons, without bloating the install

Fonts, stickers, extra effects, and speech models download inside Drift when you need them. Keep the
app light, then grab only the packs that match the video you are making.

Open the Addon Manager from the header, or follow the install prompt when a feature needs a pack.

## Why people pick Drift

Most “free” editors want an account, a watermark, or a subscription the moment the video starts
looking good. Drift is the opposite: **yours, on your computer, GPLv3, no login wall.**

It is fast enough for a 30-second social cut and deep enough for a real project — captions, effects,
audio, cutouts, multicam, and an AI-assisted timeline if you want one.

## Help us translate Drift

[![Translation status](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## For developers

Build, packaging, architecture, and agent protocol live in `docs/`:

- [Building, testing, packaging, and architecture](docs/BUILDING.md)
- [GPU effects](docs/gpu-effects.md)
- [GPU transitions](docs/gpu-transitions.md)
- [Time Echo architecture](docs/time-echo-architecture.md)
- [Agent access / MCP](docs/MCP.md)

## Help and feedback

Found a bug or have an idea? Open an
[issue on GitHub](https://github.com/CutWire-Studios/Drift/issues).

## License

GPLv3 — see [LICENSE](LICENSE).

## Star History

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&theme=dark&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
