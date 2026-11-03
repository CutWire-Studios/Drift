#include "app/editor/sessions/SubtitleController.h"

#include "app/editor/AppController.h"
#include "app/editor/sessions/SubtitleCueFilterModel.h"
#include "app/editor/sessions/SubtitleCueModel.h"
#include "core/interchange/subtitles/SrtIO.h"
#include "core/interchange/subtitles/VttIO.h"
#include "core/project/SubtitleTiming.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

bool isVtt(const QString &format)
{
    return format.compare(QLatin1String("vtt"), Qt::CaseInsensitive) == 0;
}

drift::SubtitleEdges edgesFrom(int edges)
{
    return static_cast<drift::SubtitleEdges>(std::clamp(edges, 0, 2));
}

QVariantList cuesToVariants(const QList<drift::SubtitleCue> &cues)
{
    QVariantList out;
    out.reserve(cues.size());
    for (const drift::SubtitleCue &cue : cues) {
        out.append(QVariantMap{
            {QStringLiteral("start"), drift::usToSeconds(cue.startUs)},
            {QStringLiteral("end"), drift::usToSeconds(cue.endUs)},
            {QStringLiteral("text"), cue.text},
            {QStringLiteral("source"), cue.source},
        });
    }
    return out;
}

} // namespace

SubtitleController::SubtitleController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_model(new SubtitleCueModel(this))
    , m_filtered(new SubtitleCueFilterModel(this))
{
    m_filtered->setSourceModel(m_model);
    connect(app, &AppController::tracksChanged, this, &SubtitleController::cuesChanged);
    connect(app, &AppController::tracksChanged, this, [this] {
        refreshModel();
        refreshActiveCue();
    });
    connect(app, &AppController::selectionChanged, this, [this] {
        refreshModel();
        refreshActiveCue();
    });
    connect(app, &AppController::playheadSecondsChanged, this, &SubtitleController::refreshActiveCue);
    connect(app, &AppController::selectionChanged, this, [this] {
        if (m_app->selectedTrack() == m_track && m_app->selectedClip() == m_clip)
            return;
        m_track = m_app->selectedTrack();
        m_clip = m_app->selectedClip();
        clearSelection();
    });
    connect(app, &AppController::selectedSubtitleCueChanged, this, [this] {
        if (m_syncing)
            return;
        const int cue = m_app->selectedSubtitleCue();
        if (cue < 0)
            clearSelection();
        else if (m_selected != QList<int>{cue})
            setSelection({cue}, cue);
    });
}

QObject *SubtitleController::cueModel() const
{
    return m_model;
}

QObject *SubtitleController::filteredCueModel() const
{
    return m_filtered;
}

const drift::Clip *SubtitleController::selectedSubtitleClip() const
{
    return subtitleClip(m_app->selectedTrack(), m_app->selectedClip());
}

void SubtitleController::refreshModel()
{
    const drift::Clip *clip = selectedSubtitleClip();
    m_model->setCues(clip ? clip->subtitleCues : QList<drift::SubtitleCue>{});
    const int count = m_model->rowCount();
    if (count != m_count) {
        m_count = count;
        emit countChanged();
    }
}

void SubtitleController::refreshActiveCue()
{
    const drift::Clip *clip = selectedSubtitleClip();
    const drift::TimeUs playhead = m_app->m_playheadUs;
    const int active = clip && clip->containsTime(playhead)
        ? drift::subtitleCueIndexAt(clip->subtitleCues, playhead - clip->timelineStart)
        : -1;
    if (active == m_activeCue)
        return;
    m_activeCue = active;
    emit activeCueChanged();
}

QVariantList SubtitleController::selectedCues() const
{
    QVariantList out;
    for (int index : m_selected)
        out.append(index);
    return out;
}

void SubtitleController::setSelection(QList<int> indices, int anchor)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices == m_selected && anchor == m_anchor)
        return;
    m_selected = indices;
    m_anchor = anchor;
    syncCurrentCue();
    emit selectedCuesChanged();
}

void SubtitleController::syncCurrentCue()
{
    const int current = m_selected.isEmpty() ? -1 : (m_selected.size() == 1 ? m_selected.first() : m_anchor);
    m_syncing = true;
    m_app->setSelectedSubtitleCue(current);
    m_syncing = false;
}

void SubtitleController::selectCue(int index, int mode)
{
    if (index < 0) {
        clearSelection();
        return;
    }
    if (mode == 1) {
        QList<int> next = m_selected;
        if (!next.removeOne(index))
            next.append(index);
        setSelection(next, index);
    } else if (mode == 2 && m_anchor >= 0) {
        QList<int> next;
        for (int i = std::min(m_anchor, index); i <= std::max(m_anchor, index); ++i)
            next.append(i);
        setSelection(next, m_anchor);
    } else {
        setSelection({index}, index);
    }
}

void SubtitleController::selectAll(int track, int clip)
{
    const drift::Clip *c = subtitleClip(track, clip);
    if (!c || c->subtitleCues.isEmpty()) {
        clearSelection();
        return;
    }
    QList<int> all;
    for (int i = 0; i < c->subtitleCues.size(); ++i)
        all.append(i);
    setSelection(all, m_anchor >= 0 && m_anchor < all.size() ? m_anchor : 0);
}

void SubtitleController::clearSelection()
{
    setSelection({}, -1);
}

void SubtitleController::setSelectedCues(const QVariantList &indices)
{
    QList<int> next;
    for (const QVariant &value : indices) {
        if (value.toInt() >= 0)
            next.append(value.toInt());
    }
    std::sort(next.begin(), next.end());
    setSelection(next, next.isEmpty() ? -1 : next.last());
}

const drift::Clip *SubtitleController::subtitleClip(int track, int clip) const
{
    const auto &tracks = m_app->m_project.tracks();
    if (track < 0 || track >= tracks.size())
        return nullptr;
    const drift::Track &t = tracks.at(track);
    if (clip < 0 || clip >= t.clips.size() || t.clips.at(clip).type != drift::ClipType::Subtitle)
        return nullptr;
    return &t.clips.at(clip);
}

QVariantList SubtitleController::cues(int track, int clip) const
{
    const drift::Clip *c = subtitleClip(track, clip);
    return c ? cuesToVariants(c->subtitleCues) : QVariantList{};
}

double SubtitleController::projectFps() const
{
    return m_app->m_project.fps();
}

QVariantMap SubtitleController::cueHitAt(double localSec, double pxPerSecond, double slopPx) const
{
    const drift::Clip *clip = selectedSubtitleClip();
    if (!clip || pxPerSecond <= 0)
        return {};
    const QList<drift::SubtitleCue> &cues = clip->subtitleCues;
    const auto next = std::upper_bound(cues.begin(), cues.end(), localSec,
                                       [](double sec, const drift::SubtitleCue &cue) {
                                           return sec < drift::usToSeconds(cue.startUs);
                                       });
    for (int i = static_cast<int>(next - cues.begin()) - 1; i >= 0; --i) {
        const double start = drift::usToSeconds(cues.at(i).startUs);
        const double end = drift::usToSeconds(cues.at(i).endUs);
        if (localSec > end)
            continue;
        const double edge = std::min(slopPx / pxPerSecond, (end - start) / 3);
        return {
            {QStringLiteral("index"), i},
            {QStringLiteral("part"),
             localSec < start + edge ? QStringLiteral("trimStart")
                                     : (localSec > end - edge ? QStringLiteral("trimEnd")
                                                              : QStringLiteral("move"))},
        };
    }
    return {};
}

double SubtitleController::snapCueTime(double value, double threshold, double playhead,
                                       double clipDuration, const QVariantList &excluded) const
{
    double best = value;
    double bestD = threshold;
    const auto consider = [&](double target) {
        const double d = std::abs(value - target);
        if (d < bestD) {
            bestD = d;
            best = target;
        }
    };
    if (playhead >= 0)
        consider(playhead);
    consider(0);
    if (clipDuration > 0)
        consider(clipDuration);
    if (const drift::Clip *clip = selectedSubtitleClip()) {
        QSet<int> skip;
        for (const QVariant &index : excluded)
            skip.insert(index.toInt());
        const QList<drift::SubtitleCue> &cues = clip->subtitleCues;
        for (int i = 0; i < cues.size(); ++i) {
            if (skip.contains(i))
                continue;
            consider(drift::usToSeconds(cues.at(i).startUs));
            consider(drift::usToSeconds(cues.at(i).endUs));
        }
    }
    return best;
}

QString SubtitleController::cuesText(int track, int clip, const QString &format, bool timelineTimes) const
{
    const drift::Clip *c = subtitleClip(track, clip);
    if (!c)
        return {};
    QList<drift::SubtitleCue> list = c->subtitleCues;
    if (timelineTimes) {
        for (drift::SubtitleCue &cue : list) {
            cue.startUs += c->timelineStart;
            cue.endUs += c->timelineStart;
        }
    }
    return isVtt(format) ? drift::writeVtt(list) : drift::writeSrt(list);
}

QVariantMap SubtitleController::parseText(const QString &text, const QString &format) const
{
    const drift::SubtitleParseResult result =
        isVtt(format) ? drift::parseVttDetailed(text) : drift::parseSrtDetailed(text);
    QVariantList errors;
    for (const drift::SubtitleParseError &error : result.errors) {
        errors.append(QVariantMap{
            {QStringLiteral("line"), error.line},
            {QStringLiteral("message"), error.message},
        });
    }
    return {
        {QStringLiteral("count"), int(result.cues.size())},
        {QStringLiteral("errorCount"), int(result.errors.size())},
        {QStringLiteral("errors"), errors},
    };
}

bool SubtitleController::applyText(int track, int clip, const QString &text, const QString &format,
                                   bool timelineTimes)
{
    const drift::Clip *c = subtitleClip(track, clip);
    if (!c)
        return false;
    drift::SubtitleParseResult result =
        isVtt(format) ? drift::parseVttDetailed(text) : drift::parseSrtDetailed(text);
    if (result.cues.isEmpty() && !result.errors.isEmpty())
        return false;

    const drift::TimeUs frameUs = drift::TimeUs(std::llround(1e6 / std::max(1, m_app->m_project.fps())));
    QList<drift::SubtitleCue> cues;
    cues.reserve(result.cues.size());
    for (drift::SubtitleCue cue : result.cues) {
        if (timelineTimes) {
            cue.startUs -= c->timelineStart;
            cue.endUs -= c->timelineStart;
        }
        if (cue.endUs <= 0)
            continue;
        cue.startUs = std::max<drift::TimeUs>(cue.startUs, 0);
        const int i = cues.size();
        cue.source.clear();
        if (i < c->subtitleCues.size() && std::abs(c->subtitleCues.at(i).startUs - cue.startUs) <= frameUs)
            cue.source = c->subtitleCues.at(i).source;
        cues.append(cue);
    }
    drift::sortSubtitleCues(cues);
    drift::TimeUs duration = c->timelineDuration;
    for (const drift::SubtitleCue &cue : cues)
        duration = std::max(duration, cue.endUs);
    if (duration == c->timelineDuration) {
        m_app->commitSubtitleCues(track, clip, cues, tr("Edit subtitle text"));
        return true;
    }

    const drift::Project before = m_app->m_project;
    drift::Clip &target = m_app->m_project.tracks()[track].clips[clip];
    target.subtitleCues = std::move(cues);
    target.name = drift::subtitleClipName(target.subtitleCues);
    target.timelineDuration = duration;
    target.srcOut = duration;
    m_app->pushProjectEdit(before, tr("Edit subtitle text"));
    m_app->finishEdit(tr("Edit subtitle text"));
    return true;
}

QString SubtitleController::detectFormat(const QString &text) const
{
    qsizetype i = 0;
    while (i < text.size() && (text.at(i) == QChar(0xFEFF) || text.at(i).isSpace()))
        ++i;
    return QStringView(text).mid(i).startsWith(QLatin1String("WEBVTT")) ? QStringLiteral("vtt")
                                                                         : QStringLiteral("srt");
}

bool SubtitleController::importFile(int track, int clip, const QUrl &url)
{
    return m_app->importSubtitleFileIntoClip(track, clip, url);
}

bool SubtitleController::exportFile(int track, int clip, const QUrl &url, bool timelineTimes)
{
    return m_app->exportSubtitleFile(track, clip, url, timelineTimes);
}

int SubtitleController::runTool(int track, int clip, int scope, const QString &undoText, bool commit,
                                const ToolFn &fn)
{
    const drift::Clip *c = subtitleClip(track, clip);
    if (!c)
        return 0;
    QList<drift::SubtitleCue> list = c->subtitleCues;
    QList<int> selected;
    for (int index : m_selected) {
        if (index < list.size())
            selected.append(index);
    }
    const QList<int> idx = drift::subtitleScopeIndices(list, scope, selected, m_app->m_playheadUs - c->timelineStart);
    const int changed = fn(list, idx);
    if (commit && changed > 0)
        m_app->commitSubtitleCues(track, clip, list, undoText);
    return changed;
}

int SubtitleController::previewShift(int track, int clip, int scope, double seconds, int edges)
{
    return runTool(track, clip, scope, {}, false, [=](auto &cues, const auto &idx) {
        return drift::shiftCues(cues, idx, drift::secondsToUs(seconds), edgesFrom(edges));
    });
}

int SubtitleController::applyShift(int track, int clip, int scope, double seconds, int edges)
{
    return runTool(track, clip, scope, tr("Shift subtitles"), true, [=](auto &cues, const auto &idx) {
        return drift::shiftCues(cues, idx, drift::secondsToUs(seconds), edgesFrom(edges));
    });
}

int SubtitleController::previewResync(int track, int clip, int scope, double aFrom, double aTo,
                                      double bFrom, double bTo)
{
    return runTool(track, clip, scope, {}, false, [=](auto &cues, const auto &idx) {
        return drift::linearResync(cues, idx, {drift::secondsToUs(aFrom), drift::secondsToUs(aTo)},
                                   {drift::secondsToUs(bFrom), drift::secondsToUs(bTo)});
    });
}

int SubtitleController::applyResync(int track, int clip, int scope, double aFrom, double aTo,
                                    double bFrom, double bTo)
{
    return runTool(track, clip, scope, tr("Resync subtitles"), true, [=](auto &cues, const auto &idx) {
        return drift::linearResync(cues, idx, {drift::secondsToUs(aFrom), drift::secondsToUs(aTo)},
                                   {drift::secondsToUs(bFrom), drift::secondsToUs(bTo)});
    });
}

int SubtitleController::previewScale(int track, int clip, int scope, double factor)
{
    return runTool(track, clip, scope, {}, false, [=](auto &cues, const auto &idx) {
        return drift::scaleCues(cues, idx, factor);
    });
}

int SubtitleController::applyScale(int track, int clip, int scope, double factor)
{
    return runTool(track, clip, scope, tr("Scale subtitle timing"), true, [=](auto &cues, const auto &idx) {
        return drift::scaleCues(cues, idx, factor);
    });
}

int SubtitleController::previewBridgeGaps(int track, int clip, int scope, double maxGap,
                                          double minGap, double prevRatio)
{
    return runTool(track, clip, scope, {}, false, [=](auto &cues, const auto &idx) {
        return drift::bridgeGaps(cues, idx, drift::secondsToUs(maxGap), drift::secondsToUs(minGap), prevRatio);
    });
}

int SubtitleController::applyBridgeGaps(int track, int clip, int scope, double maxGap,
                                        double minGap, double prevRatio)
{
    return runTool(track, clip, scope, tr("Bridge subtitle gaps"), true, [=](auto &cues, const auto &idx) {
        return drift::bridgeGaps(cues, idx, drift::secondsToUs(maxGap), drift::secondsToUs(minGap), prevRatio);
    });
}

int SubtitleController::previewMinGap(int track, int clip, int scope, double minGap)
{
    return runTool(track, clip, scope, {}, false, [=](auto &cues, const auto &idx) {
        return drift::enforceMinGap(cues, idx, drift::secondsToUs(minGap));
    });
}

int SubtitleController::applyMinGap(int track, int clip, int scope, double minGap)
{
    return runTool(track, clip, scope, tr("Set minimum subtitle gap"), true, [=](auto &cues, const auto &idx) {
        return drift::enforceMinGap(cues, idx, drift::secondsToUs(minGap));
    });
}

int SubtitleController::previewLeadInOut(int track, int clip, int scope, double leadIn, double leadOut)
{
    return runTool(track, clip, scope, {}, false, [=](auto &cues, const auto &idx) {
        return drift::addLeadInOut(cues, idx, drift::secondsToUs(leadIn), drift::secondsToUs(leadOut));
    });
}

int SubtitleController::applyLeadInOut(int track, int clip, int scope, double leadIn, double leadOut)
{
    return runTool(track, clip, scope, tr("Add subtitle lead-in/out"), true, [=](auto &cues, const auto &idx) {
        return drift::addLeadInOut(cues, idx, drift::secondsToUs(leadIn), drift::secondsToUs(leadOut));
    });
}

int SubtitleController::previewDurations(int track, int clip, int scope, double minDur, double maxDur)
{
    return runTool(track, clip, scope, {}, false, [=](auto &cues, const auto &idx) {
        return drift::clampDurations(cues, idx, drift::secondsToUs(minDur), drift::secondsToUs(maxDur));
    });
}

int SubtitleController::applyDurations(int track, int clip, int scope, double minDur, double maxDur)
{
    return runTool(track, clip, scope, tr("Adjust subtitle durations"), true, [=](auto &cues, const auto &idx) {
        return drift::clampDurations(cues, idx, drift::secondsToUs(minDur), drift::secondsToUs(maxDur));
    });
}

int SubtitleController::previewFixOverlaps(int track, int clip, int scope)
{
    return runTool(track, clip, scope, {}, false, [](auto &cues, const auto &idx) {
        return drift::fixOverlaps(cues, idx);
    });
}

int SubtitleController::applyFixOverlaps(int track, int clip, int scope)
{
    return runTool(track, clip, scope, tr("Fix subtitle overlaps"), true, [](auto &cues, const auto &idx) {
        return drift::fixOverlaps(cues, idx);
    });
}

#include "moc_SubtitleController.cpp"
