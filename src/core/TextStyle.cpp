#include "TextStyle.h"

#include "Effect.h"

#include "TextPresetStore.h"

#include <QCoreApplication>

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
        return QStringLiteral("middle");
    }
    return QStringLiteral("middle");
}

TextVAlign textVAlignFromString(const QString &valign)
{
    if (valign == QStringLiteral("top"))
        return TextVAlign::Top;
    if (valign == QStringLiteral("bottom"))
        return TextVAlign::Bottom;
    return TextVAlign::Middle;
}

QString textFillKindToString(TextFillKind kind)
{
    switch (kind) {
    case TextFillKind::Solid:
        return QStringLiteral("solid");
    case TextFillKind::LinearGradient:
        return QStringLiteral("linearGradient");
    case TextFillKind::RadialGradient:
        return QStringLiteral("radialGradient");
    }
    return QStringLiteral("solid");
}

TextFillKind textFillKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("linearGradient"))
        return TextFillKind::LinearGradient;
    if (kind == QStringLiteral("radialGradient"))
        return TextFillKind::RadialGradient;
    return TextFillKind::Solid;
}

QString textAnimKindToString(TextAnimKind kind)
{
    switch (kind) {
    case TextAnimKind::None:
        return QStringLiteral("none");
    case TextAnimKind::Fade:
        return QStringLiteral("fade");
    case TextAnimKind::SlideUp:
        return QStringLiteral("slideUp");
    case TextAnimKind::SlideDown:
        return QStringLiteral("slideDown");
    case TextAnimKind::SlideLeft:
        return QStringLiteral("slideLeft");
    case TextAnimKind::SlideRight:
        return QStringLiteral("slideRight");
    case TextAnimKind::Pop:
        return QStringLiteral("pop");
    case TextAnimKind::Blur:
        return QStringLiteral("blur");
    case TextAnimKind::Typewriter:
        return QStringLiteral("typewriter");
    case TextAnimKind::Rise:
        return QStringLiteral("rise");
    case TextAnimKind::Bounce:
        return QStringLiteral("bounce");
    case TextAnimKind::Wave:
        return QStringLiteral("wave");
    }
    return QStringLiteral("none");
}

TextAnimKind textAnimKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("fade"))
        return TextAnimKind::Fade;
    if (kind == QStringLiteral("slideUp"))
        return TextAnimKind::SlideUp;
    if (kind == QStringLiteral("slideDown"))
        return TextAnimKind::SlideDown;
    if (kind == QStringLiteral("slideLeft"))
        return TextAnimKind::SlideLeft;
    if (kind == QStringLiteral("slideRight"))
        return TextAnimKind::SlideRight;
    if (kind == QStringLiteral("pop"))
        return TextAnimKind::Pop;
    if (kind == QStringLiteral("blur"))
        return TextAnimKind::Blur;
    if (kind == QStringLiteral("typewriter"))
        return TextAnimKind::Typewriter;
    if (kind == QStringLiteral("rise"))
        return TextAnimKind::Rise;
    if (kind == QStringLiteral("bounce"))
        return TextAnimKind::Bounce;
    if (kind == QStringLiteral("wave"))
        return TextAnimKind::Wave;
    return TextAnimKind::None;
}

QString textEaseToString(TextEase ease)
{
    switch (ease) {
    case TextEase::Linear:
        return QStringLiteral("linear");
    case TextEase::EaseInOut:
        return QStringLiteral("easeInOut");
    case TextEase::Back:
        return QStringLiteral("back");
    case TextEase::EaseOut:
        return QStringLiteral("easeOut");
    }
    return QStringLiteral("easeOut");
}

TextEase textEaseFromString(const QString &ease)
{
    if (ease == QStringLiteral("linear"))
        return TextEase::Linear;
    if (ease == QStringLiteral("easeInOut"))
        return TextEase::EaseInOut;
    if (ease == QStringLiteral("back"))
        return TextEase::Back;
    return TextEase::EaseOut;
}

QString textAnimUnitToString(TextAnimUnit unit)
{
    switch (unit) {
    case TextAnimUnit::Word:
        return QStringLiteral("word");
    case TextAnimUnit::Character:
        return QStringLiteral("character");
    case TextAnimUnit::Line:
        return QStringLiteral("line");
    case TextAnimUnit::Block:
        return QStringLiteral("block");
    }
    return QStringLiteral("block");
}

TextAnimUnit textAnimUnitFromString(const QString &unit)
{
    if (unit == QStringLiteral("word"))
        return TextAnimUnit::Word;
    if (unit == QStringLiteral("character"))
        return TextAnimUnit::Character;
    if (unit == QStringLiteral("line"))
        return TextAnimUnit::Line;
    return TextAnimUnit::Block;
}

QString textAnimOrderToString(TextAnimOrder order)
{
    switch (order) {
    case TextAnimOrder::Backward:
        return QStringLiteral("backward");
    case TextAnimOrder::CenterOut:
        return QStringLiteral("centerOut");
    case TextAnimOrder::Random:
        return QStringLiteral("random");
    case TextAnimOrder::Forward:
        return QStringLiteral("forward");
    }
    return QStringLiteral("forward");
}

TextAnimOrder textAnimOrderFromString(const QString &order)
{
    if (order == QStringLiteral("backward"))
        return TextAnimOrder::Backward;
    if (order == QStringLiteral("centerOut"))
        return TextAnimOrder::CenterOut;
    if (order == QStringLiteral("random"))
        return TextAnimOrder::Random;
    return TextAnimOrder::Forward;
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

namespace {

QList<TextPreset> buildPresets()
{
    QList<TextPreset> presets;

    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 96;
        s.fontWeight = 800;
        s.outlineWidth = 2.0;
        s.outlineEnabled = true;
        s.animIn = {TextAnimKind::Fade, 400000, TextEase::EaseOut};
        presets.append({QStringLiteral("title"), QCoreApplication::translate("TextStyle", "Title"), s,
                        QCoreApplication::translate("TextStyle", "Main Title")});
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
        presets.append({QStringLiteral("subtitle"), QCoreApplication::translate("TextStyle", "Subtitle"), s,
                        QCoreApplication::translate("TextStyle", "A supporting line")});
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
        s.animIn = {TextAnimKind::SlideRight, 500000, TextEase::EaseOut};
        s.animOut = {TextAnimKind::SlideLeft, 400000, TextEase::EaseInOut};
        presets.append({QStringLiteral("lower-third"), QCoreApplication::translate("TextStyle", "Lower third"), s,
                        QCoreApplication::translate("TextStyle", "Alex Rivera · Host")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Oswald");
        s.pixelSize = 44;
        s.fontWeight = 600;
        s.valign = TextVAlign::Bottom;
        s.outlineWidth = 3.0;
        s.outlineEnabled = true;
        s.shadowEnabled = true;
        presets.append({QStringLiteral("caption"), QCoreApplication::translate("TextStyle", "Caption"), s,
                        QCoreApplication::translate("TextStyle", "Watch until the end")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Playfair Display");
        s.pixelSize = 64;
        s.fontWeight = 500;
        s.italic = true;
        s.lineHeight = 1.4;
        s.animIn = {TextAnimKind::Blur, 700000, TextEase::EaseOut};
        presets.append({QStringLiteral("quote"), QCoreApplication::translate("TextStyle", "Quote"), s,
                        QCoreApplication::translate("TextStyle", "Words worth keeping")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Anton");
        s.pixelSize = 120;
        s.fontWeight = 400;
        s.letterSpacing = 2.0;
        s.outlineWidth = 6.0;
        s.outlineEnabled = true;
        s.shadowEnabled = true;
        s.shadowBlur = 12.0;
        s.shadowOffsetY = 6.0;
        s.animIn = {TextAnimKind::Pop, 350000, TextEase::Back};
        presets.append({QStringLiteral("impact"), QCoreApplication::translate("TextStyle", "Impact"), s,
                        QCoreApplication::translate("TextStyle", "STOP SCROLLING")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Fredoka");
        s.pixelSize = 80;
        s.fontWeight = 600;
        s.color = QColor(255, 214, 64);
        s.outlineWidth = 5.0;
        s.outlineEnabled = true;
        s.animIn = {TextAnimKind::Pop, 450000, TextEase::Back};
        s.animOut = {TextAnimKind::Pop, 300000, TextEase::EaseInOut};
        presets.append({QStringLiteral("pop"), QCoreApplication::translate("TextStyle", "Pop"), s,
                        QCoreApplication::translate("TextStyle", "Big news!")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Bebas Neue");
        s.pixelSize = 110;
        s.fontWeight = 400;
        s.letterSpacing = 4.0;
        s.color = QColor(120, 255, 245);
        s.outlineWidth = 2.0;
        s.outlineEnabled = true;
        s.outlineColor = QColor(0, 90, 120);
        s.shadowEnabled = true;
        s.shadowColor = QColor(0, 220, 255);
        s.shadowBlur = 24.0;
        s.shadowOffsetX = 0.0;
        s.shadowOffsetY = 0.0;
        s.shadowOpacity = 0.9;
        presets.append({QStringLiteral("neon"), QCoreApplication::translate("TextStyle", "Neon"), s,
                        QCoreApplication::translate("TextStyle", "NEON NIGHTS")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Pacifico");
        s.pixelSize = 72;
        s.fontWeight = 400;
        s.lineHeight = 1.35;
        s.shadowEnabled = true;
        s.shadowBlur = 6.0;
        s.animIn = {TextAnimKind::SlideUp, 550000, TextEase::EaseOut};
        presets.append({QStringLiteral("handwritten"), QCoreApplication::translate("TextStyle", "Handwritten"), s,
                        QCoreApplication::translate("TextStyle", "With love")});
    }

    // Short-form caption packs. Unlike the presets above these carry a per-word accent rule, so the
    // pack itself decides which words are recoloured, highlighted or scaled up.
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Anton");
        s.pixelSize = 96;
        s.fontWeight = 400;
        s.outlineWidth = 5.0;
        s.outlineEnabled = true;
        s.shadowEnabled = true;
        s.shadowBlur = 10.0;
        s.shadowOffsetY = 6.0;
        s.accent.rule = WordAccentRule::FirstWord;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 45, 45);
        presets.append({QStringLiteral("hormozi"), QCoreApplication::translate("TextStyle", "Hormozi"), s,
                        QCoreApplication::translate("TextStyle", "Stop wasting time")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 84;
        s.fontWeight = 800;
        s.outlineWidth = 3.0;
        s.outlineEnabled = true;
        s.shadowEnabled = true;
        s.accent.rule = WordAccentRule::EveryNth;
        s.accent.n = 3;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 59, 48);
        presets.append({QStringLiteral("one-word-color"), QCoreApplication::translate("TextStyle", "One word colour"), s,
                        QCoreApplication::translate("TextStyle", "Make every word count")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Inter");
        s.pixelSize = 80;
        s.fontWeight = 800;
        s.outlineWidth = 2.0;
        s.outlineEnabled = true;
        s.accent.rule = WordAccentRule::EveryOther;
        s.accent.highlight.enabled = true;
        s.accent.highlight.color = QColor(230, 40, 40);
        s.accent.highlight.padding = 8.0;
        s.accent.highlight.radius = 6.0;
        presets.append({QStringLiteral("word-background"), QCoreApplication::translate("TextStyle", "Word background"), s,
                        QCoreApplication::translate("TextStyle", "Highlight what matters most")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 76;
        s.fontWeight = 800;
        s.color = QColor(20, 20, 20);
        s.boxEnabled = true;
        s.boxColor = QColor(255, 196, 0);
        s.boxPadding = 14.0;
        s.boxRadius = 10.0;
        presets.append({QStringLiteral("sentence-background"), QCoreApplication::translate("TextStyle", "Sentence background"), s,
                        QCoreApplication::translate("TextStyle", "Read this carefully")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("League Spartan");
        s.pixelSize = 88;
        s.fontWeight = 900;
        s.outlineWidth = 4.0;
        s.outlineEnabled = true;
        s.shadowEnabled = true;
        s.accent.rule = WordAccentRule::Karaoke;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 212, 0);
        s.accent.sizeScale = 1.12;
        presets.append({QStringLiteral("karaoke-pop"), QCoreApplication::translate("TextStyle", "Karaoke pop"), s,
                        QCoreApplication::translate("TextStyle", "Sing along with me")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Inter");
        s.pixelSize = 78;
        s.fontWeight = 800;
        s.outlineWidth = 2.0;
        s.outlineEnabled = true;
        s.accent.rule = WordAccentRule::Karaoke;
        s.accent.highlight.enabled = true;
        s.accent.highlight.color = QColor(34, 197, 94);
        s.accent.highlight.padding = 8.0;
        s.accent.highlight.radius = 8.0;
        presets.append({QStringLiteral("karaoke-highlight"), QCoreApplication::translate("TextStyle", "Karaoke highlight"), s,
                        QCoreApplication::translate("TextStyle", "Follow the bouncing words")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 76;
        s.fontWeight = 800;
        s.italic = true;
        s.glowEnabled = true;
        s.glowColor = QColor(255, 255, 255);
        s.glowRadius = 20.0;
        s.glowOpacity = 0.9;
        presets.append({QStringLiteral("mirage"), QCoreApplication::translate("TextStyle", "Mirage"), s,
                        QCoreApplication::translate("TextStyle", "Soft and dreamy")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Archivo Black");
        s.pixelSize = 80;
        s.fontWeight = 400;
        s.outlineWidth = 2.0;
        s.outlineEnabled = true;
        s.underlineEnabled = true;
        s.underlineColor = QColor(230, 40, 40);
        s.underlineWidth = 8.0;
        s.underlineOffset = 8.0;
        presets.append({QStringLiteral("underline"), QCoreApplication::translate("TextStyle", "Underline"), s,
                        QCoreApplication::translate("TextStyle", "Underline this line")});
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
        presets.append({QStringLiteral("bulky"), QCoreApplication::translate("TextStyle", "Bulky"), s,
                        QCoreApplication::translate("TextStyle", "Big first word")});
    }
    {
        TextStyle s;
        s.fontFamily = QStringLiteral("Montserrat");
        s.pixelSize = 82;
        s.fontWeight = 900;
        s.color = QColor(255, 255, 255, 0); // hollow by default; the accent words are the solid ones
        s.outlineWidth = 3.0;
        s.outlineEnabled = true;
        s.outlineColor = QColor(255, 255, 255);
        s.accent.rule = WordAccentRule::EveryOther;
        s.accent.colorEnabled = true;
        s.accent.color = QColor(255, 255, 255);
        presets.append({QStringLiteral("word-outline"), QCoreApplication::translate("TextStyle", "Word outline"), s,
                        QCoreApplication::translate("TextStyle", "Outline every other word")});
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

const QStringList &textKeyframeProperties()
{
    static const QStringList props{
        QStringLiteral("pixelSize"),     QStringLiteral("letterSpacing"), QStringLiteral("lineHeight"),
        QStringLiteral("outlineWidth"),  QStringLiteral("shadowOffsetX"), QStringLiteral("shadowOffsetY"),
        QStringLiteral("shadowBlur"),    QStringLiteral("shadowOpacity"), QStringLiteral("glowRadius"),
        QStringLiteral("glowOpacity"),   QStringLiteral("boxPadding"),    QStringLiteral("gradientAngle"),
        QStringLiteral("pathBend"),      QStringLiteral("color.r"),       QStringLiteral("color.g"),
        QStringLiteral("color.b"),       QStringLiteral("color.a")};
    return props;
}

bool textStyleScalar(const TextStyle &s, const QString &key, double *out)
{
    if (key == QStringLiteral("pixelSize"))
        *out = s.pixelSize;
    else if (key == QStringLiteral("letterSpacing"))
        *out = s.letterSpacing;
    else if (key == QStringLiteral("lineHeight"))
        *out = s.lineHeight;
    else if (key == QStringLiteral("outlineWidth"))
        *out = s.outlineWidth;
    else if (key == QStringLiteral("shadowOffsetX"))
        *out = s.shadowOffsetX;
    else if (key == QStringLiteral("shadowOffsetY"))
        *out = s.shadowOffsetY;
    else if (key == QStringLiteral("shadowBlur"))
        *out = s.shadowBlur;
    else if (key == QStringLiteral("shadowOpacity"))
        *out = s.shadowOpacity;
    else if (key == QStringLiteral("glowRadius"))
        *out = s.glowRadius;
    else if (key == QStringLiteral("glowOpacity"))
        *out = s.glowOpacity;
    else if (key == QStringLiteral("boxPadding"))
        *out = s.boxPadding;
    else if (key == QStringLiteral("gradientAngle"))
        *out = s.gradientAngle;
    else if (key == QStringLiteral("pathBend"))
        *out = s.pathBend;
    else if (key == QStringLiteral("color.r"))
        *out = s.color.redF();
    else if (key == QStringLiteral("color.g"))
        *out = s.color.greenF();
    else if (key == QStringLiteral("color.b"))
        *out = s.color.blueF();
    else if (key == QStringLiteral("color.a"))
        *out = s.color.alphaF();
    else
        return false;
    return true;
}

bool setTextStyleScalar(TextStyle &s, const QString &key, double value)
{
    if (key == QStringLiteral("pixelSize"))
        s.pixelSize = qMax(1, qRound(value));
    else if (key == QStringLiteral("letterSpacing"))
        s.letterSpacing = value;
    else if (key == QStringLiteral("lineHeight"))
        s.lineHeight = value;
    else if (key == QStringLiteral("outlineWidth"))
        s.outlineWidth = qMax(0.0, value);
    else if (key == QStringLiteral("shadowOffsetX"))
        s.shadowOffsetX = value;
    else if (key == QStringLiteral("shadowOffsetY"))
        s.shadowOffsetY = value;
    else if (key == QStringLiteral("shadowBlur"))
        s.shadowBlur = qMax(0.0, value);
    else if (key == QStringLiteral("shadowOpacity"))
        s.shadowOpacity = qBound(0.0, value, 1.0);
    else if (key == QStringLiteral("glowRadius"))
        s.glowRadius = qMax(0.0, value);
    else if (key == QStringLiteral("glowOpacity"))
        s.glowOpacity = qBound(0.0, value, 1.0);
    else if (key == QStringLiteral("boxPadding"))
        s.boxPadding = qMax(0.0, value);
    else if (key == QStringLiteral("gradientAngle"))
        s.gradientAngle = value;
    else if (key == QStringLiteral("pathBend"))
        s.pathBend = qBound(-100.0, value, 100.0);
    else if (key == QStringLiteral("color.r"))
        s.color.setRedF(qBound(0.0, value, 1.0));
    else if (key == QStringLiteral("color.g"))
        s.color.setGreenF(qBound(0.0, value, 1.0));
    else if (key == QStringLiteral("color.b"))
        s.color.setBlueF(qBound(0.0, value, 1.0));
    else if (key == QStringLiteral("color.a"))
        s.color.setAlphaF(qBound(0.0, value, 1.0));
    else
        return false;
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

QJsonObject textStyleToJson(const TextStyle &s)
{
    QJsonObject keyframesJson;
    for (auto it = s.keyframes.constBegin(); it != s.keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            keyframesJson.insert(it.key(), keyframesToJson(it.value()));
    }
    QJsonObject json{
        {QStringLiteral("packId"), s.packId},
        {QStringLiteral("fontFamily"), s.fontFamily},
        {QStringLiteral("pixelSize"), s.pixelSize},
        {QStringLiteral("fontWeight"), s.fontWeight},
        {QStringLiteral("italic"), s.italic},
        {QStringLiteral("color"), s.color.name(QColor::HexArgb)},
        {QStringLiteral("fillKind"), textFillKindToString(s.fillKind)},
        {QStringLiteral("colorSecondary"), s.colorSecondary.name(QColor::HexArgb)},
        {QStringLiteral("gradientAngle"), s.gradientAngle},
        {QStringLiteral("pathBend"), s.pathBend},
        {QStringLiteral("align"), textAlignToString(s.align)},
        {QStringLiteral("valign"), textVAlignToString(s.valign)},
        {QStringLiteral("wordWrap"), s.wordWrap},
        {QStringLiteral("lineHeight"), s.lineHeight},
        {QStringLiteral("letterSpacing"), s.letterSpacing},
        {QStringLiteral("outlineEnabled"), s.outlineEnabled},
        {QStringLiteral("outlineWidth"), s.outlineWidth},
        {QStringLiteral("outlineColor"), s.outlineColor.name(QColor::HexArgb)},
        {QStringLiteral("shadowEnabled"), s.shadowEnabled},
        {QStringLiteral("shadowOffsetX"), s.shadowOffsetX},
        {QStringLiteral("shadowOffsetY"), s.shadowOffsetY},
        {QStringLiteral("shadowBlur"), s.shadowBlur},
        {QStringLiteral("shadowOpacity"), s.shadowOpacity},
        {QStringLiteral("shadowColor"), s.shadowColor.name(QColor::HexArgb)},
        {QStringLiteral("glowEnabled"), s.glowEnabled},
        {QStringLiteral("glowColor"), s.glowColor.name(QColor::HexArgb)},
        {QStringLiteral("glowRadius"), s.glowRadius},
        {QStringLiteral("glowOpacity"), s.glowOpacity},
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
        {QStringLiteral("animInKind"), textAnimKindToString(s.animIn.kind)},
        {QStringLiteral("animInDurationUs"), static_cast<qint64>(s.animIn.durationUs)},
        {QStringLiteral("animInEase"), textEaseToString(s.animIn.ease)},
        {QStringLiteral("animInUnit"), textAnimUnitToString(s.animIn.unit)},
        {QStringLiteral("animInStaggerUs"), static_cast<qint64>(s.animIn.staggerUs)},
        {QStringLiteral("animInOrder"), textAnimOrderToString(s.animIn.order)},
        {QStringLiteral("animOutKind"), textAnimKindToString(s.animOut.kind)},
        {QStringLiteral("animOutDurationUs"), static_cast<qint64>(s.animOut.durationUs)},
        {QStringLiteral("animOutEase"), textEaseToString(s.animOut.ease)},
        {QStringLiteral("animOutUnit"), textAnimUnitToString(s.animOut.unit)},
        {QStringLiteral("animOutStaggerUs"), static_cast<qint64>(s.animOut.staggerUs)},
        {QStringLiteral("animOutOrder"), textAnimOrderToString(s.animOut.order)},
    };
    // Only animated styles carry the key: projects without text animation stay byte-identical.
    if (!keyframesJson.isEmpty())
        json.insert(QStringLiteral("keyframes"), keyframesJson);
    return json;
}

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
    s.color = QColor(o.value(QStringLiteral("color")).toString(s.color.name(QColor::HexArgb)));
    s.fillKind = textFillKindFromString(o.value(QStringLiteral("fillKind")).toString());
    s.colorSecondary = QColor(
        o.value(QStringLiteral("colorSecondary")).toString(s.colorSecondary.name(QColor::HexArgb)));
    s.gradientAngle = o.value(QStringLiteral("gradientAngle")).toDouble(s.gradientAngle);
    s.pathBend = o.value(QStringLiteral("pathBend")).toDouble(s.pathBend);
    s.align = textAlignFromString(o.value(QStringLiteral("align")).toString());
    s.valign = textVAlignFromString(o.value(QStringLiteral("valign")).toString());
    s.wordWrap = o.value(QStringLiteral("wordWrap")).toBool(s.wordWrap);
    s.lineHeight = o.value(QStringLiteral("lineHeight")).toDouble(s.lineHeight);
    s.letterSpacing = o.value(QStringLiteral("letterSpacing")).toDouble(s.letterSpacing);
    s.outlineWidth = o.value(QStringLiteral("outlineWidth")).toDouble(s.outlineWidth);
    s.outlineColor = QColor(o.value(QStringLiteral("outlineColor")).toString(s.outlineColor.name(QColor::HexArgb)));
    // Projects written before outlineEnabled treated any positive width as on.
    if (o.contains(QStringLiteral("outlineEnabled")))
        s.outlineEnabled = o.value(QStringLiteral("outlineEnabled")).toBool(s.outlineEnabled);
    else
        s.outlineEnabled = s.outlineWidth > 0.0;
    s.shadowEnabled = o.value(QStringLiteral("shadowEnabled")).toBool(s.shadowEnabled);
    s.shadowOffsetX = o.value(QStringLiteral("shadowOffsetX")).toDouble(s.shadowOffsetX);
    s.shadowOffsetY = o.value(QStringLiteral("shadowOffsetY")).toDouble(s.shadowOffsetY);
    s.shadowBlur = o.value(QStringLiteral("shadowBlur")).toDouble(s.shadowBlur);
    s.shadowOpacity = o.value(QStringLiteral("shadowOpacity")).toDouble(s.shadowOpacity);
    s.shadowColor = QColor(o.value(QStringLiteral("shadowColor")).toString(s.shadowColor.name(QColor::HexArgb)));
    s.glowEnabled = o.value(QStringLiteral("glowEnabled")).toBool(s.glowEnabled);
    s.glowColor = QColor(o.value(QStringLiteral("glowColor")).toString(s.glowColor.name(QColor::HexArgb)));
    s.glowRadius = o.value(QStringLiteral("glowRadius")).toDouble(s.glowRadius);
    s.glowOpacity = o.value(QStringLiteral("glowOpacity")).toDouble(s.glowOpacity);
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
    s.animIn.kind = textAnimKindFromString(o.value(QStringLiteral("animInKind")).toString());
    s.animIn.durationUs = o.value(QStringLiteral("animInDurationUs")).toInteger(s.animIn.durationUs);
    s.animIn.ease = textEaseFromString(o.value(QStringLiteral("animInEase")).toString());
    // Missing keys keep the Block/default reveal, so projects predating per-span animation are
    // deserialized identically to how they render today.
    s.animIn.unit = textAnimUnitFromString(o.value(QStringLiteral("animInUnit")).toString());
    s.animIn.staggerUs = o.value(QStringLiteral("animInStaggerUs")).toInteger(s.animIn.staggerUs);
    s.animIn.order = textAnimOrderFromString(o.value(QStringLiteral("animInOrder")).toString());
    s.animOut.kind = textAnimKindFromString(o.value(QStringLiteral("animOutKind")).toString());
    s.animOut.durationUs = o.value(QStringLiteral("animOutDurationUs")).toInteger(s.animOut.durationUs);
    s.animOut.ease = textEaseFromString(o.value(QStringLiteral("animOutEase")).toString());
    s.animOut.unit = textAnimUnitFromString(o.value(QStringLiteral("animOutUnit")).toString());
    s.animOut.staggerUs = o.value(QStringLiteral("animOutStaggerUs")).toInteger(s.animOut.staggerUs);
    s.animOut.order = textAnimOrderFromString(o.value(QStringLiteral("animOutOrder")).toString());
    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it) {
        if (textKeyframeProperties().contains(it.key()))
            s.keyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
    }
    return s;
}

} // namespace drift
