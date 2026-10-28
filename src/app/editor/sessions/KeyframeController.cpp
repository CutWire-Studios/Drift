#include "app/editor/sessions/KeyframeController.h"

#include "app/editor/AppController.h"
#include "app/editor/sessions/KeyframeCurves.h"
#include "app/editor/sessions/KeyframeRows.h"
#include "core/effects/Mask.h"
#include "core/project/sources/Model3dSource.h"
#include "core/project/sources/PdfSource.h"
#include "core/shapes/ShapeStyle.h"
#include "core/text/TextStyle.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"

#include <QOpenGLContext>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// One row of the property tree. `tracks` are the channel-level keys a row reads and writes;
// tracks[0] is the key the row's value and enabled flag are read through.
struct PropRow
{
    QString prop;
    QString label;
    QString group;
    QString groupLabel;
    QString kind;
    QStringList tracks;
    double min = 0.0;
    double max = 0.0;
    int decimals = 2;
    double step = 0.0;
};

PropRow scalarRow(const QString &prop, const QString &label, const QString &group,
                  const QString &groupLabel, double min, double max, int decimals)
{
    PropRow row;
    row.prop = prop;
    row.label = label;
    row.group = group;
    row.groupLabel = groupLabel;
    row.kind = QStringLiteral("scalar");
    row.tracks = {prop};
    row.min = min;
    row.max = max;
    row.decimals = decimals;
    return row;
}

// Channel rules mirror effectToMap: a colour keys .r/.g/.b/.a, a vec2 .x/.y and a region .x/.y/.w/.h.
PropRow specRow(const QString &prop, const drift::EffectParamSpec &spec, const QString &group,
                const QString &groupLabel)
{
    PropRow row = scalarRow(prop, spec.label, group, groupLabel, spec.min, spec.max, 2);
    row.step = spec.step;
    if (spec.isColor()) {
        row.kind = QStringLiteral("color");
        row.tracks = {prop + QLatin1String(".r"), prop + QLatin1String(".g"),
                      prop + QLatin1String(".b"), prop + QLatin1String(".a")};
        row.min = row.max = 0.0;
    } else if (spec.isVec2()) {
        row.kind = QStringLiteral("vec2");
        row.tracks = {prop + QLatin1String(".x"), prop + QLatin1String(".y")};
    } else if (spec.isRegion()) {
        row.kind = QStringLiteral("region");
        row.tracks = {prop + QLatin1String(".x"), prop + QLatin1String(".y"),
                      prop + QLatin1String(".w"), prop + QLatin1String(".h")};
    } else if (spec.isBoolean()) {
        row.kind = QStringLiteral("bool");
        row.min = row.max = 0.0;
        row.decimals = 0;
    } else if (spec.isEnum()) {
        row.kind = QStringLiteral("enum");
        row.min = row.max = 0.0;
        row.decimals = 0;
    } else if (spec.isInt()) {
        row.kind = QStringLiteral("int");
        row.decimals = 0;
    }
    return row;
}

} // namespace

KeyframeController::KeyframeController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

QVariantList KeyframeController::animatableProperties(int track, int clip) const
{
    const drift::Project &project = m_app->m_project;
    if (track < 0 || track >= project.tracks().size())
        return {};
    const drift::Track &owner = project.tracks().at(track);
    if (clip < 0 || clip >= owner.clips.size())
        return {};
    const drift::Clip &target = owner.clips.at(clip);

    const bool audio = target.type == drift::ClipType::Audio;
    const bool hasVolume = audio || target.type == drift::ClipType::Video;
    const bool textual = target.type == drift::ClipType::Text || target.type == drift::ClipType::Subtitle;

    QList<PropRow> rows;
    if (!audio) {
        const QString group = QStringLiteral("transform");
        const QString groupLabel = tr("Transform");
        const double w = project.width();
        const double h = project.height();
        rows << scalarRow(QStringLiteral("x"), QStringLiteral("X"), group, groupLabel, -w, 2 * w, 0)
             << scalarRow(QStringLiteral("y"), QStringLiteral("Y"), group, groupLabel, -h, 2 * h, 0)
             << scalarRow(QStringLiteral("width"), tr("Width"), group, groupLabel, 1, std::max(2 * w, 2.0), 0)
             << scalarRow(QStringLiteral("height"), tr("Height"), group, groupLabel, 1, std::max(2 * h, 2.0), 0)
             << scalarRow(QStringLiteral("rotation"), tr("Angle"), group, groupLabel, -180, 180, 1)
             << scalarRow(QStringLiteral("rotationX"), tr("Tilt X"), group, groupLabel, -180, 180, 1)
             << scalarRow(QStringLiteral("rotationY"), tr("Tilt Y"), group, groupLabel, -180, 180, 1)
             << scalarRow(QStringLiteral("z"), tr("Depth"), group, groupLabel, -4000, 1500, 0)
             << scalarRow(QStringLiteral("perspective"), tr("Perspective"), group, groupLabel, 200, 8000, 0)
             << scalarRow(QStringLiteral("opacity"), tr("Opacity"), group, groupLabel, 0, 1, 2);
    }
    if (hasVolume)
        rows << scalarRow(QStringLiteral("volume"), tr("Volume"), QStringLiteral("audio"), tr("Audio"), 0, 2, 2);

    // Effects and masks live on the adjustment linked to the clip. Video effects and masks
    // are not offered on audio clips, matching KeyframeGraph.supportsProperty.
    if (!audio) {
        if (const drift::Clip *host = m_app->effectHostClip(track, clip, drift::AdjustmentKind::VideoEffects)) {
            for (int i = 0; i < host->effects.size(); ++i) {
                const drift::Effect &effect = host->effects.at(i);
                const EffectPresetEntry *def = effectDefForId(effect.catalogId);
                if (!def)
                    continue;
                const QString group = QStringLiteral("fx.%1").arg(i);
                for (const drift::EffectParamSpec &spec : def->meta.parameters) {
                    if (spec.desktopGlOnly && QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES)
                        continue;
                    if (spec.isFilePath() || spec.isClip() || spec.isSampler())
                        continue;
                    rows << specRow(QStringLiteral("fx.%1.%2").arg(i).arg(spec.key), spec, group,
                                    def->meta.displayName);
                }
            }
        }
        if (const drift::Clip *host = m_app->effectHostClip(track, clip, drift::AdjustmentKind::Mask)) {
            const QStringList labels = {tr("Center X"), tr("Center Y"), tr("Width"),
                                        tr("Height"), tr("Rotation"), tr("Feather")};
            const QStringList &keys = drift::maskKeyframeProperties();
            for (int k = 0; k < keys.size(); ++k) {
                const QString &key = keys.at(k);
                const QString prop = QStringLiteral("mask.%1").arg(key);
                if (key == QLatin1String("x") || key == QLatin1String("y"))
                    rows << scalarRow(prop, labels.at(k), QStringLiteral("mask"), tr("Mask"), 0, 1, 3);
                else if (key == QLatin1String("w") || key == QLatin1String("h"))
                    rows << scalarRow(prop, labels.at(k), QStringLiteral("mask"), tr("Mask"), 0.05, 1, 3);
                else if (key == QLatin1String("rotation"))
                    rows << scalarRow(prop, labels.at(k), QStringLiteral("mask"), tr("Mask"), -180, 180, 1);
                else
                    rows << scalarRow(prop, labels.at(k), QStringLiteral("mask"), tr("Mask"), 0, 64, 0);
            }
        }
    }
    if (const drift::Clip *host = m_app->effectHostClip(track, clip, drift::AdjustmentKind::AudioEffects)) {
        for (int i = 0; i < host->audioEffects.size(); ++i) {
            const drift::Effect &effect = host->audioEffects.at(i);
            const AudioEffectEntry *def = audioEffectDefForId(effect.catalogId);
            if (!def)
                continue;
            const QString group = QStringLiteral("afx.%1").arg(i);
            for (const drift::EffectParamSpec &spec : def->parameters)
                rows << specRow(QStringLiteral("afx.%1.%2").arg(i).arg(spec.key), spec, group, def->displayName);
        }
    }

    if (textual) {
        for (const QString &key : drift::textKeyframeProperties(target.textStyle)) {
            const QString prop = QStringLiteral("text.%1").arg(key);
            rows << scalarRow(prop, m_app->keyframePropertyLabel(track, clip, prop), QStringLiteral("text"),
                              tr("Text"), 0, 0, 2);
        }
    }
    if (target.type == drift::ClipType::Shape) {
        for (const QString &key : drift::shapeKeyframeProperties(target.shapeStyle)) {
            const QString prop = QStringLiteral("shape.%1").arg(key);
            rows << scalarRow(prop, m_app->keyframePropertyLabel(track, clip, prop), QStringLiteral("shape"),
                              tr("Shape"), 0, 0, 2);
        }
    }
    if (target.type == drift::ClipType::Model3d) {
        for (const QString &key : drift::model3dKeyframeProperties()) {
            const QString prop = QStringLiteral("model3d.%1").arg(key);
            rows << scalarRow(prop, m_app->keyframePropertyLabel(track, clip, prop), QStringLiteral("model3d"),
                              tr("3D model"), 0, 0, 2);
        }
    }
    if (target.type == drift::ClipType::Pdf) {
        for (const QString &key : drift::pdfKeyframeProperties()) {
            const QString prop = QStringLiteral("pdf.%1").arg(key);
            rows << scalarRow(prop, m_app->keyframePropertyLabel(track, clip, prop), QStringLiteral("pdf"),
                              tr("PDF"), 0, 0, 2);
        }
    }
    // Vector channels only appear once animated: a vector has no fixed key set to offer.
    if (target.type == drift::ClipType::Vector) {
        for (auto it = target.vector.keyframes.constBegin(); it != target.vector.keyframes.constEnd(); ++it) {
            if (it->isEmpty())
                continue;
            const QString prop = QStringLiteral("vector.%1").arg(it.key());
            rows << scalarRow(prop, m_app->keyframePropertyLabel(track, clip, prop), QStringLiteral("vector"),
                              tr("Vector"), 0, 0, 2);
        }
    }

    const QStringList animatedNow = m_app->clipAnimatedProperties(track, clip);
    QVariantList out;
    out.reserve(rows.size());
    for (const PropRow &row : rows) {
        const QString &keyProp = row.tracks.first();
        const bool animated = std::any_of(row.tracks.cbegin(), row.tracks.cend(),
                                          [&animatedNow](const QString &t) { return animatedNow.contains(t); });
        out.append(QVariantMap{
            {QStringLiteral("prop"), row.prop},
            {QStringLiteral("label"), row.label},
            {QStringLiteral("group"), row.group},
            {QStringLiteral("groupLabel"), row.groupLabel},
            {QStringLiteral("kind"), row.kind},
            {QStringLiteral("tracks"), row.tracks},
            {QStringLiteral("keyProp"), keyProp},
            {QStringLiteral("min"), row.min},
            {QStringLiteral("max"), row.max},
            {QStringLiteral("hasRange"), row.max > row.min},
            {QStringLiteral("decimals"), row.decimals},
            {QStringLiteral("step"), row.step},
            {QStringLiteral("base"), m_app->propertyBaseValue(track, clip, keyProp, 0.0)},
            {QStringLiteral("animated"), animated},
            {QStringLiteral("enabled"), m_app->clipPropertyKeyframesEnabled(track, clip, keyProp)},
        });
    }
    return out;
}

QVariantList KeyframeController::keysFor(int track, int clip, const QString &prop) const
{
    return m_app->clipKeyframes(track, clip, prop);
}

bool KeyframeController::snapEnabled() const
{
    return m_app->snapEnabled();
}

double KeyframeController::scaleOf(double w, double h, double canvasW, double canvasH)
{
    return std::max(w / canvasW, h / canvasH);
}

QVariantMap KeyframeController::scaledRect(const QVariantMap &start, double scale, const QPointF &anchor)
{
    const auto num = [&start](const char *key) { return start.value(QLatin1String(key)).toDouble(); };
    const double k = scale / num("scale");
    const double w = std::max(1.0, num("w") * k);
    const double h = std::max(1.0, num("h") * k);
    const double ex = (anchor.x() - 0.5) * (num("w") - w);
    const double ey = (anchor.y() - 0.5) * (num("h") - h);
    const double rot = num("rot");
    const double cx = num("x") + num("w") / 2 + ex * std::cos(rot) - ey * std::sin(rot);
    const double cy = num("y") + num("h") / 2 + ex * std::sin(rot) + ey * std::cos(rot);
    return {{QStringLiteral("x"), cx - w / 2},
            {QStringLiteral("y"), cy - h / 2},
            {QStringLiteral("w"), w},
            {QStringLiteral("h"), h},
            {QStringLiteral("k"), k}};
}

void KeyframeController::previewScale(int track, int clip, const QVariantMap &start, double scale)
{
    if (start.value(QStringLiteral("scale")).toDouble() <= 0 || !m_app->isValidClipIndex(track, clip))
        return;
    const QVariantMap r = scaledRect(start, scale, QPointF(0.5, 0.5));
    const double x = r.value(QStringLiteral("x")).toDouble();
    const double y = r.value(QStringLiteral("y")).toDouble();
    const double w = r.value(QStringLiteral("w")).toDouble();
    const double h = r.value(QStringLiteral("h")).toDouble();
    const drift::ClipType type = m_app->m_project.tracks().at(track).clips.at(clip).type;
    if (type == drift::ClipType::Text || type == drift::ClipType::Subtitle) {
        const double pixelSize = start.value(QStringLiteral("pixelSize"), 64).toDouble();
        m_app->previewSetTextRect(track, clip, x, y, w, h, qRound(pixelSize * r.value(QStringLiteral("k")).toDouble()));
    } else {
        m_app->previewSetClipRect(track, clip, x, y, w, h);
    }
}

bool KeyframeController::resolveKey(int track, int clip, const QString &prop, double seconds,
                                    KeyTarget *out) const
{
    int hostTrack = track;
    int hostClip = clip;
    m_app->redirectToKeyframeHost(&hostTrack, &hostClip, prop);
    if (!m_app->isValidClipIndex(hostTrack, hostClip))
        return false;
    const drift::Clip &host = m_app->m_project.tracks().at(hostTrack).clips.at(hostClip);
    const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, prop);
    if (!kt)
        return false;
    const drift::TimeUs at = kt->nearestKeyframe(drift::secondsToUs(seconds) - host.timelineStart,
                                                 AppController::keyframeToleranceUs());
    if (at < 0)
        return false;
    *out = KeyTarget{prop, hostTrack, hostClip, at, 0.0};
    return true;
}

// Adds the partner of every x or y key: the y key at the same time, or the nearest one within
// tolerance. Originals are kept first so a caller's dv is never lost to a mirror of the same key.
QList<KeyframeController::KeyTarget> KeyframeController::expandLinked(const QList<KeyTarget> &keys) const
{
    const auto same = [](const KeyTarget &a, const KeyTarget &b) {
        return a.track == b.track && a.clip == b.clip && a.rel == b.rel && a.prop == b.prop;
    };
    const auto contains = [&same](const QList<KeyTarget> &list, const KeyTarget &key) {
        return std::any_of(list.cbegin(), list.cend(), [&](const KeyTarget &o) { return same(o, key); });
    };

    QList<KeyTarget> out;
    for (const KeyTarget &key : keys) {
        if (!contains(out, key))
            out.append(key);
    }
    if (!m_app->m_positionLinked)
        return out;

    const int originals = out.size();
    for (int i = 0; i < originals; ++i) {
        const KeyTarget key = out.at(i);
        if (key.prop != QLatin1String("x") && key.prop != QLatin1String("y"))
            continue;
        KeyTarget mirror{key.prop == QLatin1String("x") ? QStringLiteral("y") : QStringLiteral("x"),
                         key.track, key.clip, -1, 0.0};
        const drift::Clip &host = m_app->m_project.tracks().at(key.track).clips.at(key.clip);
        const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, mirror.prop);
        if (!kt)
            continue;
        mirror.rel = kt->nearestKeyframe(key.rel, AppController::keyframeToleranceUs());
        if (mirror.rel >= 0 && !contains(out, mirror))
            out.append(mirror);
    }
    return out;
}

// Keying Scale pins x and y at the same time (addKeysAt), so a Scale key carries those pins along
// when it moves. Only keys exactly at its time count, not other x or y keys near it.
QList<KeyframeController::KeyTarget> KeyframeController::withPins(const QList<KeyTarget> &keys) const
{
    QList<KeyTarget> out = keys;
    const drift::TimeUs eps = drift::secondsToUs(KeyframeRows::kTimeEps);
    for (const KeyTarget &key : keys) {
        if (key.prop != QLatin1String("width") && key.prop != QLatin1String("height"))
            continue;
        const drift::Clip &host = m_app->m_project.tracks().at(key.track).clips.at(key.clip);
        for (const QString &name : {QStringLiteral("x"), QStringLiteral("y")}) {
            const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, name);
            if (!kt)
                continue;
            const drift::TimeUs rel = kt->nearestKeyframe(key.rel, eps);
            if (rel < 0)
                continue;
            const bool present = std::any_of(out.cbegin(), out.cend(), [&](const KeyTarget &o) {
                return o.track == key.track && o.clip == key.clip && o.rel == rel && o.prop == name;
            });
            if (!present)
                out.append(KeyTarget{name, key.track, key.clip, rel, 0.0});
        }
    }
    return out;
}

QStringList KeyframeController::movedProps(const QStringList &props) const
{
    QStringList out = props;
    if (out.contains(QStringLiteral("width")) || out.contains(QStringLiteral("height")))
        out << QStringLiteral("x") << QStringLiteral("y");
    return mirrorLinkedProps(out);
}

QStringList KeyframeController::mirrorLinkedProps(const QStringList &props) const
{
    QStringList out = props;
    out.removeDuplicates();
    if (!m_app->m_positionLinked)
        return out;
    const QString x = QStringLiteral("x");
    const QString y = QStringLiteral("y");
    if (props.contains(x) && !out.contains(y))
        out.append(y);
    if (props.contains(y) && !out.contains(x))
        out.append(x);
    return out;
}

// The value the property shows at `seconds`. A track with animation switched off reads as its
// first key, which is not what propertyValueAt evaluates.
double KeyframeController::shownValue(int track, int clip, const QString &prop, double seconds) const
{
    int hostTrack = track;
    int hostClip = clip;
    m_app->redirectToKeyframeHost(&hostTrack, &hostClip, prop);
    if (m_app->isValidClipIndex(hostTrack, hostClip)) {
        const drift::Clip &host = m_app->m_project.tracks().at(hostTrack).clips.at(hostClip);
        const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, prop);
        if (kt && !kt->isEmpty() && !kt->enabled())
            return kt->keyframes().first().value;
    }
    return m_app->propertyValueAt(track, clip, prop, seconds, 0.0);
}

bool KeyframeController::eraseKeys(const QList<KeyTarget> &keys)
{
    bool any = false;
    for (const KeyTarget &key : keys) {
        drift::Clip &host = m_app->m_project.tracks()[key.track].clips[key.clip];
        drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, key.prop, /*create=*/false);
        if (!kt || !kt->keyframes().contains(key.rel))
            continue;
        kt->removeKeyframe(key.rel);
        any = true;
    }
    return any;
}

void KeyframeController::addKeysAt(int track, int clip, const QStringList &props, double seconds)
{
    if (!m_app->isValidClipIndex(track, clip))
        return;
    const drift::Project before = m_app->m_project;

    QStringList targets = props;
    // The Scale row keys width and height. X and Y are pinned at their current values so the clip
    // keeps its position while it resizes.
    if (props.contains(QStringLiteral("width")) && props.contains(QStringLiteral("height")))
        targets << QStringLiteral("x") << QStringLiteral("y");
    targets = mirrorLinkedProps(targets);

    struct Write
    {
        QString prop;
        int track;
        int clip;
        drift::TimeUs rel;
        double value;
    };
    QList<Write> writes;
    for (const QString &prop : targets) {
        int hostTrack = track;
        int hostClip = clip;
        m_app->redirectToKeyframeHost(&hostTrack, &hostClip, prop);
        if (!m_app->isValidClipIndex(hostTrack, hostClip))
            continue;
        const drift::Clip &host = m_app->m_project.tracks().at(hostTrack).clips.at(hostClip);
        const drift::TimeUs rel = qBound<drift::TimeUs>(
            0, drift::secondsToUs(seconds) - host.timelineStart, host.timelineDuration);
        // A key already there keeps its tangents and its value, so it is not touched at all.
        const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, prop);
        if (kt && kt->nearestKeyframe(rel, AppController::keyframeWriteSnapUs()) >= 0)
            continue;
        writes.append(Write{prop, hostTrack, hostClip, rel, shownValue(track, clip, prop, seconds)});
    }
    if (writes.isEmpty())
        return;

    for (const Write &write : writes) {
        drift::Clip &host = m_app->m_project.tracks()[write.track].clips[write.clip];
        m_app->writeClipProp(host, write.prop, write.rel, write.value, /*autoKey=*/false, /*force=*/true);
    }
    m_app->pushProjectEdit(before, tr("Add keyframes"));
    m_app->finishEdit(tr("Keyframes added"));
}

void KeyframeController::removeKeysAt(int track, int clip, const QStringList &props, double seconds)
{
    if (!m_app->isValidClipIndex(track, clip))
        return;
    const drift::Project before = m_app->m_project;

    QList<KeyTarget> hits;
    for (const QString &prop : mirrorLinkedProps(props)) {
        KeyTarget key;
        if (resolveKey(track, clip, prop, seconds, &key))
            hits.append(key);
    }
    if (!eraseKeys(hits))
        return;
    m_app->pushProjectEdit(before, tr("Remove keyframes"));
    m_app->finishEdit(tr("Keyframes removed"));
}

void KeyframeController::removeKeys(int track, int clip, const QVariantList &keys)
{
    if (!m_app->isValidClipIndex(track, clip))
        return;
    const drift::Project before = m_app->m_project;

    QList<KeyTarget> hits;
    for (const QVariant &ref : keys) {
        const QVariantMap map = ref.toMap();
        KeyTarget key;
        if (resolveKey(track, clip, map.value(QStringLiteral("prop")).toString(),
                       map.value(QStringLiteral("seconds")).toDouble(), &key))
            hits.append(key);
    }
    if (!eraseKeys(expandLinked(hits)))
        return;
    m_app->pushProjectEdit(before, tr("Delete keyframes"));
    m_app->finishEdit(tr("Keyframes deleted"));
}

void KeyframeController::clearProperties(int track, int clip, const QStringList &props)
{
    if (!m_app->isValidClipIndex(track, clip))
        return;
    const drift::Project before = m_app->m_project;

    struct Clear
    {
        QString prop;
        int track;
        int clip;
        double value;
    };
    QList<Clear> clears;
    const double playhead = m_app->playheadSeconds();
    for (const QString &prop : mirrorLinkedProps(props)) {
        int hostTrack = track;
        int hostClip = clip;
        m_app->redirectToKeyframeHost(&hostTrack, &hostClip, prop);
        if (!m_app->isValidClipIndex(hostTrack, hostClip))
            continue;
        const drift::Clip &host = m_app->m_project.tracks().at(hostTrack).clips.at(hostClip);
        const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, prop);
        if (!kt || kt->isEmpty())
            continue;
        clears.append(Clear{prop, hostTrack, hostClip, shownValue(track, clip, prop, playhead)});
    }
    if (clears.isEmpty())
        return;

    // Emptying the track and then writing without auto-key lands the value on the property's static
    // home: the single key at 0 for a transform, the param field for an effect.
    for (const Clear &c : clears) {
        drift::Clip &host = m_app->m_project.tracks()[c.track].clips[c.clip];
        drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, c.prop, /*create=*/false);
        const QList<drift::TimeUs> times = kt->keyframes().keys();
        for (drift::TimeUs time : times)
            kt->removeKeyframe(time);
        kt->setEnabled(true);
        m_app->writeClipProp(host, c.prop, 0, c.value, /*autoKey=*/false, /*force=*/false);
    }
    m_app->pushProjectEdit(before, tr("Clear keyframes"));
    m_app->finishEdit(tr("Keyframes cleared"));
}

QVariantMap KeyframeController::keyRef(const KeyTarget &key, drift::TimeUs rel) const
{
    const drift::Clip &host = m_app->m_project.tracks().at(key.track).clips.at(key.clip);
    return QVariantMap{{QStringLiteral("prop"), key.prop},
                       {QStringLiteral("seconds"), drift::usToSeconds(host.timelineStart + rel)}};
}

QList<KeyframeController::KeyTarget> KeyframeController::resolveKeys(int track, int clip,
                                                                     const QVariantList &keys) const
{
    QList<KeyTarget> own;
    for (const QVariant &ref : keys) {
        const QVariantMap map = ref.toMap();
        KeyTarget key;
        if (!resolveKey(track, clip, map.value(QStringLiteral("prop")).toString(),
                        map.value(QStringLiteral("seconds")).toDouble(), &key))
            continue;
        key.dv = map.value(QStringLiteral("dv")).toDouble();
        own.append(key);
    }
    return own;
}

// Puts each affected track back to its pre-drag state, so every preview call starts from the press.
// A key moved over another one and back must not lose the other.
void KeyframeController::restorePreviewTracks(int track, int clip, const QVariantList &keys)
{
    const drift::Project &original = m_app->m_previewDragBefore;
    for (const QVariant &ref : keys) {
        const QString prop = ref.toMap().value(QStringLiteral("prop")).toString();
        for (const QString &name : movedProps({prop})) {
            int hostTrack = track;
            int hostClip = clip;
            m_app->redirectToKeyframeHost(&hostTrack, &hostClip, name);
            if (!m_app->isValidClipIndex(hostTrack, hostClip))
                continue;
            const drift::KeyframeTrack<double> *source =
                m_app->propKeyframeTrack(original.tracks().at(hostTrack).clips.at(hostClip), name);
            if (!source)
                continue;
            drift::Clip &host = m_app->m_project.tracks()[hostTrack].clips[hostClip];
            *m_app->propKeyframeTrack(host, name, /*create=*/true) = *source;
        }
    }
}

// Lifts every key before placing any, so a key can land on the old spot of another.
void KeyframeController::relocate(const QList<Moved> &moved)
{
    for (const Moved &m : moved) {
        drift::Clip &host = m_app->m_project.tracks()[m.from.track].clips[m.from.clip];
        if (drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, m.from.prop, /*create=*/false))
            kt->removeKeyframe(m.from.rel);
    }
    for (const Moved &m : moved) {
        drift::Clip &host = m_app->m_project.tracks()[m.from.track].clips[m.from.clip];
        if (drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, m.from.prop, /*create=*/true))
            kt->setKeyframe(m.to, m.key);
    }
}

// Shared by moveKeys and previewMoveKeys. Reads the keys as they are in the project, so a preview
// caller restores its tracks first.
QVariantList KeyframeController::applyMove(const QList<KeyTarget> &own, double dtSeconds, bool *changed)
{
    // Keys outside their clip are left where they are and do not limit the move.
    QList<KeyTarget> moving;
    drift::TimeUs low = std::numeric_limits<drift::TimeUs>::min();
    drift::TimeUs high = std::numeric_limits<drift::TimeUs>::max();
    for (const KeyTarget &key : expandLinked(withPins(own))) {
        const drift::Clip &host = m_app->m_project.tracks().at(key.track).clips.at(key.clip);
        if (key.rel < 0 || key.rel > host.timelineDuration)
            continue;
        low = qMax(low, -key.rel);
        high = qMin(high, host.timelineDuration - key.rel);
        moving.append(key);
    }
    if (moving.isEmpty())
        return {};
    const drift::TimeUs dt = qBound<drift::TimeUs>(low, drift::secondsToUs(dtSeconds), high);

    QList<Moved> moved;
    *changed = dt != 0;
    for (const KeyTarget &key : moving) {
        const drift::Clip &host = m_app->m_project.tracks().at(key.track).clips.at(key.clip);
        const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, key.prop);
        drift::Keyframe<double> copy = kt->keyframes().value(key.rel);
        copy.value += key.dv;
        *changed = *changed || key.dv != 0.0;
        moved.append(Moved{key, key.rel + dt, copy});
    }

    QVariantList refs;
    QVariantList collisions;
    for (const Moved &m : moved) {
        refs.append(keyRef(m.from, m.to));
        if (m.to == m.from.rel)
            continue;
        const drift::Clip &host = m_app->m_project.tracks().at(m.from.track).clips.at(m.from.clip);
        const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, m.from.prop);
        const bool movesAway = std::any_of(moved.cbegin(), moved.cend(), [&m](const Moved &o) {
            return o.from.track == m.from.track && o.from.clip == m.from.clip && o.from.prop == m.from.prop
                   && o.from.rel == m.to;
        });
        if (kt && kt->keyframes().contains(m.to) && !movesAway)
            collisions.append(refs.last());
    }
    setCollisions(collisions);
    if (*changed)
        relocate(moved);
    return refs;
}

void KeyframeController::setCollisions(const QVariantList &collisions)
{
    if (m_collisions == collisions)
        return;
    m_collisions = collisions;
    emit collisionsChanged();
}

void KeyframeController::clearCollisions()
{
    setCollisions({});
}

bool KeyframeController::sameRefs(const QVariantList &a, const QVariantList &b)
{
    if (a.size() != b.size())
        return false;
    const auto id = [](const QVariant &v) {
        const QVariantMap m = v.toMap();
        return m.value(QStringLiteral("prop")).toString() + QLatin1Char('@')
               + QString::number(qRound64(m.value(QStringLiteral("seconds")).toDouble() * 1e6));
    };
    QSet<QString> ids;
    for (const QVariant &v : a)
        ids.insert(id(v));
    return std::all_of(b.cbegin(), b.cend(), [&](const QVariant &v) { return ids.contains(id(v)); });
}

bool KeyframeController::collides(const QString &prop, double seconds) const
{
    const qint64 us = qRound64(seconds * 1e6);
    return std::any_of(m_collisions.cbegin(), m_collisions.cend(), [&](const QVariant &c) {
        const QVariantMap cm = c.toMap();
        return cm.value(QStringLiteral("prop")).toString() == prop
               && qRound64(cm.value(QStringLiteral("seconds")).toDouble() * 1e6) == us;
    });
}

bool KeyframeController::collidesWith(const QVariantList &refs) const
{
    return std::any_of(refs.cbegin(), refs.cend(), [this](const QVariant &r) {
        const QVariantMap rm = r.toMap();
        return collides(rm.value(QStringLiteral("prop")).toString(), rm.value(QStringLiteral("seconds")).toDouble());
    });
}

double KeyframeController::baseValue(int track, int clip, const QString &prop) const
{
    return m_app->propertyBaseValue(track, clip, prop, 0.0);
}

QVariantMap KeyframeController::curveRange(const QString &prop, const QVariantList &points, double base, bool hasRange,
                                           double min, double max) const
{
    QList<KeyframeCurves::Point> pts;
    for (const QVariant &p : points)
        pts.append(KeyframeCurves::pointFromMap(p.toMap()));
    const KeyframeCurves::Range range = KeyframeCurves::valueRange(prop, pts, base, hasRange, min, max);
    return {{QStringLiteral("min"), range.min}, {QStringLiteral("max"), range.max}};
}

QVariantList KeyframeController::nudgeKeys(int track, int clip, const QVariantList &keys, double dtSeconds)
{
    if (!m_app->isValidClipIndex(track, clip) || keys.isEmpty())
        return {};
    const bool continuing = m_app->previewDragActive() && m_app->m_previewDragAuto && track == m_nudgeTrack
                            && clip == m_nudgeClip && sameRefs(keys, m_nudgeResult);
    if (!continuing) {
        if (m_app->previewDragActive() && m_app->m_previewDragAuto)
            m_app->commitPreviewDrag();
        m_nudgeBase = keys;
        m_nudgeTotal = 0.0;
        m_nudgeTrack = track;
        m_nudgeClip = clip;
    }
    m_app->beginImplicitPreviewDrag(tr("Nudge keyframes"));
    m_nudgeResult = previewMoveKeys(track, clip, m_nudgeBase, m_nudgeTotal + dtSeconds);
    if (m_nudgeResult.size() == m_nudgeBase.size() && !m_nudgeResult.isEmpty()) {
        m_nudgeTotal = m_nudgeResult.first().toMap().value(QStringLiteral("seconds")).toDouble()
                       - m_nudgeBase.first().toMap().value(QStringLiteral("seconds")).toDouble();
    } else {
        m_nudgeTotal += dtSeconds;
    }
    return m_nudgeResult;
}

// Shared by previewStretchKeys. A factor that would merge two selected keys is refused, leaving the
// tracks at the press state, so the drag does not hand over a key that the next move cannot restore.
QVariantList KeyframeController::applyStretch(const QList<KeyTarget> &own, double pivotSeconds, double factor)
{
    const double scale = qMax(factor, 0.05);

    QList<Moved> moved;
    for (const KeyTarget &key : expandLinked(withPins(own))) {
        const drift::Clip &host = m_app->m_project.tracks().at(key.track).clips.at(key.clip);
        if (key.rel < 0 || key.rel > host.timelineDuration)
            continue;
        const drift::TimeUs pivot = drift::secondsToUs(pivotSeconds) - host.timelineStart;
        const drift::TimeUs to = qBound<drift::TimeUs>(
            drift::TimeUs{0}, pivot + qRound64(static_cast<double>(key.rel - pivot) * scale),
            host.timelineDuration);
        const drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, key.prop);
        drift::Keyframe<double> copy = kt->keyframes().value(key.rel);
        copy.inDx *= scale;
        copy.outDx *= scale;
        moved.append(Moved{key, to, copy});
    }

    bool collides = false;
    for (qsizetype i = 0; i < moved.size() && !collides; ++i) {
        for (qsizetype j = i + 1; j < moved.size() && !collides; ++j) {
            const KeyTarget &a = moved.at(i).from;
            const KeyTarget &b = moved.at(j).from;
            collides = moved.at(i).to == moved.at(j).to && a.track == b.track && a.clip == b.clip
                       && a.prop == b.prop;
        }
    }

    QVariantList refs;
    if (collides) {
        for (const Moved &m : moved)
            refs.append(keyRef(m.from, m.from.rel));
        return refs;
    }
    for (const Moved &m : moved)
        refs.append(keyRef(m.from, m.to));
    relocate(moved);
    return refs;
}

double KeyframeController::snapKeyTime(double seconds, double pxPerSecond, const QVariantList &otherKeyTimes,
                                       bool snap) const
{
    const double fps = std::max(1, m_app->m_project.fps());
    const double framed = std::round(seconds * fps) / fps;
    if (!snap || pxPerSecond <= 0.0)
        return framed;
    const double radius = 8.0 / pxPerSecond;
    double best = framed;
    double bestDist = radius;
    auto consider = [&](double target) {
        const double d = std::abs(target - seconds);
        if (d <= bestDist) {
            bestDist = d;
            best = target;
        }
    };
    consider(drift::usToSeconds(m_app->m_playheadUs));
    for (const QVariant &t : otherKeyTimes)
        consider(t.toDouble());
    return best;
}

static QStringList propsOf(const QVariantList &keys)
{
    QStringList props;
    for (const QVariant &ref : keys)
        props.append(ref.toMap().value(QStringLiteral("prop")).toString());
    return props;
}

QVariantList KeyframeController::previewMoveKeys(int track, int clip, const QVariantList &keys,
                                                 double dtSeconds)
{
    if (!m_app->isValidClipIndex(track, clip))
        return {};
    if (!m_app->previewDragActive())
        m_app->beginPreviewDrag(tr("Move keyframes"));
    restorePreviewTracks(track, clip, keys);
    bool changed = false;
    const QVariantList refs = applyMove(resolveKeys(track, clip, keys), dtSeconds, &changed);
    m_app->emitPreviewEdit(track, clip, movedProps(propsOf(keys)));
    return refs;
}

QVariantList KeyframeController::moveKeys(int track, int clip, const QVariantList &keys,
                                          double dtSeconds)
{
    if (!m_app->isValidClipIndex(track, clip))
        return {};
    const drift::Project before = m_app->m_project;

    bool changed = false;
    const QVariantList refs = applyMove(resolveKeys(track, clip, keys), dtSeconds, &changed);
    if (changed) {
        m_app->pushProjectEdit(before, tr("Move keyframes"));
        m_app->finishEdit(tr("Keyframes moved"));
    }
    clearCollisions();
    return refs;
}

QVariantList KeyframeController::previewStretchKeys(int track, int clip, const QVariantList &keys,
                                                    double pivotSeconds, double factor)
{
    if (!m_app->isValidClipIndex(track, clip))
        return {};
    if (!m_app->previewDragActive())
        m_app->beginPreviewDrag(tr("Stretch keyframes"));
    restorePreviewTracks(track, clip, keys);
    const QVariantList refs = applyStretch(resolveKeys(track, clip, keys), pivotSeconds, factor);
    m_app->emitPreviewEdit(track, clip, movedProps(propsOf(keys)));
    return refs;
}

void KeyframeController::setSegmentsEasing(int track, int clip, const QVariantList &keys,
                                           const QVariantMap &spec)
{
    const drift::Project before = m_app->m_project;
    const drift::EasingSpec easing = AppController::easingSpecFromMap(spec);
    bool any = false;
    for (const QVariant &ref : keys) {
        const QVariantMap map = ref.toMap();
        any = m_app->shapeKeyframeSegments(track, clip, map.value(QStringLiteral("prop")).toString(),
                                           map.value(QStringLiteral("seconds")).toDouble(), easing,
                                           /*allSegments=*/false)
              || any;
    }
    if (!any)
        return;
    m_app->pushProjectEdit(before, tr("Set easing"));
    m_app->finishEdit(tr("Easing set"));
}

int KeyframeController::copyKeys(int track, int clip, const QVariantList &keys)
{
    const QList<KeyTarget> hits = resolveKeys(track, clip, keys);

    drift::TimeUs earliest = std::numeric_limits<drift::TimeUs>::max();
    for (const KeyTarget &hit : hits) {
        const drift::Clip &host = m_app->m_project.tracks().at(hit.track).clips.at(hit.clip);
        earliest = qMin(earliest, host.timelineStart + hit.rel);
    }

    QList<ClipKey> copied;
    for (const KeyTarget &hit : hits) {
        const drift::Clip &host = m_app->m_project.tracks().at(hit.track).clips.at(hit.clip);
        ClipKey entry;
        entry.prop = hit.prop;
        entry.offsetUs = host.timelineStart + hit.rel - earliest;
        entry.key = m_app->propKeyframeTrack(host, hit.prop)->keyframes().value(hit.rel);

        const bool audio = hit.prop.startsWith(QLatin1String("afx."));
        if (audio || hit.prop.startsWith(QLatin1String("fx."))) {
            const auto &effects = audio ? host.audioEffects : host.effects;
            const int index = hit.prop.section(QLatin1Char('.'), 1, 1).toInt();
            entry.catalogId = effects.at(index).catalogId;
            entry.occurrence = 0;
            for (int i = 0; i < index; ++i) {
                if (effects.at(i).catalogId == entry.catalogId)
                    ++entry.occurrence;
            }
        }
        copied.append(entry);
    }

    m_clipboard = copied;
    emit clipboardChanged();
    return m_clipboard.size();
}

QVariantList KeyframeController::pasteKeys(int track, int clip, double atSeconds)
{
    if (m_clipboard.isEmpty() || !m_app->isValidClipIndex(track, clip))
        return {};
    const drift::Project before = m_app->m_project;

    QSet<QString> offered;
    for (const QVariant &row : animatableProperties(track, clip)) {
        for (const QString &name : row.toMap().value(QStringLiteral("tracks")).toStringList())
            offered.insert(name);
    }

    // An effect key follows its effect to the target: same catalogId, same occurrence among those.
    const auto remap = [this, track, clip](const ClipKey &entry, QString *prop) {
        const bool audio = entry.prop.startsWith(QLatin1String("afx."));
        if (!audio && !entry.prop.startsWith(QLatin1String("fx."))) {
            *prop = entry.prop;
            return true;
        }
        const drift::Clip *host = m_app->effectHostClip(
            track, clip, audio ? drift::AdjustmentKind::AudioEffects : drift::AdjustmentKind::VideoEffects);
        if (!host)
            return false;
        const auto &effects = audio ? host->audioEffects : host->effects;
        int seen = 0;
        for (int i = 0; i < effects.size(); ++i) {
            if (effects.at(i).catalogId != entry.catalogId)
                continue;
            if (seen++ == entry.occurrence) {
                const QString kind = audio ? QStringLiteral("afx") : QStringLiteral("fx");
                *prop = QStringLiteral("%1.%2.%3").arg(kind).arg(i).arg(entry.prop.section(QLatin1Char('.'), 2));
                return true;
            }
        }
        return false;
    };

    const drift::TimeUs base = drift::secondsToUs(atSeconds);
    QVariantList pasted;
    int skipped = 0;
    for (const ClipKey &entry : m_clipboard) {
        QString prop;
        if (!remap(entry, &prop) || !offered.contains(prop)) {
            ++skipped;
            continue;
        }
        int hostTrack = track;
        int hostClip = clip;
        m_app->redirectToKeyframeHost(&hostTrack, &hostClip, prop);
        if (!m_app->isValidClipIndex(hostTrack, hostClip)) {
            ++skipped;
            continue;
        }
        drift::Clip &host = m_app->m_project.tracks()[hostTrack].clips[hostClip];
        const drift::TimeUs rel = base + entry.offsetUs - host.timelineStart;
        if (rel < 0 || rel > host.timelineDuration) {
            ++skipped;
            continue;
        }
        drift::KeyframeTrack<double> *kt = m_app->propKeyframeTrack(host, prop, /*create=*/true);
        if (!kt) {
            ++skipped;
            continue;
        }
        const drift::TimeUs existing = kt->nearestKeyframe(rel, AppController::keyframeWriteSnapUs());
        const drift::TimeUs time = existing >= 0 ? existing : rel;
        kt->setEnabled(true);
        kt->setKeyframe(time, entry.key);
        pasted.append(QVariantMap{{QStringLiteral("prop"), prop},
                                  {QStringLiteral("seconds"), drift::usToSeconds(host.timelineStart + time)}});
    }

    if (!pasted.isEmpty()) {
        m_app->pushProjectEdit(before, tr("Paste keyframes"));
        m_app->finishEdit(tr("Keyframes pasted"));
    }
    if (skipped > 0)
        m_app->setLastMessage(tr("%n keyframe(s) not pasted", "", skipped), QStringLiteral("warning"));
    return pasted;
}
