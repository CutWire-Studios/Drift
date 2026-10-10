<p align="center">
  <img src="../Drift_icon.png" alt="Icon ng Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>Ang libreng desktop video editor na nagbibigay sa iyong mga video ng pulidong gawa — hindi lang basta “pwede na.”</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Pinakabagong release" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Mga download sa GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Mga install sa Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Sumali sa Drift Discord" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Lisensya: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Platform: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Ang Drift ay isang desktop video editor mula sa CutWire Studios. Mag-drop ng mga clip, magdagdag ng mga effect, caption, sticker,
at musika, pagkatapos ay mag-export ng de-kalidad na video — **walang subscription, walang watermark, at walang kinakailangang account**.

Binuo ito para sa mga uri ng video na talagang ginagawa ng mga creator: Reels at Shorts, game clips, mga proyekto sa paaralan,
tutorial, demo ng produkto, memes, at anumang nais mong magmukhang propesyonal nang hindi nakatali sa browser
o nagbabayad buwan-buwan.

Ang nakikita mo sa preview ang siya mismong mae-export. Isang compositor, iisang kalidad, walang hindi inaasahang sorpresa.

## I-download

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=tl" alt="Kunin sa Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Kunin mula sa Microsoft" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Kunin sa Google Play (closed testing)" height="80">
  </a>
</p>

<p align="center">Nasa closed testing ang Google Play — <a href="../docs/play-testing.md">alamin kung paano sumali</a>.</p>

**Linux** — i-install gamit ang Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — i-install gamit ang Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Unang paglunsad sa macOS:** Ang Drift ay ad-hoc signed (hindi opisyal na notarized ng Apple). Awtomatikong inaalis ang quarantine attribute kapag nag-install gamit ang Homebrew. Kung nag-install ka gamit ang `.dmg` o hinaharangan ito ng Gatekeeper, patakbuhin:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

O kumuha ng build para sa iyong platform mula sa
[pinakabagong release](https://github.com/CutWire-Studios/Drift/releases/latest):

| Platform | Package |
|----------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Installer (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Portable zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Disk image (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (closed testing)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

Sa telepono, sumali sa [closed testing sa Google Play](../docs/play-testing.md), o kumuha ng `Drift-*-arm64-v8a.apk` mula sa pinakabagong release at i-install ito (o `adb install Drift-*-arm64-v8a.apk`). Gamitin ang `x86_64` para sa mga emulator.

Tingnan ang [lahat ng release](https://github.com/CutWire-Studios/Drift/releases) para sa mga naunang bersyon at buong talaan ng pagbabago.

## Screenshot

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Ang Drift editor: media bin sa kaliwa, 3D camera sa preview, text inspector sa kanan, multi-track timeline sa ibaba" width="900">
</p>

## Mga Tampok

**Maaaring mag-edit ang AI agent sa nakabukas na proyekto.** Mag-import, mag-cut, mag-grade, at mag-export nang direkta sa bukas na proyekto. Maaari ding gumana ang editor nang walang window (headless mode).

**3D sa mismong timeline.** I-tilt ang mga clip sa 3D space, lagyan ng lighting, at maglagay ng titulo sa likod ng paksa. Mag-drop ng 3D model at magpe-play ito sa mismong cut.

**Lottie animation support.** Ang mga motion graphics at vector art ay nananatiling malinaw sa anumang laki. Baguhin ang kulay o teksto nang direkta sa mismong lugar.

**Advanced keyframing.** I-animate ang posisyon, laki, pag-ikot, opacity, at mga parameter ng epekto sa paglipas ng panahon. Iguhit ang animation curve ayon sa gusto mo.

**150+ transition at 40+ effect.** Bawat estilo ay may live preview sa iyong aktwal na video. Isang click lang para maglagay ng buong stack — Beat Drop, Glitch Cut, Neon Cutout. Mga trail ng naunang frames, at color grading na nakatali sa isang clip.

**Lokal na pagpoproseso sa iyong computer.** I-click ang isang paksa at ihiwalay ito agad mula sa background. Magdagdag ng depth of field at lighting na partikular sa clip. Awtomatikong mga caption, pag-upscale ng video, at live face retouch — lahat sa sarili mong computer.

**I-edit ang video sa pamamagitan ng teksto.** Ang boses ay nagiging teksto sa mismong clip. Putulin ang mga parirala, alisin ang mga pampuno at katahimikan, piliin ang pinakamagagandang take, lagyan ng label ang mga nagsasalita, at bumuo ng mga caption mula sa mga panahong iyon.

**Mga titulo na may tunay na estilo.** 33 na premade style pack — karaoke, estilo ng Hormozi, neon, chrome, holographic, sulat-kamay. Kopyahin ang isang estilo sa lahat ng subtitle sa track nang sabay-sabay.

**Igalaw ang buong layer stack nang sabay-sabay.** Ang isang transform layer ay maaaring mag-drag, scale, rotate, tilt, at mag-fade ng lahat ng nasa ilalim nito. Pag-isahin ang mga layer, o magbukas ng composite kapag nangangailangan ng sariling timeline ang grupo.

**I-cut ayon sa tugtog ng musika.** Awtomatikong nag-i-snap ang timeline sa beat ng musika. Nahahati ang mga clip at lumalapag nang eksakto sa bar. Kusa ring humihina ang musika kapag may nagsasalita (audio ducking) at babalik sa dating volume pagkatapos. Awtomatikong i-reframe ang pahigang video sa patayong format na sumusubaybay sa mukha.

**Timeline na maaasahan at matatag.** Multi-track filmstrips, ripple editing, snapping, masks, freeze frame, mga bookmark, at built-in mixer. Hatiin ang isang clip at mananatili ang color grading. Kapag nag-crash ang app, mananatiling buo at ligtas ang huling save.

**Audio na may kalidad pampelikula.** Linisin ang ingay sa boses, sukatin ang loudness, mag-EQ at compress, panatilihin ang tono (pitch) kapag binabago ang bilis, at mag-record ng voiceover.

**Maghanap ng mga kuha. Lumipat sa multicam.** Maghanap sa footage batay sa kung ano ang nasa screen. Panoorin ang lahat ng anggulo nang sabay-sabay at gawin ang cut sa tamang sandali.

**Stabilization, pag-export, at pag-package ng proyekto.** Pakinisin ang mga maalog na kuha, mag-upscale, at i-play nang maayos ang mabibigat na file. Mag-export sa MP4, GIF, audio lamang, o partikular na range. I-package ang media kasama ng edit upang hindi maputol ang mga file path.

**Parehong karanasan sa Android.** Timeline, effects, at pag-export sa telepono. Ibahagi agad nang diretso mula sa application.

**Stock library, voice synthesis, at maliit na file size.** Maghanap ng stock footage nang diretso sa media bin. Bumuo ng voiceover at sound effects. Ang mga font, sticker, at karagdagang modelo ay dina-download kapag gagamitin lamang. May suporta sa interface sa Arabic, Bengali, Espanyol (Spain at Colombia), Pranses (Canada), Italyano, Japanese, Portuges (Brazil at Portugal), Ruso, Sinhala, Tagalog, Vietnamese, at Simplified Chinese.

## Bakit pinipili ng mga tao ang Drift?

Karamihan sa mga "libre" na editor ay nanghihingi ng account, naglalagay ng watermark, o nanghihingi ng bayad sa subscription sa oras na maganda na ang video. Kabaligtaran ang Drift: **iyo, sa iyong computer, lisensyadong GPLv3, at walang harang ng pag-login.**

Mabilis para sa 30-segundong social video at malakas para sa malalaking produksyon — AI agent sa timeline, 3D at Lottie, mga caption, effect, audio, smart cutout, at multicam.

## Tulungan kaming isalin ang Drift

[![Katayuan ng pagsasalin](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Para sa mga Developer

Ang mga gabay sa pag-build, packaging, arkitektura, at agent protocol ay nasa `docs/`:

- [Pagbuo, pagsubok, packaging, at arkitektura](../docs/BUILDING.md)
- [Mga GPU effect](../docs/gpu-effects.md)
- [Mga GPU transition](../docs/gpu-transitions.md)
- [Access ng Agent / MCP](../docs/MCP.md)

## Tulong at Feedback

May nakitang bug o may ideya para sa pagpapahusay? Magbukas ng
[issue sa GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Pag-ambag sa Proyekto

Ang [CONTRIBUTING.md](../CONTRIBUTING.md) ay ang gabay para sa mga pull request: magbukas ng issue bago ang malalaking pagbabago, limitahan ang bawat request sa isang pagbabago, patakbuhin ang mga test, at lisensyahan ang gawa sa ilalim ng GPLv3.

Ang mga effect, transition, template, at audio effect ay itinuturing na mga addon. Ipadala ang mga iyon sa [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Ang mga pagbabago sa core engine ay mananatili sa repository na ito.

Ang pag-install ng [opisyal na release](https://github.com/CutWire-Studios/Drift/releases) at pag-edit ng totoong proyekto ay malaking tulong na; iulat kung ano ang nagkaproblema o kung ano ang dapat mapabuti. Sa [Discord](https://discord.gg/J5ANFz6Z3y), ang mga nag-ambag ay maaaring humiling ng `@Contributor` role.

## Mga Nag-ambag

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Mga Nag-ambag" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Lisensya

GPLv3 — tingnan ang [LICENSE](../LICENSE).

## Kasaysayan ng Star (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
