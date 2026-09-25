#include "TimelineClipsModel.h"

#include <QSet>
#include <QStringList>

TimelineClipsModel::TimelineClipsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

namespace {

// The roles whose value differs between two rows. dataChanged() with an empty role list
// invalidates every binding on the row, which on a clip delegate means re-running the filmstrip,
// the waveform query and both fade canvases — so a move must say it moved, and nothing else.
QList<int> changedRoles(const TimelineClipsModel::Row &a, const TimelineClipsModel::Row &b)
{
    QList<int> roles;
    const auto diff = [&roles](bool changed, int role) {
        if (changed)
            roles.append(role);
    };
    diff(a.id != b.id, TimelineClipsModel::IdRole);
    diff(a.name != b.name, TimelineClipsModel::NameRole);
    diff(a.path != b.path, TimelineClipsModel::PathRole);
    diff(a.kind != b.kind, TimelineClipsModel::KindRole);
    diff(a.adjustmentKind != b.adjustmentKind, TimelineClipsModel::AdjustmentKindRole);
    diff(a.linkedClipId != b.linkedClipId, TimelineClipsModel::LinkedClipIdRole);
    diff(a.linkId != b.linkId, TimelineClipsModel::LinkIdRole);
    diff(a.linked != b.linked, TimelineClipsModel::LinkedRole);
    diff(a.filmstripPath != b.filmstripPath, TimelineClipsModel::FilmstripPathRole);
    diff(a.textContent != b.textContent, TimelineClipsModel::TextContentRole);
    diff(a.rotationCorrection != b.rotationCorrection, TimelineClipsModel::RotationCorrectionRole);
    diff(a.start != b.start, TimelineClipsModel::StartRole);
    diff(a.duration != b.duration, TimelineClipsModel::DurationRole);
    diff(a.inPoint != b.inPoint, TimelineClipsModel::InPointRole);
    diff(a.outPoint != b.outPoint, TimelineClipsModel::OutPointRole);
    diff(a.sourceDuration != b.sourceDuration, TimelineClipsModel::SourceDurationRole);
    diff(a.audioStreamIndex != b.audioStreamIndex, TimelineClipsModel::AudioStreamIndexRole);
    diff(a.hasEmbeddedAudio != b.hasEmbeddedAudio, TimelineClipsModel::HasEmbeddedAudioRole);
    diff(a.fadeIn != b.fadeIn, TimelineClipsModel::FadeInRole);
    diff(a.fadeOut != b.fadeOut, TimelineClipsModel::FadeOutRole);
    diff(a.fadeCurve != b.fadeCurve, TimelineClipsModel::FadeCurveRole);
    diff(a.fadeShape != b.fadeShape, TimelineClipsModel::FadeShapeRole);
    diff(a.fadeHandles != b.fadeHandles, TimelineClipsModel::FadeHandlesRole);
    diff(a.effects != b.effects, TimelineClipsModel::EffectsRole);
    diff(a.audioEffects != b.audioEffects, TimelineClipsModel::AudioEffectsRole);
    return roles;
}

} // namespace

void TimelineClipsModel::setRows(QList<Row> rows)
{
    // Same clips in the same order is the common case — every trim, move, fade and effect edit.
    bool sameShape = rows.size() == m_rows.size();
    for (int i = 0; sameShape && i < rows.size(); ++i)
        sameShape = rows.at(i).id == m_rows.at(i).id;

    if (!sameShape) {
        // A split, delete, paste or add changes the set but keeps everything else in order.
        // Removing and inserting just those rows leaves every other delegate standing; a reset
        // would rebuild the whole track, filmstrips and waveforms included.
        QSet<QString> newIds;
        newIds.reserve(rows.size());
        for (const Row &row : rows)
            newIds.insert(row.id);
        QSet<QString> oldIds;
        oldIds.reserve(m_rows.size());
        for (const Row &row : m_rows)
            oldIds.insert(row.id);

        QStringList keptOld;
        for (const Row &row : m_rows) {
            if (newIds.contains(row.id))
                keptOld.append(row.id);
        }
        QStringList keptNew;
        for (const Row &row : rows) {
            if (oldIds.contains(row.id))
                keptNew.append(row.id);
        }
        const bool unique = newIds.size() == rows.size() && oldIds.size() == m_rows.size();
        if (!unique || keptOld != keptNew) {
            beginResetModel();
            m_rows = std::move(rows);
            endResetModel();
            return;
        }

        for (int i = m_rows.size() - 1; i >= 0;) {
            if (newIds.contains(m_rows.at(i).id)) {
                --i;
                continue;
            }
            int first = i;
            while (first > 0 && !newIds.contains(m_rows.at(first - 1).id))
                --first;
            beginRemoveRows({}, first, i);
            m_rows.remove(first, i - first + 1);
            endRemoveRows();
            i = first - 1;
        }
        for (int i = 0; i < rows.size();) {
            if (oldIds.contains(rows.at(i).id)) {
                ++i;
                continue;
            }
            int last = i;
            while (last + 1 < rows.size() && !oldIds.contains(rows.at(last + 1).id))
                ++last;
            beginInsertRows({}, i, last);
            for (int j = i; j <= last; ++j)
                m_rows.insert(j, rows.at(j));
            endInsertRows();
            i = last + 1;
        }
        // Same ids in the same order now; fall through to the per-role update.
    }

    for (int i = 0; i < rows.size(); ++i) {
        const QList<int> roles = changedRoles(m_rows.at(i), rows.at(i));
        if (roles.isEmpty())
            continue;
        m_rows[i] = rows.at(i);
        emit dataChanged(index(i), index(i), roles);
    }
}

void TimelineClipsModel::setDecorations(Decorations decorations)
{
    if (decorations == m_decorations)
        return;
    m_decorations = std::move(decorations);
    emit decorationsChanged();
}

QVariantList TimelineClipsModel::gaps() const
{
    QVariantList out;
    out.reserve(m_decorations.gaps.size());
    for (const Gap &gap : m_decorations.gaps)
        out.append(QVariantMap{{QStringLiteral("start"), gap.start}, {QStringLiteral("end"), gap.end}});
    return out;
}

QVariantList TimelineClipsModel::transitionRegions() const
{
    QVariantList out;
    out.reserve(m_decorations.transitions.size());
    for (const TransitionRegion &region : m_decorations.transitions) {
        out.append(QVariantMap{
            {QStringLiteral("leftClip"), region.leftClip},
            {QStringLiteral("start"), region.start},
            {QStringLiteral("end"), region.end},
            {QStringLiteral("hasTransition"), region.hasTransition},
            {QStringLiteral("label"), region.label},
            {QStringLiteral("kind"), region.kind},
        });
    }
    return out;
}

QVariantList TimelineClipsModel::adjustmentLanes() const
{
    QVariantList out;
    out.reserve(m_decorations.adjustmentLanes.size());
    for (int lane : m_decorations.adjustmentLanes)
        out.append(lane);
    return out;
}

int TimelineClipsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant TimelineClipsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case IdRole: return row.id;
    case NameRole: return row.name;
    case PathRole: return row.path;
    case KindRole: return row.kind;
    case AdjustmentKindRole: return row.adjustmentKind;
    case LinkedClipIdRole: return row.linkedClipId;
    case LinkIdRole: return row.linkId;
    case LinkedRole: return row.linked;
    case FilmstripPathRole: return row.filmstripPath;
    case TextContentRole: return row.textContent;
    case RotationCorrectionRole: return row.rotationCorrection;
    case StartRole: return row.start;
    case DurationRole: return row.duration;
    case InPointRole: return row.inPoint;
    case OutPointRole: return row.outPoint;
    case SourceDurationRole: return row.sourceDuration;
    case AudioStreamIndexRole: return row.audioStreamIndex;
    case HasEmbeddedAudioRole: return row.hasEmbeddedAudio;
    case FadeInRole: return row.fadeIn;
    case FadeOutRole: return row.fadeOut;
    case FadeCurveRole: return row.fadeCurve;
    case FadeShapeRole: return row.fadeShape;
    case FadeHandlesRole: return row.fadeHandles;
    case EffectsRole: return row.effects;
    case AudioEffectsRole: return row.audioEffects;
    default: return {};
    }
}

QHash<int, QByteArray> TimelineClipsModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {PathRole, "path"},
        {KindRole, "kind"},
        {AdjustmentKindRole, "adjustmentKind"},
        {LinkedClipIdRole, "linkedClipId"},
        {LinkIdRole, "linkId"},
        {LinkedRole, "linked"},
        {FilmstripPathRole, "filmstripPath"},
        {TextContentRole, "textContent"},
        {RotationCorrectionRole, "rotationCorrection"},
        {StartRole, "start"},
        {DurationRole, "duration"},
        {InPointRole, "inPoint"},
        {OutPointRole, "outPoint"},
        {SourceDurationRole, "sourceDuration"},
        {AudioStreamIndexRole, "audioStreamIndex"},
        {HasEmbeddedAudioRole, "hasEmbeddedAudio"},
        {FadeInRole, "fadeIn"},
        {FadeOutRole, "fadeOut"},
        {FadeCurveRole, "fadeCurve"},
        {FadeShapeRole, "fadeShape"},
        {FadeHandlesRole, "fadeHandles"},
        {EffectsRole, "effects"},
        {AudioEffectsRole, "audioEffects"},
    };
}
