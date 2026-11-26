#include "core/text/TitleTemplate.h"

#include "core/animation/ClipAnimation.h"
#include "core/shapes/ShapeStyle.h"
#include "core/text/TextStyle.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutex>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

static void initTitleTemplateResources()
{
    Q_INIT_RESOURCE(drift_title_templates);
}

namespace drift {

namespace {

QMutex g_catalogMutex;

QRectF boxOf(const QJsonObject &e, int canvasW, int canvasH)
{
    const QJsonArray b = e.value(QStringLiteral("box")).toArray();
    if (b.size() != 4)
        return QRectF(0.0, 0.35 * canvasH, canvasW, 0.30 * canvasH);
    return QRectF(b.at(0).toDouble() * canvasW, b.at(1).toDouble() * canvasH, b.at(2).toDouble() * canvasW,
                  b.at(3).toDouble() * canvasH);
}

QColor colorOf(const QJsonValue &v, const QColor &fallback)
{
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}

QMap<QString, VectorSlotValue> slotParams(const QJsonObject &o)
{
    QMap<QString, VectorSlotValue> params;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (it.key() == QLatin1String("preset"))
            continue;
        if (it.value().isString())
            params.insert(it.key(), VectorSlotValue::fromText(it.value().toString()));
        else if (it.value().isDouble())
            params.insert(it.key(), VectorSlotValue::fromScalar(it.value().toDouble()));
    }
    return params;
}

TextAnimationSlot textSlot(const QJsonObject &o)
{
    TextAnimationSlot slot;
    slot.presetId = o.value(QStringLiteral("preset")).toString();
    slot.params = slotParams(o);
    return slot;
}

ClipAnimation clipAnimation(const QJsonObject &o)
{
    ClipAnimation anim;
    anim.kindId = o.value(QStringLiteral("anim")).toString();
    anim.durationUs = secondsToUs(o.value(QStringLiteral("duration")).toDouble(0.5));
    anim.curve = clipAnimEaseToCurve(clipAnimEaseFromString(o.value(QStringLiteral("ease")).toString(QStringLiteral("easeOut"))));
    return anim;
}

void setFrame(Clip &clip, const QRectF &box)
{
    clip.frameW = qMax(1.0, box.width());
    clip.frameH = qMax(1.0, box.height());
    clip.positionX.setKeyframe(0, box.x() + clip.frameW / 2.0);
    clip.positionY.setKeyframe(0, box.y() + clip.frameH / 2.0);
}

TextStyle textStyleOf(const QJsonObject &e, double refH)
{
    TextStyle s;
    s.fontFamily = e.value(QStringLiteral("font")).toString(s.fontFamily);
    s.fontWeight = qBound(100, e.value(QStringLiteral("weight")).toInt(700), 900);
    s.italic = e.value(QStringLiteral("italic")).toBool(false);
    s.pixelSize = qBound(8, qRound(e.value(QStringLiteral("size")).toDouble(0.06) * refH), 800);
    s.align = textAlignFromString(e.value(QStringLiteral("align")).toString(QStringLiteral("center")));
    s.valign = textVAlignFromString(e.value(QStringLiteral("valign")).toString(QStringLiteral("middle")));
    s.textCase = textCaseFromString(e.value(QStringLiteral("case")).toString());
    s.letterSpacing = e.value(QStringLiteral("letterSpacing")).toDouble(0.0) * s.pixelSize;
    s.lineHeight = e.value(QStringLiteral("lineHeight")).toDouble(1.2);
    s.wordWrap = e.value(QStringLiteral("wrap")).toBool(true);

    QList<TextShadingLayer> layers;
    const QJsonObject shadow = e.value(QStringLiteral("shadow")).toObject();
    if (!shadow.isEmpty()) {
        layers.append(shadowLayer(colorOf(shadow.value(QStringLiteral("color")), Qt::black),
                                  shadow.value(QStringLiteral("x")).toDouble(0.0) * s.pixelSize,
                                  shadow.value(QStringLiteral("y")).toDouble(0.08) * s.pixelSize,
                                  shadow.value(QStringLiteral("blur")).toDouble(0.15) * s.pixelSize,
                                  shadow.value(QStringLiteral("opacity")).toDouble(0.5)));
    }
    const QJsonObject stroke = e.value(QStringLiteral("stroke")).toObject();
    if (!stroke.isEmpty()) {
        layers.append(strokeLayer(stroke.value(QStringLiteral("width")).toDouble(0.05) * s.pixelSize,
                                  colorOf(stroke.value(QStringLiteral("color")), Qt::black)));
    }
    layers.append(solidFillLayer(colorOf(e.value(QStringLiteral("color")), Qt::white)));
    s.layers = layers;

    const QJsonObject back = e.value(QStringLiteral("background")).toObject();
    if (!back.isEmpty()) {
        s.boxEnabled = true;
        s.boxColor = colorOf(back.value(QStringLiteral("color")), QColor(0, 0, 0, 160));
        s.boxPadding = back.value(QStringLiteral("padding")).toDouble(0.3) * s.pixelSize;
        s.boxRadius = back.value(QStringLiteral("radius")).toDouble(0.0) * s.pixelSize;
    }

    if (e.contains(QStringLiteral("in")))
        s.animation.in = textSlot(e.value(QStringLiteral("in")).toObject());
    if (e.contains(QStringLiteral("out")))
        s.animation.out = textSlot(e.value(QStringLiteral("out")).toObject());
    if (e.contains(QStringLiteral("loop")))
        s.animation.loop = textSlot(e.value(QStringLiteral("loop")).toObject());

    s.scroll = textScrollFromString(e.value(QStringLiteral("scroll")).toString());
    s.scrollEnterOffscreen = e.value(QStringLiteral("scrollEnter")).toBool(true);
    s.scrollExitOffscreen = e.value(QStringLiteral("scrollExit")).toBool(true);
    return s;
}

ShapeStyle shapeStyleOf(const QJsonObject &e, const QRectF &box)
{
    ShapeStyle s;
    s.kind = shapeKindFromString(e.value(QStringLiteral("shape")).toString(QStringLiteral("rectangle")));
    const QColor fill = colorOf(e.value(QStringLiteral("fill")), Qt::white);
    const QJsonObject stroke = e.value(QStringLiteral("stroke")).toObject();
    s.layers = defaultShapeLayers(fill, QColor(), colorOf(stroke.value(QStringLiteral("color")), Qt::white),
                                  stroke.value(QStringLiteral("width")).toDouble(0.0));
    s.setSolidFill(fill);
    if (!stroke.isEmpty())
        s.setStroke(stroke.value(QStringLiteral("width")).toDouble(0.01) * qMin(box.width(), box.height()),
                    colorOf(stroke.value(QStringLiteral("color")), Qt::white));
    s.cornerRadius = e.value(QStringLiteral("radius")).toDouble(0.0) * qMin(box.width(), box.height());
    return s;
}

} // namespace

QList<TitleTemplateField> TitleTemplate::fields() const
{
    QList<TitleTemplateField> out;
    for (const QJsonValue &v : elements) {
        const QJsonObject e = v.toObject();
        if (e.value(QStringLiteral("type")).toString() != QLatin1String("text"))
            continue;
        const QString key = e.value(QStringLiteral("key")).toString();
        if (key.isEmpty())
            continue;
        out.append({key, e.value(QStringLiteral("label")).toString(key), e.value(QStringLiteral("text")).toString()});
    }
    return out;
}

QJsonObject TitleTemplate::toJson() const
{
    return QJsonObject{{QStringLiteral("format"), 1},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("category"), category},
                       {QStringLiteral("order"), order},
                       {QStringLiteral("duration"), durationSec},
                       {QStringLiteral("elements"), elements}};
}

std::optional<TitleTemplate> TitleTemplate::fromJson(const QJsonObject &o, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<TitleTemplate> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    TitleTemplate t;
    t.id = o.value(QStringLiteral("id")).toString();
    if (t.id.isEmpty())
        return fail(QStringLiteral("missing id"));
    t.label = o.value(QStringLiteral("label")).toString(t.id);
    t.category = o.value(QStringLiteral("category")).toString(QStringLiteral("title"));
    t.order = o.value(QStringLiteral("order")).toInt(100);
    t.durationSec = qBound(0.5, o.value(QStringLiteral("duration")).toDouble(5.0), 600.0);
    t.elements = o.value(QStringLiteral("elements")).toArray();
    if (t.elements.isEmpty())
        return fail(QStringLiteral("no elements"));
    for (const QJsonValue &v : std::as_const(t.elements)) {
        const QString type = v.toObject().value(QStringLiteral("type")).toString();
        if (type != QLatin1String("text") && type != QLatin1String("shape"))
            return fail(QStringLiteral("element type must be text or shape"));
    }
    return t;
}

TitleTemplateCatalog &TitleTemplateCatalog::instance()
{
    static TitleTemplateCatalog catalog;
    return catalog;
}

QString TitleTemplateCatalog::userDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("title-templates"));
}

void TitleTemplateCatalog::loadDir(const QString &dir, bool builtIn) const
{
    for (const QFileInfo &info : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QString error;
        std::optional<TitleTemplate> tpl = TitleTemplate::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &error);
        if (!tpl) {
            qWarning("title template %s: %s", qPrintable(info.fileName()), qPrintable(error));
            continue;
        }
        tpl->builtIn = builtIn;
        bool replaced = false;
        for (TitleTemplate &existing : m_templates) {
            if (existing.id == tpl->id) {
                existing = *tpl;
                replaced = true;
            }
        }
        if (!replaced)
            m_templates.append(*tpl);
    }
}

void TitleTemplateCatalog::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_templates.clear();
    initTitleTemplateResources();
    loadDir(QStringLiteral(":/title-templates"), true);
    for (const QString &dir : qEnvironmentVariable("DRIFT_TITLE_TEMPLATES").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        loadDir(dir, true);
    if (!userDir().isEmpty())
        loadDir(userDir(), false);
    std::stable_sort(m_templates.begin(), m_templates.end(),
                     [](const TitleTemplate &a, const TitleTemplate &b) { return a.order < b.order; });
}

void TitleTemplateCatalog::reload()
{
    QMutexLocker lock(&g_catalogMutex);
    m_loaded = false;
    ensureLoaded();
}

QList<TitleTemplate> TitleTemplateCatalog::templates() const
{
    QMutexLocker lock(&g_catalogMutex);
    ensureLoaded();
    return m_templates;
}

std::optional<TitleTemplate> TitleTemplateCatalog::templateForId(const QString &id) const
{
    QMutexLocker lock(&g_catalogMutex);
    ensureLoaded();
    for (const TitleTemplate &t : m_templates) {
        if (t.id == id)
            return t;
    }
    return std::nullopt;
}

QList<Clip> instantiateTitleTemplate(const TitleTemplate &tpl, int canvasW, int canvasH, TimeUs startUs,
                                     const QMap<QString, QString> &texts)
{
    canvasW = qMax(1, canvasW);
    canvasH = qMax(1, canvasH);
    // Sizes follow a 16:9 frame of the same width, so a portrait project does not get type taller
    // than it is wide.
    const double refH = qMin<double>(canvasH, canvasW * 9.0 / 16.0);
    const QString linkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const TimeUs total = secondsToUs(tpl.durationSec);

    QList<Clip> clips;
    for (const QJsonValue &v : tpl.elements) {
        const QJsonObject e = v.toObject();
        const TimeUs delay = secondsToUs(qMax(0.0, e.value(QStringLiteral("start")).toDouble(0.0)));
        const TimeUs trim = secondsToUs(qMax(0.0, e.value(QStringLiteral("end")).toDouble(0.0)));
        const TimeUs duration = qMax<TimeUs>(secondsToUs(0.2), total - delay - trim);
        const QRectF box = boxOf(e, canvasW, canvasH);

        Clip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.linkId = linkId;
        clip.linkLoose = true;
        clip.timelineStart = startUs + delay;
        clip.timelineDuration = duration;
        clip.srcIn = 0;
        clip.srcOut = duration;
        clip.name = e.value(QStringLiteral("name")).toString();
        setFrame(clip, box);
        if (e.contains(QStringLiteral("opacity")))
            clip.opacity.setKeyframe(0, qBound(0.0, e.value(QStringLiteral("opacity")).toDouble(1.0), 1.0));

        if (e.value(QStringLiteral("type")).toString() == QLatin1String("text")) {
            clip.type = ClipType::Text;
            const QString key = e.value(QStringLiteral("key")).toString();
            const QString text = texts.value(key, e.value(QStringLiteral("text")).toString());
            clip.textContent = text;
            if (clip.name.isEmpty())
                clip.name = text.left(32);
            clip.textStyle = textStyleOf(e, refH);
        } else {
            clip.type = ClipType::Shape;
            if (clip.name.isEmpty())
                clip.name = e.value(QStringLiteral("shape")).toString(QStringLiteral("Shape"));
            clip.shapeStyle = shapeStyleOf(e, box);
            if (e.contains(QStringLiteral("in")))
                clip.animIn = clipAnimation(e.value(QStringLiteral("in")).toObject());
            if (e.contains(QStringLiteral("out")))
                clip.animOut = clipAnimation(e.value(QStringLiteral("out")).toObject());
            clip.fadeInUs = secondsToUs(e.value(QStringLiteral("fadeIn")).toDouble(0.0));
            clip.fadeOutUs = secondsToUs(e.value(QStringLiteral("fadeOut")).toDouble(0.0));
        }
        clips.append(clip);
    }
    return clips;
}

} // namespace drift
