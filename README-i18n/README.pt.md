<p align="center">
  <img src="../Drift_icon.png" alt="Ícone do Drift" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>O editor de vídeo gratuito para desktop que deixa seus vídeos com acabamento profissional — e não apenas “aceitável”.</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="Versão mais recente" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="Downloads no GitHub" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Instalações no Flathub" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="Entrar no Discord do Drift" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="Licença: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Plataformas: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift é um editor de vídeo para computador criado pela CutWire Studios. Adicione seus clipes, aplique efeitos, legendas, adesivos
e trilhas sonoras, e exporte um vídeo com excelente acabamento — **sem assinaturas, sem marcas d’água e sem exigir cadastro**.

Ele foi desenvolvido para os projetos que as pessoas realmente produzem: Reels e Shorts, gravações de gameplay, apresentações escolares,
tutoriais, demonstrações de produtos, memes e tudo aquilo que você deseja que fique impecável sem ficar preso a um navegador
ou pagar mensalidades recorrentes.

O que você vê na pré-visualização é exatamente o que é exportado. Um único compositor, o mesmo visual, sem surpresas.

## Download

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=pt" alt="Disponível no Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Baixe na Microsoft Store" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="Baixe no Google Play (teste fechado)" height="80">
  </a>
</p>

<p align="center">O Google Play está em teste fechado — <a href="../docs/play-testing.md">veja como participar</a>.</p>

**Linux** — instale pelo Flathub:

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — instale via Homebrew:

```bash
brew install --cask cutwire-studios/tap/drift
```

> **Primeira inicialização no macOS:** O Drift possui assinatura ad-hoc (não é notarizado pela Apple). O atributo de quarentena é removido automaticamente ao instalar via Homebrew. Caso instale manualmente pelo arquivo `.dmg` ou o Gatekeeper bloqueie a execução, rode:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

Ou baixe um executável pré-compilado para o seu sistema diretamente na página da
[última versão](https://github.com/CutWire-Studios/Drift/releases/latest):

| Plataforma | Pacote |
|------------|--------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Instalador (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [Arquivo portátil (.zip)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Imagem de disco (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (teste fechado)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

Em smartphones, participe do [teste fechado no Google Play](../docs/play-testing.md), ou baixe `Drift-*-arm64-v8a.apk` da versão mais recente e instale (ou execute `adb install Drift-*-arm64-v8a.apk`). Utilize `x86_64` para emuladores.

Veja [todas as versões](https://github.com/CutWire-Studios/Drift/releases) para acompanhar versões anteriores e notas de atualização detalhadas.

## Captura de Tela

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Interface do Drift: bandeja de mídia à esquerda, câmera 3D na pré-visualização, inspetor de texto à direita, linha do tempo multipista abaixo" width="900">
</p>

## Recursos

**Agentes de IA podem editar o projeto em andamento.** Importe, corte, gradue cores e exporte dentro do projeto aberto.
O mesmo editor pode operar no modo headless (sem janela gráfica).

**3D na linha do tempo.** Incline clipes no espaço tridimensional, ajuste iluminação e posicione títulos atrás do assunto gravado. Arraste
um modelo 3D para a timeline e ele será reproduzido no mesmo corte.

**Animações Lottie.** Gráficos em movimento e vetores permanecem perfeitamente nítidos em qualquer escala. Altere cores ou
textos diretamente no local.

**Keyframing avançado.** Anime posição, escala, rotação, opacidade e propriedades de efeitos ao
longo do tempo. Desenhe as curvas de interpolação manualmente.

**Mais de 150 transições e 40 efeitos.** Visualize previamente cada predefinição nos seus próprios vídeos. Um clique aplica
combinações prontas completas — Beat Drop, Glitch Cut, Neon Cutout. Rastros de frames anteriores e correções de cor vinculadas
ao clipe escolhido.

**Processamento local no seu computador.** Clique em um sujeito para destacá-lo da imagem. Crie profundidade de campo e luzes
direcionadas ao elemento. Legendas automáticas, aumento de resolução (upscaling) e retoque facial em tempo real — tudo no seu
próprio hardware.

**Edição baseada em texto.** Falas são transformadas em texto sobre o clipe. Corte orações, remova hesitações e silêncios, selecione
os melhores takes, identifique locutores e crie legendas perfeitamente cronometradas.

**Títulos com visual marcante.** 33 pacotes temáticos: karaokê, estilo Hormozi, neon, cromado, holográfico
e manuscrito. Aplique um mesmo estilo a todas as legendas da faixa com um clique.

**Mova camadas inteiras de uma só vez.** Uma camada de transformação pode reposicionar, redimensionar, girar, inclinar e aplicar fade a todos os elementos
abaixo dela. Agrupe faixas ou crie composições quando o trecho demandar sua própria timeline.

**Corte no ritmo da música.** A linha do tempo se alinha à batida das faixas sonoras. Cortes sincronizam no compasso. A música
atenua suavemente durante as falas (audio ducking) e retorna ao volume original. Reenquadre vídeos horizontais para formatos verticais
rastreando o rosto automaticamente.

**Uma linha do tempo confiável.** Rolo de filme multipista, edição ondulada (ripple), ajuste magnético (snap), máscaras, congelamento de quadro, marcadores
e mixer de áudio. Divida um clipe sem perder a gradação de cor. Caso ocorra uma interrupção, o último salvamento é mantido intacto.

**Áudio com qualidade de estúdio.** Aprimore a clareza da voz, monitore níveis de volume (loudness), aplique equalização e compressão, preserve o tom
original ao mudar velocidades e grave narrações diretamente.

**Localize tomadas. Alterne ângulos de câmera.** Pesquise no catálogo o que surge na tela. Visualize todos os ângulos simultaneamente
e faça o corte no momento exato.

**Estabilize, exporte e leve o projeto com você.** Suavize vídeos tremidos, aplique upscaling e reproduza gravações pesadas
com alta fluidez. Exporte em MP4, GIF, áudio individual ou intervalos parciais. Empacote a mídia junto com o projeto para manter todos os vínculos intactos.

**Mesma experiência completa no Android.** Linha do tempo, efeitos e exportação no smartphone. Compartilhe diretamente pelo
aplicativo.

**Banco de recursos, narrações e download leve.** Pesquise fotos, vídeos e áudios de banco diretamente na bandeja. Crie narrações e efeitos
sonoros. Fontes, adesivos e modelos complementares são baixados sob demanda. Interface disponível em árabe, bengali, espanhol (Espanha
e Colômbia), francês (Canadá), italiano, japonês, português (Brasil e Portugal), russo,
cingalês, tagalo, vietnamita e chinês simplificado.

## Por que escolher o Drift?

Muitos editores “gratuitos” cobram cadastro, carimbam marcas d'água no arquivo final ou impõem assinaturas no instante em que a edição
começa a ficar de alto nível. O Drift faz o oposto: **é seu, roda no seu computador, sob licença GPLv3 e sem tela de login.**

Rápido o suficiente para cortes rápidos de 30 segundos nas redes e robusto para produções complexas — agente na
linha do tempo, 3D e Lottie, legendagem, efeitos visuais, áudio profissional, recorte de fundo e multicâmera.

## Ajude na tradução do Drift

[![Status da tradução](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## Para desenvolvedores

Guias de compilação, empacotamento, arquitetura e protocolo de agentes encontram-se em `docs/`:

- [Compilação, testes, empacotamento e arquitetura](../docs/BUILDING.md)
- [Efeitos em GPU](../docs/gpu-effects.md)
- [Transições em GPU](../docs/gpu-transitions.md)
- [Acesso a Agentes / MCP](../docs/MCP.md)

## Ajuda e Feedback

Encontrou uma inconsistência ou tem uma sugestão? Registre uma
[issue no GitHub](https://github.com/CutWire-Studios/Drift/issues).

## Contribuições

O guia [CONTRIBUTING.md](../CONTRIBUTING.md) contém as orientações de contribuição: abra um chamado antes de alterações expressivas, restrinja cada pull request a um único objetivo, execute os testes unitários e disponibilize seu código sob licença GPLv3.

Efeitos, transições, modelos e efeitos sonoros são tratados como complementos (addons). Envie-os para [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons). Modificações no motor principal continuam neste repositório.

Instalar uma [versão pública](https://github.com/CutWire-Studios/Drift/releases) e utilizá-la em edições reais já é um grande suporte: informe bugs ou melhorias necessárias. No canal do [Discord](https://discord.gg/J5ANFz6Z3y), colaboradores podem solicitar o cargo de `@Contributor`.

## Colaboradores

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="Colaboradores" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## Licença

GPLv3 — consulte [LICENSE](../LICENSE).

## Histórico de Estrelas

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Gráfico de Estrelas no Star History" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
