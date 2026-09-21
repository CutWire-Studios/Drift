#include "TimelineClipsModel.h"

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
    // Anything that adds, removes or reorders resets: the delegates have to be rebuilt anyway.
    bool sameShape = rows.size() == m_rows.size();
    for (int i = 0; sameShape && i < rows.size(); ++i)
        sameShape = rows.at(i).id == m_rows.at(i).id;

    if (!sameShape) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
        return;
    }

    for (int i = 0; i < rows.size(); ++i) {
        const QList<int> roles = changedRoles(m_rows.at(i), rows.at(i));
        if (roles.isEmpty())
            continue;
        m_rows[i] = rows.at(i);
        emit dataChanged(index(i), index(i), roles);
    }
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
        {FadeInRole, "fadeIn"},
        {FadeOutRole, "fadeOut"},
        {FadeCurveRole, "fadeCurve"},
        {FadeShapeRole, "fadeShape"},
        {FadeHandlesRole, "fadeHandles"},
        {EffectsRole, "effects"},
        {AudioEffectsRole, "audioEffects"},
    };
}
