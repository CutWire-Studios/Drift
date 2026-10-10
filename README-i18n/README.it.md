<p align="center">
  <img src="../Drift_icon.png" alt="Icona di Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>L'editor video desktop gratuito che rende i tuoi video rifiniti — non semplicemente “abbastanza buoni”.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Ultima release" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Download da GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Installazioni Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Entra nel Discord di Drift" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Licenza: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Piattaforme: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift è un editor video per computer di CutWire Studios. Trascina i tuoi filmati, applica effetti, sottotitoli, adesivi
e musica, quindi esporta un video impeccabile — **senza abbonamenti, senza filigrane e senza account obbligatorio**.

È progettato per i video che le persone creano davvero ogni giorno: Reels e Shorts, clip di videogiochi, progetti scolastici,
tutorial, presentazioni di prodotti, meme e qualunque contenuto tu voglia rendere professionale senza dover lavorare nel browser
o pagare una quota mensile.

Ciò che vedi nell'anteprima è esattamente ciò che viene esportato. Un solo compositore grafico, un'unica resa, zero imprevisti.

## Download

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=it" alt="Scarica da Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Disponibile su Microsoft Store" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Disponibile su Google Play (test chiuso)" height="80">
  </a>
</p>

<p align="center">Google Play è in fase di test chiuso — <a href="../docs/play-testing.md">scopri come partecipare</a>.</p>

**Linux** — installa da Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — installa tramite Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Primo avvio su macOS:** Drift è firmato ad-hoc (non notarizzato da Apple). L'attributo di quarantena viene rimosso automaticamente installando con Homebrew. Se effettui l'installazione manuale con `.dmg` o se Gatekeeper ne blocca l'apertura, esegui:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Oppure scarica una build pronta per la tua piattaforma dall'
[ultima release](https://github.com/CutWire-Studios/Drift/releases/latest):

| Piattaforma | Pacchetto |
|-------------|-----------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Installer (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Archivio zip portatile](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Immagine disco (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (test chiuso)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

Su smartphone, unisciti al [programma di test chiuso su Google Play](../docs/play-testing.md), oppure scarica il file `Drift-*-arm64-v8a.apk` dalla pagina delle release e installalo (o con `adb install Drift-*-arm64-v8a.apk`). Usa `x86_64` per gli emulatori.

Consulta [tutte le versioni](https://github.com/CutWire-Studios/Drift/releases) per visualizzare le release precedenti e i changelog completi.

## Schermata

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="L'editor Drift: contenitore multimediale a sinistra, telecamera 3D nell'anteprima, ispettore di testo a destra, timeline multitraccia in basso" width="900">
</p>

## Funzionalità

**Un agente AI può modificare il progetto aperto.** Importa, taglia, applica color grading ed esporta direttamente nel progetto in corso d'opera.
Lo stesso editor può operare senza interfaccia grafica (headless).

**3D integrato nella timeline.** Inclina i filmati nello spazio tridimensionale, illuminali e posiziona titoli dietro il soggetto. Inserisci
un modello 3D e riproducilo direttamente nel montaggio.

**Animazioni Lottie.** Grafica in movimento e illustrazioni vettoriali rimangono nitide a qualsiasi risoluzione. Modifica colori
e testi direttamente sul posto.

**Keyframing avanzato.** Anima posizione, scala, rotazione, opacità e parametri degli effetti nel corso
del tempo. Disegna manualmente la curva d'animazione.

**Oltre 150 transizioni e più di 40 effetti.** Guarda l'anteprima di ogni stile direttamente sul tuo filmato. Con un solo clic applichi
un'intera sequenza di effetti: Beat Drop, Glitch Cut, Neon Cutout. Scie di fotogrammi precedenti e gradazioni cromatiche che restano
fissate a una clip specifica.

**Elaborazione locale sulla tua macchina.** Clicca su un soggetto per scontornarlo dal piano. Aggiungi profondità di campo e luci
legate alla clip. Sottotitoli automatici, upscaling video e ritocco volti in tempo reale — tutto interamente sul tuo
computer.

**Monta tramite il testo.** Il parlato viene trascritto in testo sulla clip. Taglia le frasi, rimuovi i riempitivi verbali, cancella i silenzi, scegli
i ciak migliori, contrassegna i parlanti e crea sottotitoli a partire da tali punti temporali.

**Titoli con resa professionale.** 33 pacchetti di stili: karaoke, stile Hormozi, neon, cromo, olografico,
a mano libera. Applica un solo stile a tutti i sottotitoli della traccia.

**Sposta un intero livello composto in un colpo solo.** Un livello può trascinare, scalare, ruotare, inclinare e sfumare tutto ciò che si trova al di
sotto. Raggruppa i livelli o apri un piano composto quando la sequenza richiede una propria timeline dedicata.

**Taglio a tempo di musica.** La timeline si aggancia al ritmo. I tagli si allineano con precisione alla battuta. La musica
si abbassa automaticamente (audio ducking) sotto la voce e riprende il suo volume. Ricadra un'inquadratura orizzontale in formato verticale
seguendo il volto.

**Una timeline che risponde a ogni esigenza.** Pellicole multitraccia, montaggio a catena (ripple), aggancio magnetico, maschere, fermo immagine, segnalibri,
un mixer audio completo. Dividi una clip e il color grading rimane invariato. In caso di chiusura imprevista, l'ultimo salvataggio resta intatto.

**Audio pulito e bilanciato.** Pulisci la traccia vocale, monitora il volume (loudness), equalizza e comprimi, mantieni l'intonazione
originale al variare della velocità e registra voci fuori campo.

**Trova le inquadrature. Alterna tra più telecamere.** Cerca nei filmati ciò che compare a video. Guarda tutte le angolazioni contemporaneamente
e cambia inquadratura con precisione.

**Stabilizza, esporta e porta il progetto con te.** Riduci le oscillazioni, aumenta la risoluzione e riproduci file pesanti
in modo fluido. Esporta in MP4, GIF, solo traccia audio o intervallo selezionato. Archivia tutti i contenuti assieme al montaggio così da preservare ogni percorso.

**Android offre la stessa potenza.** Timeline, effetti ed esportazione completa direttamente dallo smartphone. Condividi all'istante
dall'applicazione.

**Media stock, voci sintetiche e installazione snella.** Cerca materiali stock direttamente nel contenitore multimediale. Genera voci fuori campo ed effetti
sonori. Font, adesivi e modelli aggiuntivi vengono scaricati all'occorrenza. Interfaccia disponibile in arabo, bengalese, spagnolo (Spagna
e Colombia), francese (Canada), italiano, giapponese, portoghese (Brasile e Portogallo), russo,
singalese, tagalog, vietnamita e cinese semplificato.

## Perché le persone scelgono Drift

La maggior parte degli editor video definiti “gratuiti” richiede la registrazione, applica filigrane o propone un piano a pagamento non appena il montaggio
inizia ad avere un bell'aspetto. Drift fa il contrario: **tuo, sul tuo computer, GPLv3 e senza alcun blocco all'accesso.**

È sufficientemente reattivo per un video breve da 30 secondi e profondo abbastanza per una produzione completa — un agente AI sulla
timeline, 3D e Lottie, sottotitoli, effetti avanzati, equalizzazione audio, ritaglio intelligente e multicamera.

## Aiutaci a tradurre Drift

[![Stato della traduzione](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Per gli sviluppatori

Istruzioni di compilazione, packaging, architettura e protocollo per agenti sono documentati nella cartella `docs/`:

- [Compilazione, testing, packaging e architettura](../docs/BUILDING.md)
- [Effetti GPU](../docs/gpu-effects.md)
- [Transizioni GPU](../docs/gpu-transitions.md)
- [Integrazione Agenti / MCP](../docs/MCP.md)

## Supporto e riscontri

Hai riscontrato un bug o vuoi proporre una nuova funzionalità? Apri una
[segnalazione su GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Come contribuire

Il file [CONTRIBUTING.md](../CONTRIBUTING.md) descrive la procedura per le pull request: apri una discussione prima di modifiche rilevanti, limita ogni proposta a un unico cambiamento mirato, esegui i test e distribuisci il lavoro con licenza GPLv3.

Effetti, transizioni, modelli e filtri audio sono considerati add-on. Inviali al repository [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Le modifiche al motore principale restano in questo repository.

Scaricare una [release](https://github.com/CutWire-Studios/Drift/releases) e lavorare su un progetto reale è già un aiuto prezioso: segnala eventuali problemi o migliorie. Su [Discord](https://discord.gg/J5ANFz6Z3y), chi ha inviato contributi può richiedere il ruolo `@Contributor`.

## Collaboratori

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Contributori" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Licenza

GPLv3 — consulta [LICENSE](../LICENSE).

## Storico stelle

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Grafico Star History" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
