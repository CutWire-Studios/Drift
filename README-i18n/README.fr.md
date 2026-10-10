<p align="center">
  <img src="../Drift_icon.png" alt="Icône Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>L'éditeur vidéo de bureau gratuit qui donne à vos vidéos un rendu soigné — pas seulement « assez bien ».</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Dernière version" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Téléchargements GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Installations Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Rejoindre le Discord de Drift" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Licence : GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Plateforme : Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift est un éditeur vidéo de bureau développé par CutWire Studios. Glissez-y vos clips, ajoutez des effets, des sous-titres, des autocollants
et de la musique, puis exportez une vidéo impeccable — **sans abonnement, sans filigrane et sans compte obligatoire**.

Il est conçu pour les projets réels d'aujourd'hui : Reels et Shorts, clips de jeux vidéo, projets scolaires,
tutoriels, démonstrations de produits, mèmes et tout ce que vous voulez rendre percutant sans dépendre d'un navigateur
ni payer un abonnement mensuel.

Ce que vous voyez dans l'aperçu correspond exactement à ce que vous exportez. Un seul moteur de composition, un seul rendu, zéro mauvaise surprise.

## Téléchargement

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=fr" alt="Disponible sur Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Disponible sur Microsoft Store" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Disponible sur Google Play (test fermé)" height="80">
  </a>
</p>

<p align="center">Google Play est en phase de test fermé — <a href="../docs/play-testing.md">comment y participer</a>.</p>

**Linux** — installer depuis Flathub :

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — installer via Homebrew :

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Premier lancement sur macOS :** Drift bénéficie d'une signature ad-hoc (non notariée par Apple). L'attribut de quarantaine est automatiquement supprimé lors d'une installation via Homebrew. En cas d'installation manuelle via `.dmg` ou si Gatekeeper bloque l'ouverture, exécutez :
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Ou récupérez une version prête à l'emploi pour votre système depuis la
[dernière publication](https://github.com/CutWire-Studios/Drift/releases/latest) :

| Plateforme | Paquet |
|------------|--------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Programme d'installation (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Archive portable zip](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Image disque (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (test fermé)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

Sur smartphone, rejoignez le [programme de test fermé Google Play](../docs/play-testing.md), ou téléchargez `Drift-*-arm64-v8a.apk` depuis la dernière version pour l'installer (ou via `adb install Drift-*-arm64-v8a.apk`). Utilisez `x86_64` pour les émulateurs.

Consultez [toutes les versions](https://github.com/CutWire-Studios/Drift/releases) pour retrouver les versions antérieures et les journaux de modifications détaillés.

## Capture d'écran

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="L'éditeur Drift : bac multimédia à gauche, caméra 3D dans l'aperçu, inspecteur de texte à droite, timeline multipiste en bas" width="900">
</p>

## Fonctionnalités

**Un agent IA peut modifier le projet en cours.** Importez, découpez, étalonnez et exportez directement dans le projet ouvert.
Le même éditeur peut fonctionner en mode sans fenêtre (headless).

**3D sur la timeline.** Inclinez vos clips dans l'espace, éclairez-les et insérez un titre en arrière-plan derrière votre sujet. Déposez
un modèle 3D et visualisez-le directement dans votre montage.

**Animations Lottie.** Les animations graphiques et vectorielles restent parfaitement nettes, peu importe la résolution. Changez leurs couleurs
ou leurs textes directement sur place.

**Images clés (Keyframing) avancées.** Animez la position, l'échelle, la rotation, l'opacité et les paramètres d'effets au
fil du temps. Dessinez vos propres courbes d'animation.

**Plus de 150 transitions et 40 effets.** Prévisualisez chaque rendu directement sur vos rushes. Un clic suffit pour appliquer
une chaîne complète : Beat Drop, Glitch Cut, Neon Cutout. Traînées d'images précédentes et étalonnages verrouillés à un clip.

**Traitement 100% local.** Cliquez sur un sujet pour le détacher du plan. Ajoutez de la profondeur de champ et des lumières
dédiées au clip. Sous-titres automatiques, agrandissement vidéo (upscale) et retouche faciale en direct — le tout en local sur votre
ordinateur.

**Éditez par le texte.** La parole est retranscrite en texte sur le clip. Supprimez des phrases, retirez les hésitations et tics de langage, effacez les silences,
sélectionnez les meilleures prises, identifiez les intervenants et générez des sous-titres synchronisés.

**Des titres au rendu professionnel.** 33 packs de styles : karaoké, style Hormozi, néon, chrome, holographique,
manuscrit. Dupliquez un style sur l'ensemble des sous-titres de la piste.

**Déplacez une pile entière d'un seul coup.** Un calque peut déplacer, redimensionner, faire pivoter, incliner et estomper tout ce qui se trouve en
dessous. Imbriquez des calques ou ouvrez un plan composé lorsque votre pile nécessite sa propre timeline.

**Montage au rythme de la musique.** La timeline s'aligne automatiquement sur le tempo. Les clips se scindent et se calent sur les temps. La musique
baisse automatiquement sous la voix (audio ducking) puis reprend son niveau normal. Recadrez un plan horizontal en format vertical
en suivant les visages.

**Une timeline fluide et intuitive.** Bandes de film multipistes, montage ondulé (ripple), magnétisme, masques, arrêt sur image, signets,
table de mixage intégrée. Scindez un clip : l'étalonnage est préservé. En cas de plantage, la dernière sauvegarde reste intacte.

**Un rendu audio soigné.** Nettoyez la voix, mesurez l'intensité sonore, égalisez et compressez, préservez la hauteur tonale
lors des changements de vitesse et enregistrez une voix off.

**Recherchez des plans. Changez d'angle multicaméra.** Recherchez dans vos vidéos ce qui s'affiche à l'écran. Visionnez tous les angles simultanément
et basculez au bon moment.

**Stabilisez, exportez et transportez votre projet.** Atténuez les tremblements, améliorez la résolution et lisez les fichiers lourds
en toute fluidité. MP4, GIF, audio seul ou sélection d'une plage temporelle. Regroupez les médias avec le projet pour que les chemins restent intacts.

**Android offre la même expérience.** Timeline complète, effets et export directement sur votre smartphone. Partagez instantanément depuis
l'application.

**Banque de médias, voix synthétiques et installation compacte.** Cherchez des médias libres de droits directement dans votre bac. Générez des voix off et des effets
sonores. Les polices, autocollants et modèles supplémentaires se téléchargent à la demande. Interface disponible en arabe, bengali, espagnol (Espagne
et Colombie), français (Canada), italien, japonais, portugais (Brésil et Portugal), russe,
cinghalais, tagalog, vietnamien et chinois simplifié.

## Pourquoi choisir Drift ?

La majorité des éditeurs vidéo dits « gratuits » exigent un compte, appliquent un filigrane gênant ou imposent un abonnement payant dès que votre vidéo commence
à avoir de l'allure. Drift fait le choix inverse : **votre logiciel, sur votre machine, sous licence GPLv3, sans compte ni barrière.**

Il est assez rapide pour un montage court de 30 secondes et suffisamment complet pour un projet d'envergure — agent IA sur la
timeline, 3D et Lottie, sous-titres, effets vidéo, traitement audio, détourage intelligent et multicaméra.

## Aidez-nous à traduire Drift

[![Statut de la traduction](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Espace développeurs

La documentation pour la compilation, l'empaquetage, l'architecture et le protocole d'agent se trouve dans `docs/` :

- [Compilation, tests, empaquetage et architecture](../docs/BUILDING.md)
- [Effets GPU](../docs/gpu-effects.md)
- [Transitions GPU](../docs/gpu-transitions.md)
- [Accès Agent / MCP](../docs/MCP.md)

## Aide et retours d'expérience

Vous avez repéré un bug ou avez une suggestion ? Ouvrez un
[ticket sur GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Contribuer

[CONTRIBUTING.md](../CONTRIBUTING.md) présente les consignes pour les pull requests : ouvrez une issue avant tout changement majeur, limitez chaque requête à une seule modification, lancez la suite de tests et publiez votre code sous licence GPLv3.

Les effets, transitions, modèles et filtres audio sont des extensions. Proposez-les sur [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Les améliorations du moteur restent dans ce dépôt.

Installer une [version officielle](https://github.com/CutWire-Studios/Drift/releases) et monter un vrai projet constitue déjà une aide précieuse ; signalez ce qui bloque ou ce qui mérite d'être perfectionné. Sur [Discord](https://discord.gg/J5ANFz6Z3y), les contributeurs peuvent demander le rôle `@Contributor`.

## Contributeurs

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Contributeurs" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Licence

GPLv3 — voir [LICENSE](../LICENSE).

## Évolution des étoiles (Star History)

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Graphique Star History" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
