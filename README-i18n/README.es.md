<p align="center">
  <img src="../Drift_icon.png" alt="Icono de Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>El editor de vídeo gratuito de escritorio que hace que tus vídeos parezcan acabados — no simplemente “suficientemente buenos”.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Última versión" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Descargas de GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Instalaciones de Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Únete al Discord de Drift" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Licencia: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Plataforma: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift es un editor de vídeo de escritorio de CutWire Studios. Añade clips, aplica efectos, subtítulos, pegatinas
y música, y luego exporta un vídeo impecable — **sin suscripción, sin marcas de agua y sin cuenta**.

Está creado para los vídeos que la gente realmente hace: Reels y Shorts, clips de videojuegos, trabajos escolares,
tutoriales, demos de productos, memes y cualquier proyecto que quieras que se vea profesional sin depender de un navegador
ni pagar cuotas mensuales.

Lo que ves en la vista previa es lo que exportas. Un solo compositor, un solo estilo, sin sorpresas.

## Descarga

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=es" alt="Consíguelo en Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Consíguelo en Microsoft" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Consíguelo en Google Play (prueba cerrada)" height="80">
  </a>
</p>

<p align="center">Google Play está en prueba cerrada — <a href="../docs/play-testing.md">cómo unirse</a>.</p>

**Linux** — instalar desde Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — instalar mediante Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Primer inicio en macOS:** Drift cuenta con firma ad-hoc (no está notarizado por Apple). El atributo de cuarentena se elimina automáticamente al instalar a través de Homebrew. Si lo instalas manualmente mediante `.dmg` o si Gatekeeper impide abrirlo, ejecuta:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

O descarga una versión compilada para tu plataforma desde la
[última versión](https://github.com/CutWire-Studios/Drift/releases/latest):

| Plataforma | Paquete |
|------------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Instalador (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Zip portable](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Imagen de disco (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (prueba cerrada)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

En un teléfono móvil, únete a las [pruebas cerradas en Google Play](../docs/play-testing.md), o descarga `Drift-*-arm64-v8a.apk` de la última versión e instálalo (o con `adb install Drift-*-arm64-v8a.apk`). Usa `x86_64` para emuladores.

Consulta [todas las versiones](https://github.com/CutWire-Studios/Drift/releases) para ver versiones anteriores y registros de cambios completos.

## Captura de pantalla

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="El editor Drift: contenedor de medios a la izquierda, cámara 3D en la vista previa, inspector de texto a la derecha, línea de tiempo multipista abajo" width="900">
</p>

## Características

**Un agente puede editar el proyecto abierto.** Importa, corta, etalona y exporta en el proyecto que tienes abierto.
El mismo editor puede funcionar sin ventana.

**3D en la línea de tiempo.** Inclina clips en el espacio, ilumínalos y coloca un título detrás del sujeto. Arrastra
un modelo 3D y se reproducirá en el mismo corte.

**Animaciones Lottie.** Los gráficos animados y el arte vectorial se mantienen nítidos a cualquier tamaño. Cambia el color
o el texto directamente en su lugar.

**Fotogramas clave avanzados.** Anima posición, escala, rotación, opacidad y parámetros de efectos a lo
largo del tiempo. Dibuja la curva tú mismo.

**Más de 150 transiciones y más de 40 efectos.** Cada estilo se previsualiza sobre tu metraje. Un solo clic puede aplicar
una pila completa — Beat Drop, Glitch Cut, Neon Cutout. Rastros de fotogramas anteriores y etalonajes que se mantienen
en cada clip.

**En tu propio equipo.** Haz clic en un sujeto y sepáralo de la toma. Añade profundidad de campo y luces que
pertenecen al clip. Subtítulos automáticos, escalado de vídeo y retoque facial en vivo — todo en tu
ordenador.

**Edita las palabras.** El habla se convierte en texto en el clip. Corta frases, elimina muletillas, quita silencios, elige
las mejores tomas, identifica interlocutores y genera subtítulos basados en esos tiempos.

**Títulos con estilo auténtico.** 33 paquetes: karaoke, estilo Hormozi, neón, cromo, holográfico,
manuscrito. Copia un estilo en todos los subtítulos de la pista.

**Mueve pilas enteras a la vez.** Una capa puede arrastrar, escalar, rotar, inclinar y atenuar todo lo que esté debajo
de ella. Agrupa capas o abre una composición cuando la pila requiera su propia línea de tiempo.

**Corta al ritmo de la música.** La línea de tiempo se ajusta al ritmo. Los clips se dividen y caen sobre el compás. La música
se atenúa bajo la voz y recupera su volumen. Reencuadra una toma horizontal a un vídeo vertical que
sigue el rostro.

**Una línea de tiempo con comportamiento predecible.** Tiras de película multipista, ripple (edición ondulada), ajuste magnético, máscaras, congelación de fotogramas, marcadores,
un mezclador. Divide un clip y el etalonaje se conserva. Ante un cierre inesperado, el último guardado permanece intacto.

**Audio que suena pulido.** Limpia la voz, mide la sonoridad, ecualiza y comprime, mantén el tono
al cambiar la velocidad y graba voz en off.

**Busca tomas. Cambia de cámara.** Busca en el metraje lo que aparece en pantalla. Visualiza todos los ángulos a la vez y
ejecuta el corte al instante.

**Estabiliza, exporta y lleva el proyecto contigo.** Suaviza clips temblorosos, escala resolución y reproduce archivos pesados
con fluidez. MP4, GIF, solo audio o solo un rango seleccionado. Empaqueta los medios con el proyecto para mantener todas las rutas.

**Android es el mismo editor.** Línea de tiempo, efectos y exportación directamente en el teléfono. Comparte directamente desde
la app.

**Stock, voces y una instalación ligera.** Busca contenido de stock directo al contenedor. Genera voces en off y efectos
de sonido. Las fuentes, pegatinas y modelos adicionales se descargan cuando los usas. Interfaz en árabe, bengalí, español (España
y Colombia), francés (Canadá), italiano, japonés, portugués (Brasil y Portugal), ruso,
cingalés, tagalo, vietnamita y chino simplificado.

## Por qué elegir Drift

La mayoría de los editores “gratuitos” exigen una cuenta, dejan marcas de agua o piden una suscripción en cuanto el vídeo empieza
a verse bien. Drift es lo contrario: **tuyo, en tu ordenador, GPLv3 y sin barreras de inicio de sesión.**

Es lo suficientemente rápido para un corte social de 30 segundos y lo bastante potente para un proyecto profesional — un agente en la
línea de tiempo, 3D y Lottie, subtítulos, efectos, audio, recorte inteligente y multicámara.

## Ayúdanos a traducir Drift

[![Estado de la traducción](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Para desarrolladores

Compilación, empaquetado, arquitectura y el protocolo de agentes se encuentran en `docs/`:

- [Compilación, pruebas, empaquetado y arquitectura](../docs/BUILDING.md)
- [Efectos GPU](../docs/gpu-effects.md)
- [Transiciones GPU](../docs/gpu-transitions.md)
- [Acceso de agentes / MCP](../docs/MCP.md)

## Ayuda y comentarios

¿Encontraste un error o tienes una idea? Abre una
[incidencia en GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Contribuir

[CONTRIBUTING.md](../CONTRIBUTING.md) es la guía para pull requests: abre una incidencia antes de un cambio grande, limita cada solicitud a un solo cambio, ejecuta las pruebas y licencia el trabajo bajo GPLv3.

Los efectos, transiciones, plantillas y efectos de audio son complementos. Envíalos a [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Las modificaciones del motor que los ejecuta permanecen en este repositorio.

Instalar una [versión oficial](https://github.com/CutWire-Studios/Drift/releases) y editar un proyecto real ya es una gran ayuda; reporta lo que falle o lo que pueda mejorar. En [Discord](https://discord.gg/J5ANFz6Z3y), quienes hayan contribuido pueden solicitar el rol de `@Contributor`.

## Colaboradores

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Colaboradores" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Licencia

GPLv3 — consulta [LICENSE](../LICENSE).

## Historial de estrellas

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Gráfico de estrellas de Star History" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
