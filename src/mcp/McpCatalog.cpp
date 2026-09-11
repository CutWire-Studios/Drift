#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"

#include <QHash>

#include <algorithm>

namespace drift::mcp {
namespace {

const QStringList kTrackTypes = {QStringLiteral("video"), QStringLiteral("audio"),
                                 QStringLiteral("text"), QStringLiteral("subtitle"),
                                 QStringLiteral("shape")};

QJsonObject clipRefProps()
{
    return {
        {QStringLiteral("clip"),
         stringProp(QStringLiteral(
             "Clip UUID from inspect({clips:true}) — preferred, stable across edits. Required unless "
             "both track and index are given; with neither the op fails not_found (it does not fall "
             "back to the selection)."))},
        {QStringLiteral("track"),
         integerProp(QStringLiteral("Track index if clip is omitted (0 = top). Positional — shifts "
                                    "when tracks are added or reordered."))},
        {QStringLiteral("index"),
         integerProp(QStringLiteral("Clip index on that track if clip is omitted"))},
    };
}

QJsonObject assetRefProp()
{
    QJsonObject prop = stringProp(QStringLiteral(
        "Asset id, bin index (number or numeric string), or exact asset name (case-insensitive). A "
        "numeric value is tried as a bin index first, so prefer the id from list_assets."));
    prop.insert(QStringLiteral("type"), QJsonArray{QStringLiteral("string"), QStringLiteral("integer")});
    return prop;
}

// Effect/audio-effect stack position. Repeated across ~10 ops, so the discovery path lives here.
QJsonObject effectIndexProp()
{
    return integerProp(QStringLiteral(
        "0-based position in this clip's effect stack — NOT a catalog id. Read the stack from "
        "inspect({clips:true,detail:true}), or use the index returned by add_effect/add_audio_effect."));
}

QJsonObject bookmarkIndexProp()
{
    return integerProp(QStringLiteral(
        "0-based position in inspect({detail:true}).bookmarks — bookmarks have no stable id, so "
        "re-read that list before using this and after any bookmark is removed."));
}

QJsonObject transitionIdProp()
{
    return stringProp(QStringLiteral(
        "Transition id returned by add_transition, or from inspect({clips:true,detail:true}) under "
        "tracks[].transitions. Unique within a track only."));
}

QJsonObject animPropProp()
{
    return stringProp(QStringLiteral(
        "Animated property: x, y, width, height, rotation, opacity, volume, fx.<effectIndex>.<paramKey> "
        "(e.g. fx.0.amount), mask.<x|y|w|h|rotation|feather>, or on a text/subtitle clip text.<key> with "
        "key one of pixelSize, letterSpacing, lineHeight, boxPadding, pathBend, or a shading layer field "
        "text.layer.<layerId>.<opacity|offsetX|offsetY|blur|width|spread|color.r|color.g|color.b|color.a|"
        "gradient.angle|gradient.offset|gradient.scale> (the legacy names outlineWidth, shadowBlur, "
        "glowRadius, gradientAngle, color.r… still map onto the stroke/shadow/glow/fill layers; colour "
        "channels 0..1). Note width/height here vs w/h in set_transform. Spellings live in this "
"schema — list_animated_properties returns only properties that already have keys (empty on a "
        "fresh clip), so do not use it to learn names."));
}

// Which grid detect_beats' result is read through. Repeated across four ops.
QJsonObject beatUnitProp()
{
    return propWithDefault(
        enumProp(QStringLiteral(
                     "Grid to use from the last detect_beats. beat = every beat; bar = every "
                     "beatsPerBar-th beat counted from firstDownbeat; onset = detected transients, "
                     "which need no tempo and work when bpm is 0."),
                 {QStringLiteral("beat"), QStringLiteral("bar"), QStringLiteral("onset")}),
        QStringLiteral("beat"));
}

QJsonObject maskSchema()
{
    QJsonObject s = objectSchema({
        {QStringLiteral("shape"),
         enumProp(QStringLiteral("Mask shape. Omitted or unrecognised = none, i.e. mask off."),
                  {QStringLiteral("none"), QStringLiteral("rectangle"), QStringLiteral("ellipse"),
                   QStringLiteral("star"), QStringLiteral("heart"), QStringLiteral("bars"),
                   QStringLiteral("freeform"), QStringLiteral("matte")})},
        {QStringLiteral("x"), numberProp(QStringLiteral("Center x, fraction of canvas (default 0.5)"), 0, 1)},
        {QStringLiteral("y"), numberProp(QStringLiteral("Center y, fraction of canvas (default 0.5)"), 0, 1)},
        {QStringLiteral("w"), numberProp(QStringLiteral("Width, fraction of canvas (default 0.6)"), 0, 1)},
        {QStringLiteral("h"), numberProp(QStringLiteral("Height, fraction of canvas (default 0.6)"), 0, 1)},
        {QStringLiteral("rotation"), numberProp(QStringLiteral("Degrees clockwise (default 0)"))},
        {QStringLiteral("feather"), numberProp(QStringLiteral("Alpha edge blur in pixels (default 0)"))},
        {QStringLiteral("invert"), boolProp(QStringLiteral("Invert coverage (default false)"))},
        {QStringLiteral("points"),
         arrayProp({{QStringLiteral("type"), QStringLiteral("array")}},
                   QStringLiteral("freeform only: [[x,y], …] normalised 0..1"))},
    });
    s.insert(QStringLiteral("description"),
             QStringLiteral("The whole mask; every omitted key reverts to its default and omitting shape turns the mask off"));
    return s;
}

QJsonObject shapeStyleSchema()
{
    QJsonObject s = objectSchema({
        {QStringLiteral("kind"), stringProp(QStringLiteral("Shape catalog id from list_shapes"))},
        {QStringLiteral("fillKind"),
         enumProp(QStringLiteral("Fill type"),
                  {QStringLiteral("none"), QStringLiteral("solid"), QStringLiteral("linearGradient"),
                   QStringLiteral("radialGradient")})},
        {QStringLiteral("fill"), stringProp(QStringLiteral("Fill color #RRGGBB or #AARRGGBB"))},
        {QStringLiteral("fillSecondary"), stringProp(QStringLiteral("Gradient end stop color"))},
        {QStringLiteral("gradientAngle"), numberProp(QStringLiteral("Degrees; 0 = left to right"))},
        {QStringLiteral("stroke"), stringProp(QStringLiteral("Stroke color"))},
        {QStringLiteral("strokeWidth"), numberProp(QStringLiteral("Stroke width px"), 0, 200)},
        {QStringLiteral("strokeStyle"),
         enumProp(QStringLiteral("Stroke dash pattern"),
                  {QStringLiteral("none"), QStringLiteral("solid"), QStringLiteral("dash"),
                   QStringLiteral("dot"), QStringLiteral("dashDot")})},
        {QStringLiteral("cornerRadius"), numberProp(QStringLiteral("Rect family: corner px"), 0, 2000)},
        {QStringLiteral("points"), integerProp(QStringLiteral("Star/polygon point count"), 3, 60)},
        {QStringLiteral("innerRatio"), numberProp(QStringLiteral("Star inner radius"), 0.05, 0.95)},
        {QStringLiteral("headSize"), numberProp(QStringLiteral("Arrow head size"), 0.05, 0.9)},
        {QStringLiteral("thickness"), numberProp(QStringLiteral("Arrow/banner thickness"), 0.05, 1)},
        {QStringLiteral("tailX"), numberProp(QStringLiteral("Bubble tail x"), 0.08, 0.92)},
        {QStringLiteral("tailSize"), numberProp(QStringLiteral("Bubble tail size"), 0.05, 0.5)},
    });
    s.insert(QStringLiteral("description"),
             QStringLiteral("Partial style patch; only supplied keys change. Each geometry knob is read by a subset of shape kinds"));
    return s;
}

QJsonObject mergeProps(QJsonObject a, const QJsonObject &b)
{
    for (auto it = b.begin(); it != b.end(); ++it)
        a.insert(it.key(), it.value());
    return a;
}

QJsonObject textHighlightSchema(const QString &description)
{
    QJsonObject s = objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("On/off"))},
                                  {QStringLiteral("color"), stringProp(QStringLiteral("Pill colour"))},
                                  {QStringLiteral("padding"), numberProp(QStringLiteral("Padding in px at pixelSize"))},
                                  {QStringLiteral("radius"), numberProp(QStringLiteral("Corner radius in px"))}});
    s.insert(QStringLiteral("description"), description);
    return s;
}

// Enum spellings are TextStyle.cpp's; a misspelling silently falls back to the default.
// The v6 {kind, duration, …} form, still accepted for animIn/animOut and mapped onto presets.
QJsonObject legacyTextAnimationSchema(const QString &description)
{
    QJsonObject s = objectSchema({
        {QStringLiteral("kind"), enumProp(QStringLiteral("Motion (legacy; prefer animation.in/out presets)"),
                                          {QStringLiteral("none"), QStringLiteral("fade"), QStringLiteral("slideUp"), QStringLiteral("slideDown"),
                                           QStringLiteral("slideLeft"), QStringLiteral("slideRight"), QStringLiteral("pop"), QStringLiteral("blur"),
                                           QStringLiteral("typewriter"), QStringLiteral("rise"), QStringLiteral("bounce"), QStringLiteral("wave")})},
        {QStringLiteral("duration"), numberProp(QStringLiteral("Seconds"), 0.0, 10.0)},
        {QStringLiteral("ease"), enumProp(QStringLiteral("Easing"),
                                          {QStringLiteral("linear"), QStringLiteral("easeIn"), QStringLiteral("easeOut"), QStringLiteral("easeInOut"),
                                           QStringLiteral("back"), QStringLiteral("bounce"), QStringLiteral("smooth")})},
        {QStringLiteral("unit"), enumProp(QStringLiteral("What animates separately"),
                                          {QStringLiteral("block"), QStringLiteral("word"), QStringLiteral("character"), QStringLiteral("line")})},
        {QStringLiteral("stagger"), numberProp(QStringLiteral("Seconds between units"), 0.0, 2.0)},
        {QStringLiteral("order"), enumProp(QStringLiteral("Unit order"),
                                           {QStringLiteral("forward"), QStringLiteral("backward"), QStringLiteral("centerOut"), QStringLiteral("random")})},
    });
    s.insert(QStringLiteral("description"), description);
    return s;
}

QJsonObject textAnimationSlotSchema(const QString &description)
{
    QJsonObject s = objectSchema({
        {QStringLiteral("preset"), stringProp(QStringLiteral("Preset id from list_text_animations; \"\" or \"none\" clears the slot. Picking a preset resets its params to the preset defaults."))},
        {QStringLiteral("params"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                               {QStringLiteral("description"), QStringLiteral("Preset param overrides {id: value}; ids and types come from the preset's params list")}}},
        {QStringLiteral("duration"), numberProp(QStringLiteral("Seconds per unit (In/Out)"), 0.0, 10.0)},
        {QStringLiteral("stagger"), numberProp(QStringLiteral("Seconds between units"), 0.0, 2.0)},
        {QStringLiteral("unit"), enumProp(QStringLiteral("What animates separately"),
                                          {QStringLiteral("block"), QStringLiteral("word"), QStringLiteral("character"), QStringLiteral("line")})},
        {QStringLiteral("order"), enumProp(QStringLiteral("Unit order"),
                                           {QStringLiteral("forward"), QStringLiteral("backward"), QStringLiteral("centerOut"), QStringLiteral("random")})},
        {QStringLiteral("ease"), enumProp(QStringLiteral("Easing"),
                                          {QStringLiteral("linear"), QStringLiteral("easeIn"), QStringLiteral("easeOut"), QStringLiteral("easeInOut"),
                                           QStringLiteral("back"), QStringLiteral("bounce"), QStringLiteral("smooth")})},
        {QStringLiteral("period"), numberProp(QStringLiteral("Loop period in seconds; 0 = once over the whole clip (hold motion)"), 0.0, 60.0)},
        {QStringLiteral("delay"), numberProp(QStringLiteral("Seconds after the start (In) / before the end (Out)"), 0.0, 10.0)},
        {QStringLiteral("enabled"), boolProp(QStringLiteral("On/off without losing the preset"))},
        {QStringLiteral("animators"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                  {QStringLiteral("description"), QStringLiteral("Inline AE-style animator tree (expert): [{selectors:[{driver, domain, …}], props:{position, scale, rotation, opacity, blur, tracking, fillColor, …}}]. Replaces the preset.")}}},
    });
    s.insert(QStringLiteral("description"), description);
    return s;
}

QJsonObject textLayerSchema(const QString &description)
{
    QJsonObject paint = objectSchema({
        {QStringLiteral("kind"), enumProp(QStringLiteral("Paint source"),
                                          {QStringLiteral("solid"), QStringLiteral("gradient"), QStringLiteral("texture"), QStringLiteral("effect")})},
        {QStringLiteral("color"), stringProp(QStringLiteral("Solid colour, or the tint for texture/effect"))},
        {QStringLiteral("gradient"), objectSchema({
             {QStringLiteral("kind"), enumProp(QStringLiteral("Gradient shape"), {QStringLiteral("linear"), QStringLiteral("radial"), QStringLiteral("sweep")})},
             {QStringLiteral("stops"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("description"), QStringLiteral("[{pos 0..1, color}]")}}},
             {QStringLiteral("angle"), numberProp(QStringLiteral("Degrees: 0 = left→right, 90 = top→bottom"))},
             {QStringLiteral("offset"), numberProp(QStringLiteral("Shift along the axis in box widths (keyframable)"))},
             {QStringLiteral("offsetSpeed"), numberProp(QStringLiteral("Box widths per second: a moving gradient"))},
             {QStringLiteral("scale"), numberProp(QStringLiteral("Axis length multiplier"))},
             {QStringLiteral("repeat"), boolProp(QStringLiteral("Tile instead of clamp"))},
             {QStringLiteral("oklab"), boolProp(QStringLiteral("Interpolate in OKLab"))},
             {QStringLiteral("space"), enumProp(QStringLiteral("Box the gradient maps onto"),
                                                {QStringLiteral("block"), QStringLiteral("line"), QStringLiteral("word"), QStringLiteral("glyph"), QStringLiteral("accentRun")})}})},
        {QStringLiteral("texture"), objectSchema({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute image path"))},
                                                  {QStringLiteral("scale"), numberProp(QStringLiteral("Scale"))},
                                                  {QStringLiteral("angle"), numberProp(QStringLiteral("Degrees"))},
                                                  {QStringLiteral("tile"), boolProp(QStringLiteral("Tile (else cover the block)"))}})},
        {QStringLiteral("effect"), objectSchema({{QStringLiteral("id"), enumProp(QStringLiteral("Shader effect"),
                                                                                  {QStringLiteral("shine"), QStringLiteral("shimmer"), QStringLiteral("neon-pulse"), QStringLiteral("glitch"), QStringLiteral("chrome"), QStringLiteral("dissolve")})},
                                                 {QStringLiteral("params"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("description"), QStringLiteral("{id: {type, value}} typed params")}}}})},
    });
    QJsonObject s = objectSchema({
        {QStringLiteral("id"), stringProp(QStringLiteral("Stable layer id (lowercase); minted when omitted"))},
        {QStringLiteral("kind"), enumProp(QStringLiteral("Layer kind"),
                                          {QStringLiteral("fill"), QStringLiteral("stroke"), QStringLiteral("shadow"), QStringLiteral("glow"), QStringLiteral("extrude")})},
        {QStringLiteral("enabled"), boolProp(QStringLiteral("On/off"))},
        {QStringLiteral("paint"), paint},
        {QStringLiteral("opacity"), numberProp(QStringLiteral("0..1"), 0.0, 1.0)},
        {QStringLiteral("blend"), enumProp(QStringLiteral("Blend mode"),
                                           {QStringLiteral("normal"), QStringLiteral("multiply"), QStringLiteral("screen"), QStringLiteral("overlay"), QStringLiteral("add"), QStringLiteral("darken"), QStringLiteral("lighten")})},
        {QStringLiteral("offsetX"), numberProp(QStringLiteral("px at pixelSize"))},
        {QStringLiteral("offsetY"), numberProp(QStringLiteral("px at pixelSize"))},
        {QStringLiteral("blur"), numberProp(QStringLiteral("Shadow blur / glow radius in px"))},
        {QStringLiteral("width"), numberProp(QStringLiteral("Stroke width or extrude depth in px"))},
        {QStringLiteral("spread"), numberProp(QStringLiteral("Shadow/glow dilation in px"))},
        {QStringLiteral("strokeOutside"), boolProp(QStringLiteral("Stroke grows outward only (default true)"))},
        {QStringLiteral("knockout"), boolProp(QStringLiteral("A fill that punches through the layers beneath (hollow)"))},
        {QStringLiteral("trimStart"), numberProp(QStringLiteral("Stroke write-on start 0..1"), 0.0, 1.0)},
        {QStringLiteral("trimEnd"), numberProp(QStringLiteral("Stroke write-on end 0..1"), 0.0, 1.0)},
        {QStringLiteral("extrudeSteps"), integerProp(QStringLiteral("Extrude copies"))},
        {QStringLiteral("extrudeAngle"), numberProp(QStringLiteral("Extrude direction in degrees"))},
        {QStringLiteral("extrudeDarken"), numberProp(QStringLiteral("How much the far end darkens 0..1"), 0.0, 1.0)},
        {QStringLiteral("scope"), enumProp(QStringLiteral("Which words the layer paints"),
                                           {QStringLiteral("all"), QStringLiteral("base"), QStringLiteral("accent")})},
    });
    s.insert(QStringLiteral("description"), description);
    return s;
}

QJsonObject textStyleSchema()
{
    QJsonObject layerPatch = textLayerSchema(QStringLiteral("Partial patch of one existing layer, addressed by id or index"));
    layerPatch.insert(QStringLiteral("properties"),
                      mergeProps(layerPatch.value(QStringLiteral("properties")).toObject(),
                                 {{QStringLiteral("index"), integerProp(QStringLiteral("Layer index, when no id"))}}));
    return objectSchema({
        {QStringLiteral("fontFamily"), stringProp(QStringLiteral("Font family name"))},
        {QStringLiteral("fontWeight"), integerProp(QStringLiteral("Font weight (e.g. 400, 700)"))},
        {QStringLiteral("pixelSize"), numberProp(QStringLiteral("Font size in pixels"))},
        {QStringLiteral("layers"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                               {QStringLiteral("items"), textLayerSchema(QStringLiteral("A shading layer"))},
                                               {QStringLiteral("description"), QStringLiteral("The whole shading stack, replaced as given; layers[0] is drawn first (back-most). Use `layer` to patch one.")}}},
        {QStringLiteral("layer"), layerPatch},
        {QStringLiteral("lookId"), stringProp(QStringLiteral("\"\" clears the look bookkeeping; use apply_text_look to apply one"))},
        {QStringLiteral("color"), stringProp(QStringLiteral("Primary text colour (#RRGGBB or #AARRGGBB): edits the front-most fill layer"))},
        {QStringLiteral("fillKind"), enumProp(QStringLiteral("Legacy: solid, or a two-stop gradient on the fill layer"),
                                              {QStringLiteral("solid"), QStringLiteral("linearGradient"), QStringLiteral("radialGradient")})},
        {QStringLiteral("colorSecondary"), stringProp(QStringLiteral("Legacy: gradient end colour"))},
        {QStringLiteral("gradientAngle"), numberProp(QStringLiteral("Legacy: gradient angle in degrees"))},
        {QStringLiteral("pathBend"), numberProp(QStringLiteral("Arch a single-line block along an arc: -100..100, positive bends upward; ignored on multi-line blocks"), -100.0, 100.0)},
        {QStringLiteral("italic"), boolProp(QStringLiteral("Italic"))},
        {QStringLiteral("align"), enumProp(QStringLiteral("Horizontal alignment"),
                                          {QStringLiteral("left"), QStringLiteral("center"),
                                           QStringLiteral("right")})},
        {QStringLiteral("valign"), enumProp(QStringLiteral("Vertical alignment"),
                                            {QStringLiteral("top"), QStringLiteral("center"),
                                             QStringLiteral("bottom")})},
        {QStringLiteral("lineHeight"), numberProp(QStringLiteral("Line height multiplier"))},
        {QStringLiteral("letterSpacing"), numberProp(QStringLiteral("Letter spacing"))},
        {QStringLiteral("wordWrap"), boolProp(QStringLiteral("Wrap long lines"))},
        {QStringLiteral("outlineEnabled"), boolProp(QStringLiteral("Legacy: the stroke layer on/off"))},
        {QStringLiteral("outlineWidth"), numberProp(QStringLiteral("Legacy: stroke width in px at pixelSize"))},
        {QStringLiteral("outlineColor"), stringProp(QStringLiteral("Legacy: stroke colour"))},
        {QStringLiteral("shadowEnabled"), boolProp(QStringLiteral("Legacy: the shadow layer on/off"))},
        {QStringLiteral("shadowOffsetX"), numberProp(QStringLiteral("Legacy: shadow x offset in px"))},
        {QStringLiteral("shadowOffsetY"), numberProp(QStringLiteral("Legacy: shadow y offset in px"))},
        {QStringLiteral("shadowBlur"), numberProp(QStringLiteral("Legacy: shadow blur radius in px"))},
        {QStringLiteral("shadowOpacity"), numberProp(QStringLiteral("Legacy: shadow opacity 0..1"))},
        {QStringLiteral("shadowColor"), stringProp(QStringLiteral("Legacy: shadow colour"))},
        {QStringLiteral("glowEnabled"), boolProp(QStringLiteral("Legacy: the glow layer on/off"))},
        {QStringLiteral("glowColor"), stringProp(QStringLiteral("Legacy: glow colour"))},
        {QStringLiteral("glowRadius"), numberProp(QStringLiteral("Legacy: glow radius in px"))},
        {QStringLiteral("glowOpacity"), numberProp(QStringLiteral("Legacy: glow opacity 0..1"))},
        {QStringLiteral("boxEnabled"), boolProp(QStringLiteral("Filled box behind the block"))},
        {QStringLiteral("boxColor"), stringProp(QStringLiteral("Box colour"))},
        {QStringLiteral("boxPadding"), numberProp(QStringLiteral("Box padding in px"))},
        {QStringLiteral("boxRadius"), numberProp(QStringLiteral("Box corner radius in px"))},
        {QStringLiteral("underlineEnabled"), boolProp(QStringLiteral("Underline"))},
        {QStringLiteral("underlineColor"), stringProp(QStringLiteral("Underline colour"))},
        {QStringLiteral("underlineWidth"), numberProp(QStringLiteral("Underline thickness in px"))},
        {QStringLiteral("underlineOffset"), numberProp(QStringLiteral("Underline distance below the baseline in px"))},
        {QStringLiteral("wordHighlight"), textHighlightSchema(QStringLiteral("Filled pill behind every word"))},
        {QStringLiteral("accent"),
         objectSchema({{QStringLiteral("rule"), enumProp(QStringLiteral("Which words get the accent"),
                                                         {QStringLiteral("none"), QStringLiteral("firstWord"), QStringLiteral("lastWord"),
                                                          QStringLiteral("everyOther"), QStringLiteral("everyNth"), QStringLiteral("longestWord"),
                                                          QStringLiteral("randomStable"), QStringLiteral("karaoke")})},
                       {QStringLiteral("n"), integerProp(QStringLiteral("Stride for everyNth"))},
                       {QStringLiteral("phase"), integerProp(QStringLiteral("Index of the first accented word"))},
                       {QStringLiteral("colorEnabled"), boolProp(QStringLiteral("Recolour accented words"))},
                       {QStringLiteral("color"), stringProp(QStringLiteral("Accent colour"))},
                       {QStringLiteral("sizeScale"), numberProp(QStringLiteral("Accented word size relative to pixelSize"))},
                       {QStringLiteral("outlineEnabled"), boolProp(QStringLiteral("Outline accented words"))},
                       {QStringLiteral("outlineWidth"), numberProp(QStringLiteral("Accent outline width in px"))},
                       {QStringLiteral("outlineColor"), stringProp(QStringLiteral("Accent outline colour"))},
                       {QStringLiteral("highlight"), textHighlightSchema(QStringLiteral("Pill behind accented words"))}})},
        {QStringLiteral("animation"),
         objectSchema({{QStringLiteral("in"), textAnimationSlotSchema(QStringLiteral("Entrance"))},
                       {QStringLiteral("out"), textAnimationSlotSchema(QStringLiteral("Exit"))},
                       {QStringLiteral("loop"), textAnimationSlotSchema(QStringLiteral("While on screen (loops and hold motion)"))},
                       {QStringLiteral("caret"), objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("Typewriter caret"))},
                                                               {QStringLiteral("lead"), numberProp(QStringLiteral("Seconds the caret blinks alone first"))},
                                                               {QStringLiteral("blinkOn"), numberProp(QStringLiteral("Seconds on"))},
                                                               {QStringLiteral("blinkOff"), numberProp(QStringLiteral("Seconds off"))},
                                                               {QStringLiteral("holdAfter"), numberProp(QStringLiteral("Seconds to keep blinking after the reveal; -1 = whole clip"))},
                                                               {QStringLiteral("shape"), enumProp(QStringLiteral("Caret shape"), {QStringLiteral("bar"), QStringLiteral("underscore"), QStringLiteral("block")})},
                                                               {QStringLiteral("color"), stringProp(QStringLiteral("Caret colour; \"\" = the text colour"))}})},
                       {QStringLiteral("anchorGrouping"), enumProp(QStringLiteral("What per-fragment scale/rotation pivots on"),
                                                                    {QStringLiteral("character"), QStringLiteral("word"), QStringLiteral("line"), QStringLiteral("all")})}})},
        {QStringLiteral("animIn"), legacyTextAnimationSchema(QStringLiteral("Legacy entrance animation (maps onto animation.in)"))},
        {QStringLiteral("animOut"), legacyTextAnimationSchema(QStringLiteral("Legacy exit animation (maps onto animation.out)"))},
    });
}

QJsonObject speedPointSchema()
{
    return objectSchema({
        {QStringLiteral("pos"), numberProp(QStringLiteral("Normalised position 0..1 over trimmed source"))},
        {QStringLiteral("speed"), numberProp(QStringLiteral("Playback rate at this point"))},
        {QStringLiteral("inDx"), numberProp(QStringLiteral("Incoming tangent dx (curve space)"))},
        {QStringLiteral("inDy"), numberProp(QStringLiteral("Incoming tangent dy"))},
        {QStringLiteral("outDx"), numberProp(QStringLiteral("Outgoing tangent dx"))},
        {QStringLiteral("outDy"), numberProp(QStringLiteral("Outgoing tangent dy"))},
        {QStringLiteral("corner"), boolProp(QStringLiteral("Break tangent collinearity"))},
    });
}

QJsonObject exportSettingsProps()
{
    return {
        {QStringLiteral("scale"), stringProp(QStringLiteral("Scale id from list_export_options"))},
        {QStringLiteral("height"), integerProp(QStringLiteral("Target height in pixels; 0 = project height"))},
        {QStringLiteral("fps"), QJsonObject{{QStringLiteral("type"), QJsonArray{QStringLiteral("number"), QStringLiteral("string")}},
                                            {QStringLiteral("description"), QStringLiteral("Output fps; 0 = project rate. An fps id from list_export_options is also accepted as a string.")}}},
        {QStringLiteral("video"), stringProp(QStringLiteral("Video codec id from list_export_options.video (h264, hevc, …)"))},
        {QStringLiteral("audio"), stringProp(QStringLiteral("Audio codec id from list_export_options.audio (aac, opus, …)"))},
        {QStringLiteral("rate"), enumProp(QStringLiteral("Rate control mode — decides whether crf or bitrate is honoured. Defaults to crf. Not listed by list_export_options; crf is only applied by codecs that support it, otherwise the encoder falls back to bitrate."),
                                          {QStringLiteral("crf"), QStringLiteral("bitrate")})},
        {QStringLiteral("crf"), integerProp(QStringLiteral("Quality when rate is crf (lower is better)"), 0, 51)},
        {QStringLiteral("bitrate"), integerProp(QStringLiteral("Video kbps when rate is bitrate"))},
        {QStringLiteral("preset"), stringProp(QStringLiteral("Encoder speed/quality preset. Defaults to the codec's own default (medium for x264, p4 for NVENC)."))},
        {QStringLiteral("audio_bitrate"), integerProp(QStringLiteral("Audio kbps"))},
        {QStringLiteral("audio_only"), boolProp(QStringLiteral("Encode audio only. Forced off when gif is true."))},
        {QStringLiteral("gif"), boolProp(QStringLiteral("Encode animated GIF. Forces audio_only off and, when no fps is given, 15fps."))},
        {QStringLiteral("work_area"), boolProp(QStringLiteral("Limit to the In/Out work area. Fails bad_args when no work area is set. Ignored when in/out are given."))},
        {QStringLiteral("in"), numberProp(QStringLiteral("Range start seconds. Overrides work_area."))},
        {QStringLiteral("out"), numberProp(QStringLiteral("Range end seconds; must be greater than in."))},
    };
}

struct Op {
    const char *name;
    const char *toolbox;
    const char *when;
    const char *description;
    QJsonObject schema;
    bool readOnly = false;
    bool destructive = false;
    bool idempotent = false;
};

const QList<Op> &ops()
{
    static const QList<Op> k = {
        { "import_media", "media", "Bring files into the bin",
          "Import local media files and block until each probe finishes (15s cap). Paths must be "
          "absolute (or file://). There is no directory-listing op: if the user named a file loosely "
          "(\"GX010023.mp4 in Downloads\", \"the wedding clip\"), glob or search with YOUR own "
          "filesystem tools, then pass the hits here. Returns {assets:[{id, index, name, kind, dur, "
          "pending}]}, plus missing:[paths that do not exist] and refreshed:[paths already in the bin] "
          "— treat a non-empty missing as \"search again\", not as a bin problem. On timeout returns "
          "error import_timeout with the same assets array and pending:true on the stragglers. Not "
          "undoable.",
          objectSchema({{QStringLiteral("paths"),
                         arrayProp({{QStringLiteral("type"), QStringLiteral("string")}},
                                   QStringLiteral("Absolute file paths from your own filesystem search"))}},
                       {QStringLiteral("paths")}) },
        { "list_assets", "media", "See what is in the bin",
          "List imported assets. Returns {assets:[{index, id, name, kind, dur, w, h}]}. There is no "
          "recording timestamp — \"in the order I shot them\" cannot be recovered from this list; "
          "sort by name or by the order they were imported.",
          objectSchema({}), true, false, true },
        { "rename_asset", "media", "Rename a bin row",
          "Rename an asset in the bin. Does not rename the file on disk.",
          objectSchema({{QStringLiteral("asset"), assetRefProp()},
                        {QStringLiteral("name"), stringProp(QStringLiteral("New display name"))}},
                       {QStringLiteral("asset"), QStringLiteral("name")}) },

        { "add_track", "timeline", "Need a new lane",
          "Prepend a track. New track becomes index 0, so every existing track index shifts down by "
          "one — re-read inspect before reusing track/index clip references.",
          objectSchema({{QStringLiteral("type"), enumProp(QStringLiteral("Track type"), kTrackTypes)}},
                       {QStringLiteral("type")}) },
        { "remove_track", "timeline", "Delete a lane and its clips",
          "Delete a track and everything on it.",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))}},
                       {QStringLiteral("track")}),
          false, true },
        { "set_track", "timeline", "Mute or hide a lane",
          "Set track muted/hidden.",
          objectSchema(mergeProps({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                                   {QStringLiteral("muted"), boolProp(QStringLiteral("Mute"))},
                                   {QStringLiteral("hidden"), boolProp(QStringLiteral("Hide from composite"))}},
                                  {}),
                       {QStringLiteral("track")}) },
        { "place_clip", "timeline", "Put media on the timeline",
          "Place an asset as a clip and return the new clip's id. When overlap is off (default) the "
          "start is pushed to the next free gap; the reply carries requested, placed, and "
          "reason:\"gap\" when they differ. Note `track` means two different things: with "
          "new_track:true it is the insert position of the new track, otherwise it is the destination "
          "track and a type mismatch fails type_mismatch. With `track` omitted, the first track "
          "accepting this asset is used, and a new track is created if none does.",
          objectSchema(mergeProps({{QStringLiteral("asset"), assetRefProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline start seconds (default: playhead)"))},
                                   {QStringLiteral("track"), integerProp(QStringLiteral("Destination track, or insert position when new_track is true"))},
                                   {QStringLiteral("new_track"), boolProp(QStringLiteral("Insert a new matching track above"))}},
                                  {})) },
        { "move_clip", "timeline", "Change a clip's start time",
          "Move a clip on its track. When overlap is off (default) the start may be pushed forward to "
          "the next gap; the reply carries requested, placed, and reason:\"gap\" when they differ.",
          objectSchema(mergeProps({{QStringLiteral("at"), numberProp(QStringLiteral("New start seconds"))}},
                                  clipRefProps()),
                       {QStringLiteral("at")}) },
        { "set_duration", "timeline", "Change timeline length",
          "Set the clip's timeline duration in seconds. Trims source out (or in if reversed). Clamped "
          "to the source material left after the current trim, so the applied dur in the reply may be "
          "shorter than requested; clips with no intrinsic length (text, shape, still image) accept "
          "any duration.",
          objectSchema(mergeProps({{QStringLiteral("duration"), numberProp(QStringLiteral("Seconds"))}},
                                  clipRefProps()),
                       {QStringLiteral("duration")}) },
        { "set_trim", "timeline", "Set source in/out",
          "Set source in/out points in seconds. Recomputes timeline duration from the span and speed. "
          "Values are not validated here — out <= in or a span past the end of the source is clamped "
          "downstream; read back in/out/dur from the reply.",
          objectSchema(mergeProps({{QStringLiteral("in"), numberProp(QStringLiteral("Source in seconds"))},
                                   {QStringLiteral("out"), numberProp(QStringLiteral("Source out seconds"))}},
                                  clipRefProps()),
                       {QStringLiteral("in"), QStringLiteral("out")}) },
        { "move_to_track", "timeline", "Move a clip to another lane",
          "Move a clip to another track. Type must match the destination, else type_mismatch.",
          objectSchema(mergeProps({{QStringLiteral("to_track"), integerProp(QStringLiteral("Destination track"))},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: current)"))}},
                                  clipRefProps()),
                       {QStringLiteral("to_track")}) },
        { "split_clip", "timeline", "Cut a clip in two",
          "Split a clip at `at` seconds (default: playhead). `at` must fall strictly inside the clip, "
          "else bad_args. Returns {clips:[original id, new id], at}; the original id keeps the left "
          "part.",
          objectSchema(mergeProps({{QStringLiteral("at"), numberProp(QStringLiteral("Timeline time seconds"))}},
                                  clipRefProps())) },
        { "delete_clip", "timeline", "Remove a clip",
          "Delete a clip and its linked A/V partner. Reversible with undo.",
          objectSchema(clipRefProps()), false, true },
        { "duplicate_clip", "timeline", "Copy a clip after itself",
          "Duplicate a clip immediately after it on the same track. Returns the new clip's id.",
          objectSchema(clipRefProps()) },
        { "undo", "timeline", "Revert the last edit",
          "Undo the last project edit; one apply batch is one step. Fails bad_args when the stack is "
          "empty. Does not cover the ops in catalog.limitations (import, save, playback, export, "
          "view-state toggles). set_beat_layers is the dangerous one: it changes the user's own "
          "snapping and cannot be undone. Use list_history and undo_to to jump further back.",
          objectSchema({}) },
        { "redo", "timeline", "Re-apply an undone edit",
          "Redo the last undone edit. Fails bad_args when there is nothing to redo.",
          objectSchema({}) },
        { "list_history", "timeline", "Read the undo stack",
          "Linear history (no branches). Returns {entries:[{index, label, short, snapshot?}] newest "
          "first, current, hash, short, total, linear:true}. index 0 is Origin (empty/loaded project); "
          "each later entry is one undo step. short is the first 12 chars of that version's SHA-256 "
          "(undo_to takes any ≥8-char prefix); hash is the full HEAD hash — the same bytes "
          "take_snapshot writes to <hash>.json. Read-only.",
          objectSchema({{QStringLiteral("limit"), propWithDefault(integerProp(QStringLiteral("Newest entries to return"), 1, 10000), 20)}}),
          true, false, true },
        { "undo_to", "timeline", "Jump to a history version",
          "Restore the project to a linear history version. Pass index (from list_history) or hash "
          "(full SHA-256 or unique 8+ char prefix). Redo of later steps remains until the next edit, "
          "which discards them — there is no branching. Not itself undoable.",
          objectSchema({{QStringLiteral("index"),
                         integerProp(QStringLiteral("Target index from list_history (0 = Origin)"))},
                        {QStringLiteral("hash"),
                         stringProp(QStringLiteral("Content hash from list_history, or a unique prefix"))}}) },
        { "take_snapshot", "timeline", "Keep a JSON copy of HEAD",
          "Write the current history version as compact project JSON named <hash>.json under the app "
          "history folder. The file's SHA-256 is the same hash list_history reports. Does not change "
          "the project. Caps at 32 files (oldest dropped). Not undoable.",
          objectSchema({{QStringLiteral("label"),
                         stringProp(QStringLiteral("Optional note stored only in the reply"))}}) },
        { "list_snapshots", "timeline", "On-disk history JSON files",
          "Snapshots written by take_snapshot. Returns {snapshots:[{hash, short, path, bytes, savedAt}]}.",
          objectSchema({}), true, false, true },
        { "restore_snapshot", "timeline", "Reset to a saved JSON",
          "If hash is still on the undo stack, same as undo_to. Otherwise load the on-disk JSON and "
          "clear the stack (new Origin). Linear — redo history is discarded. Not undoable.",
          objectSchema({{QStringLiteral("hash"),
                         stringProp(QStringLiteral("Snapshot hash or unique prefix"))}},
                       {QStringLiteral("hash")}) },
        { "set_overlap", "timeline", "Allow clips to overlap",
          "Project setting. Off (default): place/move push to the next gap. On: requested start is kept.",
          objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("Allow overlapping clips"))}},
                       {QStringLiteral("enabled")}) },

        { "set_transform", "canvas", "Position, size, rotate, fade a clip",
          "Set canvas transform in project-canvas pixels (0,0 = canvas top-left). Omitted fields are "
          "left unchanged. IMPORTANT: the write lands at the CURRENT PLAYHEAD — if the property is "
          "already keyframed, or autoKey is on, this creates/updates a keyframe there instead of "
          "setting a constant value, so seek first. Use set_property_keyframes_enabled(false) or "
          "remove the keys for an unconditional value. Fails bad_args on audio clips and when no "
          "transform field is supplied.",
          objectSchema(mergeProps({{QStringLiteral("x"), numberProp(QStringLiteral("Left edge, canvas pixels"))},
                                   {QStringLiteral("y"), numberProp(QStringLiteral("Top edge, canvas pixels"))},
                                   {QStringLiteral("w"), numberProp(QStringLiteral("On-canvas width in pixels (not source resolution)"))},
                                   {QStringLiteral("h"), numberProp(QStringLiteral("On-canvas height in pixels"))},
                                   {QStringLiteral("rotation"), numberProp(QStringLiteral("Degrees clockwise"))},
                                   {QStringLiteral("opacity"), numberProp(QStringLiteral("Opacity"), 0, 1)}},
                                  clipRefProps())) },
        { "reset_transform", "canvas", "Reset a clip to fill the canvas",
          "Reset position, size, rotation, opacity, and flips to defaults.",
          objectSchema(clipRefProps()) },

        { "seek", "playback", "Jump the playhead",
          "Move the playhead to `at` seconds and return the clamped position. Worth doing before ops "
          "that read the playhead: set_transform, capture, split_clip, freeze_frame, "
          "upsert_subtitle_cue, paste_at_playhead, and any add_* with `at` omitted.",
          objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Seconds; clamped to the project duration"))}},
                       {QStringLiteral("at")}) },
        { "play", "playback", "Let the timeline run (loops the work area if set)",
          "Start playback from the current playhead, looping over the work area when one is set. "
          "Playback moves the playhead, which changes where playhead-based ops land — pause before "
          "editing. Returns {playing}.",
          objectSchema({}) },
        { "pause", "playback", "Freeze the playhead before playhead-based edits",
          "Pause playback, leaving the playhead where it stopped. Returns {playing}. capture pauses "
          "on its own.",
          objectSchema({}) },
        { "set_work_area", "playback", "Set the In/Out range",
          "Set the In/Out work area, used for loop playback and for export_video's work_area option. "
          "out must be greater than in, else bad_args. Read it back from inspect as work_in/work_out, "
          "which are absent when no work area is set.",
          objectSchema({{QStringLiteral("in"), numberProp(QStringLiteral("In point seconds"))},
                        {QStringLiteral("out"), numberProp(QStringLiteral("Out point seconds; must be greater than in"))}},
                       {QStringLiteral("in"), QStringLiteral("out")}) },
        { "clear_work_area", "playback", "Clear the In/Out range",
          "Remove the In/Out work area. Afterwards export_video with work_area:true fails bad_args, "
          "and inspect stops reporting work_in/work_out.",
          objectSchema({}) },

        { "add_text", "text", "Put a title or caption on the timeline",
          "Add a text clip on a text track, creating one if needed, and return the new clip's id. "
          "Empty text becomes \"Text\".",
          objectSchema({{QStringLiteral("text"), stringProp(QStringLiteral("Caption"))},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: playhead)"))},
                        {QStringLiteral("preset"), stringProp(QStringLiteral("Optional text style pack id from list_text_presets"))}}) },
        { "set_text", "text", "Change caption copy or style",
          "Set text content and/or a partial style patch. Only supplied style keys change; omitted "
          "ones keep their current value.",
          objectSchema(mergeProps({{QStringLiteral("text"), stringProp(QStringLiteral("New content"))},
                                   {QStringLiteral("style"), textStyleSchema()}},
                                  clipRefProps())) },

        { "list_effects", "effects", "See available video effects",
          "The installable catalog, not what is on a clip. Default reply is compact: "
          "{cats:{<cat>:[{id, label}…]}, n}. Pass id for one effect with its params "
          "[{key, type, default, min, max, bool}], cat for a whole category with params, or q for a "
          "substring match (params included when ≤5 hits). Use id with add_effect and param keys with "
          "set_effect_param. To see a clip's current stack use inspect({clips:true,detail:true}).",
          objectSchema({{QStringLiteral("id"), stringProp(QStringLiteral("One effect id (aliases such as adjust_contrast resolve)"))},
                        {QStringLiteral("cat"), stringProp(QStringLiteral("Category name from the default reply"))},
                        {QStringLiteral("q"), stringProp(QStringLiteral("Case-insensitive substring over id/label/cat"))},
                        {QStringLiteral("limit"), propWithDefault(integerProp(QStringLiteral("Max rows for q"), 1, 500), 50)}}),
          true, false, true },
        { "list_audio_effects", "effects", "See available audio effects",
          "The installable catalog, not what is on a clip. Same shape and modes as list_effects: "
          "compact {cats, n} by default; id, cat or q for params. Use id with add_audio_effect.",
          objectSchema({{QStringLiteral("id"), stringProp(QStringLiteral("One audio effect id"))},
                        {QStringLiteral("cat"), stringProp(QStringLiteral("Category name from the default reply"))},
                        {QStringLiteral("q"), stringProp(QStringLiteral("Case-insensitive substring over id/label/cat"))},
                        {QStringLiteral("limit"), propWithDefault(integerProp(QStringLiteral("Max rows for q"), 1, 500), 50)}}),
          true, false, true },
        { "list_transitions", "effects", "See available transitions",
          "Same shape and modes as list_effects: compact {cats, n} by default; id, cat or q for "
          "params. Pass the id as `kind` to add_transition (default crossfade).",
          objectSchema({{QStringLiteral("id"), stringProp(QStringLiteral("One transition id"))},
                        {QStringLiteral("cat"), stringProp(QStringLiteral("Category name from the default reply"))},
                        {QStringLiteral("q"), stringProp(QStringLiteral("Case-insensitive substring over id/label/cat"))},
                        {QStringLiteral("limit"), propWithDefault(integerProp(QStringLiteral("Max rows for q"), 1, 500), 50)}}),
          true, false, true },
        { "add_effect", "effects", "Put a video effect on a clip",
          "Append a video effect to the end of the clip's stack. Returns index — its stack position, "
          "which is what every other effect op takes. Ids are matched case-insensitively with '.' and "
          "'_' interchangeable; an unknown id fails not_found with the nearest ids. The stack is "
          "stored on an adjustment clip linked to the target (created on its own lane the first "
          "time, and reported as host:{track,index,clip}); keep addressing the original clip in "
          "every effect op, and expect that extra lane in inspect.",
          objectSchema(mergeProps({{QStringLiteral("effect"), stringProp(QStringLiteral("Effect catalog id from list_effects"))}},
                                  clipRefProps()),
                       {QStringLiteral("effect")}) },
        { "remove_effect", "effects", "Remove a video effect",
          "Remove a video effect by stack index. Later effects shift down one position.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()}},
                                  clipRefProps()),
                       {QStringLiteral("index")}),
          false, true },
        { "set_effect_param", "effects", "Tweak a video effect",
          "Set one numeric/boolean video effect parameter. WARNING: the write is not validated — an "
          "unknown key or out-of-range index still returns ok. Take keys from list_effects and confirm "
          "the result with inspect({clips:true,detail:true}). Use set_effect_color_param for colors.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("key"), stringProp(QStringLiteral("Parameter key from the effect's params in list_effects"))},
                                   {QStringLiteral("value"), numberProp(QStringLiteral("Value (booleans as 0/1); range from list_effects({id})"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("key"), QStringLiteral("value")}) },
        { "add_audio_effect", "effects", "Put an audio effect on a clip",
          "Append an audio effect to the clip's audio stack. Returns index — its stack position. The "
          "audio stack is numbered separately from the video effect stack. Fails not_found on an "
          "unknown id. Like add_effect, the stack lives on a linked adjustment clip reported as "
          "host; keep addressing the original clip.",
          objectSchema(mergeProps({{QStringLiteral("effect"), stringProp(QStringLiteral("Audio effect catalog id from list_audio_effects"))}},
                                  clipRefProps()),
                       {QStringLiteral("effect")}) },
        { "remove_audio_effect", "effects", "Remove an audio effect",
          "Remove an audio effect by stack index. Later effects shift down one position.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()}},
                                  clipRefProps()),
                       {QStringLiteral("index")}),
          false, true },
        { "set_audio_effect_param", "effects", "Tweak an audio effect",
          "Set one audio effect parameter (booleans as 0/1). WARNING: not validated — an unknown key "
          "or bad index still returns ok. Take keys from list_audio_effects and confirm with "
          "inspect({clips:true,detail:true}).",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("key"), stringProp(QStringLiteral("Parameter key from list_audio_effects"))},
                                   {QStringLiteral("value"), numberProp(QStringLiteral("Value (booleans as 0/1); range from list_audio_effects({id})"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("key"), QStringLiteral("value")}) },
        { "add_transition", "effects", "Bridge two adjacent clips",
          "Add or replace a transition between this clip and the next eligible clip on the same track "
          "(the neighbour with the earliest start after it); fails bad_args when there is none. Only "
          "video, shape, and text tracks take transitions. Duration is forced to the physical overlap "
          "when the clips already overlap, and is floored at 0.1s otherwise. Replacing an existing "
          "transition clears its parameter overrides. Returns {id, kind, dur, track} — keep id for "
          "remove_transition and set_transition_*.",
          objectSchema(mergeProps(
              {{QStringLiteral("kind"),
                propWithDefault(stringProp(QStringLiteral("Transition id from list_transitions; an unknown id fails not_found with the nearest ids")),
                                QStringLiteral("crossfade"))},
               {QStringLiteral("duration"), numberProp(QStringLiteral("Seconds (ignored when clips already overlap)"))}},
              clipRefProps())) },
        { "remove_transition", "effects", "Remove a transition",
          "Remove a transition by id from a track. Transition ids are unique within a track only, so "
          "track is required. Ids come from add_transition or inspect({clips:true,detail:true}).",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                        {QStringLiteral("id"), transitionIdProp()}},
                       {QStringLiteral("track"), QStringLiteral("id")}),
          false, true },

        { "set_project_setup", "project", "Change canvas size or frame rate",
          "Set project width, height, and fps. Changing width/height rebases every clip's canvas "
          "transform onto the new size (same helper apply_canvas_crop uses) so existing clips stay "
          "visually in place rather than jumping; fps does not retime clips.",
          objectSchema({{QStringLiteral("width"), integerProp(QStringLiteral("Canvas width pixels"))},
                        {QStringLiteral("height"), integerProp(QStringLiteral("Canvas height pixels"))},
                        {QStringLiteral("fps"), integerProp(QStringLiteral("Frames per second"))}},
                       {QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("fps")}) },
        { "set_background", "project", "Change canvas background",
          "Set background kind, color, and/or blur strength. At least one field is required. "
          "blurStrength only has a visible effect when kind is blur.",
          objectSchema({{QStringLiteral("kind"), enumProp(QStringLiteral("Background kind"),
                                                          {QStringLiteral("color"), QStringLiteral("blur")})},
                        {QStringLiteral("color"), stringProp(QStringLiteral("Background color #AARRGGBB"))},
                        {QStringLiteral("blurStrength"), numberProp(QStringLiteral("Blur amount"), 0, 200)}}) },
        { "set_metadata", "project", "Set project title and author",
          "Set project metadata. Omitted fields are left unchanged.",
          objectSchema({{QStringLiteral("title"), stringProp(QStringLiteral("Project title"))},
                        {QStringLiteral("author"), stringProp(QStringLiteral("Author name"))},
                        {QStringLiteral("description"), stringProp(QStringLiteral("Project description"))}}) },
        { "save_project", "project", "Save the project file",
          "Save the open project. With path omitted, saves to the current project path (inspect.path); "
          "fails bad_args when the project has never been saved. With path given, writes a .drift "
          "bundle there, creating parent folders as needed. saveAs:true duplicates instead: the copy "
          "at path gets its own id and takes its title from the file name, the session continues in "
          "it, and the file it was opened from is left untouched — path is required and must differ "
          "from inspect.path. Returns ok once the save is dispatched — "
          "it does NOT report a failed write. Confirm with inspect: dirty should be false and path "
          "should match.",
          objectSchema({{QStringLiteral("path"),
                         stringProp(QStringLiteral("Absolute .drift path; omit to save to the current path"))},
                        {QStringLiteral("saveAs"),
                         boolProp(QStringLiteral("Save a duplicate at path and keep editing it"))}}) },
        { "list_export_options", "project", "See codecs, scales, and fps choices",
          "Returns {scales:[{id,w,h}], fps:[{id}], video:[{id,label}], audio:[{id,label}], gif, folder} "
          "— only codecs available on this machine are listed. Does not list rate or preset values; "
          "those live in the export_video schema.",
          objectSchema({}), true, false, true },
        { "export_video", "project", "Render the timeline to a file",
          "Start an async encode and return immediately with {started, path, busy}. Poll "
          "inspect().export.{active,progress} or export_status until active is false. Fails export_busy "
          "when an encode is already running — cancel_export first. The output path is normalised: a "
          "directory gets <project name>.<ext> appended and a suffix-less path gets the container "
          "extension added, so use the path echoed in the reply, not the one you sent. Omitted settings "
          "inherit from the last export in this app profile, then defaults — pass every setting you "
          "care about rather than relying on them.",
          objectSchema(mergeProps({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute output path, or a directory to auto-name inside"))}},
                                  exportSettingsProps()),
                       {QStringLiteral("path")}) },
        { "export_status", "project", "Check an in-flight encode",
          "Returns {busy, progress 0..1, message}. Same data as inspect().export.",
          objectSchema({}), true, false, true },

        { "list_animated_properties", "keyframes", "See what already has keys",
          "Returns {props:[…]} — only the properties that already carry keyframes on this clip. Empty "
          "on a fresh clip. Property spellings live in the `prop` schema of the other keyframes ops "
          "(x, y, width, height, rotation, opacity, volume, fx.<i>.<key>), not here.",
          objectSchema(clipRefProps()), true, false, true },
        { "list_keyframes", "keyframes", "Read keys for one property",
          "Returns {prop, enabled, keys:[{seconds, value, inDx, inDy, outDx, outDy, corner, hold, "
          "easing, custom}]}. Times are timeline seconds. enabled is false when the property was muted "
          "via set_property_keyframes_enabled.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()}},
                                  clipRefProps()),
                       {QStringLiteral("prop")}),
          true, false, true },
        { "set_keyframe", "keyframes", "Add or update a key",
          "Add a keyframe, or overwrite the value of an existing one at that time. Creates the "
          "animation if the property had no keys yet.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds"))},
                                   {QStringLiteral("value"), numberProp(QStringLiteral("Property value, in the property's own units (pixels, degrees, 0..1)"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at"), QStringLiteral("value")}) },
        { "remove_keyframe", "keyframes", "Delete a key",
          "Remove the keyframe NEAREST to `at` — there is no distance limit, so a time that misses "
          "every key still deletes the closest one, and a no-op returns ok. Confirm the exact key time "
          "with list_keyframes first.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds of the key to delete"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at")}),
          false, true },
        { "set_keyframe_interpolation", "keyframes", "Set linear/hold/ease on a key",
          "Set the easing preset on the key nearest `at`. SIDE EFFECT: this moves the playhead to `at`, "
          "which changes the default time of later ops in the same apply batch and the target of "
          "set_transform — seek back if that matters.",
          objectSchema(mergeProps(
              {{QStringLiteral("prop"), animPropProp()},
               {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds; the playhead is moved here"))},
               {QStringLiteral("mode"), enumProp(QStringLiteral("Interpolation"),
                                                {QStringLiteral("linear"), QStringLiteral("hold"),
                                                 QStringLiteral("ease")})}},
              clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at"), QStringLiteral("mode")}) },
        { "set_keyframe_tangents", "keyframes", "Shape bezier handles",
          "Set tangent handles on the key at `at`, relative to it. Omitted handle fields are sent as 0, "
          "not left alone — pass all four to avoid flattening the ones you skip.",
          objectSchema(mergeProps(
              {{QStringLiteral("prop"), animPropProp()},
               {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds"))},
               {QStringLiteral("inDx"), numberProp(QStringLiteral("Incoming handle dx in seconds (defaults to 0)"))},
               {QStringLiteral("inDy"), numberProp(QStringLiteral("Incoming handle dy in property units (defaults to 0)"))},
               {QStringLiteral("outDx"), numberProp(QStringLiteral("Outgoing handle dx in seconds (defaults to 0)"))},
               {QStringLiteral("outDy"), numberProp(QStringLiteral("Outgoing handle dy in property units (defaults to 0)"))},
               {QStringLiteral("corner"), boolProp(QStringLiteral("Break tangent collinearity"))}},
              clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at")}) },
        { "set_keyframe_hold", "keyframes", "Step-hold a key",
          "When hold is true the property steps: it keeps this key's value until the next key instead "
          "of interpolating.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds"))},
                                   {QStringLiteral("hold"), boolProp(QStringLiteral("Hold until next key"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at"), QStringLiteral("hold")}) },
        { "set_property_keyframes_enabled", "keyframes", "Mute animation without deleting keys",
          "When false the keys are kept but the property holds its first key's value. Use this before "
          "set_transform when you want a constant value on an already-animated property.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("enabled"), boolProp(QStringLiteral("Keyframes drive the property"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("enabled")}) },

        { "list_speed_curve", "speed", "Read a clip's speed ramp",
          "Returns {hasCurve, points:[{pos,speed,…}], retimedDuration}. Despite being a read, this "
          "opens and closes a transient curve session and will CLOSE any speed-curve session already "
          "open. Fails bad_args on clips that cannot carry a curve (no continuous source: text, shape, "
          "still image).",
          objectSchema(clipRefProps()), false, false, true },
        { "set_speed_curve", "speed", "Apply a custom speed ramp",
          "Replace the clip with a retimed copy carrying the curve. Needs at least two points. Returns "
          "{id, track, index, retimedDuration} with a NEW id — the old clip UUID is dead. An ops array "
          "cannot reference an id produced earlier in the same batch, so END THE BATCH after this op "
          "and use the returned id in the next apply. Fails bad_args on clips that cannot carry a "
          "curve.",
          objectSchema(mergeProps(
              {{QStringLiteral("points"),
                arrayProp(speedPointSchema(), QStringLiteral("Speed curve control points, at least two, ordered by pos"))}},
              clipRefProps()),
                       {QStringLiteral("points")}) },
        { "clear_speed_curve", "speed", "Remove a speed ramp",
          "Clear the curve and restore the scalar-speed timeline duration.",
          objectSchema(clipRefProps()), false, true },

        { "get_ui_preferences", "ui", "Read editor UI settings",
          "Returns {theme:{overridden, dark}, autoKey, mediaGrid, reopenLastProject}. autoKey matters "
          "for set_transform: when true, transform writes become keyframes at the playhead.",
          objectSchema({}), true, false, true },
        { "set_theme", "ui", "Set dark or light theme",
          "Set an explicit dark-mode preference. This overrides the OS theme; clear it with "
          "set_ui_preferences({followSystem:true}). Not undoable.",
          objectSchema({{QStringLiteral("dark"), boolProp(QStringLiteral("true = dark, false = light"))}},
                       {QStringLiteral("dark")}) },
        { "list_shortcuts", "ui", "List action bindings",
          "Returns {actions:[{id, label, shortcut}], n} for every editor action.",
          objectSchema({{QStringLiteral("q"), stringProp(QStringLiteral("Case-insensitive substring over id/label/shortcut"))}}),
          true, false, true },
        { "set_shortcut", "ui", "Rebind a shortcut",
          "Bind keys to an action id from list_shortcuts. An empty keys string clears the binding. On "
          "a clash the call FAILS with error \"conflict\" and the conflicting action's label in detail; "
          "nothing is rebound. Not undoable.",
          objectSchema({{QStringLiteral("action"), stringProp(QStringLiteral("Action id from list_shortcuts"))},
                        {QStringLiteral("keys"), stringProp(QStringLiteral("Qt key sequence, e.g. Ctrl+S; empty string clears"))}},
                       {QStringLiteral("action"), QStringLiteral("keys")}) },
        { "reset_shortcuts", "ui", "Restore default shortcuts",
          "Reset every action binding to its default. Not undoable.",
          objectSchema({}), false, true }
#include "mcp/McpCatalogExtendedOps.inl"
    };
    return k;
}

QJsonObject opTool(const Op &op)
{
    return toolDef(QString::fromUtf8(op.name),
                   QStringLiteral("When: %1. %2").arg(QString::fromUtf8(op.when),
                                                      QString::fromUtf8(op.description)),
                   op.schema,
                   toolAnnotations(op.readOnly, op.destructive, op.idempotent));
}

QJsonArray endpointList()
{
    QJsonArray endpoints;
    endpoints.append(QStringLiteral("/mcp"));
    for (const QString &name : toolboxNames()) {
        if (name != QStringLiteral("mcp"))
            endpoints.append(QStringLiteral("/mcp/") + name);
    }
    return endpoints;
}

const QHash<QString, int> &opIndex()
{
    static const QHash<QString, int> k = [] {
        QHash<QString, int> h;
        const QList<Op> &all = ops();
        for (int i = 0; i < all.size(); ++i)
            h.insert(QString::fromUtf8(all.at(i).name), i);
        return h;
    }();
    return k;
}

const Op *findOp(const QString &name)
{
    const int i = opIndex().value(name, -1);
    return i < 0 ? nullptr : &ops().at(i);
}

QString matchKey(const QString &s)
{
    QString k = s.trimmed().toLower();
    k.replace(QLatin1Char('-'), QLatin1Char('_'));
    k.replace(QLatin1Char(' '), QLatin1Char('_'));
    k.replace(QLatin1Char('.'), QLatin1Char('_'));
    return k;
}

int levenshtein(const QString &a, const QString &b)
{
    QList<int> prev(b.size() + 1);
    QList<int> cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            cur[j] = qMin(qMin(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
        }
        prev.swap(cur);
    }
    return prev[b.size()];
}

} // namespace

QStringList toolboxNames()
{
    return {QStringLiteral("media"),     QStringLiteral("timeline"), QStringLiteral("canvas"),
            QStringLiteral("playback"),  QStringLiteral("text"),     QStringLiteral("effects"),
            QStringLiteral("project"),   QStringLiteral("keyframes"), QStringLiteral("speed"),
            QStringLiteral("ui"),        QStringLiteral("shapes"),   QStringLiteral("motion"),
            QStringLiteral("subtitles"), QStringLiteral("segmentation"), QStringLiteral("ai"),
            QStringLiteral("audio"),     QStringLiteral("scene"),    QStringLiteral("multicam"),
            QStringLiteral("market")};
}

QStringList undoExemptOps()
{
    static const QStringList k = {
        QStringLiteral("import_media"),        QStringLiteral("import_media_bytes"),
        QStringLiteral("market_download"),     QStringLiteral("market_cancel_download"),
        QStringLiteral("seek"),                QStringLiteral("play"),
        QStringLiteral("pause"),               QStringLiteral("undo"),
        QStringLiteral("redo"),                QStringLiteral("undo_to"),
        QStringLiteral("take_snapshot"),       QStringLiteral("restore_snapshot"),
        QStringLiteral("set_overlap"),         QStringLiteral("set_ripple"),
        QStringLiteral("set_snap"),            QStringLiteral("set_guides"),
        QStringLiteral("set_loop_work_area"),  QStringLiteral("export_video"),
        QStringLiteral("save_project"),        QStringLiteral("set_theme"),
        QStringLiteral("set_shortcut"),        QStringLiteral("reset_shortcuts"),
        QStringLiteral("set_beat_layers"),     QStringLiteral("detect_beats"),
        QStringLiteral("list_speed_curve"),    QStringLiteral("list_fade_curve"),
        QStringLiteral("install_addon"),       QStringLiteral("cancel_addon_install"),
        QStringLiteral("set_acceleration"),    QStringLiteral("switch_angle"),
        QStringLiteral("end_multicam"),
    };
    return k;
}

QStringList selectionBasedOps()
{
    static const QStringList k = {
        QStringLiteral("separate_audio"), QStringLiteral("unlink_audio"),
        QStringLiteral("merge_clips"),    QStringLiteral("align_clip_left"),
        QStringLiteral("align_clip_right"), QStringLiteral("copy_selection"),
        QStringLiteral("cut_selection"),
    };
    return k;
}

QString agentGuideText()
{
    return QStringLiteral(
        "Drift MCP agent guide\n"
        "\n"
        "Workflow:\n"
        "1. Call catalog on POST /mcp (homepage), or search({q}) to find an op by keyword.\n"
        "2. Call toolbox({name}) or toolbox({ops:[…]}) for JSON schemas.\n"
        "3. Call apply({ops:[{tool, args}, …]}) to run one or many mutations in one undo step.\n"
        "4. Call inspect({clips:true, detail:true}) for clip UUIDs, effect stacks, mask/fade/speed, "
        "subtitle cues, face-track/stabilize fields, and job state. inspect always reports "
        "selection:{track,index,clip} (omitted when nothing is selected) and "
        "undo:{can,canRedo,depth,index,hash}. inspect({clips:true,cues:true}) adds subtitleCues without "
        "the rest of detail.\n"
        "5. To see the footage: activity() finds where things happen, frames() renders a contact\n"
        "   sheet of distinct moments, capture() gives one full-size still.\n"
        "\n"
        "Pinned endpoints (/mcp/media, /mcp/timeline, …) list toolbox ops directly. "
        "catalog, toolbox, search, and apply are only on /mcp; inspect, capture, frames, and activity work on both. "
        "Toolbox ops may also be called by name directly on /mcp instead of through apply, but only "
        "apply gives you one undo step for a batch.\n"
        "\n"
        "Conventions:\n"
        "- Times are seconds. Track index 0 is the top lane.\n"
        "- Identify clips by UUID from inspect({clips:true}); track+index are positional and shift as\n"
        "  tracks and clips move. Clip-ref ops need one or the other — they never fall back to the\n"
        "  selection.\n"
        "- Clip overlap is off by default (place/move snap to the next gap; the reply reports\n"
        "  requested vs placed).\n"
        "- Every op returns {ok:true, …} or {ok:false, error:<code>, detail:<text>}. Codes: bad_args,\n"
        "  not_found, type_mismatch, unknown_op, unknown_toolbox, wrong_endpoint, wrong_toolbox,\n"
        "  apply_failed, import_failed, import_timeout, export_busy, export_failed, export_timeout,\n"
        "  capture_failed, conflict.\n"
        "- apply is not atomic: on failure the ops before it stay applied; done lists only those.\n"
        "- Args are validated against the schema: bad_args (missing/enum/range), type_mismatch;\n"
        "  unknown keys come back as ignored:[…]. Numbers are rounded to 3 dp except fps/pos.\n"
        "\n"
        "Selection-based ops take no clip argument and act on the current selection — call\n"
        "select_clip or select_clips first: separate_audio, unlink_audio, merge_clips,\n"
        "align_clip_left, align_clip_right, copy_selection, cut_selection.\n"
        "freeze_frame and paste_at_playhead are playhead-based, not selection-based — seek first.\n"
        "\n"
        "History is linear, like git without branches. list_history returns every version (index 0 =\n"
        "Origin) with a SHA-256 hash of that version's compact project JSON. undo_to({hash}) or\n"
        "undo_to({index}) jumps; the next edit discards redo. take_snapshot writes <hash>.json whose\n"
        "bytes hash to the same value; restore_snapshot loads a disk copy only if it has fallen off\n"
        "the stack (and resets Origin).\n"
        "\n"
        "Async jobs return {started:true} immediately. Poll these fields, all of which need\n"
        "inspect({detail:true}) except export:\n"
        "- export_video      -> export.{active,progress}      (or export_status)\n"
        "- package_project   -> package.{active,progress}\n"
        "- generate_subtitles-> subtitleGen.{active,progress,status}\n"
        "- detect_scenes     -> sceneDetect.{active,progress,status}\n"
        "- set_clip_reverse  -> reverseRender.{active,progress,status}\n"
        "Segmentation, denoise, and face detection report through the app's status only; re-read\n"
        "inspect({clips:true,detail:true}) and compare to detect completion.\n"
        "\n"
        "Working to the music (audio toolbox):\n"
        "1. detect_beats({start, duration}) blocks and returns bpm plus exact beat and onset times.\n"
        "2. set_beat_layers({grid:true}) turns those beats into snap targets, so place_clip,\n"
        "   move_clip and move_to_track magnet to the nearest beat within 150 ms from then on.\n"
        "3. split_on_beats and snap_clips_to_beats cut and quantise against the same grid;\n"
        "   bookmark_beats writes it into the project as markers that survive re-analysis.\n"
        "The analysis is transient: any edit that changes the mix drops it, and\n"
        "inspect({detail:true}).beats.stale says whether what you have still describes the audio.\n"
        "\n"
        "Understanding the footage (scene toolbox):\n"
        "0. activity → frames → capture shows you the pictures before any scan: activity for\n"
        "   where content changes, frames for a labelled contact sheet, capture for one moment.\n"
        "1. detect_scenes({clip, with_objects:true}) starts a scan; poll\n"
        "   inspect({detail:true}).sceneDetect until active is false. A clip already scanned at\n"
        "   the same settings comes back {cached:true} straight away.\n"
        "2. describe_clip gives the one-call impression — shot count, pacing, what is in it.\n"
        "   list_scenes gives every shot, with timeline_start/timeline_end already mapped through\n"
        "   trim, speed and reverse so you can act on them directly.\n"
        "3. find_scenes({label}) searches EVERY scanned clip on the timeline, which is how you\n"
        "   gather material rather than inspect one clip at a time.\n"
        "4. split_on_scenes cuts a clip at its boundaries; bookmark_scenes marks them instead.\n"
        "Unlike beats this analysis is NOT transient: it describes the source file and is cached\n"
        "against that file's timestamp, so it survives edits, undo and reload. with_objects needs\n"
        "the object-model addon — ai_capabilities reports what is installed; list_addons /\n"
        "install_addon can install a missing model from the addon store.\n"
        "\n"
        "Finding media: import_media takes absolute paths only. Drift will not list folders. When\n"
        "the user gives a partial name, glob or search the host with YOUR own tools (find, ls,\n"
        "whatever the client exposes), then pass those absolute paths to import_media and confirm\n"
        "missing:[] is empty.\n"
        "\n"
        "Toolboxes: media, timeline, canvas, playback, text, effects, project, keyframes, speed, ui, "
        "shapes, motion, subtitles, segmentation, ai, audio, scene, multicam.\n");
}

QJsonObject catalogPayload(const QJsonObject &args)
{
    struct Box {
        const char *name;
        const char *when;
    };
    static const Box boxes[] = {
        {"media", "Import (absolute paths only — resolve fuzzy names with your own filesystem tools) "
                   "and read the media bin before placing clips."},
        {"timeline", "Tracks, place/move/trim/split/delete clips, ripple/gap, overlap, selection, undo."},
        {"canvas", "Transform, flip, blend, mask, fade, constant speed, reverse, animation, stabilisation."},
        {"playback", "Seek, play, pause, In/Out work area."},
        {"text", "Add and edit title/caption clips."},
        {"effects", "Video/audio effects and transitions."},
        {"project", "Canvas size, background, metadata, save, and export."},
        {"keyframes", "Animate clip and effect properties over time."},
        {"speed", "Speed ramps (retimed clips) and reading custom fade curves."},
        {"ui", "Editor theme and keyboard shortcuts."},
        {"shapes", "Builtin shapes, stickers, emoji, fonts."},
        {"motion", "Lottie animations and SVG drawings as vector clips: add, inspect, re-theme through slots."},
        {"subtitles", "Subtitle clips, import/export, Whisper generation."},
        {"segmentation", "SAM-style cutout and mask output."},
        {"ai", "Denoise, face detection, auto-reframe, model add-ons (list/install), acceleration."},
        {"audio", "Waveforms, silence, loudness, ducking, beat detection, beat-synced cuts, clip volume."},
        {"scene", "Detect shots, read what is in them, and cut or assemble against them."},
        {"multicam", "Multi-camera session: set up angles, switch at the playhead, save separate or combined."},
        {"market", "Stock media from the Cutwire marketplace: search or resolve a link, download into the bin. Needs the user's one-time consent in the app; downloads spend a per-machine quota."},
    };

    const bool brief = args.value(QStringLiteral("brief")).toBool();
    QJsonArray toolboxes;
    for (const Box &box : boxes) {
        QJsonArray opEntries;
        for (const Op &op : ops()) {
            if (qstrcmp(op.toolbox, box.name) != 0)
                continue;
            if (brief)
                opEntries.append(QString::fromUtf8(op.name));
            else
                opEntries.append(QStringLiteral("%1 — %2").arg(QString::fromUtf8(op.name), QString::fromUtf8(op.when)));
        }
        toolboxes.append(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(box.name)},
            {QStringLiteral("when"), QString::fromUtf8(box.when)},
            {QStringLiteral("ops"), opEntries},
        });
    }

    QJsonObject out = ok({
        {QStringLiteral("toolboxes"), toolboxes},
        {QStringLiteral("limitations"),
         QJsonArray{
             QStringLiteral("apply is not atomic: on failure the ops before it stay applied; done lists only those, failed carries the error."),
             QStringLiteral("apply cannot run catalog, toolbox, search, inspect, capture, frames, activity, or apply; call those directly."),
             QStringLiteral("Clip-ref ops need clip (uuid) or track+index; they never fall back to the selection (read it from inspect.selection)."),
             QStringLiteral("set_effect_param, set_audio_effect_param and set_transition_param do not validate key or index; verify with inspect."),
             QStringLiteral("set_transform writes at the playhead and becomes a keyframe when the property is animated or autoKey is on."),
             QStringLiteral("set_mask and set_subtitle_cues replace the whole mask / cue list; read the current one from inspect first."),
             QStringLiteral("Segmentation, denoise and face detection expose no progress field; diff inspect to detect completion."),
             QStringLiteral("Outside the undo stack (as are all read-only ops): %1. set_beat_layers changes the user's own snapping and cannot be undone.")
                 .arg(undoExemptOps().join(QStringLiteral(", "))),
             QStringLiteral("set_speed_curve returns a new clip id and the old UUID stops resolving; end the apply batch after it."),
             QStringLiteral("generate_subtitles must run AFTER remove_silence, which shifts cue times."),
             QStringLiteral("import_media takes absolute paths only; there is no directory listing, so find files with your own filesystem tools and check missing:[]."),
         }},
    });
    if (args.value(QStringLiteral("endpoints")).toBool())
        out.insert(QStringLiteral("endpoints"), endpointList());
    if (args.value(QStringLiteral("guide")).toBool())
        out.insert(QStringLiteral("guide"), agentGuideText());
    return out;
}

QJsonObject toolboxPayload(const QString &name, const QStringList &only)
{
    const QString key = name.trimmed().toLower();
    if (!key.isEmpty() && !toolboxNames().contains(key))
        return err("unknown_toolbox", QStringLiteral("Known: %1").arg(toolboxNames().join(QLatin1Char(' '))));
    if (key.isEmpty() && only.isEmpty())
        return err("bad_args", QStringLiteral("name (toolbox) or ops (op names) required"));

    QJsonArray tools;
    QJsonArray unknown;
    if (only.isEmpty()) {
        for (const Op &op : ops()) {
            if (key == QLatin1String(op.toolbox))
                tools.append(opTool(op));
        }
    } else {
        for (const QString &raw : only) {
            const Op *op = findOp(raw.trimmed());
            if (!op || (!key.isEmpty() && key != QLatin1String(op->toolbox))) {
                unknown.append(raw);
                continue;
            }
            tools.append(opTool(*op));
        }
    }
    QJsonObject out = ok({{QStringLiteral("tools"), tools}, {QStringLiteral("n"), tools.size()}});
    if (!key.isEmpty())
        out.insert(QStringLiteral("name"), key);
    if (!unknown.isEmpty())
        out.insert(QStringLiteral("unknown"), unknown);
    return out;
}

QJsonArray homepageTools()
{
    const QStringList toolboxEnum = toolboxNames();
    QJsonArray tools;
    tools.append(toolDef(
        QStringLiteral("catalog"),
        QStringLiteral("When: Start here. Returns every toolbox with its ops as \"name — when\" one-liners plus "
                       "limitations (~3k tokens). brief:true = op names only; guide:true adds the agent guide; "
                       "endpoints:true adds the pinned HTTP endpoints. Next: toolbox({name}) or toolbox({ops}) "
                       "for schemas, or search({q}) to jump straight to an op."),
        objectSchema({{QStringLiteral("brief"), boolProp(QStringLiteral("Op names only, no when hints"))},
                      {QStringLiteral("guide"), boolProp(QStringLiteral("Include the agent guide prose"))},
                      {QStringLiteral("endpoints"), boolProp(QStringLiteral("Include the pinned HTTP endpoints"))}}),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("toolbox"),
        QStringLiteral("When: Load schemas. Returns full JSON schemas for one toolbox's ops (name), or for just "
                       "the named ops from any toolbox (ops). Then call those ops via apply."),
        objectSchema({{QStringLiteral("name"), enumProp(QStringLiteral("Toolbox name"), toolboxEnum)},
                      {QStringLiteral("ops"),
                       arrayProp(stringProp(QStringLiteral("Op name")),
                                 QStringLiteral("Only these ops, from any toolbox; name is then optional"))}}),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("inspect"),
        QStringLiteral("When: Read state. Summary: revision, name, w, h, fps, dur, playhead, playing, overlap, "
                       "clips (count), tracks, assets, path, dirty, background, export {active, progress}, "
                       "selection (absent when none), undo {can, canRedo, depth, index, hash (12-char)}, "
                       "work_in/work_out when set. clips=true adds compact rows under tracks[].items; "
                       "cues=true adds subtitleCues to them. detail=true expands rows to the full clip map "
                       "(effects, mask, fade, speed, keyframes, stabilize*, ...) minus what the kind cannot "
                       "use or still sits at its default: an absent boolean is false; transform "
                       "{x,y,w,h,rotation,opacity} is always present (values at the playhead) and "
                       "animated lists the keyframed properties. verbose=true returns every field. "
                       "clip=<uuid> returns just that clip (detail on); track=<n> just that track. "
                       "Async jobs appear under jobs {package|subtitleGen|reverseRender|sceneDetect} only while "
                       "active; beats only once analysed. since=<revision> returns {unchanged:true, revision}."),
        objectSchema({{QStringLiteral("clips"), boolProp(QStringLiteral("Include per-clip rows under tracks[].items"))},
                      {QStringLiteral("detail"),
                       boolProp(QStringLiteral("Expand rows to the full clip map with kind-irrelevant and default-valued "
                                               "fields omitted. Combine with clips=true."))},
                      {QStringLiteral("verbose"),
                       boolProp(QStringLiteral("With detail=true, return every field of the clip map, defaults included"))},
                      {QStringLiteral("cues"),
                       boolProp(QStringLiteral("Include subtitleCues on clip rows without requiring detail. Use this for an hour-long transcript index."))},
                      {QStringLiteral("clip"),
                       stringProp(QStringLiteral("Clip uuid: return only that clip's detail row (implies clips and detail)"))},
                      {QStringLiteral("track"),
                       integerProp(QStringLiteral("Track index: return only that track"))},
                      {QStringLiteral("since"),
                       integerProp(QStringLiteral("Revision from a prior inspect; returns {unchanged:true} when current"))}}),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("apply"),
        QStringLiteral("When: Mutate. Runs ops in order and stops at the first failure. NOT atomic — ops before the failure stay applied; the reply is {ok:false, error:\"apply_failed\", stopped:<index>, tool, failed:<that op's error>, done:[results of the ops before it]}. On success: {ok:true, n, done}. Successful mutations collapse into a single undo step labelled from the op names, except the ops listed in catalog.limitations (import, save, playback, export, view-state toggles including set_beat_layers), which undo cannot revert. Only toolbox ops go here — catalog, toolbox, search, inspect, capture, frames, activity, and apply itself return unknown_op, so call those directly. Args are validated against the op schema (required, type, enum, min/max); unknown keys are reported back as ignored:[…]. An ops array cannot reference an id produced earlier in the same batch — end the batch after set_speed_curve or any op that mints a clip id you need next."),
        objectSchema({{QStringLiteral("ops"),
                       arrayProp(objectSchema({{QStringLiteral("tool"), stringProp(QStringLiteral("Toolbox op name, e.g. place_clip"))},
                                               {QStringLiteral("args"),
                                                QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                            {QStringLiteral("description"), QStringLiteral("That op's arguments, per its inputSchema from toolbox")}}}},
                                              {QStringLiteral("tool")}),
                                 QStringLiteral("Sequential operations"))}},
                     {QStringLiteral("ops")})));
    tools.append(toolDef(
        QStringLiteral("capture"),
        QStringLiteral("When: Verify visually. Still of the composition at `at`, defaulting to the playhead. Pauses playback first. By default returns an inline JPEG scaled to a 1280px long edge plus {at, w, h, full}; full=true instead writes a full-resolution PNG next to the project's frame captures and returns its path in the reply. beyond_end:true and dur are added when `at` is past the end of the timeline. Cannot be used inside apply."),
        objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds (default: playhead)"))},
                      {QStringLiteral("full"), boolProp(QStringLiteral("Full-res PNG on disk instead of inline JPEG"))}}),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("frames"),
        QStringLiteral("When: See many moments at once. One contact-sheet image of the timeline (or of one clip) "
                       "instead of repeated capture calls — about the cost of a single capture. sample:changes "
                       "(default) hashes candidate frames and keeps only the ones that look different; uniform "
                       "spaces n evenly; scenes uses detect_scenes thumbnails; at:[…] renders exact times. Every "
                       "tile has its time burned in AND listed in frames[].t, with diff = how different it is from "
                       "the previous kept tile (in-shot motion ~5-10, a cut 30+). Times are timeline seconds, or source "
                       "seconds when clip/track+index is given (tl is then the timeline time). Tiles past the "
                       "end of the material render black and carry beyond_end:true. Cannot run inside "
                       "apply. Follow up with capture({at}) for a full-size look."),
        objectSchema(mergeProps(
            {{QStringLiteral("start"), numberProp(QStringLiteral("Range start seconds (default: work area or 0)"))},
             {QStringLiteral("end"), numberProp(QStringLiteral("Range end seconds (default: work area or timeline end)"))},
             {QStringLiteral("at"), arrayProp(numberProp(QStringLiteral("Seconds")), QStringLiteral("Exact times to render, max 20; overrides sample"))},
             {QStringLiteral("n"), propWithDefault(integerProp(QStringLiteral("Tiles to keep"), 1, 20), 12)},
             {QStringLiteral("sample"),
              propWithDefault(enumProp(QStringLiteral("changes keeps visually distinct frames; uniform spaces them evenly; scenes uses detect_scenes thumbnails"),
                                       {QStringLiteral("changes"), QStringLiteral("uniform"), QStringLiteral("scenes")}),
                              QStringLiteral("changes"))},
             {QStringLiteral("min_change"), propWithDefault(integerProp(QStringLiteral("changes only: dHash bits (0..64) a frame must differ from the last kept frames by; in-shot motion scores ~5-10, a cut 30+"), 0, 64), 12)},
             {QStringLiteral("cols"), integerProp(QStringLiteral("Grid columns; 0 = auto (3 when n ≤ 6, else 4)"), 0, 8)},
             {QStringLiteral("tile"), integerProp(QStringLiteral("Tile width in pixels; 0 = auto so the sheet fits one vision image"), 0, 720)},
             {QStringLiteral("label"), propWithDefault(boolProp(QStringLiteral("Burn the time into each tile")), true)},
             {QStringLiteral("return"),
              propWithDefault(enumProp(QStringLiteral("inline JPEG in the reply, or path to a JPEG next to the project's frame captures"),
                                       {QStringLiteral("inline"), QStringLiteral("path")}),
                              QStringLiteral("inline"))}},
            clipRefProps())),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("activity"),
        QStringLiteral("When: Find where something happens before looking at it. A cheap text profile over a "
                       "range: per-sample content change (0..255, the same scale as detect_scenes.threshold 27), "
                       "motion fraction and audio level (0..1), plus the strongest peaks. At coarse steps a fast "
                       "pan scores like a cut, so these are peaks, not cuts — confirm with frames({at:[…]}). "
                       "Timeline space by default, source space with clip/track+index. Cannot run inside apply."),
        objectSchema(mergeProps(
            {{QStringLiteral("start"), numberProp(QStringLiteral("Range start seconds (default: work area or 0)"))},
             {QStringLiteral("end"), numberProp(QStringLiteral("Range end seconds (default: work area or timeline end)"))},
             {QStringLiteral("samples"), propWithDefault(integerProp(QStringLiteral("Frames to score across the range"), 8, 600), 200)},
             {QStringLiteral("peaks"), propWithDefault(integerProp(QStringLiteral("How many of the strongest content peaks to list"), 0, 30), 8)},
             {QStringLiteral("audio"), propWithDefault(boolProp(QStringLiteral("Include the audio lane")), true)}},
            clipRefProps())),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("search"),
        QStringLiteral("When: You know what you want but not the op name. Find an op by keyword: effects, "
                       "transitions, keyframes, animation, subtitles, captions, transcribe, beats, tempo, scenes, "
                       "shots, silence, loudness, mask, fade, speed, reverse, crop, resize, export, render, "
                       "import, undo, history, bookmark, stabilize, denoise, faces, emoji, fonts, shapes, lottie, "
                       "stickers, multicam. Scores op names, toolbox, when hints, descriptions and argument names; "
                       "returns hits:[{name, toolbox, when, args, required}]. schema:true inlines inputSchema when "
                       "there are ≤3 hits, so you can go straight to apply."),
        objectSchema({{QStringLiteral("q"), stringProp(QStringLiteral("Keywords, space separated"))},
                      {QStringLiteral("limit"), propWithDefault(integerProp(QStringLiteral("Max hits"), 1, 50), 8)},
                      {QStringLiteral("schema"), boolProp(QStringLiteral("Inline inputSchema when ≤3 hits"))}},
                     {QStringLiteral("q")}),
        toolAnnotations(true, false, true)));
    return tools;
}

QJsonArray toolboxDirectTools(const QString &name)
{
    const QString key = name.trimmed().toLower();
    QJsonArray tools;
    for (const Op &op : ops()) {
        if (key == QLatin1String(op.toolbox))
            tools.append(opTool(op));
    }
    return tools;
}

bool isHomepageTool(const QString &name)
{
    static const QStringList k = {QStringLiteral("catalog"), QStringLiteral("toolbox"),
                                  QStringLiteral("inspect"), QStringLiteral("apply"),
                                  QStringLiteral("capture"), QStringLiteral("frames"),
                                  QStringLiteral("activity"), QStringLiteral("search")};
    return k.contains(name);
}

bool isKnownOp(const QString &name)
{
    return opIndex().contains(name);
}

bool isReadOnlyOp(const QString &name)
{
    const Op *op = findOp(name);
    return op && op->readOnly;
}

QString toolboxForOp(const QString &name)
{
    const Op *op = findOp(name);
    return op ? QString::fromUtf8(op->toolbox) : QString();
}

QJsonObject opInputSchema(const QString &name)
{
    const Op *op = findOp(name);
    return op ? op->schema : QJsonObject();
}

QStringList opNames()
{
    static const QStringList k = [] {
        QStringList names;
        for (const Op &op : ops())
            names.append(QString::fromUtf8(op.name));
        return names;
    }();
    return k;
}

QStringList nearestStrings(const QString &needle, const QStringList &pool, int max)
{
    const QString n = matchKey(needle);
    if (n.isEmpty())
        return {};
    QList<QPair<int, QString>> scored;
    for (const QString &candidate : pool) {
        const QString k = matchKey(candidate);
        const int d = levenshtein(n, k);
        const bool related = d <= 3 || (n.size() >= 4 && (k.contains(n) || n.contains(k)));
        if (related)
            scored.append({d, candidate});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first < b.first; });
    QStringList out;
    for (const auto &entry : scored) {
        if (out.size() >= max)
            break;
        out.append(entry.second);
    }
    return out;
}

QStringList nearestOps(const QString &name, int max)
{
    return nearestStrings(name, opNames(), max);
}

QJsonObject unknownOpError(const QString &name)
{
    if (name.trimmed().isEmpty())
        return err("unknown_op", QStringLiteral("tool name required"));
    if (isHomepageTool(name))
        return err("unknown_op", QStringLiteral("%1 is a homepage tool; call it directly, not inside apply").arg(name));
    QStringList suggestions;
    for (const QString &op : nearestOps(name))
        suggestions.append(QStringLiteral("%1 (%2)").arg(op, toolboxForOp(op)));
    if (suggestions.isEmpty())
        return err("unknown_op", QStringLiteral("unknown op %1; try search({q:\"…\"}) or catalog").arg(name));
    return err("unknown_op", QStringLiteral("unknown op %1; did you mean %2?").arg(name, suggestions.join(QStringLiteral(", "))));
}

QJsonObject searchOps(const QString &q, int limit, bool schema)
{
    const QStringList tokens = q.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return err("bad_args", QStringLiteral("q required"));
    limit = qBound(1, limit, 50);

    QList<QPair<int, const Op *>> scored;
    for (const Op &op : ops()) {
        const QString name = QString::fromUtf8(op.name);
        const QString toolbox = QString::fromUtf8(op.toolbox);
        const QString when = QString::fromUtf8(op.when).toLower();
        const QString description = QString::fromUtf8(op.description).toLower();
        const QJsonObject props = op.schema.value(QStringLiteral("properties")).toObject();
        int score = 0;
        for (const QString &token : tokens) {
            if (name == token)
                score += 100;
            else if (name.startsWith(token))
                score += 50;
            else if (name.contains(token))
                score += 30;
            if (toolbox == token)
                score += 20;
            if (when.contains(token))
                score += 20;
            if (description.contains(token))
                score += 10;
            bool inKeys = false;
            bool inDescriptions = false;
            for (auto it = props.begin(); it != props.end(); ++it) {
                if (it.key().toLower().contains(token))
                    inKeys = true;
                if (it.value().toObject().value(QStringLiteral("description")).toString().toLower().contains(token))
                    inDescriptions = true;
            }
            score += (inKeys ? 5 : 0) + (inDescriptions ? 5 : 0);
        }
        if (score > 0)
            scored.append({score, &op});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const QPair<int, const Op *> &a, const QPair<int, const Op *> &b) {
                         if (a.first != b.first)
                             return a.first > b.first;
                         return qstrcmp(a.second->name, b.second->name) < 0;
                     });

    QJsonArray hits;
    const bool inline_ = schema && scored.size() <= 3;
    for (const auto &entry : scored) {
        if (hits.size() >= limit)
            break;
        const Op &op = *entry.second;
        QJsonArray args;
        const QJsonObject props = op.schema.value(QStringLiteral("properties")).toObject();
        for (auto it = props.begin(); it != props.end(); ++it)
            args.append(it.key());
        QJsonObject row{
            {QStringLiteral("name"), QString::fromUtf8(op.name)},
            {QStringLiteral("toolbox"), QString::fromUtf8(op.toolbox)},
            {QStringLiteral("when"), QString::fromUtf8(op.when)},
            {QStringLiteral("args"), args},
            {QStringLiteral("required"), op.schema.value(QStringLiteral("required")).toArray()},
        };
        if (inline_)
            row.insert(QStringLiteral("inputSchema"), op.schema);
        hits.append(row);
    }
    return ok({{QStringLiteral("q"), q}, {QStringLiteral("hits"), hits}, {QStringLiteral("n"), hits.size()}});
}

QString homepageHtml()
{
    const QJsonObject cat = catalogPayload({{QStringLiteral("brief"), true}, {QStringLiteral("endpoints"), true}});
    QString body = QStringLiteral(
        "<!doctype html><meta charset=utf-8><title>Drift MCP</title>"
        "<body style='font:14px/1.45 system-ui;max-width:42rem;margin:2rem auto;padding:0 1rem'>"
        "<h1>Drift agent access</h1>"
        "<p>This editor is exposing an MCP server on localhost. Any local process with the "
        "session token can edit the open project and capture frames.</p>"
        "<p><strong>Workflow:</strong> catalog or search({q}) → toolbox({name}) → "
        "apply({ops:[{tool,args}…]}); activity → frames → capture to see the footage.</p>"
        "<p>Agents: POST JSON-RPC to <code>/mcp</code> with "
        "<code>Authorization: Bearer …</code>.</p>"
        "<h2>Toolboxes</h2><ul>");
    const QJsonArray boxes = cat.value(QStringLiteral("toolboxes")).toArray();
    for (const QJsonValue &v : boxes) {
        const QJsonObject b = v.toObject();
        QStringList names;
        for (const QJsonValue &op : b.value(QStringLiteral("ops")).toArray())
            names.append(op.toString());
        body += QStringLiteral("<li><strong>%1</strong> — %2<br><code>%3</code></li>")
                    .arg(b.value(QStringLiteral("name")).toString(),
                         b.value(QStringLiteral("when")).toString(),
                         names.join(QStringLiteral(", ")));
    }
    QStringList endpointStrings;
    for (const QJsonValue &ep : cat.value(QStringLiteral("endpoints")).toArray())
        endpointStrings.append(QStringLiteral("<code>%1</code>").arg(ep.toString()));
    body += QStringLiteral("</ul><p>Pinned endpoints: %1.</p></body>").arg(endpointStrings.join(QStringLiteral(", ")));
    return body;
}

} // namespace drift::mcp
