#include "app/surfaces/EditorSurfaceTargets.h"

#include "app/color/ColorController.h"
#include "app/editor/AppController.h"
#include "app/editor/keymap/KeymapCatalog.h"
#include "app/surfaces/FaderScale.h"

#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

constexpr int kGestureIdleMs = 400;
const double kNan = std::nan("");

struct ColorSpec {
    const char *id;
    const char *label;
    double min;
    double max;
};

const ColorSpec kColorSliders[] = {
    {"exposure", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Exposure"), -4, 4},
    {"contrast", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Contrast"), -1, 1},
    {"highlights", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Highlights"), -1, 1},
    {"shadows", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Shadows"), -1, 1},
    {"whites", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Whites"), -1, 1},
    {"blacks", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Blacks"), -1, 1},
    {"temperature", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Temperature"), -1, 1},
    {"tint", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Tint"), -1, 1},
    {"saturation", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Saturation"), 0, 2},
    {"vibrance", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Vibrance"), -1, 1},
};

struct WheelSpec {
    const char *id;
    const char *label;
};

const WheelSpec kWheels[] = {
    {"lift", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Shadows wheel")},
    {"gamma", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Midtones wheel")},
    {"gain", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Highlights wheel")},
};

struct ClipSpec {
    const char *id;
    const char *label;
    QStringList keys;
    double min;
    double max;
};

const QList<ClipSpec> &clipSpecs()
{
    static const QList<ClipSpec> specs = {
        {"clip.volume", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Clip volume"), {"volume"}, 0, 2},
        {"clip.opacity", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Clip opacity"), {"opacity"}, 0, 1},
        {"clip.scale", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Clip scale"), {"scaleX", "scaleY"}, 0.05, 4},
        {"clip.rotation", QT_TRANSLATE_NOOP("EditorSurfaceTargets", "Clip rotation"), {"rotation"}, -180, 180},
    };
    return specs;
}

const ClipSpec *clipSpec(const QString &id)
{
    for (const ClipSpec &s : clipSpecs()) {
        if (id == QLatin1String(s.id))
            return &s;
    }
    return nullptr;
}

// "prop:<key>:<min>:<max>" and "effect:<catalogId>:<param>:<min>:<max>".
bool parseRange(const QStringList &parts, double *min, double *max)
{
    bool a = false;
    bool b = false;
    *min = parts.at(parts.size() - 2).toDouble(&a);
    *max = parts.at(parts.size() - 1).toDouble(&b);
    return a && b && *max > *min;
}

} // namespace

EditorSurfaceTargets::EditorSurfaceTargets(AppController *app, QObject *parent)
    : QObject(parent), m_app(app)
{
    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(kGestureIdleMs);
    connect(&m_commitTimer, &QTimer::timeout, this, &EditorSurfaceTargets::commitGesture);
    m_shuttleTimer.setInterval(33);
    connect(&m_shuttleTimer, &QTimer::timeout, this, &EditorSurfaceTargets::shuttleTick);

    connect(m_app, &AppController::selectionChanged, this, [this] {
        if (invalidated)
            invalidated();
    });
    connect(m_app, &AppController::tracksChanged, this, [this] {
        const QStringList before = stripLabels();
        m_stripsBuilt = false;
        if (before != stripLabels() && labelsChanged)
            labelsChanged();
    });
}

void EditorSurfaceTargets::refreshStrips() const
{
    const quint32 revision = m_app->tracksRevision();
    if (m_stripsBuilt && revision == m_stripRevision)
        return;
    m_stripRevision = revision;
    m_stripsBuilt = true;
    m_strips.clear();
    int videos = 0;
    int audios = 0;
    const QVariantList tracks = m_app->tracks();
    for (int i = 0; i < tracks.size(); ++i) {
        const QVariantMap t = tracks.at(i).toMap();
        const QString type = t.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("video"))
            ++videos;
        else if (type == QLatin1String("audio"))
            ++audios;
        else
            continue;
        if (!t.value(QStringLiteral("hasAudio")).toBool())
            continue;
        const bool video = type == QLatin1String("video");
        QString name = t.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            const int number = t.value(QStringLiteral("number")).toInt();
            name = QLatin1String(video ? "V" : "A") + QString::number(number > 0 ? number : (video ? videos : audios));
        }
        m_strips.append({i, name});
    }
}

int EditorSurfaceTargets::stripTrack(int slot) const
{
    refreshStrips();
    const int i = m_bank * kBankSize + slot - 1;
    return i >= 0 && i < m_strips.size() ? m_strips.at(i).track : -1;
}

QStringList EditorSurfaceTargets::stripLabels() const
{
    refreshStrips();
    QStringList out;
    for (int slot = 0; slot < kBankSize; ++slot) {
        const int i = m_bank * kBankSize + slot;
        out.append(i < m_strips.size() ? m_strips.at(i).name : QString());
    }
    return out;
}

double EditorSurfaceTargets::playheadSeconds() const
{
    return m_app->playheadSeconds();
}

void EditorSurfaceTargets::seek(double seconds)
{
    m_app->setPlayheadSeconds(std::max(0.0, seconds));
}

QList<SurfaceTarget> EditorSurfaceTargets::targets() const
{
    QList<SurfaceTarget> out;
    const QString playhead = tr("Playhead");
    out.append({"playhead.jog", tr("Jog (frames)"), playhead, SurfaceTarget::Relative});
    out.append({"playhead.shuttle", tr("Shuttle (spring fader)"), playhead, SurfaceTarget::Absolute});
    out.append({"playhead.scrub", tr("Playhead position"), playhead, SurfaceTarget::Absolute});
    out.append({"timeline.zoom", tr("Timeline zoom"), playhead, SurfaceTarget::Relative});

    const QString mixer = tr("Mixer");
    for (int n = 1; n <= kBankSize; ++n) {
        const QString s = QString::number(n);
        out.append({"mixer.volume." + s, tr("Strip %1 volume").arg(n), mixer, SurfaceTarget::Absolute});
        out.append({"mixer.pan." + s, tr("Strip %1 pan").arg(n), mixer, SurfaceTarget::Absolute});
        out.append({"mixer.mute." + s, tr("Strip %1 mute").arg(n), mixer, SurfaceTarget::Action});
        out.append({"mixer.solo." + s, tr("Strip %1 solo").arg(n), mixer, SurfaceTarget::Action});
    }
    out.append({"mixer.bankPrev", tr("Previous strip bank"), mixer, SurfaceTarget::Action});
    out.append({"mixer.bankNext", tr("Next strip bank"), mixer, SurfaceTarget::Action});

    const QString clip = tr("Selected clip");
    for (const ClipSpec &s : clipSpecs())
        out.append({QString::fromLatin1(s.id), tr(s.label), clip, SurfaceTarget::Absolute});

    const QString color = tr("Color");
    for (const ColorSpec &s : kColorSliders)
        out.append({QStringLiteral("color.") + QLatin1String(s.id), tr(s.label), color, SurfaceTarget::Absolute});
    for (const WheelSpec &w : kWheels) {
        const QString id = QStringLiteral("color.") + QLatin1String(w.id);
        out.append({id + QStringLiteral(".x"), tr("%1 hue X").arg(tr(w.label)), color, SurfaceTarget::Absolute});
        out.append({id + QStringLiteral(".y"), tr("%1 hue Y").arg(tr(w.label)), color, SurfaceTarget::Absolute});
        out.append({id + QStringLiteral(".luma"), tr("%1 brightness").arg(tr(w.label)), color,
                    SurfaceTarget::Absolute});
    }

    for (const keymap::ActionDef &a : keymap::actions()) {
        const keymap::CategoryDef *c = keymap::findCategory(QString::fromLatin1(a.category));
        out.append({QString::fromLatin1(a.id), keymap::translatedLabel(a),
                    c ? keymap::translatedCategoryLabel(*c) : tr("Actions"), SurfaceTarget::Action});
    }
    return out;
}

SurfaceTarget EditorSurfaceTargets::describe(const QString &id) const
{
    static const QHash<QString, SurfaceTarget> index = [this] {
        QHash<QString, SurfaceTarget> h;
        for (const SurfaceTarget &t : targets())
            h.insert(t.id, t);
        return h;
    }();
    const auto it = index.constFind(id);
    if (it != index.constEnd())
        return *it;
    SurfaceTarget t;
    t.id = id;
    t.label = id;
    t.kind = id.startsWith(QLatin1String("prop:")) || id.startsWith(QLatin1String("effect:"))
                 ? SurfaceTarget::Absolute
                 : SurfaceTarget::Action;
    return t;
}

QString EditorSurfaceTargets::idForProperty(const QString &key, double min, double max) const
{
    if (!(max > min))
        return {};
    const QString range = QString::number(min, 'g', 8) + QLatin1Char(':') + QString::number(max, 'g', 8);
    if (!key.startsWith(QLatin1String("fx.")))
        return QStringLiteral("prop:") + key + QLatin1Char(':') + range;
    const QStringList parts = key.split(QLatin1Char('.'));
    if (parts.size() < 3)
        return {};
    const int index = parts.at(1).toInt();
    const QVariantList effects = m_app->selectedClipEffects();
    if (index < 0 || index >= effects.size())
        return {};
    const QString catalogId = effects.at(index).toMap().value(QStringLiteral("catalogId")).toString();
    if (catalogId.isEmpty())
        return {};
    return QStringLiteral("effect:") + catalogId + QLatin1Char(':') + parts.mid(2).join(QLatin1Char('.'))
           + QLatin1Char(':') + range;
}

bool EditorSurfaceTargets::selectionValid() const
{
    return m_app->selectedTrack() >= 0 && m_app->selectedClip() >= 0;
}

bool EditorSurfaceTargets::resolveEffectKey(const QString &catalogId, const QString &param, QString *propKey) const
{
    const QVariantList effects = m_app->selectedClipEffects();
    for (int i = 0; i < effects.size(); ++i) {
        if (effects.at(i).toMap().value(QStringLiteral("catalogId")).toString() == catalogId) {
            *propKey = QStringLiteral("fx.%1.%2").arg(i).arg(param);
            return true;
        }
    }
    return false;
}

bool EditorSurfaceTargets::beginGesture(const QString &owner, std::function<void()> commit)
{
    if (!m_gestureOwner.isEmpty() && m_gestureOwner != owner)
        commitGesture();
    const bool started = m_gestureOwner.isEmpty();
    if (started) {
        m_gestureOwner = owner;
        m_gestureCommit = std::move(commit);
    }
    m_commitTimer.start();
    return started;
}

void EditorSurfaceTargets::commitGesture()
{
    m_commitTimer.stop();
    const std::function<void()> commit = std::move(m_gestureCommit);
    m_gestureCommit = nullptr;
    m_gestureOwner.clear();
    if (commit)
        commit();
}

void EditorSurfaceTargets::applyProp(const QStringList &keys, double value)
{
    if (!selectionValid())
        return;
    const QString owner = QStringLiteral("prop:") + keys.join(QLatin1Char(','));
    if (beginGesture(owner, [this] { m_app->commitPreviewDrag(); }))
        m_app->beginPreviewDrag(tr("Adjust with control surface"));
    for (const QString &key : keys) {
        m_app->previewSetClipKeyframe(m_app->selectedTrack(), m_app->selectedClip(), key,
                                      m_app->playheadSeconds(), value);
    }
}

bool EditorSurfaceTargets::trigger(const QString &id)
{
    if (id.startsWith(QLatin1String("mixer."))) {
        if (id == QLatin1String("mixer.bankPrev") || id == QLatin1String("mixer.bankNext")) {
            refreshStrips();
            const int banks = std::max(1, (static_cast<int>(m_strips.size()) + kBankSize - 1) / kBankSize);
            const int next = std::clamp(m_bank + (id == QLatin1String("mixer.bankNext") ? 1 : -1), 0, banks - 1);
            if (next != m_bank) {
                m_bank = next;
                if (invalidated)
                    invalidated();
                if (labelsChanged)
                    labelsChanged();
            }
            return true;
        }
        const QStringList parts = id.split(QLatin1Char('.'));
        if (parts.size() == 3) {
            const int track = stripTrack(parts.at(2).toInt());
            if (track < 0)
                return true;
            const QVariantMap t = m_app->tracks().at(track).toMap();
            if (parts.at(1) == QLatin1String("mute"))
                m_app->setTrackMuted(track, !t.value(QStringLiteral("muted")).toBool());
            else if (parts.at(1) == QLatin1String("solo"))
                m_app->setTrackSolo(track, !t.value(QStringLiteral("solo")).toBool());
            return true;
        }
    }
    return false;
}

void EditorSurfaceTargets::setAbsolute(const QString &id, double n)
{
    n = std::clamp(n, 0.0, 1.0);
    const QStringList parts = id.split(QLatin1Char(':'));

    if (id.startsWith(QLatin1String("mixer.volume.")) || id.startsWith(QLatin1String("mixer.pan."))) {
        const QStringList p = id.split(QLatin1Char('.'));
        const int track = stripTrack(p.last().toInt());
        if (track < 0)
            return;
        const bool volume = p.at(1) == QLatin1String("volume");
        const double value = volume ? surface::gainFromFader(n) : n * 2.0 - 1.0;
        const QString owner = id;
        beginGesture(owner, [this, track, volume, owner] {
            const double v = m_gestureLast.value(owner);
            if (volume)
                m_app->setTrackVolume(track, v);
            else
                m_app->setTrackPan(track, v);
        });
        m_gestureLast.insert(owner, value);
        if (volume)
            m_app->previewTrackVolume(track, value);
        else
            m_app->previewTrackPan(track, value);
        return;
    }
    if (id == QLatin1String("playhead.scrub")) {
        m_app->setPlayheadSeconds(n * m_app->durationSeconds());
        return;
    }
    if (id == QLatin1String("playhead.shuttle")) {
        const double rate = (n - 0.5) * 2.0;
        m_shuttleRate = std::abs(rate) < 0.05 ? 0.0 : rate;
        if (m_shuttleRate != 0.0 && !m_shuttleTimer.isActive())
            m_shuttleTimer.start();
        else if (m_shuttleRate == 0.0)
            m_shuttleTimer.stop();
        return;
    }
    if (const ClipSpec *s = clipSpec(id)) {
        applyProp(s->keys, s->min + n * (s->max - s->min));
        return;
    }
    if (id.startsWith(QLatin1String("prop:")) && parts.size() >= 4) {
        double min = 0;
        double max = 0;
        if (parseRange(parts, &min, &max))
            applyProp({parts.at(1)}, min + n * (max - min));
        return;
    }
    if (id.startsWith(QLatin1String("effect:")) && parts.size() >= 5) {
        double min = 0;
        double max = 0;
        QString key;
        if (parseRange(parts, &min, &max) && resolveEffectKey(parts.at(1), parts.at(2), &key))
            applyProp({key}, min + n * (max - min));
        return;
    }
    if (id.startsWith(QLatin1String("color."))) {
        ColorController *color = m_app->color();
        if (!color || !color->target().value(QStringLiteral("hasTarget")).toBool())
            return;
        const QStringList p = id.split(QLatin1Char('.'));
        if (beginGesture(QStringLiteral("color"), [color] { color->commitEdit(); }))
            color->beginEdit(tr("Adjust with control surface"));
        const QVariantMap params = color->target().value(QStringLiteral("params")).toMap();
        if (p.size() == 2) {
            for (const ColorSpec &s : kColorSliders) {
                if (p.at(1) == QLatin1String(s.id))
                    color->setParam(p.at(1), s.min + n * (s.max - s.min));
            }
            return;
        }
        const double v = n * 2.0 - 1.0;
        if (p.at(2) == QLatin1String("luma")) {
            color->setParam(p.at(1) + QStringLiteral("Luma"), v);
            return;
        }
        const QVariantList vec = params.value(p.at(1)).toList();
        double x = vec.size() > 0 ? vec.at(0).toDouble() : 0.0;
        double y = vec.size() > 1 ? vec.at(1).toDouble() : 0.0;
        (p.at(2) == QLatin1String("x") ? x : y) = v;
        const double len = std::hypot(x, y);
        if (len > 1.0) {
            x /= len;
            y /= len;
        }
        color->setVec2(p.at(1), x, y);
    }
}

void EditorSurfaceTargets::nudge(const QString &id, double ticks)
{
    if (id == QLatin1String("playhead.jog")) {
        m_jogRemainder += ticks;
        const int whole = static_cast<int>(m_jogRemainder);
        m_jogRemainder -= whole;
        if (whole != 0)
            m_app->stepFrames(whole);
        return;
    }
    if (id == QLatin1String("timeline.zoom")) {
        if (requestAction && ticks != 0.0)
            requestAction(ticks > 0 ? QStringLiteral("zoomIn") : QStringLiteral("zoomOut"));
        return;
    }
    const double cur = current(id);
    if (!std::isnan(cur))
        setAbsolute(id, cur + ticks * 0.01);
}

void EditorSurfaceTargets::shuttleTick()
{
    if (m_shuttleRate == 0.0) {
        m_shuttleTimer.stop();
        return;
    }
    // Full deflection is four frames a tick, about 120 frames a second.
    m_shuttleRemainder += m_shuttleRate * 4.0;
    const int whole = static_cast<int>(m_shuttleRemainder);
    m_shuttleRemainder -= whole;
    if (whole != 0)
        m_app->stepFrames(whole);
}

double EditorSurfaceTargets::current(const QString &id) const
{
    if (id.startsWith(QLatin1String("mixer."))) {
        const QStringList p = id.split(QLatin1Char('.'));
        if (p.size() != 3)
            return kNan;
        const int track = stripTrack(p.at(2).toInt());
        if (track < 0)
            return p.at(1) == QLatin1String("mute") || p.at(1) == QLatin1String("solo") ? 0.0 : kNan;
        if (p.at(1) == QLatin1String("volume"))
            return surface::faderFromGain(m_app->trackVolume(track));
        if (p.at(1) == QLatin1String("pan"))
            return (m_app->trackPan(track) + 1.0) / 2.0;
        if (p.at(1) == QLatin1String("mute"))
            return m_app->tracks().at(track).toMap().value(QStringLiteral("muted")).toBool() ? 1.0 : 0.0;
        if (p.at(1) == QLatin1String("solo"))
            return m_app->trackSolo(track) ? 1.0 : 0.0;
        return kNan;
    }
    if (id == QLatin1String("playPause"))
        return m_app->playing() ? 1.0 : 0.0;
    if (id == QLatin1String("stop"))
        return m_app->playing() ? 0.0 : 1.0;
    if (id == QLatin1String("playhead.scrub")) {
        const double d = m_app->durationSeconds();
        return d > 0.0 ? std::clamp(m_app->playheadSeconds() / d, 0.0, 1.0) : 0.0;
    }

    const QStringList parts = id.split(QLatin1Char(':'));
    double min = 0;
    double max = 1;
    QString key;
    if (const ClipSpec *s = clipSpec(id)) {
        key = s->keys.first();
        min = s->min;
        max = s->max;
    } else if (id.startsWith(QLatin1String("prop:")) && parts.size() >= 4 && parseRange(parts, &min, &max)) {
        key = parts.at(1);
    } else if (id.startsWith(QLatin1String("effect:")) && parts.size() >= 5 && parseRange(parts, &min, &max)) {
        if (!resolveEffectKey(parts.at(1), parts.at(2), &key))
            return kNan;
    } else if (id.startsWith(QLatin1String("color."))) {
        ColorController *color = m_app->color();
        const QVariantMap target = color ? color->target() : QVariantMap();
        if (!target.value(QStringLiteral("hasTarget")).toBool())
            return kNan;
        const QVariantMap params = target.value(QStringLiteral("params")).toMap();
        const QStringList p = id.split(QLatin1Char('.'));
        if (p.size() == 2) {
            for (const ColorSpec &s : kColorSliders) {
                if (p.at(1) == QLatin1String(s.id))
                    return std::clamp((params.value(p.at(1)).toDouble() - s.min) / (s.max - s.min), 0.0, 1.0);
            }
            return kNan;
        }
        if (p.at(2) == QLatin1String("luma"))
            return (params.value(p.at(1) + QStringLiteral("Luma")).toDouble() + 1.0) / 2.0;
        const QVariantList vec = params.value(p.at(1)).toList();
        const int axis = p.at(2) == QLatin1String("x") ? 0 : 1;
        return ((axis < vec.size() ? vec.at(axis).toDouble() : 0.0) + 1.0) / 2.0;
    } else {
        return kNan;
    }
    if (!selectionValid())
        return kNan;
    double value = m_app->propertyValueAt(m_app->selectedTrack(), m_app->selectedClip(), key,
                                          m_app->playheadSeconds(), kNan);
    if (key.startsWith(QLatin1String("fx.")) && std::isnan(value)) {
        const QStringList kp = key.split(QLatin1Char('.'));
        const QVariantList effects = m_app->selectedClipEffects();
        const int index = kp.at(1).toInt();
        if (index >= 0 && index < effects.size()) {
            for (const QVariant &pv : effects.at(index).toMap().value(QStringLiteral("params")).toList()) {
                if (pv.toMap().value(QStringLiteral("key")).toString() == kp.mid(2).join(QLatin1Char('.')))
                    value = pv.toMap().value(QStringLiteral("value")).toDouble();
            }
        }
    }
    return std::isnan(value) ? kNan : std::clamp((value - min) / (max - min), 0.0, 1.0);
}

} // namespace drift
