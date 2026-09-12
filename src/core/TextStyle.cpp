#include "TextStyle.h"

#include "Effect.h"
#include "TextAnimationPreset.h"
#include "TextLook.h"
#include "TextPresetStore.h"

#include <QCoreApplication>
#include <QJsonArray>

namespace drift {

QString textAlignToString(TextAlign align)
{
    switch (align) {
    case TextAlign::Left:
        return QStringLiteral("left");
    case TextAlign::Right:
        return QStringLiteral("right");
    case TextAlign::Center:
        return QStringLiteral("center");
    }
    return QStringLiteral("center");
}

TextAlign textAlignFromString(const QString &align)
{
    if (align == QStringLiteral("left"))
        return TextAlign::Left;
    if (align == QStringLiteral("right"))
        return TextAlign::Right;
    return TextAlign::Center;
}

QString textVAlignToString(TextVAlign valign)
{
    switch (valign) {
    case TextVAlign::Top:
        return QStringLiteral("top");
    case TextVAlign::Bottom:
        return QStringLiteral("bottom");
    case TextVAlign::Middle:
        return QStringLiteral("center");
    }
    return QStringLiteral("center");
}

TextVAlign textVAlignFromString(const QString &valign)
{
    if (valign == QStringLiteral("top"))
        return TextVAlign::Top;
    if (valign == QStringLiteral("bottom"))
        return TextVAlign::Bottom;
    return TextVAlign::Middle;
}

QString wordAccentRuleToString(WordAccentRule rule)
{
    switch (rule) {
    case WordAccentRule::FirstWord:
        return QStringLiteral("firstWord");
    case WordAccentRule::LastWord:
        return QStringLiteral("lastWord");
    case WordAccentRule::EveryOther:
        return QStringLiteral("everyOther");
    case WordAccentRule::EveryNth:
        return QStringLiteral("everyNth");
    case WordAccentRule::LongestWord:
        return QStringLiteral("longestWord");
    case WordAccentRule::RandomStable:
        return QStringLiteral("randomStable");
    case WordAccentRule::Karaoke:
        return QStringLiteral("karaoke");
    case WordAccentRule::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

WordAccentRule wordAccentRuleFromString(const QString &rule)
{
    if (rule == QStringLiteral("firstWord"))
        return WordAccentRule::FirstWord;
    if (rule == QStringLiteral("lastWord"))
        return WordAccentRule::LastWord;
    if (rule == QStringLiteral("everyOther"))
        return WordAccentRule::EveryOther;
    if (rule == QStringLiteral("everyNth"))
        return WordAccentRule::EveryNth;
    if (rule == QStringLiteral("longestWord"))
        return WordAccentRule::LongestWord;
    if (rule == QStringLiteral("randomStable"))
        return WordAccentRule::RandomStable;
    if (rule == QStringLiteral("karaoke"))
        return WordAccentRule::Karaoke;
    return WordAccentRule::None;
}

// ---------------------------------------------------------------------------------------------
// Accessors

QColor TextStyle::primaryColor() const
{
    const TextShadingLayer *fill = firstTextLayerOfKind(layers, TextLayerKind::Fill, true);
    if (!fill)
        fill = firstTextLayerOfKind(layers, TextLayerKind::Fill, false);
    if (!fill)
        return Qt::white;
    if (fill->paint.kind == TextPaintKind::Gradient && !fill->paint.gradient.stops.isEmpty())
        return fill->paint.gradient.stops.first().color;
    return fill->paint.color;
}

void TextStyle::setPrimaryColor(const QColor &color)
{
    TextShadingLayer *fill = firstTextLayerOfKind(layers, TextLayerKind::Fill, true);
    if (!fill)
        fill = firstTextLayerOfKind(layers, TextLayerKind::Fill, false);
    if (!fill) {
        layers.append(solidFillLayer(color, mintTextLayerId(layers)));
        return;
    }
    if (fill->paint.kind == TextPaintKind::Gradient && !fill->paint.gradient.stops.isEmpty())
        fill->paint.gradient.stops.first().color = color;
    else
        fill->paint.color = color;
}

QColor textFillColor(const TextStyle &style, bool accent)
{
    if (accent && style.accent.colorEnabled)
        return style.accent.color;
    return style.primaryColor();
}

double textStrokeWidth(const TextStyle &style, bool accent)
{
    if (accent && style.accent.outlineEnabled)
        return style.accent.outlineWidth;
    const TextShadingLayer *stroke = firstTextLayerOfKind(style.layers, TextLayerKind::Stroke, true);
    return stroke ? stroke->width : 0.0;
}

QColor textStrokeColor(const TextStyle &style, bool accent)
{
    if (accent && style.accent.outlineEnabled)
        return style.accent.outlineColor;
    const TextShadingLayer *stroke = firstTextLayerOfKind(style.layers, TextLayerKind::Stroke, true);
    return stroke ? stroke->paint.color : QColor(Qt::black);
}

void setSolidFill(TextStyle &style, const QColor &color)
{
    TextShadingLayer *fill = firstTextLayerOfKind(style.layers, TextLayerKind::Fill, false);
    if (!fill) {
        style.layers.append(solidFillLayer(color, mintTextLayerId(style.layers)));
        return;
    }
    fill->enabled = true;
    fill->paint = TextPaint{};
    fill->paint.color = color;
}

// ---------------------------------------------------------------------------------------------
// Built-in packs

namespace {

// The legacy decorations as layers, in the order they were painted: shadow, glow, outline, fill.
TextShadingLayer legacyShadow(bool enabled = true, const QColor &color = QColor(0, 0, 0), double offsetX = 0.0,
                              double offsetY = 4.0, double blur = 8.0, double opacity = 0.6)
{
    TextShadingLayer layer = shadowLayer(color, offsetX, offsetY, blur, opacity);
    layer.enabled = enabled;
    return layer;
}

TextShadingLayer legacyGlow(bool enabled = true, const QColor &color = QColor(255, 255, 255), double radius = 18.0,
                            double opacity = 0.8)
{
    TextShadingLayer layer = glowLayer(color, radius, opacity);
    layer.enabled = enabled;
    return layer;
}

TextShadingLayer legacyStroke(double width, const QColor &color = QColor(Qt::black), bool enabled = true)
{
    TextShadingLayer layer = strokeLayer(width, color);
    layer.enabled = enabled;
    return layer;
}

TextShadingLayer gradientFillLayer(const char *gradientPresetId, double angle,
                                   TextGradientSpace space = TextGradientSpace::Block, double offsetSpeed = 0.0,
                                   bool repeat = false, const QString &id = QStringLiteral("fill"))
{
    TextShadingLayer layer = solidFillLayer(Qt::white, id);
    layer.paint.kind = TextPaintKind::Gradient;
    for (const TextGradientPreset &preset : textGradientPresets())
        if (preset.id == QLatin1String(gradientPresetId))
            layer.paint.gradient = preset.gradient;
    layer.paint.gradient.angle = angle;
    layer.paint.gradient.space = space;
    layer.paint.gradient.offsetSpeed = offsetSpeed;
    layer.paint.gradient.repeat = repeat;
    return layer;
}

TextShadingLayer effectLayer(const char *effectId, const QMap<QString, VectorSlotValue> &params,
                             const QColor &tint = Qt::white, BlendMode blend = BlendMode::Normal,
                             const QString &id = QStringLiteral("fill"))
{
    TextShadingLayer layer = solidFillLayer(tint, id);
    layer.paint.kind = TextPaintKind::Effect;
    layer.paint.effect.id = QLatin1String(effectId);
    layer.paint.effect.params = params;
    layer.blend = blend;
    return layer;
}

TextShadingLayer extrudeLayer(const QColor &color, double depth, double angle = 45.0, int steps = 8,
                              double darken = 0.5)
{
    TextShadingLayer layer = solidFillLayer(color, QStringLiteral("extrude"));
    layer.kind = TextLayerKind::Extrude;
    layer.width = depth;
    layer.extrudeSteps = steps;
    layer.extrudeAngle = angle;
    layer.extrudeDarken = darken;
    return layer;
}

VectorSlotValue num(double v) { return VectorSlotValue::fromScalar(v); }
VectorSlotValue txt(const char *v) { return VectorSlotValue::fromText(QLatin1String(v)); }
VectorSlotValue col(const QColor &c) { return VectorSlotValue::fromColor(c); }

TextAnimationSlot presetSlot(const char *presetId, double durationS, const char *ease,
                             const QMap<QString, VectorSlotValue> &extra = {})
{
    TextAnimationSlot slot;
    slot.presetId = QLatin1String(presetId);
    slot.params = extra;
    slot.params.insert(QStringLiteral("duration"), num(durationS));
    slot.params.insert(QStringLiteral("ease"), txt(ease));
    return slot;
}

// Loop presets carry their own speed / amount params and no duration or ease.
TextAnimationSlot loopSlot(const char *presetId, const QMap<QString, VectorSlotValue> &params = {})
{
    TextAnimationSlot slot;
    slot.presetId = QLatin1String(presetId);
    slot.params = params;
    return slot;
}

QList<TextPreset> buildPresets()
{
    QList<TextPreset> presets;
    const auto add = [&](const char *id, const char *label, const TextStyle &style, const char *sample) {
        presets.append({QLatin1String(id), QCoreApplication::translate("TextStyle", label), style,
                        QCoreApplication::translate("TextStyle", sample)});
    };
    const auto byWord = [] { return QMap<QString, VectorSlotValue>{{QStringLiteral("unit"), txt("word")}}; };
    const auto byChar = [] { return QMap<QString, VectorSlotValue>{{QStringLiteral("unit"), txt("character")}}; };

    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 96;
        s.fontWeight = 800;
        s.layers = {legacyShadow(true, QColor(0, 0, 0), 0.0, 6.0, 16.0, 0.45), legacyStroke(2.0),
                    gradientFillLayer("mono", 90.0)};
        s.animation.in = presetSlot("rise", 0.5, "easeOut", byWord());
        s.animation.out = presetSlot("fade-scale", 0.4, "easeIn");
        add("title", "Title", s, "Main Title");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Inter");
        s.pixelSize = 40;
        s.fontWeight = 500;
        s.valign = TextVAlign::Bottom;
        s.boxEnabled = true;
        s.boxColor = QColor(0, 0, 0, 140);
        s.boxPadding = 10.0;
        s.boxRadius = 6.0;
        s.animation.in = presetSlot("fade", 0.3, "easeOut");
        s.animation.out = presetSlot("fade", 0.25, "easeIn");
        add("subtitle", "Subtitle", s, "A supporting line");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Poppins");
        s.pixelSize = 48;
        s.fontWeight = 700;
        s.align = TextAlign::Left;
        s.valign = TextVAlign::Bottom;
        s.boxEnabled = true;
        s.boxColor = QColor(0, 0, 0, 160);
        s.boxPadding = 12.0;
        s.animation.in = presetSlot("slide-right", 0.5, "easeOut");
        s.animation.out = presetSlot("slide-left", 0.4, "easeInOut");
        add("lower-third", "Lower third", s, "Alex Rivera · Host");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Oswald");
        s.pixelSize = 44;
        s.fontWeight = 600;
        s.valign = TextVAlign::Bottom;
        s.layers = {legacyShadow(), legacyStroke(3.0), solidFillLayer(Qt::white)};
        s.animation.in = presetSlot("pop", 0.25, "back", {{QStringLiteral("unit"), txt("word")}, {QStringLiteral("stagger"), num(0.03)}});
        s.animation.out = presetSlot("fade", 0.15, "easeIn");
        add("caption", "Caption", s, "Watch until the end");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Playfair Display");
        s.pixelSize = 64;
        s.fontWeight = 500;
        s.italic = true;
        s.lineHeight = 1.4;
        s.layers = {legacyGlow(true, QColor(255, 255, 255), 14.0, 0.35), gradientFillLayer("mono", 90.0)};
        s.animation.in = presetSlot("blur-in-words", 0.7, "easeOut");
        s.animation.out = presetSlot("fade-shift", 0.5, "easeIn");
        add("quote", "Quote", s, "Words worth keeping");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Anton");
        s.pixelSize = 120;
        s.fontWeight = 400;
        s.letterSpacing = 2.0;
        s.layers = {legacyShadow(true, QColor(0, 0, 0), 0.0, 8.0, 14.0), extrudeLayer(QColor(30, 30, 30), 10.0),
                    legacyStroke(4.0), gradientFillLayer("fire", 90.0)};
        s.animation.in = presetSlot("stamp", 0.35, "easeIn");
        s.animation.out = presetSlot("fade-scale", 0.4, "easeIn");
        add("impact", "Impact", s, "STOP SCROLLING");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Fredoka");
        s.pixelSize = 80;
        s.fontWeight = 600;
        TextShadingLayer outer = strokeLayer(9.0, Qt::black, QStringLiteral("outline"));
        s.layers = {outer, strokeLayer(5.0, Qt::white), gradientFillLayer("candy", 0.0, TextGradientSpace::Word)};
        s.animation.in = presetSlot("bounce", 0.6, "bounce", byChar());
        s.animation.out = presetSlot("shrink", 0.3, "easeIn");
        s.animation.loop = loopSlot("wave", {{QStringLiteral("amount"), num(0.04)}});
        add("pop", "Pop", s, "Big news!");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Bebas Neue");
        s.pixelSize = 110;
        s.fontWeight = 400;
        s.letterSpacing = 4.0;
        const QColor cyan(0, 220, 255);
        TextShadingLayer fill = effectLayer("neon-pulse", {{QStringLiteral("speed"), num(1.2)}, {QStringLiteral("intensity"), num(0.35)},
                                                           {QStringLiteral("flicker"), num(0.25)}, {QStringLiteral("colorA"), col(cyan)}},
                                            QColor(235, 255, 255));
        s.layers = {glowLayer(cyan, 40.0, 0.5, QStringLiteral("glowouter")), glowLayer(cyan, 14.0, 0.9),
                    strokeLayer(2.0, cyan), fill};
        s.animation.in = presetSlot("flicker-in", 0.9, "linear");
        s.animation.out = presetSlot("fade", 0.3, "easeIn");
        add("neon", "Neon", s, "NEON NIGHTS");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Pacifico");
        s.pixelSize = 72;
        s.fontWeight = 400;
        s.lineHeight = 1.35;
        s.layers = {legacyShadow(true, QColor(0, 0, 0), 0.0, 4.0, 6.0), solidFillLayer(Qt::white)};
        s.animation.in = presetSlot("typewriter", 0.4, "linear", {{QStringLiteral("stagger"), num(0.07)}});
        s.animation.out = presetSlot("fade", 0.4, "easeIn");
        add("handwritten", "Handwritten", s, "With love");
    }

    // Short-form caption packs. Unlike the presets above these carry a per-word accent rule, so the
    // pack itself decides which words are recoloured, highlighted or scaled up.
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Anton");
        s.pixelSize = 96;
        s.fontWeight = 400;
        s.layers = {legacyShadow(true, QColor(0, 0, 0), 0.0, 6.0, 10.0), legacyStroke(5.0), solidFillLayer(Qt::white)};
        s.accent.rule = WordAccentRule::FirstWord;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 45, 45);
        s.animation.in = presetSlot("pop", 0.3, "back", byWord());
        s.animation.out = presetSlot("fade", 0.15, "easeIn");
        add("hormozi", "Hormozi", s, "Stop wasting time");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 84;
        s.fontWeight = 800;
        s.layers = {legacyShadow(), legacyStroke(3.0), solidFillLayer(Qt::white)};
        s.accent.rule = WordAccentRule::EveryNth;
        s.accent.n = 3;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 59, 48);
        s.animation.in = presetSlot("fade-up-char", 0.35, "easeOut");
        s.animation.out = presetSlot("fade", 0.2, "easeIn");
        add("one-word-color", "One word colour", s, "Make every word count");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Inter");
        s.pixelSize = 80;
        s.fontWeight = 800;
        s.layers = {legacyStroke(2.0), solidFillLayer(Qt::white)};
        s.accent.rule = WordAccentRule::EveryOther;
        s.accent.highlight.enabled = true;
        s.accent.highlight.color = QColor(230, 40, 40);
        s.accent.highlight.padding = 8.0;
        s.accent.highlight.radius = 6.0;
        s.animation.in = presetSlot("slide-up", 0.4, "easeOut", byWord());
        s.animation.out = presetSlot("fade", 0.2, "easeIn");
        add("word-background", "Word background", s, "Highlight what matters most");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 76;
        s.fontWeight = 800;
        s.layers = {solidFillLayer(QColor(20, 20, 20))};
        s.boxEnabled = true;
        s.boxColor = QColor(255, 196, 0);
        s.boxPadding = 14.0;
        s.boxRadius = 10.0;
        s.animation.in = presetSlot("wipe", 0.4, "easeOut");
        s.animation.out = presetSlot("wipe", 0.3, "easeIn");
        add("sentence-background", "Sentence background", s, "Read this carefully");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("League Spartan");
        s.pixelSize = 88;
        s.fontWeight = 900;
        s.layers = {legacyShadow(), legacyStroke(4.0), solidFillLayer(Qt::white)};
        s.accent.rule = WordAccentRule::Karaoke;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 212, 0);
        s.accent.sizeScale = 1.12;
        s.animation.in = presetSlot("fade", 0.2, "easeOut");
        s.animation.out = presetSlot("fade", 0.2, "easeIn");
        s.animation.loop = loopSlot("karaoke-pop");
        add("karaoke-pop", "Karaoke pop", s, "Sing along with me");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Inter");
        s.pixelSize = 78;
        s.fontWeight = 800;
        s.layers = {legacyStroke(2.0), solidFillLayer(Qt::white)};
        s.accent.rule = WordAccentRule::Karaoke;
        s.accent.highlight.enabled = true;
        s.accent.highlight.color = QColor(34, 197, 94);
        s.accent.highlight.padding = 8.0;
        s.accent.highlight.radius = 8.0;
        s.animation.in = presetSlot("fade", 0.2, "easeOut");
        s.animation.out = presetSlot("fade", 0.2, "easeIn");
        s.animation.loop = loopSlot("karaoke-bounce");
        add("karaoke-highlight", "Karaoke highlight", s, "Follow the bouncing words");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 76;
        s.fontWeight = 800;
        s.italic = true;
        s.layers = {legacyGlow(true, QColor(255, 255, 255), 20.0, 0.9),
                    gradientFillLayer("holo", 20.0, TextGradientSpace::Glyph, 0.15, true)};
        s.animation.in = presetSlot("blur-in", 0.6, "easeOut");
        s.animation.out = presetSlot("fade-scale", 0.5, "easeIn");
        s.animation.loop = loopSlot("breathe");
        add("mirage", "Mirage", s, "Soft and dreamy");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Archivo Black");
        s.pixelSize = 80;
        s.fontWeight = 400;
        s.layers = {legacyStroke(2.0), solidFillLayer(Qt::white)};
        s.underlineEnabled = true;
        s.underlineColor = QColor(230, 40, 40);
        s.underlineWidth = 8.0;
        s.underlineOffset = 8.0;
        s.animation.in = presetSlot("slide-right", 0.4, "easeOut");
        s.animation.out = presetSlot("slide-left", 0.35, "easeIn");
        add("underline", "Underline", s, "Underline this line");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Archivo Black");
        s.pixelSize = 78;
        s.fontWeight = 400;
        s.align = TextAlign::Left;
        s.wordHighlight.enabled = true;
        s.wordHighlight.color = QColor(0, 0, 0, 235);
        s.wordHighlight.padding = 8.0;
        s.wordHighlight.radius = 2.0;
        s.accent.rule = WordAccentRule::FirstWord;
        s.accent.sizeScale = 1.35;
        s.animation.in = presetSlot("drop", 0.5, "bounce");
        s.animation.out = presetSlot("fall", 0.5, "easeIn");
        add("bulky", "Bulky", s, "Big first word");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 82;
        s.fontWeight = 900;
        // Hollow by default; the accent words are the solid ones.
        s.layers = {legacyStroke(3.0, QColor(255, 255, 255)), solidFillLayer(QColor(255, 255, 255, 0))};
        s.accent.rule = WordAccentRule::EveryOther;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 255, 255);
        s.animation.in = presetSlot("converge", 0.6, "easeOut");
        s.animation.out = presetSlot("fade-scale", 0.4, "easeIn");
        add("word-outline", "Word outline", s, "Outline every other word");
    }

    // Showpiece packs: gradients, shader effects, extrusions and arcs on top of the layer stack.
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Cinzel");
        s.pixelSize = 84;
        s.fontWeight = 700;
        s.letterSpacing = 6.0;
        TextShadingLayer sheen = effectLayer("shine", {{QStringLiteral("speed"), num(0.4)}, {QStringLiteral("width"), num(0.2)},
                                                       {QStringLiteral("angle"), num(20.0)}, {QStringLiteral("intensity"), num(0.9)}},
                                             QColor(0, 0, 0, 0), BlendMode::Screen, QStringLiteral("shine"));
        s.layers = {legacyShadow(true, QColor(0, 0, 0), 0.0, 6.0, 14.0, 0.5), strokeLayer(1.0, QColor(120, 90, 20)),
                    gradientFillLayer("gold", 90.0), sheen};
        s.animation.in = presetSlot("fade", 0.6, "easeOut");
        s.animation.out = presetSlot("fade", 0.5, "easeIn");
        add("gold-luxe", "Gold", s, "GOLD STANDARD");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Rubik One");
        s.pixelSize = 96;
        s.fontWeight = 400;
        s.layers = {legacyShadow(true, QColor(0, 0, 0), 0.0, 5.0, 8.0, 0.5), strokeLayer(1.0, QColor(42, 42, 42)),
                    effectLayer("chrome", {{QStringLiteral("speed"), num(0.2)}, {QStringLiteral("bands"), num(2.0)},
                                           {QStringLiteral("colorA"), col(QColor(240, 245, 255))},
                                           {QStringLiteral("colorB"), col(QColor(40, 50, 70))}})};
        s.animation.in = presetSlot("zoom-out", 0.5, "easeOut");
        s.animation.out = presetSlot("fade-scale", 0.4, "easeIn");
        add("chrome", "Chrome", s, "CHROME");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Rubik One");
        s.pixelSize = 100;
        s.fontWeight = 400;
        s.layers = {extrudeLayer(QColor(60, 30, 90), 16.0), legacyStroke(3.0), gradientFillLayer("sunset", 90.0)};
        s.animation.in = presetSlot("drop", 0.5, "bounce", byWord());
        s.animation.out = presetSlot("fall", 0.5, "easeIn");
        add("retro-3d", "Retro 3D", s, "TOTALLY RAD");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("DM Sans");
        s.pixelSize = 88;
        s.fontWeight = 800;
        TextShadingLayer a = solidFillLayer(QColor(255, 0, 255), QStringLiteral("glitcha"));
        a.offsetX = -4.0;
        a.blend = BlendMode::Screen;
        a.opacity = 0.8;
        TextShadingLayer b = solidFillLayer(QColor(0, 255, 255), QStringLiteral("glitchb"));
        b.offsetX = 4.0;
        b.blend = BlendMode::Screen;
        b.opacity = 0.8;
        s.layers = {a, b, effectLayer("glitch", {{QStringLiteral("speed"), num(6.0)}, {QStringLiteral("amount"), num(10.0)},
                                                 {QStringLiteral("block"), num(8.0)}})};
        s.animation.in = presetSlot("glitch-in", 0.3, "linear");
        s.animation.out = presetSlot("glitch-in", 0.3, "linear");
        s.animation.loop = loopSlot("jitter", {{QStringLiteral("amount"), num(0.8)}, {QStringLiteral("angle"), num(0.0)}});
        add("glitch", "Glitch", s, "SIGNAL LOST");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Fredoka");
        s.pixelSize = 92;
        s.fontWeight = 700;
        s.layers = {extrudeLayer(Qt::black, 8.0), strokeLayer(10.0, Qt::white, QStringLiteral("outline")),
                    strokeLayer(6.0, Qt::black), solidFillLayer(QColor(255, 214, 64))};
        s.animation.in = presetSlot("stamp", 0.35, "easeIn");
        s.animation.out = presetSlot("pop", 0.3, "easeIn");
        s.animation.loop = loopSlot("sway");
        add("comic", "Comic", s, "KAPOW!");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Anton");
        s.pixelSize = 110;
        s.fontWeight = 400;
        s.letterSpacing = 2.0;
        s.layers = {glowLayer(QColor(255, 120, 0), 22.0, 0.8), gradientFillLayer("fire", 90.0)};
        s.animation.in = presetSlot("rise", 0.5, "easeOut", {{QStringLiteral("unit"), txt("character")}, {QStringLiteral("order"), txt("random")}});
        s.animation.out = presetSlot("fade-scale", 0.4, "easeIn");
        s.animation.loop = loopSlot("flicker", {{QStringLiteral("intensity"), num(0.15)}});
        add("fire", "Fire", s, "ON FIRE");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Bebas Neue");
        s.pixelSize = 110;
        s.fontWeight = 400;
        s.letterSpacing = 4.0;
        s.layers = {glowLayer(Qt::white, 18.0, 0.7), strokeLayer(2.0, QColor(140, 210, 255)), gradientFillLayer("ice", 90.0)};
        s.animation.in = presetSlot("converge", 0.7, "easeOut");
        s.animation.out = presetSlot("fade-scale", 0.5, "easeIn");
        add("ice", "Ice", s, "FROZEN");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Fredoka");
        s.pixelSize = 84;
        s.fontWeight = 600;
        s.layers = {strokeLayer(6.0, QColor(255, 110, 199)), gradientFillLayer("candy", 0.0, TextGradientSpace::Word)};
        s.animation.in = presetSlot("bounce", 0.6, "bounce", byChar());
        s.animation.out = presetSlot("shrink", 0.3, "easeIn");
        s.animation.loop = loopSlot("wave", {{QStringLiteral("amount"), num(0.05)}});
        add("candy", "Candy", s, "Sweet treats");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Plus Jakarta Sans");
        s.pixelSize = 80;
        s.fontWeight = 800;
        s.pathBend = 30.0;
        s.layers = {legacyShadow(true, QColor(0, 0, 0), 0.0, 4.0, 0.0, 0.35), strokeLayer(14.0, Qt::white, QStringLiteral("outline")),
                    strokeLayer(4.0, QColor(255, 59, 48)), solidFillLayer(Qt::white)};
        s.animation.in = presetSlot("spin-in", 0.5, "back");
        s.animation.out = presetSlot("shrink", 0.3, "easeIn");
        add("sticker", "Sticker", s, "Sticker!");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Poppins");
        s.pixelSize = 92;
        s.fontWeight = 900;
        s.layers = {legacyStroke(3.0), gradientFillLayer("rainbow", 0.0, TextGradientSpace::Glyph, 0.25, true)};
        s.animation.in = presetSlot("rain", 0.5, "easeOut");
        s.animation.out = presetSlot("fade-scale", 0.4, "easeIn");
        s.animation.loop = loopSlot("wave");
        add("rainbow", "Rainbow wave", s, "Taste the rainbow");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Cormorant Garamond");
        s.pixelSize = 72;
        s.fontWeight = 500;
        s.letterSpacing = 14.0;
        s.layers = {legacyGlow(true, QColor(255, 255, 255), 12.0, 0.25), gradientFillLayer("mono", 90.0)};
        s.animation.in = presetSlot("snap-together", 1.0, "easeOut");
        s.animation.out = presetSlot("fade-scale", 0.8, "easeIn");
        s.animation.loop = loopSlot("tracking-drift");
        add("cinematic", "Cinematic", s, "THE BEGINNING");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Outfit");
        s.pixelSize = 88;
        s.fontWeight = 800;
        TextShadingLayer shimmer = effectLayer("shimmer", {{QStringLiteral("speed"), num(0.3)}, {QStringLiteral("scale"), num(3.0)},
                                                           {QStringLiteral("intensity"), num(0.6)}},
                                               Qt::white, BlendMode::Screen, QStringLiteral("shimmer"));
        shimmer.opacity = 0.7;
        s.layers = {glowLayer(Qt::white, 10.0, 0.4), gradientFillLayer("holo", 20.0, TextGradientSpace::Glyph, 0.1, true), shimmer};
        s.animation.in = presetSlot("flip", 0.45, "back");
        s.animation.out = presetSlot("flip", 0.35, "easeIn");
        s.animation.loop = loopSlot("float");
        add("holo-shimmer", "Shimmer", s, "Shimmer");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Ubuntu");
        s.pixelSize = 84;
        s.fontWeight = 700;
        TextShadingLayer stroke = strokeLayer(3.0, Qt::white);
        stroke.sketchLength = 6.0;
        stroke.sketchDeviation = 2.0;
        stroke.dash = StrokeDash::Dash;
        TextShadingLayer fill = solidFillLayer(Qt::white);
        fill.enabled = false;
        s.layers = {stroke, fill};
        s.animation.in = presetSlot("typewriter", 0.4, "linear", {{QStringLiteral("stagger"), num(0.06)}});
        s.animation.out = presetSlot("fade", 0.3, "easeIn");
        s.animation.loop = loopSlot("jitter", {{QStringLiteral("amount"), num(0.5)}, {QStringLiteral("angle"), num(0.5)}, {QStringLiteral("speed"), num(6.0)}});
        add("sketch", "Sketch", s, "Rough draft");
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Bodoni Moda");
        s.pixelSize = 80;
        s.fontWeight = 600;
        s.italic = true;
        s.lineHeight = 1.3;
        s.underlineEnabled = true;
        s.underlineColor = QColor(255, 255, 255, 180);
        s.underlineWidth = 2.0;
        s.underlineOffset = 10.0;
        s.animation.in = presetSlot("rise-by-word", 0.5, "easeOut");
        s.animation.out = presetSlot("remove-by-word", 0.4, "easeIn");
        add("editorial", "Editorial", s, "The quiet issue");
    }

    return presets;
}

} // namespace

const QList<TextPreset> &textPresets()
{
    static const QList<TextPreset> presets = buildPresets();
    return presets;
}

std::optional<TextPreset> textPresetForId(const QString &id)
{
    if (isUserTextPresetId(id))
        return TextPresetStore::instance().presetForId(id);
    for (const TextPreset &preset : textPresets()) {
        if (preset.id == id)
            return preset;
    }
    return std::nullopt;
}

std::optional<TextStyle> textStyleForPresetId(const QString &id)
{
    const std::optional<TextPreset> preset = textPresetForId(id);
    if (!preset)
        return std::nullopt;
    return preset->style;
}

// ---------------------------------------------------------------------------------------------
// Highlight / accent JSON

QJsonObject textHighlightToJson(const TextHighlight &h)
{
    return QJsonObject{
        {QStringLiteral("enabled"), h.enabled},
        {QStringLiteral("color"), h.color.name(QColor::HexArgb)},
        {QStringLiteral("padding"), h.padding},
        {QStringLiteral("radius"), h.radius},
    };
}

TextHighlight textHighlightFromJson(const QJsonObject &o, const TextHighlight &fallback)
{
    TextHighlight h = fallback;
    if (o.isEmpty())
        return h;
    h.enabled = o.value(QStringLiteral("enabled")).toBool(h.enabled);
    h.color = QColor(o.value(QStringLiteral("color")).toString(h.color.name(QColor::HexArgb)));
    h.padding = o.value(QStringLiteral("padding")).toDouble(h.padding);
    h.radius = o.value(QStringLiteral("radius")).toDouble(h.radius);
    return h;
}

QJsonObject wordAccentToJson(const WordAccent &a)
{
    return QJsonObject{
        {QStringLiteral("rule"), wordAccentRuleToString(a.rule)},
        {QStringLiteral("n"), a.n},
        {QStringLiteral("phase"), a.phase},
        {QStringLiteral("colorEnabled"), a.colorEnabled},
        {QStringLiteral("color"), a.color.name(QColor::HexArgb)},
        {QStringLiteral("sizeScale"), a.sizeScale},
        {QStringLiteral("outlineEnabled"), a.outlineEnabled},
        {QStringLiteral("outlineWidth"), a.outlineWidth},
        {QStringLiteral("outlineColor"), a.outlineColor.name(QColor::HexArgb)},
        {QStringLiteral("highlight"), textHighlightToJson(a.highlight)},
    };
}

WordAccent wordAccentFromJson(const QJsonObject &o)
{
    WordAccent a;
    if (o.isEmpty())
        return a; // projects predating style packs: no accent at all
    a.rule = wordAccentRuleFromString(o.value(QStringLiteral("rule")).toString());
    a.n = o.value(QStringLiteral("n")).toInt(a.n);
    a.phase = o.value(QStringLiteral("phase")).toInt(a.phase);
    a.colorEnabled = o.value(QStringLiteral("colorEnabled")).toBool(a.colorEnabled);
    a.color = QColor(o.value(QStringLiteral("color")).toString(a.color.name(QColor::HexArgb)));
    a.sizeScale = o.value(QStringLiteral("sizeScale")).toDouble(a.sizeScale);
    a.outlineEnabled = o.value(QStringLiteral("outlineEnabled")).toBool(a.outlineEnabled);
    a.outlineWidth = o.value(QStringLiteral("outlineWidth")).toDouble(a.outlineWidth);
    a.outlineColor = QColor(o.value(QStringLiteral("outlineColor")).toString(a.outlineColor.name(QColor::HexArgb)));
    a.highlight = textHighlightFromJson(o.value(QStringLiteral("highlight")).toObject(), a.highlight);
    return a;
}

// ---------------------------------------------------------------------------------------------
// Keyframe keys

namespace {

const QStringList &flatKeyframeKeys()
{
    static const QStringList keys{QStringLiteral("pixelSize"), QStringLiteral("letterSpacing"),
                                  QStringLiteral("lineHeight"), QStringLiteral("boxPadding"),
                                  QStringLiteral("pathBend")};
    return keys;
}

// Legacy flat names → the layer path they map to on a migrated style.
const QMap<QString, QString> &legacyKeyAliases()
{
    static const QMap<QString, QString> aliases{
        {QStringLiteral("outlineWidth"), QStringLiteral("layer.stroke.width")},
        {QStringLiteral("shadowOffsetX"), QStringLiteral("layer.shadow.offsetX")},
        {QStringLiteral("shadowOffsetY"), QStringLiteral("layer.shadow.offsetY")},
        {QStringLiteral("shadowBlur"), QStringLiteral("layer.shadow.blur")},
        {QStringLiteral("shadowOpacity"), QStringLiteral("layer.shadow.opacity")},
        {QStringLiteral("glowRadius"), QStringLiteral("layer.glow.blur")},
        {QStringLiteral("glowOpacity"), QStringLiteral("layer.glow.opacity")},
        {QStringLiteral("gradientAngle"), QStringLiteral("layer.fill.gradient.angle")},
        {QStringLiteral("color.r"), QStringLiteral("layer.fill.color.r")},
        {QStringLiteral("color.g"), QStringLiteral("layer.fill.color.g")},
        {QStringLiteral("color.b"), QStringLiteral("layer.fill.color.b")},
        {QStringLiteral("color.a"), QStringLiteral("layer.fill.color.a")},
    };
    return aliases;
}

} // namespace

QStringList textKeyframeProperties(const TextStyle &s)
{
    QStringList out = flatKeyframeKeys();
    for (const TextShadingLayer &layer : s.layers) {
        for (const QString &field : shadingLayerKeyframeFields(layer))
            out.append(QStringLiteral("layer.%1.%2").arg(layer.id, field));
    }
    return out;
}

QString textKeyframeCanonicalKey(const QString &key, const TextStyle &s)
{
    if (flatKeyframeKeys().contains(key))
        return key;
    QString candidate = key;
    const auto alias = legacyKeyAliases().constFind(key);
    if (alias != legacyKeyAliases().constEnd())
        candidate = *alias;
    LayerKeyPath path;
    if (!parseLayerKey(candidate, &path))
        return {};
    const TextShadingLayer *layer = findTextLayer(s.layers, path.layerId);
    if (!layer)
        return {};
    double probe = 0.0;
    return shadingLayerScalar(*layer, path.field, &probe) ? candidate : QString();
}

QString textKeyframeLabel(const QString &key, const TextStyle &s)
{
    const QString canonical = textKeyframeCanonicalKey(key, s);
    if (canonical == QLatin1String("pixelSize"))
        return QCoreApplication::translate("TextStyle", "Text size");
    if (canonical == QLatin1String("letterSpacing"))
        return QCoreApplication::translate("TextStyle", "Letter spacing");
    if (canonical == QLatin1String("lineHeight"))
        return QCoreApplication::translate("TextStyle", "Line height");
    if (canonical == QLatin1String("boxPadding"))
        return QCoreApplication::translate("TextStyle", "Box padding");
    if (canonical == QLatin1String("pathBend"))
        return QCoreApplication::translate("TextStyle", "Bend");
    return shadingLayerKeyframeLabel(s.layers, canonical.isEmpty() ? key : canonical);
}

bool textStyleScalar(const TextStyle &s, const QString &rawKey, double *out)
{
    const QString key = textKeyframeCanonicalKey(rawKey, s);
    if (key.isEmpty())
        return false;
    if (key == QStringLiteral("pixelSize"))
        *out = s.pixelSize;
    else if (key == QStringLiteral("letterSpacing"))
        *out = s.letterSpacing;
    else if (key == QStringLiteral("lineHeight"))
        *out = s.lineHeight;
    else if (key == QStringLiteral("boxPadding"))
        *out = s.boxPadding;
    else if (key == QStringLiteral("pathBend"))
        *out = s.pathBend;
    else {
        LayerKeyPath path;
        parseLayerKey(key, &path);
        return shadingLayerScalar(*findTextLayer(s.layers, path.layerId), path.field, out);
    }
    return true;
}

bool setTextStyleScalar(TextStyle &s, const QString &rawKey, double value)
{
    const QString key = textKeyframeCanonicalKey(rawKey, s);
    if (key.isEmpty())
        return false;
    if (key == QStringLiteral("pixelSize"))
        s.pixelSize = qMax(1, qRound(value));
    else if (key == QStringLiteral("letterSpacing"))
        s.letterSpacing = value;
    else if (key == QStringLiteral("lineHeight"))
        s.lineHeight = value;
    else if (key == QStringLiteral("boxPadding"))
        s.boxPadding = qMax(0.0, value);
    else if (key == QStringLiteral("pathBend"))
        s.pathBend = qBound(-100.0, value, 100.0);
    else {
        LayerKeyPath path;
        parseLayerKey(key, &path);
        return setShadingLayerScalar(*findTextLayer(s.layers, path.layerId), path.field, value);
    }
    return true;
}

bool TextStyle::isAnimated() const
{
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

TextStyle TextStyle::resolvedAt(TimeUs clipTimeUs) const
{
    TextStyle out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            setTextStyleScalar(out, it.key(), it->evaluateAt(clipTimeUs));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// JSON

QJsonObject textStyleToJson(const TextStyle &s)
{
    QJsonObject keyframesJson;
    for (auto it = s.keyframes.constBegin(); it != s.keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            keyframesJson.insert(it.key(), keyframesToJson(it.value()));
    }
    QJsonArray layers;
    for (const TextShadingLayer &layer : s.layers)
        layers.append(textShadingLayerToJson(layer));
    QJsonObject lookParams;
    for (auto it = s.lookParams.constBegin(); it != s.lookParams.constEnd(); ++it)
        lookParams.insert(it.key(), it->toJson());
    QJsonObject json{
        {QStringLiteral("packId"), s.packId},
        {QStringLiteral("fontFamily"), s.fontFamily},
        {QStringLiteral("pixelSize"), s.pixelSize},
        {QStringLiteral("fontWeight"), s.fontWeight},
        {QStringLiteral("italic"), s.italic},
        {QStringLiteral("layers"), layers},
        {QStringLiteral("pathBend"), s.pathBend},
        {QStringLiteral("align"), textAlignToString(s.align)},
        {QStringLiteral("valign"), textVAlignToString(s.valign)},
        {QStringLiteral("wordWrap"), s.wordWrap},
        {QStringLiteral("lineHeight"), s.lineHeight},
        {QStringLiteral("letterSpacing"), s.letterSpacing},
        {QStringLiteral("boxEnabled"), s.boxEnabled},
        {QStringLiteral("boxColor"), s.boxColor.name(QColor::HexArgb)},
        {QStringLiteral("boxPadding"), s.boxPadding},
        {QStringLiteral("boxRadius"), s.boxRadius},
        {QStringLiteral("wordHighlight"), textHighlightToJson(s.wordHighlight)},
        {QStringLiteral("underlineEnabled"), s.underlineEnabled},
        {QStringLiteral("underlineColor"), s.underlineColor.name(QColor::HexArgb)},
        {QStringLiteral("underlineWidth"), s.underlineWidth},
        {QStringLiteral("underlineOffset"), s.underlineOffset},
        {QStringLiteral("accent"), wordAccentToJson(s.accent)},
        {QStringLiteral("animation"), textAnimationSetToJson(s.animation)},
    };
    if (!s.lookId.isEmpty()) {
        json.insert(QStringLiteral("lookId"), s.lookId);
        json.insert(QStringLiteral("lookParams"), lookParams);
    }
    // Only animated styles carry the key: projects without text animation stay byte-identical.
    if (!keyframesJson.isEmpty())
        json.insert(QStringLiteral("keyframes"), keyframesJson);
    return json;
}

namespace {

// The v6 flat look, migrated into the four well-known layers in their painting order. Disabled
// layers are still created so toggling one back on restores the old width or blur.
QList<TextShadingLayer> legacyLayersFromJson(const QJsonObject &o)
{
    const auto color = [&](const char *key, const QColor &fallback) {
        return QColor(o.value(QLatin1String(key)).toString(fallback.name(QColor::HexArgb)));
    };
    const auto number = [&](const char *key, double fallback) { return o.value(QLatin1String(key)).toDouble(fallback); };

    TextShadingLayer shadow = shadowLayer(color("shadowColor", QColor(0, 0, 0)), number("shadowOffsetX", 0.0),
                                          number("shadowOffsetY", 4.0), number("shadowBlur", 8.0),
                                          number("shadowOpacity", 0.6));
    shadow.enabled = o.value(QStringLiteral("shadowEnabled")).toBool(false);

    TextShadingLayer glow = glowLayer(color("glowColor", QColor(255, 255, 255)), number("glowRadius", 18.0),
                                      number("glowOpacity", 0.8));
    glow.enabled = o.value(QStringLiteral("glowEnabled")).toBool(false);

    TextShadingLayer stroke = strokeLayer(number("outlineWidth", 2.0), color("outlineColor", QColor(Qt::black)));
    // Projects written before outlineEnabled treated any positive width as on.
    stroke.enabled = o.contains(QStringLiteral("outlineEnabled")) ? o.value(QStringLiteral("outlineEnabled")).toBool()
                                                                  : stroke.width > 0.0;

    TextShadingLayer fill = solidFillLayer(color("color", QColor(Qt::white)));
    const QString fillKind = o.value(QStringLiteral("fillKind")).toString();
    if (fillKind == QLatin1String("linearGradient") || fillKind == QLatin1String("radialGradient")) {
        fill.paint.kind = TextPaintKind::Gradient;
        fill.paint.gradient.kind = fillKind == QLatin1String("radialGradient") ? TextGradientKind::Radial
                                                                                : TextGradientKind::Linear;
        fill.paint.gradient.stops = {{0.0, fill.paint.color}, {1.0, color("colorSecondary", QColor(255, 120, 0))}};
        fill.paint.gradient.angle = number("gradientAngle", 90.0);
    }
    return {shadow, glow, stroke, fill};
}

TextAnimationSet legacyAnimationFromJson(const QJsonObject &o)
{
    TextAnimationSet set;
    bool inIsLoop = false;
    const TextAnimationSlot in = legacyTextAnimationSlot(
        o.value(QStringLiteral("animInKind")).toString(), o.value(QStringLiteral("animInDurationUs")).toInteger(400000),
        o.value(QStringLiteral("animInEase")).toString(), o.value(QStringLiteral("animInUnit")).toString(),
        o.value(QStringLiteral("animInStaggerUs")).toInteger(60000), o.value(QStringLiteral("animInOrder")).toString(),
        &inIsLoop);
    if (inIsLoop)
        set.loop = in;
    else
        set.in = in;
    bool outIsLoop = false;
    const TextAnimationSlot out = legacyTextAnimationSlot(
        o.value(QStringLiteral("animOutKind")).toString(), o.value(QStringLiteral("animOutDurationUs")).toInteger(400000),
        o.value(QStringLiteral("animOutEase")).toString(), o.value(QStringLiteral("animOutUnit")).toString(),
        o.value(QStringLiteral("animOutStaggerUs")).toInteger(60000), o.value(QStringLiteral("animOutOrder")).toString(),
        &outIsLoop);
    if (!outIsLoop) // an exiting Wave was never drawn
        set.out = out;
    return set;
}

} // namespace

TextStyle textStyleFromJson(const QJsonObject &o)
{
    TextStyle s;
    if (o.isEmpty())
        return s; // old projects: keep defaults
    s.packId = o.value(QStringLiteral("packId")).toString(s.packId);
    s.fontFamily = o.value(QStringLiteral("fontFamily")).toString(s.fontFamily);
    s.pixelSize = o.value(QStringLiteral("pixelSize")).toInt(s.pixelSize);
    // Projects written before the weight ladder only had a bold flag.
    if (o.contains(QStringLiteral("fontWeight")))
        s.fontWeight = qBound(100, o.value(QStringLiteral("fontWeight")).toInt(s.fontWeight), 900);
    else
        s.fontWeight = o.value(QStringLiteral("bold")).toBool(true) ? 700 : 400;
    s.italic = o.value(QStringLiteral("italic")).toBool(s.italic);
    s.pathBend = o.value(QStringLiteral("pathBend")).toDouble(s.pathBend);
    s.align = textAlignFromString(o.value(QStringLiteral("align")).toString());
    s.valign = textVAlignFromString(o.value(QStringLiteral("valign")).toString());
    s.wordWrap = o.value(QStringLiteral("wordWrap")).toBool(s.wordWrap);
    s.lineHeight = o.value(QStringLiteral("lineHeight")).toDouble(s.lineHeight);
    s.letterSpacing = o.value(QStringLiteral("letterSpacing")).toDouble(s.letterSpacing);
    s.boxEnabled = o.value(QStringLiteral("boxEnabled")).toBool(s.boxEnabled);
    s.boxColor = QColor(o.value(QStringLiteral("boxColor")).toString(s.boxColor.name(QColor::HexArgb)));
    s.boxPadding = o.value(QStringLiteral("boxPadding")).toDouble(s.boxPadding);
    s.boxRadius = o.value(QStringLiteral("boxRadius")).toDouble(s.boxRadius);
    s.wordHighlight =
        textHighlightFromJson(o.value(QStringLiteral("wordHighlight")).toObject(), s.wordHighlight);
    s.underlineEnabled = o.value(QStringLiteral("underlineEnabled")).toBool(s.underlineEnabled);
    s.underlineColor = QColor(o.value(QStringLiteral("underlineColor")).toString(s.underlineColor.name(QColor::HexArgb)));
    s.underlineWidth = o.value(QStringLiteral("underlineWidth")).toDouble(s.underlineWidth);
    s.underlineOffset = o.value(QStringLiteral("underlineOffset")).toDouble(s.underlineOffset);
    s.accent = wordAccentFromJson(o.value(QStringLiteral("accent")).toObject());

    if (o.contains(QStringLiteral("layers"))) {
        s.layers.clear();
        for (const QJsonValue &v : o.value(QStringLiteral("layers")).toArray())
            s.layers.append(textShadingLayerFromJson(v.toObject()));
        if (s.layers.isEmpty())
            s.layers = {solidFillLayer(Qt::white)};
        for (TextShadingLayer &layer : s.layers) {
            if (layer.id.isEmpty())
                layer.id = mintTextLayerId(s.layers);
        }
        s.lookId = o.value(QStringLiteral("lookId")).toString();
        const QJsonObject lookParams = o.value(QStringLiteral("lookParams")).toObject();
        for (auto it = lookParams.constBegin(); it != lookParams.constEnd(); ++it)
            s.lookParams.insert(it.key(), VectorSlotValue::fromJson(it->toObject()));
        s.animation = textAnimationSetFromJson(o.value(QStringLiteral("animation")).toObject());
    } else {
        // Version 6 and older: the flat look and the animIn/animOut kinds. Kept for good — the
        // user preset library and exported style files share this reader.
        s.layers = legacyLayersFromJson(o);
        s.animation = legacyAnimationFromJson(o);
    }

    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it) {
        const QString canonical = textKeyframeCanonicalKey(it.key(), s);
        if (!canonical.isEmpty())
            s.keyframes.insert(canonical, keyframesFromJson(it.value().toObject()));
    }
    return s;
}

} // namespace drift
