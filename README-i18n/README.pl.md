<p align="center">
  <img src="../Drift_icon.png" alt="Ikona Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>Darmowy edytor wideo na komputer, dzięki któremu Twoje filmy wyglądają na dopracowane — a nie po prostu „wystarczająco dobre”.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Najnowsze wydanie" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Pobrania z GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Instalacje Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Dołącz do Drift Discord" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Licencja: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Platformy: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift to komputerowy edytor wideo od CutWire Studios. Przeciągaj klipy, dodawaj efekty, napisy, naklejki
i muzykę, a następnie eksportuj dopracowany film — **bez subskrypcji, bez znaków wodnych i bez konieczności zakładania konta**.

Został stworzony z myślą o materiałach, które twórcy montują na co dzień: Reels i Shorts, fragmenty gier, projekty szkolne,
poradniki, prezentacje produktów, memy oraz wszystko, co ma wyglądać profesjonalnie bez ograniczeń przeglądarki
i bez comiesięcznych opłat.

To, co widzisz na podglądzie, jest dokładnie tym, co eksportujesz. Jeden silnik kompozytowy, spójny wygląd, zero niespodzianek.

## Pobierz

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=pl" alt="Pobierz z Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Pobierz z Microsoft Store" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Pobierz z Google Play (zamknięte testy)" height="80">
  </a>
</p>

<p align="center">Google Play znajduje się w fazie zamkniętych testów — <a href="../docs/play-testing.md">jak dołączyć</a>.</p>

**Linux** — instalacja przez Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — instalacja przez Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Pierwsze uruchomienie na macOS:** Drift jest podpisany ad-hoc (bez notaryzacji Apple). Atrybut kwarantanny jest usuwany automatycznie przy instalacji przez Homebrew. Jeśli instalujesz ręcznie z pliku `.dmg` lub Gatekeeper blokuje otwarcie, uruchom w terminalu:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Możesz też pobrać gotową paczkę dla swojej platformy z
[najnowszego wydania](https://github.com/CutWire-Studios/Drift/releases/latest):

| Platforma | Pakiet |
|-----------|--------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Instalator (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Wersja przenośna zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Obraz dysku (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (zamknięte testy)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

Na telefonie dołącz do [zamkniętych testów w Google Play](../docs/play-testing.md) lub pobierz pakiet `Drift-*-arm64-v8a.apk` z najnowszego wydania i zainstaluj go (albo za pomocą `adb install Drift-*-arm64-v8a.apk`). W przypadku emulatorów użyj wersji `x86_64`.

Zobacz [wszystkie wydania](https://github.com/CutWire-Studios/Drift/releases), aby przejrzeć wcześniejsze wersje i pełne listy zmian.

## Zrzut ekranu

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Edytor Drift: zasobnik mediów po lewej, kamera 3D w podglądzie, inspektor tekstu po prawej, wielościeżkowa oś czasu poniżej" width="900">
</p>

## Funkcje

**Agent AI może edytować otwarty projekt.** Importuj, przycinaj, koryguj kolory i eksportuj bezpośrednio w otwartym projekcie. Ten sam edytor może działać bez interfejsu graficznego (tryb headless).

**3D na osi czasu.** Przechylaj klipy w przestrzeni 3D, oświetlaj je i umieszczaj napisy za obiektem. Przeciągnij model 3D, a odtworzy się bezpośrednio w montażu.

**Animacje Lottie.** Grafika wektorowa i ruchoma pozostaje idealnie ostra w każdym rozmiarze. Zmieniaj kolory i teksty bezpośrednio w projekcie.

**Zaawansowane klatki kluczowe.** Animuj pozycję, skalę, obrót, przezroczystość i parametry efektów w czasie. Samodzielnie rysuj krzywe przejść.

**Ponad 150 przejść i 40+ efektów.** Podgląd każdego stylu na żywo na Twoich materiałach. Jednym kliknięciem nałożysz całe zestawy efektów: Beat Drop, Glitch Cut, Neon Cutout. Smugi poprzednich klatek oraz grading przypisany do konkretnego klipu.

**Lokalne przetwarzanie na Twoim komputerze.** Kliknij obiekt, aby wyciąć go z tła. Dodaj głębię ostrości i oświetlenie przypisane do ujęcia. Automatyczne napisy, upscaling wideo i retusz twarzy na żywo — wszystko w 100% na Twoim komputerze.

**Montaż tekstem.** Mowa zamienia się w tekst na klipie. Wycinaj niepotrzebne frazy, usuwaj pauzy i powtórzenia, wybieraj najlepsze duble, oznaczaj mówców i twórz precyzyjne napisy na podstawie znaczników czasowych.

**Profesjonalne szablony tytułów.** 33 pakiety stylów: karaoke, styl Hormozi, neon, chrom, hologram, pismo odręczne. Kopiuj jeden styl na wszystkie napisy na ścieżce za jednym razem.

**Zarządzanie całymi stosami warstw.** Jedna warstwa przekształceń może przesuwać, skalować, obracać, przechylać i wygaszać wszystko pod sobą. Zagnieżdżaj warstwy lub twórz kompozycje, gdy ujęcie wymaga własnej osi czasu.

**Montaż pod rytm muzyki.** Oś czasu automatycznie przyciąga do bitu. Klipy dzielą się idealnie w takt. Muzyka automatycznie wycisza się pod głosem (audio ducking) i wraca do normy. Automatycznie przekadruj poziome wideo do formatu pionowego ze śledzeniem twarzy.

**Przewidywalna oś czasu.** Wielościeżkowe taśmy filmowe, montaż ze zwijaniem (ripple), przyciąganie magnetyczne, maski, stopklatki, zakładki, wbudowany mikser audio. Podziel klip, a korekcja barwna zostanie zachowana. Awaria programu nie narusza ostatniego zapisu.

**Studyjna jakość dźwięku.** Czyszczenie głosu z szumów, pomiar głośności (loudness), korektor EQ i kompresor, zachowanie tonacji przy zmianie prędkości, nagrywanie lektora bezpośrednio w aplikacji.

**Wyszukiwanie ujęć i montaż wielokamerowy.** Wyszukuj materiały na podstawie tego, co znajduje się na ekranie. Podglądaj wszystkie kamery jednocześnie i przełączaj ujęcia w idealnym momencie.

**Stabilizacja, eksport i przenośność projektu.** Wygładzaj drgania kamery, zwiększaj rozdzielczość i płynnie odtwarzaj wymagające pliki. Eksport do MP4, GIF, samego dźwięku lub wybranego fragmentu. Pakuj multimedia razem z projektem, aby ścieżki plików zawsze pozostały nienaruszone.

**Ten sam edytor na systemie Android.** Oś czasu, efekty i eksport bezpośrednio na telefonie. Udostępniaj gotowe wideo prosto z aplikacji.

**Materiały stockowe, synteza głosu i niewielki instalator.** Wyszukuj darmowe multimedia bezpośrednio w zasobniku. Generuj głos lektora i efekty dźwiękowe. Czcionki, naklejki i dodatkowe modele pobierają się na żądanie. Interfejs w językach: arabski, bengalski, hiszpański (Hiszpania i Kolumbia), francuski (Kanada), włoski, japoński, portugalski (Brazylia i Portugalia), rosyjski, syngaleski, tagalski, wietnamski i chiński uproszczony.

## Dlaczego Drift?

Większość „darmowych” edytorów wymaga rejestracji konta, nakłada znak wodny lub żąda płatnej subskrypcji w momencie, gdy film zaczyna wyglądać profesjonalnie. Drift działa odwrotnie: **jest Twój, działa lokalnie na Twoim komputerze, jest oparty na licencji GPLv3 i nie wymaga logowania.**

Jest wystarczająco szybki do 30-sekundowego klipu na media społecznościowe i wystarczająco zaawansowany do pełnowymiarowej produkcji — asystent AI na osi czasu, 3D i Lottie, automatyczne napisy, efekty, zaawansowany dźwięk, inteligentne wycinanie tła i praca na wielu kamerach.

## Pomóż w tłumaczeniu Drift

[![Status tłumaczenia](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Dla programistów

Instrukcje budowania, pakowania, opis architektury oraz protokół komunikacji z agentami znajdują się w katalogu `docs/`:

- [Budowanie, testowanie, pakowanie i architektura](../docs/BUILDING.md)
- [Efekty GPU](../docs/gpu-effects.md)
- [Przejścia GPU](../docs/gpu-transitions.md)
- [Integracja z Agentami / MCP](../docs/MCP.md)

## Pomoc i opinie

Znalazłeś błąd lub masz pomysł na nową funkcję? Zgłoś
[problem na GitHubie](https://github.com/CutWire-Studios/Drift/issues).

## Jak współtworzyć projekt

Plik [CONTRIBUTING.md](../CONTRIBUTING.md) to przewodnik po pull requestach: przed dużymi zmianami załóż zgłoszenie, ograniczaj każdy PR do jednej modyfikacji, uruchom testy i udostępniaj kod na licencji GPLv3.

Efekty, przejścia, szablony i filtry audio są dodatkami. Zgłaszaj je do repozytorium [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Zmiany w silniku głównym pozostają w tym repozytorium.

Zainstalowanie [oficjalnego wydania](https://github.com/CutWire-Studios/Drift/releases) i montaż prawdziwego projektu to już ogromna pomoc — zgłaszaj napotkane usterki. Na naszym serwerze [Discord](https://discord.gg/J5ANFz6Z3y) współtwórcy mogą otrzymać rolę `@Contributor`.

## Współtwórcy

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Współtwórcy" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Licencja

GPLv3 — szczegóły w pliku [LICENSE](../LICENSE).

## Historia gwiazdek (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Wykres Star History" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
