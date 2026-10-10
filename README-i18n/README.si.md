<p align="center">
  <img src="../Drift_icon.png" alt="Drift අයිකනය" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>ඔබේ වීඩියෝ "සාමාන්‍යයෙන් හොඳයි" මට්ටමෙන් ඔබ්බට ගෙන ගොස් පරිපූර්ණ නිමාවක් ලබා දෙන නොමිලේ ලැබෙන පරිගණක වීඩියෝ සංස්කාරකය.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="නවතම නිකුතුව" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="GitHub බාගැනීම්" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Flathub ස්ථාපනයන්" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Drift Discord සමඟ එක්වන්න" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="බලපත්‍රය: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="සහාය දක්වන වේදිකා: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift යනු CutWire Studios වෙතින් නිර්මාණය කරන ලද ඩෙස්ක්ටොප් වීඩියෝ සංස්කාරකයකි. වීඩියෝ ක්ලිප් එකතු කරන්න, ප්‍රයෝග (effects), උපසිරැසි (captions), ස්ටිකර්
සහ සංගීතය එක් කරන්න, ඉන්පසු ඉතා පැහැදිලි හා ආකර්ෂණීය වීඩියෝවක් එක්ස්පෝට් කරන්න — **මාසික ගාස්තු නැත, දිය සලකුණු (watermarks) නැත, ගිණුම් සෑදීම අවශ්‍යම නැත**.

මෙය නිර්මාණකරුවන් සැබවින්ම කරන නිර්මාණ සඳහාම සකසා ඇත: Reels සහ Shorts, ගේමින් ක්ලිප්, පාසල් ව්‍යාපෘති,
නිබන්ධන, නිෂ්පාදන ආදර්ශන, මීමස් සහ බ්‍රවුසර සීමාවන්ට හෝ මාසික ගෙවීම්වලට කොටු නොවී උසස් නිමාවකින් කළ යුතු ඕනෑම නිර්මාණයක් සඳහා.

පෙරදසුනෙහි (preview) ඔබ දකින දේම අවසානයේ එක්ස්පෝට් වේ. තනි සංයුක්තකාරකයක් (compositor), එකම පෙනුම, කිසිදු අනපේක්ෂිත දෝෂයක් නැත.

## බාගත කිරීම

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=si" alt="Flathub වෙතින් ලබාගන්න" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Microsoft Store වෙතින් ලබාගන්න" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Google Play වෙතින් ලබාගන්න (සීමිත පරීක්ෂණ)" height="80">
  </a>
</p>

<p align="center">Google Play යෙදුම සීමිත පරීක්ෂණ මට්ටමේ පවතී — <a href="../docs/play-testing.md">එක්වන ආකාරය බලන්න</a>.</p>

**Linux** — Flathub මඟින් ස්ථාපනය කරන්න:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — Homebrew මඟින් ස්ථාපනය කරන්න:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **macOS හි පළමු දියත් කිරීම:** Drift සඳහා ad-hoc අත්සන් කර ඇත (Apple විසින් notarize කර නැත). Homebrew මඟින් ස්ථාපනය කිරීමේදී නිරෝධායන ගුණාංගය (quarantine attribute) ස්වයංක්‍රීයව ඉවත් වේ. ඔබ `.dmg` ගොනුවෙන් ස්ථාපනය කළේ නම් හෝ Gatekeeper විවෘත කිරීම වළක්වන්නේ නම්, ටර්මිනලයේ මෙය ක්‍රියාත්මක කරන්න:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

නැතහොත් [නවතම නිකුතුවෙන්](https://github.com/CutWire-Studios/Drift/releases/latest) ඔබේ මෙහෙයුම් පද්ධතිය සඳහා සෘජුවම බිල්ඩ් එකක් බාගත කරගත හැක:

| මෙහෙයුම් පද්ධතිය | පැකේජය |
|-------------------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [ස්ථාපක ගොනුව (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [පෝටබල් zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [ඩිස්ක් ඉමේජය (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (සීමිත පරීක්ෂණ)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

දුරකථනයෙන් [Google Play සීමිත පරීක්ෂණ මණ්ඩලයට එක්වන්න](../docs/play-testing.md), නැතහොත් නවතම නිකුතුවෙන් `Drift-*-arm64-v8a.apk` බාගත කර ස්ථාපනය කරගන්න (හෝ `adb install Drift-*-arm64-v8a.apk`). Emulator සඳහා `x86_64` භාවිතා කරන්න.

පෙර අනුවාද සහ සම්පූර්ණ වෙනස්කම් ලැයිස්තුව බැලීමට [සියලු නිකුතු](https://github.com/CutWire-Studios/Drift/releases) පරීක්ෂා කරන්න.

## තිර ඡායාරූපය

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Drift සංස්කාරකය: වම්පස මාධ්‍ය බඳුන (media bin), පෙරදසුනෙහි 3D කැමරාව, දකුණුපස පෙළ පරීක්ෂක (inspector), පහළ බහු-ධාවන කාලරාමුව (timeline)" width="900">
</p>

## ප්‍රධාන විශේෂාංග

**විවෘත කර ඇති ව්‍යාපෘතිය AI සහායකයෙකුට (Agent) සංස්කරණය කළ හැක.** විවෘත කර ඇති ව්‍යාපෘතිය තුළම මාධ්‍ය ගෙන්වීම (import), කැපීම (cut), වර්ණ ගැන්වීම (grade) සහ එක්ස්පෝට් කිරීම සිදු කරන්න. කවුළුවකින් තොරව (headless) පසුබිමෙන්ද මෙය ධාවනය කළ හැක.

**කාලරාමුව (Timeline) මත 3D හැසිරවීම්.** ක්ලිප් ත්‍රිමාණ අවකාශයේ ඇල කරන්න, ආලෝකය ලබා දෙන්න, සහ විෂය පිටුපසින් මාතෘකා රඳවන්න. 3D ආකෘතියක් ඇද දැමූ විට එය එම සංස්කරණයේදීම වාදනය වේ.

**Lottie සජීවිකරණ සහාය.** චලන ග්‍රැෆික්ස් සහ දෛශික කලා (vector art) ඕනෑම විශාලත්වයකදී පැහැදිලිව පවතී. එම ස්ථානයේදීම ඒවායේ වර්ණ හෝ වචන වෙනස් කරන්න.

**උසස් යතුරුරාමු (Keyframing).** කාලයත් සමඟ පිහිටීම, ප්‍රමාණය, භ්‍රමණය, පාරදෘශ්‍යතාව සහ ප්‍රයෝග පරාමිතීන් සජීවිකරණය කරන්න. චලන වක්‍රය ඔබම අඳින්න.

**150+ ට වැඩි සංක්‍රාන්ති (Transitions) සහ 40+ ට වැඩි ප්‍රයෝග.** ඔබේ වීඩියෝ මතම සෑම පෙනුමක්ම සජීවීව පූර්වදර්ශනය වේ. තනි ක්ලික් කිරීමකින් සම්පූර්ණ ප්‍රයෝග එකතුවක් යෙදිය හැක — Beat Drop, Glitch Cut, Neon Cutout. පෙර රාමු වල චලන රටා සහ එක් ක්ලිප් එකකට පමණක් රැඳෙන වර්ණ ගැන්වීම්.

**ඔබේම පරිගණකයේ සැකසුම්.** විෂය මත ක්ලික් කර එය ක්ෂණිකව පසුබිමෙන් වෙන් කරගන්න (cutout). ක්ලිප් එකට ගැළපෙන ක්ෂේත්‍ර ගැඹුර (depth of field) සහ ආලෝකකරණය එක් කරන්න. ස්වයංක්‍රීය උපසිරැසි, වීඩියෝ පැහැදිලි බව ඉහළ නැංවීම (upscale) සහ සජීවී මුහුණු ඔපදැමීම — සියල්ල ඔබේම පරිගණකයෙන්.

**පෙළ සංස්කරණය මඟින් වීඩියෝ සංස්කරණය.** කථනය ක්ලිප් එක මත පෙළක් බවට පත්වේ. අනවශ්‍ය වාක්‍ය කපා දමන්න, පිරවුම් වචන සහ නිහඬතා ඉවත් කරන්න, හොඳම දර්ශන තෝරාගන්න, කථිකයන් නම් කරන්න සහ එම වේලාවන්ගෙන් උපසිරැසි ජනනය කරන්න.

**ආකර්ෂණීය නිමාවක් සහිත මාතෘකා.** සූදානම් කළ මෝස්තර පැකේජ 33ක් — කැරෝකේ, හෝමෝසි විලාසය, නියොන්, ක්‍රෝම්, හොලෝග්‍රැෆික්, අත් අකුරු. එක් විලාසයක් ට්‍රැක් එකේ ඇති සියලු උපසිරැසිවලට ක්ෂණිකව පිටපත් කරන්න.

**සම්පූර්ණ ස්ථර එකවර හසුරුවන්න.** එක් පරිවර්තන ස්ථරයකට (layer) තමන් යටතේ ඇති සියල්ල එකවර ඇදගෙන යාම, විශාල කිරීම, කරකැවීම, ඇල කිරීම සහ මැකී යාම (fade) කළ හැක. ස්ථර එකතු කරන්න (nest), නැතහොත් වෙනම කාලරාමුවක් අවශ්‍ය වූ විට සංයුක්තයක් විවෘත කරන්න.

**සංගීතයේ රිද්මයට අනුව සංස්කරණය.** කාලරාමුව ස්වයංක්‍රීයව බීට් එකට සම්බන්ධ වේ. ක්ලිප් රිද්මයට අනුව නියමිත ස්ථානයට වෙන් වේ. කථනය සිදුවන විට සංගීතය ස්වයංක්‍රීයව අඩුවී (audio ducking) නැවත සාමාන්‍ය මට්ටමට පැමිණේ. තිරස් දර්ශන මුහුණ ලුහුබඳින සිරස් වීඩියෝ බවට ස්වයංක්‍රීයව ප්‍රතිරාමු (reframe) කරන්න.

**විශ්වාසදායක කාලරාමුවක්.** බහු-ධාවන පථ සේයාපට (filmstrips), රිපල් සංස්කරණය, ස්නැප් වීම, ආවරණ (masks), රාමු නිශ්චල කිරීම (freeze frame), පිටුසන්, සහ ඕඩියෝ මික්සර්. ක්ලිප් එකක් වෙන් කළද වර්ණ ගැන්වීම් නොසැලී පවතී. බිඳවැටීමකදී පවා අවසාන සුරැකීම ආරක්ෂිතව පවතී.

**පරිපූර්ණ ශබ්ද සංස්කරණය.** කථනයේ පසුබිම් ඝෝෂා ඉවත් කිරීම, ශබ්ද මට්ටම් මැනීම, ඊකියු (EQ) සහ සම්පීඩනය (compressor), වේගය වෙනස් කිරීමේදී ස්වර උන්නතාංශය (pitch) රැකගැනීම, සෘජුවම පසුබිම් හඬ පටිගත කිරීම.

**දර්ශන සෙවීම සහ කැමරා මාරු කිරීම (Multicam).** තිරයේ දිස්වන දේ අනුව දර්ශන සොයන්න. සියලුම කෝණ එකවර නරඹා නිවැරදි තත්පරයේදී කටවුට් එක තෝරන්න.

**සෙලවීම් සමනය, එක්ස්පෝට් කිරීම සහ ව්‍යාපෘති ඇසුරුම් කිරීම.** කැමරා සෙලවීම් සුමට කිරීම, උසස් පැහැදිලි බවකට නැංවීම, සහ බරැති ගොනු සුමටව වාදනය කිරීම. MP4, GIF, ශ්‍රව්‍ය පමණක් හෝ තෝරාගත් කොටසක් එක්ස්පෝට් කරන්න. ගොනු මාර්ග නොවෙනස්ව තබා ගැනීමට මාධ්‍ය ගොනු ව්‍යාපෘතිය සමඟම අසුරන්න.

**Android හිද එකම සංස්කාරක අත්දැකීම.** දුරකථනයේදීම සම්පූර්ණ කාලරාමුව, ප්‍රයෝග සහ එක්ස්පෝට් හැකියාවන්. යෙදුමෙන්ම සෘජුවම බෙදාගන්න.

**නොමිලේ මාධ්‍ය, කෘතිම හඬ සහ කුඩා ඉඩ ප්‍රමාණයක්.** මාධ්‍ය බඳුනෙන්ම නොමිලේ ලබාගත හැකි ඡායාරූප, වීඩියෝ සොයන්න. හඬ කැවීම් සහ ශබ්ද ප්‍රයෝග ජනනය කරන්න. අකුරු විලාස, ස්ටිකර් සහ අමතර මොඩල බාගත වන්නේ අවශ්‍ය වූ විට පමණි. අරාබි, බෙංගාලි, ස්පාඤ්ඤ (ස්පාඤ්ඤය සහ කොලොම්බියාව), ප්‍රංශ (කැනඩාව), ඉතාලි, ජපන්, පෘතුගීසි (බ්‍රසීලය සහ පෘතුගාලය), රුසියානු, සිංහල, ටැගලොග්, වියට්නාම සහ සරල කළ චීන භාෂාවලින් අතුරුමුහුණත ලබාගත හැක.

## මිනිසුන් Drift තෝරාගන්නේ ඇයි?

බොහෝ "නොමිලේ" ලැබෙන සංස්කාරක ඔබේ වීඩියෝව හොඳ මට්ටමකට පැමිණි වහාම ගිණුම් සෑදීම, දිය සලකුණු ඇතුළත් කිරීම හෝ මුදල් අය කිරීම සිදු කරයි. Drift එහි ප්‍රතිවිරුද්ධ දෙයයි: **සම්පූර්ණයෙන්ම ඔබට අයිතියි, ඔබේ පරිගණකයේම ක්‍රියාත්මක වේ, GPLv3 විවෘත මෘදුකාංගයකි, පිවිසීමේ සීමා නොමැත.**

එය සමාජ මාධ්‍ය සඳහා තත්පර 30ක කෙටි වීඩියෝවක් සඳහා ඉතා වේගවත් වන අතරම, මහා පරිමාණ ව්‍යාපෘතියකට පවා ප්‍රමාණවත් තරම් බලවත්ය — කාලරාමුව මත AI සහායක, 3D සහ Lottie, උපසිරැසි, ප්‍රයෝග, උසස් ශ්‍රව්‍ය පාලනය, බුද්ධිමත් පසුබිම් වෙන් කිරීම් සහ බහු-කැමරා පාලනය.

## Drift පරිවර්තනය කිරීමට අපට උදව් වන්න

[![පරිවර්තන තත්ත්වය](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## සංවර්ධකයින් සඳහා

බිල්ඩ් කිරීම, පැකේජ සැකසීම, ගෘහ නිර්මාණ ශිල්පය සහ සහායක ප්‍රොටෝකෝලය `docs/` තුළ අඩංගු වේ:

- [ගොඩනැගීම, පරීක්ෂා කිරීම, පැකේජ සැකසීම සහ ගෘහ නිර්මාණ ශිල්පය](../docs/BUILDING.md)
- [GPU ප්‍රයෝග](../docs/gpu-effects.md)
- [GPU සංක්‍රාන්ති](../docs/gpu-transitions.md)
- [Agent ප්‍රවේශය / MCP](../docs/MCP.md)

## සහාය සහ ප්‍රතිපෝෂණ

දෝෂයක් හමු වූයේද නැතහොත් නව අදහසක් තිබේද?
[GitHub හි Issue එකක් විවෘත කරන්න](https://github.com/CutWire-Studios/Drift/issues).

## දායක වීම

[CONTRIBUTING.md](../CONTRIBUTING.md) යනු පුල් රික්වෙස්ට් (Pull Request) සඳහා වන මාර්ගෝපදේශයයි: විශාල වෙනසකට පෙර issue එකක් විවෘත කරන්න, සෑම ඉල්ලීමක්ම තනි වෙනසකට සීමා කරන්න, පරීක්ෂණ ධාවනය කරන්න, සහ කාර්යය GPLv3 යටතේ බලපත්‍ර ලබා දෙන්න.

ප්‍රයෝග, සංක්‍රාන්ති, ආකෘති (templates) සහ ශබ්ද ප්‍රයෝග ඇඩෝන ලෙස සැලකේ. ඒවා [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons) වෙත යොමු කරන්න. ඒවා ධාවනය වන මූලික එන්ජිමේ වෙනස්කම් මෙම ගබඩාව තුළම පවතී.

[නිල නිකුතුවක්](https://github.com/CutWire-Studios/Drift/releases) ස්ථාපනය කර සැබෑ ව්‍යාපෘතියක් සංස්කරණය කිරීමද විශාල උපකාරයකි; අක්‍රිය වූ දේ හෝ වඩා හොඳින් ක්‍රියා කළ යුතු දේ වාර්තා කරන්න. [Discord](https://discord.gg/J5ANFz6Z3y) තුළ දායක වූ අයට `@Contributor` තනතුර ඉල්ලා සිටිය හැක.

## දායකයින්

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="දායකයින්" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## බලපත්‍රය

GPLv3 — [LICENSE](../LICENSE) බලන්න.

## තරු ප්‍රස්ථාරය (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
