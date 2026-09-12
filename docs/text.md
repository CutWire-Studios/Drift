# Text: looks, animators and presets

How a text or subtitle clip is modelled, drawn and animated since project format 7. The
user-facing surface is the Text tab (Type · Look · Animate) and the `text` MCP toolbox
(`docs/MCP.md`); this page is the map underneath.

## Model (`src/core`)

- **`TextStyle`** (`TextStyle.h`): font, size, weight, italic, layout (align, wrap, line height,
  letter spacing, bend), decorations (box, word pill, underline, word accent), the shading stack
  `layers`, the `animation` set, `lookId` / `lookParams`, and `keyframes`.
- **`TextShadingLayer`** (`TextShading.h`): `kind` fill | stroke | shadow | glow | extrude;
  `paint` solid | gradient | texture | effect; `opacity`, `blend`, `offsetX/Y`, `blur`, `width`
  (stroke width / extrude depth), `spread`, `strokeAlign` center | outside | inside (the legacy
  `strokeOutside` bool reads as outside / center), `dash` solid | dash | dot | dashdot with
  `dashOffset` (in stroke widths), `knockout`, `trimStart/End`, `sketchLength/Deviation/Seed`
  (a hand-drawn wobble on fills and strokes), `extrude*`, `scope` all | base | accent.
  `layers[0]` is drawn first. Ids are lowercase and stable; the four migrated ones are `shadow`,
  `glow`, `stroke`, `fill`. The `Text` prefix is historical: `ShapeStyle` carries the same stack
  (project format 8), painted by `SkiaShapePainter` through the shared `SkiaShading` helpers, with
  a fresh shape's layers named `fill` and `stroke`.
- **`TextGradient`**: multi-stop, linear | radial | sweep, `angle`, `offset` (keyframable, in
  box widths), `offsetSpeed` (box widths per second: a moving gradient), `scale`, `center`,
  `repeat`, `oklab`, `space` block | line | word | glyph | accentRun (the box it maps onto).
- **`TextAnimationSet`** (`TextAnimator.h`): slots `in`, `out`, `loop` (`TextAnimationSlot`:
  preset id + typed params, or inline `animators`), `caret`, `anchorGrouping`,
  `anchorAlignment`.
- **`TextAnimator`**: `selectors` × `props`. A selector yields a coverage weight per fragment
  (character / character-with-spaces / word / line / block); the props are the deltas applied at
  coverage 1 (position, scale, rotation, skew, opacity, fill/stroke colour and opacity, blur,
  tracking, line spacing, stroke width, wipe). Drivers: `curves` (After Effects range selector:
  start/end/offset/amount/ease/smoothness, shapes square … smooth, modes add … difference),
  `stagger` (per-unit ramps with an ease, the classic reveal), `wiggle` (sine / triangle /
  noise over absolute time), `karaoke` (the spoken word). Composition and coverage maths are a
  port of Skottie's `modules/skottie/src/text`.
- Time: props are curves over the slot's **progress** 0..1 (`TextAnimParam`, keys at
  `progress × 1e6`). In runs from the window start, Out ends at the window end (stagger ranks
  mirrored), Loop is periodic (`periodUs`) or one pass over the whole window ("hold" motion). A
  subtitle cue is its own window.
- **Presets** (`TextAnimationPreset.h`, `presets/text-animations/*.json`, user files in
  `AppData/text-animations/`): a recipe with `{param}` references and `{"$": id, "mul", "add"}`
  scaled numbers, expanded by `resolvePresetSlot`. `"selectors": ["stagger"]` is the standard
  reveal selector; `"commonParams": true` (default for In/Out presets) adds duration, stagger,
  unit, order and ease. `resolveTextAnimation` caches the expanded set per style.
- **Looks** (`TextLook.h`): recipes that rewrite the stack from a few params; the style keeps
  `lookId` + `lookParams` so sliders regenerate it. A hand edit of a layer clears the id.
- **Keyframe keys**: `pixelSize`, `letterSpacing`, `lineHeight`, `boxPadding`, `pathBend`, and
  `layer.<id>.<opacity|offsetX|offsetY|blur|width|spread|trimStart|trimEnd|dashOffset|
  sketchLength|sketchDeviation|color.r|g|b|a|gradient.angle|gradient.offset|gradient.scale|
  gradient.center.x|y|gradient.stop.<n>.pos|effect.<param>>` (the layer registry is shared
  with shapes: `shadingLayerKeyframeFields` / `shadingLayerScalar` in `TextShading.h`).
  `textKeyframeCanonicalKey` maps the v6 names (`outlineWidth`, `shadowBlur`, `glowRadius`,
  `gradientAngle`, `color.r`…) onto the migrated layers.
- **Migration**: `textStyleFromJson` reads both forms forever (the user preset library shares
  it). v6 flat fields become the four layers (disabled ones kept so toggling restores their
  values); `animIn/animOut` kinds become preset slots (`wave` moves to `loop`).

## Rendering (`src/engine`)

- `TextLayout` (QTextLayout) lays the text out once per text/size change into a cached
  `FragmentSet`: per-word or per-grapheme pieces plus the metadata the engine needs
  (`textanim::FragmentInfo`). `splitFor` picks characters when any selector works per character
  or the line is bent.
- `FrameCompositor::fillTextLayer` resolves the animation, evaluates the frame
  (`textanim::evaluateTextAnimation`) and hands both to `SkiaTextPainter::makeTextPainter`.
  Whole-block motion (animators whose selectors are all domain block) rides on the GPU layer;
  everything else is drawn per fragment.
- `AnimatedTextPainter` draws layer by layer: box, pills, then for every enabled layer the
  fragments (bucketed by their animator blur), so every stroke sits under every fill. Fragments
  mid-fade get their own translucent layer so a stroke never shows through the glyph. Strokes are
  Skia strokes (width × 2 clipped to one side for outside / inside, plain width centred; trim →
  dash → sketch path effects), shadows/glows are blurred silhouettes on a saveLayer, extrude
  is N offset copies, wipes are `kDstIn` gradient draws (block) or shader mask filters
  (fragment). The image is sized for the whole animation (`animationBounds` envelope) so its rect
  never moves; the held pose is cached, moving frames and time-driven paints are not.
- `SkiaTextEffects`: gradients built in unit space and mapped with a local matrix; textures
  (`SkImage::makeShader`, LRU by path); the SkSL registry. Every effect shares the uniforms
  `uOrigin, uSize, uTime, uProgress, uSeed, uSpeed, uIntensity, uScale, uWidth, uAngle,
  uAmount, uBlock, uBands, uFlicker, uEdge, uColorA, uColorB` and the child shader `base`; the
  effect's declared params (`textShaderEffectSpecs()`) fill them.
- Thumbnails: `image://textanim/<slot>/<id>?w=&h=&frames=` (a 6-column sprite grid of the preset
  on canned text), `image://textlook/<id>?text=&font=&weight=&italic=&rev=`, and
  `image://textstyle/<packId>` all render through `renderTextCard` on the CPU.

## Adding things

- **A preset**: drop a JSON file in `presets/text-animations/` (see `type-on-blur.json`); it is
  validated by `tst_core::textAnimationPresetsAreWellFormed`.
- **A look**: add a row to `textLooks()` and a branch to `applyTextLook` (`TextLook.cpp`).
- **A shader effect**: add its SkSL to `kEffects` in `SkiaTextEffects.cpp` and its params to
  `textShaderEffectSpecs()` in `TextShading.cpp`; `tst_skia::skslEffectsCompileAndRender` compiles
  every id on the CPU path and on Ganesh.
- **An animator prop**: extend `TextAnimatorProps`, `applyFragmentProps`, the JSON in
  `TextAnimator.cpp`, and the painter's `drawPiece`.

## Not done yet

- Glyph substitution (decoder / scramble presets) needs per-frame re-layout of substituted
  characters.
- Text on a user-drawn path: `Bend` only builds the arc from `pathBend`; a `PathPlacement` over
  `SkContourMeasure` is the generalisation.
- Per-character 3D, wiggly selectors with correlation, and expression selectors from After
  Effects are reported as unsupported by the Lottie importer.
