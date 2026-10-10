<p align="center">
  <img src="Drift_icon.png" alt="Drift icon" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>The free desktop editor that makes your videos look finished — not “good enough.”</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Latest release" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="GitHub downloads" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Flathub installs" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Join Drift Discord" height="24"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="License: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Platform: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
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
    <img src="https://flathub.org/api/badge?locale=en" alt="Get it on Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Get it from Microsoft" width="293">
  </a>
  <a href="docs/play-testing.md">
    <img src="docs/btn-gplay-en.png" alt="Get it on Google Play (closed testing)" height="80">
  </a>
</p>

<p align="center">Google Play is closed testing — <a href="docs/play-testing.md">how to join</a>.</p>

**Linux** — install from Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — install via Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **First launch on macOS:** Drift is signed ad-hoc (not notarized by Apple). The quarantine attribute is automatically removed when installing via Homebrew. If you install manually via `.dmg` or if Gatekeeper still blocks opening it, run:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Or grab a build for your platform from the
[latest release](https://github.com/CutWire-Studios/Drift/releases/latest):

| Platform | Package |
|----------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Installer (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Portable zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Disk image (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (closed testing)](docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

On a phone, join [closed testing on Google Play](docs/play-testing.md), or grab `Drift-*-arm64-v8a.apk` from the latest release and install it (or `adb install Drift-*-arm64-v8a.apk`). Use `x86_64` for emulators.

See [all releases](https://github.com/CutWire-Studios/Drift/releases) for previous versions and full changelogs.

## Screenshot

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="The Drift editor: media bin on the left, 3D camera in the preview, text inspector on the right, multi-track timeline below" width="900">
</p>

## Features

**An agent can edit the open project.** Import, cut, grade, and export in the project you have open.
The same editor can run with no window.

**3D on the timeline.** Tilt clips in space, light them, and park a title behind the subject. Drop
in a 3D model and it plays in the same cut.

**Lottie animations.** Motion graphics and vector art stay sharp at any size. Recolour or reword
them in place.

**Advanced keyframing.** Animate position, scale, rotation, opacity, and effect parameters over
time. Draw the curve yourself.

**150+ transitions and 40+ effects.** Every look previews on your footage. One click can drop a
whole stack — Beat Drop, Glitch Cut, Neon Cutout. Trails of earlier frames, and grades that stick
to one clip.

**On this machine.** Click a subject and lift it off the shot. Add depth of field and lights that
belong to the clip. Automatic captions, video upscale, and live face retouch — all on your
computer.

**Edit the words.** Speech becomes text on the clip. Cut phrases, drop fillers, strip silence, pick
the best takes, label speakers, and build captions from those times.

**Titles with a real look.** Thirty-three packs — karaoke, Hormozi-style, neon, chrome, holographic,
handwritten. Copy one style onto every subtitle on the track.

**Move a whole stack at once.** One layer can drag, scale, rotate, tilt, and fade everything under
it. Nest layers, or open a composite when the stack needs its own timeline.

**Cut to the music.** The timeline snaps to the beat. Clips split and land on the bar. Music ducks
under speech and comes back to its own level. Reframe a landscape take into a vertical video that
follows the face.

**A timeline that behaves.** Multi-track filmstrips, ripple, snap, masks, freeze frame, bookmarks,
a mixer. Split a clip and the grade stays. A crash leaves the last save intact.

**Audio that sounds finished.** Clean up speech, meter loudness, EQ and compress, keep pitch when
you change speed, record a voiceover.

**Find shots. Switch cameras.** Search footage for what’s on screen. Watch every angle at once and
punch the cut.

**Stabilise, export, take the project with you.** Smooth shaky clips, upscale, play heavy files
smoothly. MP4, GIF, audio-only, or just a range. Pack the media with the edit so paths stay whole.

**Android is the same editor.** Timeline, effects, and export on the phone. Share straight from the
app.

**Stock, voices, and a small install.** Search stock into the bin. Generate voiceover and sound
effects. Fonts, stickers, and extra models download when you use them. UI in Arabic, Bengali, Spanish (Spain
and Colombia), French (Canada), Italian, Japanese, Portuguese (Brazil and Portugal), Russian,
Sinhala, Tagalog, Vietnamese, and Simplified Chinese.

## Why people pick Drift

Most “free” editors want an account, a watermark, or a subscription the moment the video starts
looking good. Drift is the opposite: **yours, on your computer, GPLv3, no login wall.**

It is fast enough for a 30-second social cut and deep enough for a real project — an agent on the
timeline, 3D and Lottie, captions, effects, audio, cutouts, and multicam.

## Help us translate Drift

[![Translation status](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## For developers

Build, packaging, architecture, and agent protocol live in `docs/`:

- [Building, testing, packaging, and architecture](docs/BUILDING.md)
- [GPU effects](docs/gpu-effects.md)
- [GPU transitions](docs/gpu-transitions.md)
- [Agent access / MCP](docs/MCP.md)

## Help and feedback

Found a bug or have an idea? Open an
[issue on GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Contributing

[CONTRIBUTING.md](./CONTRIBUTING.md) is the pull-request guide: open an issue before a large change, keep each request to one change, run the tests, and license the work under GPLv3.

Effects, transitions, templates, and audio effects are addons. Send those to [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Changes to the engine that runs them stay in this repository.

Installing a [release](https://github.com/CutWire-Studios/Drift/releases) and editing a real project is enough to help; file what broke, or what should work better. On [Discord](https://discord.gg/J5ANFz6Z3y), people who have contributed can ask for the `@Contributor` role.

## Contributors

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Contributors" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

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
