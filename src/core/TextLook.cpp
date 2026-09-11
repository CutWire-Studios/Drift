#include "TextLook.h"

#include <QCoreApplication>
#include <QtMath>

namespace drift {

namespace {

TextAnimParamSpec scalar(const char *id, const char *label, const char *unit, double min, double max, double step,
                         double def)
{
    TextAnimParamSpec s;
    s.id = QLatin1String(id);
    s.label = QCoreApplication::translate("TextLook", label);
    s.unit = QLatin1String(unit);
    s.min = min;
    s.max = max;
    s.step = step;
    s.defaultValue = VectorSlotValue::fromScalar(def);
    return s;
}

TextAnimParamSpec color(const char *id, const char *label, const QColor &def)
{
    TextAnimParamSpec s;
    s.id = QLatin1String(id);
    s.label = QCoreApplication::translate("TextLook", label);
    s.type = TextAnimParamSpec::Type::Color;
    s.defaultValue = VectorSlotValue::fromColor(def);
    return s;
}

TextAnimParamSpec choice(const char *id, const char *label, const QStringList &values, const char *def)
{
    TextAnimParamSpec s;
    s.id = QLatin1String(id);
    s.label = QCoreApplication::translate("TextLook", label);
    s.type = TextAnimParamSpec::Type::Enum;
    s.enumValues = values;
    s.defaultValue = VectorSlotValue::fromText(QLatin1String(def));
    return s;
}

QString tr(const char *s)
{
    return QCoreApplication::translate("TextLook", s);
}

TextGradient gradientPreset(const QString &id)
{
    for (const TextGradientPreset &p : textGradientPresets())
        if (p.id == id)
            return p.gradient;
    return textGradientPresets().first().gradient;
}

QStringList gradientPresetIds()
{
    QStringList ids;
    for (const TextGradientPreset &p : textGradientPresets())
        ids.append(p.id);
    return ids;
}

} // namespace

const QList<TextGradientPreset> &textGradientPresets()
{
    static const QList<TextGradientPreset> presets = [] {
        const auto make = [](const char *id, const char *label, std::initializer_list<QColor> colors,
                             TextGradientKind kind = TextGradientKind::Linear, double angle = 90.0) {
            TextGradientPreset p;
            p.id = QLatin1String(id);
            p.label = QCoreApplication::translate("TextLook", label);
            p.gradient.kind = kind;
            p.gradient.angle = angle;
            p.gradient.stops.clear();
            int i = 0;
            const int n = int(colors.size());
            for (const QColor &c : colors)
                p.gradient.stops.append({n > 1 ? double(i++) / (n - 1) : 0.0, c});
            return p;
        };
        return QList<TextGradientPreset>{
            make("sunset", "Sunset", {QColor(255, 94, 98), QColor(255, 153, 102)}),
            make("ocean", "Ocean", {QColor(43, 88, 118), QColor(78, 67, 118)}, TextGradientKind::Linear, 0.0),
            make("candy", "Candy", {QColor(255, 110, 199), QColor(120, 115, 245)}, TextGradientKind::Linear, 0.0),
            make("gold", "Gold", {QColor(255, 236, 160), QColor(212, 160, 23)}),
            make("chrome", "Chrome", {QColor(230, 235, 240), QColor(120, 130, 145), QColor(240, 245, 250), QColor(90, 100, 115)}),
            make("rainbow", "Rainbow", {QColor(255, 0, 0), QColor(255, 154, 0), QColor(208, 222, 33), QColor(79, 220, 74), QColor(63, 218, 216), QColor(47, 201, 226), QColor(28, 127, 238), QColor(95, 21, 242), QColor(186, 12, 248), QColor(251, 7, 217)}, TextGradientKind::Linear, 0.0),
            make("fire", "Fire", {QColor(255, 230, 120), QColor(255, 80, 20), QColor(120, 10, 0)}),
            make("ice", "Ice", {QColor(255, 255, 255), QColor(120, 200, 255)}),
            make("mono", "Mono", {QColor(255, 255, 255), QColor(120, 120, 120)}),
            make("holo", "Holographic", {QColor(255, 120, 200), QColor(120, 220, 255), QColor(200, 255, 140), QColor(255, 120, 200)}, TextGradientKind::Linear, 20.0),
            make("mint", "Mint", {QColor(180, 255, 220), QColor(40, 190, 140)}),
            make("berry", "Berry", {QColor(26, 19, 22), QColor(163, 18, 63), QColor(192, 32, 148), QColor(44, 0, 138)}, TextGradientKind::Linear, 0.0),
        };
    }();
    return presets;
}

const QList<TextLook> &textLooks()
{
    static const QList<TextLook> looks{
        {QStringLiteral("plain"), tr("Plain"), {}},
        {QStringLiteral("shadow"), tr("Shadow"),
         {scalar("offset", "Offset", "px", 0, 60, 1, 6), scalar("direction", "Direction", "°", 0, 360, 1, 90),
          scalar("blur", "Blur", "px", 0, 60, 1, 6), scalar("opacity", "Opacity", "", 0, 1, 0.05, 0.6),
          color("color", "Colour", Qt::black)}},
        {QStringLiteral("lift"), tr("Lift"), {scalar("intensity", "Intensity", "", 0, 1, 0.05, 0.5)}},
        {QStringLiteral("hollow"), tr("Hollow"), {scalar("thickness", "Thickness", "px", 0.5, 20, 0.5, 3)}},
        {QStringLiteral("splice"), tr("Splice"),
         {scalar("thickness", "Thickness", "px", 0.5, 20, 0.5, 3), scalar("offset", "Offset", "px", 0, 40, 1, 4),
          scalar("direction", "Direction", "°", 0, 360, 1, 45), color("color", "Colour", QColor(255, 255, 255, 128))}},
        {QStringLiteral("outline"), tr("Outline"),
         {scalar("thickness", "Thickness", "px", 0.5, 30, 0.5, 4), color("color", "Colour", Qt::black)}},
        {QStringLiteral("echo"), tr("Echo"),
         {scalar("offset", "Offset", "px", 0, 40, 1, 4), scalar("direction", "Direction", "°", 0, 360, 1, 45),
          scalar("count", "Copies", "", 1, 4, 1, 3), color("color", "Colour", QColor(0, 0, 0, 0))}},
        {QStringLiteral("glitch"), tr("Glitch"),
         {scalar("offset", "Offset", "px", 0, 30, 1, 3), scalar("direction", "Direction", "°", 0, 360, 1, 0),
          color("colorA", "Colour A", QColor(255, 0, 255)), color("colorB", "Colour B", QColor(0, 255, 255))}},
        {QStringLiteral("neon"), tr("Neon"),
         {scalar("glow", "Glow", "px", 2, 80, 1, 14), color("color", "Colour", QColor(255, 60, 220)),
          scalar("intensity", "Intensity", "", 0, 2, 0.05, 1.0)}},
        {QStringLiteral("background"), tr("Background"),
         {scalar("roundness", "Roundness", "px", 0, 60, 1, 8), scalar("spread", "Spread", "px", 0, 80, 1, 12),
          scalar("transparency", "Transparency", "", 0, 1, 0.05, 0.2), color("color", "Colour", Qt::black)}},
        {QStringLiteral("curve"), tr("Curve"), {scalar("curve", "Curve", "", -100, 100, 1, 40)}},
        {QStringLiteral("gradient"), tr("Gradient"),
         {choice("preset", "Preset", gradientPresetIds(), "sunset"), scalar("angle", "Angle", "°", 0, 360, 1, 90),
          choice("space", "Map to", {QStringLiteral("block"), QStringLiteral("line"), QStringLiteral("word"), QStringLiteral("glyph")}, "block")}},
        {QStringLiteral("shine"), tr("Shine"),
         {scalar("speed", "Speed", "cyc/s", 0, 3, 0.05, 0.5), scalar("width", "Width", "", 0.05, 1, 0.01, 0.25),
          scalar("angle", "Angle", "°", -180, 180, 1, 20), scalar("brightness", "Brightness", "", 0, 2, 0.05, 0.9)}},
        {QStringLiteral("chrome"), tr("Chrome"),
         {color("colorA", "Light", QColor(240, 245, 255)), color("colorB", "Dark", QColor(40, 50, 70)),
          scalar("bands", "Reflection", "", 0.5, 6, 0.5, 2)}},
        {QStringLiteral("holographic"), tr("Holographic"),
         {scalar("speed", "Speed", "cyc/s", 0, 2, 0.05, 0.15), scalar("angle", "Angle", "°", 0, 360, 1, 20),
          scalar("glow", "Glow", "px", 0, 40, 1, 8)}},
    };
    return looks;
}

const TextLook *textLookForId(const QString &id)
{
    for (const TextLook &look : textLooks())
        if (look.id == id)
            return &look;
    return nullptr;
}

bool applyTextLook(TextStyle &style, const QString &id, const QMap<QString, VectorSlotValue> &overrides)
{
    const TextLook *look = textLookForId(id);
    if (!look)
        return false;
    QMap<QString, VectorSlotValue> params;
    for (const TextAnimParamSpec &spec : look->params)
        params.insert(spec.id, spec.defaultValue);
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
        if (params.contains(it.key()))
            params.insert(it.key(), *it);
    const auto num = [&](const char *key, double def) { return scalarParam(params, QLatin1String(key), def); };
    const auto col = [&](const char *key, const QColor &def) { return colorParam(params, QLatin1String(key), def); };
    const auto text = [&](const char *key, const QString &def) {
        const auto it = params.constFind(QLatin1String(key));
        return it != params.constEnd() && it->type == VectorSlotValue::Type::Text ? it->text : def;
    };

    const QColor primary = style.primaryColor();
    const double scale = style.pixelSize / 64.0;
    const auto offsetVec = [&](double offset, double directionDeg) {
        const double rad = qDegreesToRadians(directionDeg);
        return QPointF(std::cos(rad) * offset * scale, std::sin(rad) * offset * scale);
    };
    QList<TextShadingLayer> layers;
    TextShadingLayer fill = solidFillLayer(primary);

    if (id == QLatin1String("plain")) {
        layers = {fill};
    } else if (id == QLatin1String("shadow")) {
        const QPointF off = offsetVec(num("offset", 6), num("direction", 90));
        layers = {shadowLayer(col("color", Qt::black), off.x() / scale, off.y() / scale, num("blur", 6) * scale, num("opacity", 0.6)), fill};
    } else if (id == QLatin1String("lift")) {
        const double k = num("intensity", 0.5);
        layers = {shadowLayer(Qt::black, 0.0, 2.0 * scale, (6.0 + 34.0 * k) * scale, 0.2 + 0.3 * k), fill};
    } else if (id == QLatin1String("hollow")) {
        TextShadingLayer stroke = strokeLayer(num("thickness", 3) * scale, primary);
        fill.enabled = false;
        layers = {stroke, fill};
    } else if (id == QLatin1String("splice")) {
        const QPointF off = offsetVec(num("offset", 4), num("direction", 45));
        TextShadingLayer copy = shadowLayer(col("color", QColor(255, 255, 255, 128)), off.x() / scale, off.y() / scale, 0.0, 1.0);
        TextShadingLayer stroke = strokeLayer(num("thickness", 3) * scale, primary);
        fill.enabled = false;
        layers = {copy, stroke, fill};
    } else if (id == QLatin1String("outline")) {
        layers = {strokeLayer(num("thickness", 4) * scale, col("color", Qt::black)), fill};
    } else if (id == QLatin1String("echo")) {
        const int count = qBound(1, qRound(num("count", 3)), 4);
        QColor echo = col("color", QColor(0, 0, 0, 0));
        if (echo.alpha() == 0)
            echo = primary;
        for (int k = count; k >= 1; --k) {
            const QPointF off = offsetVec(num("offset", 4) * k, num("direction", 45));
            layers.append(shadowLayer(echo, off.x() / scale, off.y() / scale, 0.0, 0.6 / k, QStringLiteral("echo%1").arg(k)));
        }
        layers.append(fill);
    } else if (id == QLatin1String("glitch")) {
        const QPointF off = offsetVec(num("offset", 3), num("direction", 0));
        TextShadingLayer a = solidFillLayer(col("colorA", QColor(255, 0, 255)), QStringLiteral("glitcha"));
        a.offsetX = -off.x() / scale;
        a.offsetY = -off.y() / scale;
        a.blend = BlendMode::Screen;
        a.opacity = 0.8;
        TextShadingLayer b = solidFillLayer(col("colorB", QColor(0, 255, 255)), QStringLiteral("glitchb"));
        b.offsetX = off.x() / scale;
        b.offsetY = off.y() / scale;
        b.blend = BlendMode::Screen;
        b.opacity = 0.8;
        layers = {a, b, fill};
    } else if (id == QLatin1String("neon")) {
        const QColor c = col("color", QColor(255, 60, 220));
        const double glow = num("glow", 14) * scale;
        const double k = num("intensity", 1.0);
        TextShadingLayer outer = glowLayer(c, glow * 2.8, qMin(1.0, 0.5 * k), QStringLiteral("glowouter"));
        TextShadingLayer inner = glowLayer(c, glow, qMin(1.0, 0.9 * k), QStringLiteral("glow"));
        TextShadingLayer stroke = strokeLayer(2.0 * scale, c);
        TextShadingLayer white = solidFillLayer(QColor(255, 255, 255, 230));
        layers = {outer, inner, stroke, white};
    } else if (id == QLatin1String("background")) {
        layers = {fill};
        style.boxEnabled = true;
        QColor bg = col("color", Qt::black);
        bg.setAlphaF(1.0 - num("transparency", 0.2));
        style.boxColor = bg;
        style.boxPadding = num("spread", 12) * scale;
        style.boxRadius = num("roundness", 8) * scale;
    } else if (id == QLatin1String("curve")) {
        layers = style.layers;
        style.pathBend = num("curve", 40);
    } else if (id == QLatin1String("gradient")) {
        fill.paint.kind = TextPaintKind::Gradient;
        fill.paint.gradient = gradientPreset(text("preset", QStringLiteral("sunset")));
        fill.paint.gradient.angle = num("angle", fill.paint.gradient.angle);
        fill.paint.gradient.space = textGradientSpaceFromString(text("space", QStringLiteral("block")));
        layers = {fill};
    } else if (id == QLatin1String("shine")) {
        TextShadingLayer sheen = solidFillLayer(Qt::white, QStringLiteral("shine"));
        sheen.paint.kind = TextPaintKind::Effect;
        sheen.paint.color = QColor(0, 0, 0, 0);
        sheen.paint.effect.id = QStringLiteral("shine");
        sheen.paint.effect.params.insert(QStringLiteral("speed"), VectorSlotValue::fromScalar(num("speed", 0.5)));
        sheen.paint.effect.params.insert(QStringLiteral("width"), VectorSlotValue::fromScalar(num("width", 0.25)));
        sheen.paint.effect.params.insert(QStringLiteral("angle"), VectorSlotValue::fromScalar(num("angle", 20)));
        sheen.paint.effect.params.insert(QStringLiteral("intensity"), VectorSlotValue::fromScalar(num("brightness", 0.9)));
        sheen.blend = BlendMode::Screen;
        layers = {fill, sheen};
    } else if (id == QLatin1String("chrome")) {
        fill.paint.kind = TextPaintKind::Effect;
        fill.paint.effect.id = QStringLiteral("chrome");
        fill.paint.effect.params.insert(QStringLiteral("colorA"), VectorSlotValue::fromColor(col("colorA", QColor(240, 245, 255))));
        fill.paint.effect.params.insert(QStringLiteral("colorB"), VectorSlotValue::fromColor(col("colorB", QColor(40, 50, 70))));
        fill.paint.effect.params.insert(QStringLiteral("bands"), VectorSlotValue::fromScalar(num("bands", 2)));
        layers = {shadowLayer(Qt::black, 0.0, 3.0 * scale, 4.0 * scale, 0.5), strokeLayer(1.0 * scale, QColor(42, 42, 42)), fill};
    } else if (id == QLatin1String("holographic")) {
        fill.paint.kind = TextPaintKind::Gradient;
        fill.paint.gradient = gradientPreset(QStringLiteral("holo"));
        fill.paint.gradient.angle = num("angle", 20);
        fill.paint.gradient.offsetSpeed = num("speed", 0.15);
        fill.paint.gradient.repeat = true;
        fill.paint.gradient.space = TextGradientSpace::Glyph;
        TextShadingLayer glow = glowLayer(Qt::white, num("glow", 8) * scale, 0.4);
        layers = {glow, fill};
    } else {
        return false;
    }

    style.layers = layers;
    style.lookId = id;
    style.lookParams = params;
    return true;
}

} // namespace drift
