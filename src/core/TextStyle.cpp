#include "TextStyle.h"

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

const TextPreset *textPresetForId(const QString &id)
{
    for (const TextPreset &preset : textPresets()) {
        if (preset.id == id)
            return &preset;
    }
    return nullptr;
}

const TextStyle *textStyleForPresetId(const QString &id)
{
    const TextPreset *preset = textPresetForId(id);
    return preset ? &preset->style : nullptr;
}

} // namespace drift
