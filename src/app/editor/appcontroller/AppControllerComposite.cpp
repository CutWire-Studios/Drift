#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/decode/probe/MediaProbe.h"
#include "app/mcp/McpController.h"
#include "app/bin/MediaBinController.h"

#include "engine/compositor/layers/TransformLayer.h"
#include "engine/encode/export/Exporter.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/encode/MediaEditor.h"


using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

namespace {

// Split every embedded audio stream onto dedicated, consecutive audio tracks.
//
// All streams from the same video stay together, and that group follows the
// source video's position relative to the other video tracks.
bool detachAllAudioTracksFromVideo(drift::Project &project,
                                   AssetLibrary *library,
                                   int videoTrackIndex,
                                   int videoClipIndex)
{
    if (videoTrackIndex < 0
        || videoTrackIndex >= project.tracks().size())
        return false;

    if (videoClipIndex < 0
        || videoClipIndex >= project.tracks().at(videoTrackIndex).clips.size())
        return false;

    drift::Clip &videoClip =
        project.tracks()[videoTrackIndex].clips[videoClipIndex];

    if (!clipHasEmbeddedAudio(project, library, videoClip))
        return false;

    const QList<StreamInfo> streams =
        cachedAudioStreams(videoClip.path);

    if (streams.isEmpty()) {
        return detachEmbeddedAudioFromVideo(
            project,
            library,
            videoTrackIndex,
            videoClipIndex);
    }

    const QString linkId =
        videoClip.linkId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : videoClip.linkId;

    videoClip.linkId = linkId;
    videoClip.suppressEmbeddedAudio = true;

    const QString videoClipId = videoClip.id;
    // The effects were applied to the stream the clip was actually playing, so they follow that
    // companion rather than whichever one happens to come first.
    const int effectStreamIndex = videoClip.audioStreamIndex;

    QList<drift::Clip> companions;
    companions.reserve(streams.size());

    for (int i = 0; i < streams.size(); ++i) {
        const StreamInfo &stream = streams.at(i);

        QString trackName = videoClip.name;

        if (!stream.title.isEmpty()) {
            trackName =
                QStringLiteral("%1 (%2)")
                    .arg(videoClip.name, stream.title);
        } else if (streams.size() > 1) {
            trackName =
                QStringLiteral("%1 (Audio %2)")
                    .arg(videoClip.name)
                    .arg(i + 1);
        }

        companions.append(
            makeAudioCompanionFromVideo(
                videoClip,
                linkId,
                i,
                trackName));
    }

    const int insertAt =
        audioTrackInsertIndexForVideoTrack(
            project,
            videoTrackIndex);

    QString effectCompanionId;
    for (int i = 0; i < companions.size(); ++i) {
        drift::Track audioTrack;
        audioTrack.type = drift::TrackType::Audio;
        audioTrack.clips.append(companions.at(i));

        if (companions.at(i).audioStreamIndex == effectStreamIndex)
            effectCompanionId = companions.at(i).id;

        project.tracks().insert(
            insertAt + i,
            audioTrack);
    }

    if (!effectCompanionId.isEmpty())
        migrateAudioEffectsToCompanion(project, videoClipId, effectCompanionId);

    return true;
}


QVariantMap transformParentToMap(const drift::TransformParent &parent)
{
    return {{QStringLiteral("active"), parent.hasParent},
            {QStringLiteral("affine"), parent.matrix.isAffine()},
            {QStringLiteral("parent"), transformToList(parent.matrix)},
            {QStringLiteral("matrix"), liftHomography(parent.matrix)},
            {QStringLiteral("opacity"), parent.opacity}};
}
} // namespace

bool AppController::canMergeSelection() const
{
    int leftTrack = -1;
    int leftClip = -1;
    int rightTrack = -1;
    int rightClip = -1;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    if (!subtitleMergeIndices(pairs).isEmpty())
        return true;

    if (pairs.size() == 2) {
        leftTrack = pairs[0].first;
        leftClip = pairs[0].second;
        rightTrack = pairs[1].first;
        rightClip = pairs[1].second;
        if (leftTrack != rightTrack || !isValidClipIndex(leftTrack, leftClip)
            || !isValidClipIndex(rightTrack, rightClip))
            return false;
        const drift::Clip &a = m_project.tracks().at(leftTrack).clips.at(leftClip);
        const drift::Clip &b = m_project.tracks().at(rightTrack).clips.at(rightClip);
        if (a.timelineStart <= b.timelineStart)
            return drift::clipsCanMerge(a, b);
        return drift::clipsCanMerge(b, a);
    }

    if (pairs.size() == 1) {
        const int trackIndex = pairs[0].first;
        const int clipIndex = pairs[0].second;
        if (!isValidClipIndex(trackIndex, clipIndex))
            return false;
        const drift::Track &track = m_project.tracks().at(trackIndex);
        const drift::Clip &left = track.clips.at(clipIndex);
        // Prefer merging with the clip that starts at this clip's end.
        for (int i = 0; i < track.clips.size(); ++i) {
            if (i == clipIndex)
                continue;
            if (drift::clipsCanMerge(left, track.clips.at(i)))
                return true;
        }
        return false;
    }

    return false;
}

void AppController::mergeSelectedClips()
{
    int trackIndex = -1;
    int leftIndex = -1;
    int rightIndex = -1;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    const QList<int> subtitleIndices = subtitleMergeIndices(pairs);
    if (!subtitleIndices.isEmpty()) {
        mergeSubtitleClipsAt(pairs.first().first, subtitleIndices);
        return;
    }

    if (pairs.size() == 2) {
        if (pairs[0].first != pairs[1].first)
            return;
        trackIndex = pairs[0].first;
        if (!isValidClipIndex(trackIndex, pairs[0].second) || !isValidClipIndex(trackIndex, pairs[1].second))
            return;
        const drift::Clip &a = m_project.tracks().at(trackIndex).clips.at(pairs[0].second);
        const drift::Clip &b = m_project.tracks().at(trackIndex).clips.at(pairs[1].second);
        if (a.timelineStart <= b.timelineStart) {
            leftIndex = pairs[0].second;
            rightIndex = pairs[1].second;
        } else {
            leftIndex = pairs[1].second;
            rightIndex = pairs[0].second;
        }
    } else if (pairs.size() == 1) {
        trackIndex = pairs[0].first;
        leftIndex = pairs[0].second;
        if (!isValidClipIndex(trackIndex, leftIndex))
            return;
        const drift::Track &track = m_project.tracks().at(trackIndex);
        const drift::Clip &left = track.clips.at(leftIndex);
        for (int i = 0; i < track.clips.size(); ++i) {
            if (i == leftIndex)
                continue;
            if (drift::clipsCanMerge(left, track.clips.at(i))) {
                rightIndex = i;
                break;
            }
        }
        if (rightIndex < 0)
            return;
    } else {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::Clip &left = track.clips.at(leftIndex);
    const drift::Clip &right = track.clips.at(rightIndex);
    if (!drift::clipsCanMerge(left, right))
        return;

    const drift::Project before = m_project;
    drift::Clip merged = drift::mergeClips(left, right);
    // Remove right first if its index is higher so leftIndex stays valid.
    if (rightIndex > leftIndex) {
        track.clips.removeAt(rightIndex);
        track.clips[leftIndex] = merged;
    } else {
        track.clips.removeAt(leftIndex);
        track.clips[rightIndex] = merged;
        leftIndex = rightIndex;
    }

    pushProjectEdit(before, tr("Clips merged"));
    finishEdit(tr("Clips merged"));
    selectClip(trackIndex, leftIndex);
}

// Clip indices of a selection that is two or more subtitle clips on one track; empty otherwise.
QList<int> AppController::subtitleMergeIndices(const QList<QPair<int, int>> &pairs) const
{
    if (pairs.size() < 2)
        return {};
    const int trackIndex = pairs.first().first;
    QList<int> indices;
    QList<drift::Clip> clips;
    for (const QPair<int, int> &pair : pairs) {
        if (pair.first != trackIndex || !isValidClipIndex(pair.first, pair.second))
            return {};
        if (indices.contains(pair.second))
            continue;
        indices.append(pair.second);
        clips.append(m_project.tracks().at(trackIndex).clips.at(pair.second));
    }
    if (!drift::subtitleClipsCanMerge(clips))
        return {};
    return indices;
}

bool AppController::canMergeAllSubtitlesOnTrack(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    int count = 0;
    for (const drift::Clip &clip : m_project.tracks().at(trackIndex).clips) {
        if (clip.type == drift::ClipType::Subtitle)
            ++count;
    }
    return count >= 2;
}

void AppController::mergeAllSubtitlesOnTrack(int trackIndex)
{
    if (!canMergeAllSubtitlesOnTrack(trackIndex))
        return;
    QList<int> indices;
    const QList<drift::Clip> &clips = m_project.tracks().at(trackIndex).clips;
    for (int i = 0; i < clips.size(); ++i) {
        if (clips.at(i).type == drift::ClipType::Subtitle)
            indices.append(i);
    }
    mergeSubtitleClipsAt(trackIndex, indices);
}

void AppController::mergeSubtitleClipsAt(int trackIndex, QList<int> clipIndices)
{
    std::sort(clipIndices.begin(), clipIndices.end());
    drift::Track &track = m_project.tracks()[trackIndex];
    QList<drift::Clip> clips;
    for (int index : clipIndices)
        clips.append(track.clips.at(index));

    const drift::Project before = m_project;
    const drift::Clip merged = drift::mergeSubtitleClips(clips);
    for (int i = clipIndices.size() - 1; i >= 0; --i)
        track.clips.removeAt(clipIndices.at(i));
    const int insertAt = clipIndices.first();
    track.clips.insert(insertAt, merged);

    pushProjectEdit(before, tr("Subtitles merged"));
    finishEdit(tr("Subtitles merged"));
    selectClip(trackIndex, insertAt);
}

void AppController::convertSubtitleToTextClips(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip subtitle = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (subtitle.type != drift::ClipType::Subtitle)
        return;

    QList<drift::SubtitleCue> cues = subtitle.subtitleCues;
    drift::sortSubtitleCues(cues);
    QList<drift::Clip> texts;
    for (const drift::SubtitleCue &cue : cues) {
        if (cue.text.trimmed().isEmpty())
            continue;
        const drift::TimeUs startUs = qMax<drift::TimeUs>(cue.startUs, 0);
        const drift::TimeUs endUs = qMin(cue.endUs, subtitle.timelineDuration);
        if (endUs <= startUs)
            continue;

        // Built by hand rather than with sliceClipToTimelineRange: its split refuses cuts closer
        // than one frame to an edge, which would drop cues that start right at the clip's
        // head or are very short.
        drift::Clip text = subtitle;
        text.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        text.type = drift::ClipType::Text;
        text.subtitleCues.clear();
        text.textContent = cue.text;
        text.name = cue.text.left(32);
        text.timelineStart = subtitle.timelineStart + startUs;
        text.timelineDuration = endUs - startUs;
        text.srcIn = 0;
        text.srcOut = text.timelineDuration;
        drift::rebaseKeyframesForSplitTail(text, startUs);
        // The subtitle's own fades and intro/outro belong to its outer edges only.
        if (startUs > 0) {
            text.fadeInUs = 0;
            text.animIn = drift::ClipAnimation{};
        }
        if (endUs < subtitle.timelineDuration) {
            text.fadeOutUs = 0;
            text.animOut = drift::ClipAnimation{};
        }
        text.fadeInUs = qMin(text.fadeInUs, text.timelineDuration);
        text.fadeOutUs = qMin(text.fadeOutUs, text.timelineDuration);
        texts.append(text);
    }
    if (texts.isEmpty()) {
        setLastMessage(tr("This subtitle clip has no captions"), QStringLiteral("warning"));
        return;
    }

    setPlaying(false);
    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips.removeAt(clipIndex);
    for (drift::Track &track : m_project.tracks()) {
        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = track.transitions.at(i);
            if (transition.fromClipId == subtitle.id || transition.toClipId == subtitle.id)
                track.transitions.removeAt(i);
        }
    }

    const int textTrack = drift::ensureFreeTrackForClipType(
        m_project, drift::ClipType::Text, subtitle.timelineStart, subtitle.timelineDuration, true);
    if (textTrack < 0)
        return;
    drift::Track &track = m_project.tracks()[textTrack];
    const int firstIndex = track.clips.size();
    track.clips.append(texts);

    pushProjectEdit(before, tr("Subtitles converted to text"));
    clearSelection();
    finishEdit(tr("Subtitles converted to text"));
    selectClip(textTrack, firstIndex);
    setLastMessage(tr("Created %n text clips", "", int(texts.size())), QStringLiteral("success"));
}

bool AppController::canConvertSelectionToSubtitle() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return false;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second)
            || m_project.tracks().at(pair.first).clips.at(pair.second).type != drift::ClipType::Text)
            return false;
    }
    return true;
}

void AppController::convertSelectionToSubtitle()
{
    if (!canConvertSelectionToSubtitle())
        return;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty())
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    QList<drift::Clip> texts;
    QSet<QString> ids;
    for (const QPair<int, int> &pair : pairs) {
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        if (ids.contains(clip.id))
            continue;
        ids.insert(clip.id);
        texts.append(clip);
    }
    std::stable_sort(texts.begin(), texts.end(), [](const drift::Clip &a, const drift::Clip &b) {
        return a.timelineStart < b.timelineStart;
    });

    const drift::Clip &first = texts.first();
    drift::TimeUs endUs = first.timelineEnd();
    for (const drift::Clip &text : texts)
        endUs = qMax(endUs, text.timelineEnd());

    QList<drift::SubtitleCue> cues;
    for (const drift::Clip &text : texts) {
        drift::SubtitleCue cue;
        cue.startUs = text.timelineStart - first.timelineStart;
        cue.endUs = text.timelineEnd() - first.timelineStart;
        cue.text = text.textContent.isEmpty() ? text.name : text.textContent;
        cues.append(cue);
    }

    drift::Clip subtitle = first;
    subtitle.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    subtitle.type = drift::ClipType::Subtitle;
    subtitle.textContent.clear();
    subtitle.subtitleCues = cues;
    subtitle.name = drift::subtitleClipName(cues);
    subtitle.timelineDuration = endUs - first.timelineStart;
    subtitle.srcIn = 0;
    subtitle.srcOut = subtitle.timelineDuration;

    setPlaying(false);
    const drift::Project before = m_project;
    for (drift::Track &track : m_project.tracks()) {
        for (int i = track.clips.size() - 1; i >= 0; --i) {
            if (ids.contains(track.clips.at(i).id))
                track.clips.removeAt(i);
        }
        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = track.transitions.at(i);
            if (ids.contains(transition.fromClipId) || ids.contains(transition.toClipId))
                track.transitions.removeAt(i);
        }
    }

    const int subtitleTrack = drift::ensureFreeTrackForClipType(
        m_project, drift::ClipType::Subtitle, subtitle.timelineStart, subtitle.timelineDuration, true);
    if (subtitleTrack < 0)
        return;
    drift::Track &track = m_project.tracks()[subtitleTrack];
    track.clips.append(subtitle);
    const int subtitleIndex = track.clips.size() - 1;

    pushProjectEdit(before, tr("Text converted to subtitles"));
    clearSelection();
    finishEdit(tr("Text converted to subtitles"));
    selectClip(subtitleTrack, subtitleIndex);
}

bool AppController::canSeparateAudioSelection() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (clipHasEmbeddedAudio(m_project, m_assetLibrary,
                                 m_project.tracks().at(pair.first).clips.at(pair.second)))
            return true;
    }
    return false;
}

bool AppController::canSeparateAudioClip(int trackIndex, int clipIndex) const
{
    return isValidClipIndex(trackIndex, clipIndex)
           && clipHasEmbeddedAudio(m_project, m_assetLibrary,
                                   m_project.tracks().at(trackIndex).clips.at(clipIndex));
}

void AppController::separateAudioFromClip(int trackIndex, int clipIndex)
{
    if (!canSeparateAudioClip(trackIndex, clipIndex))
        return;

    const QString videoId = m_project.tracks().at(trackIndex).clips.at(clipIndex).id;
    const drift::Project before = m_project;
    if (!detachEmbeddedAudioFromVideo(m_project, m_assetLibrary, trackIndex, clipIndex))
        return;

    // Inserting an audio track can shift indexes; restore selection by identity.
    if (findClipById(m_project, videoId, &trackIndex, &clipIndex)) {
        m_selectedTrack = trackIndex;
        m_selectedClip = clipIndex;
        m_selection = selectionWithLinkedPartners(m_project, trackIndex, clipIndex);
    }
    pushProjectEdit(before, tr("Audio separated"));
    finishEdit(tr("Audio separated"));
}

bool AppController::canUnlinkSelection() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (!m_project.tracks().at(pair.first).clips.at(pair.second).linkId.isEmpty())
            return true;
    }
    return false;
}

void AppController::separateAudioFromSelection()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;

    QStringList videoIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Composite)
            videoIds.append(clip.id);
    }
    QString selectedId;
    if (isValidClipIndex(m_selectedTrack, m_selectedClip))
        selectedId = m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).id;

    const drift::Project before = m_project;
    bool changed = false;
    QSet<QString> detachedVideoIds;
    for (const QString &videoId : videoIds) {
        if (detachedVideoIds.contains(videoId))
            continue;
        int trackIndex = -1;
        int clipIndex = -1;
        if (!findClipById(m_project, videoId, &trackIndex, &clipIndex))
            continue;
        if (detachEmbeddedAudioFromVideo(
                m_project,
                m_assetLibrary,
                trackIndex,
                clipIndex)) {
            detachedVideoIds.insert(videoId);
            changed = true;
        }
    }

    if (!changed)
        return;

    if (!selectedId.isEmpty()
        && findClipById(m_project, selectedId, &m_selectedTrack, &m_selectedClip))
        m_selection = selectionWithLinkedPartners(m_project, m_selectedTrack, m_selectedClip);

    pushProjectEdit(before, tr("Audio separated"));
    finishEdit(tr("Audio separated"));
}

bool AppController::canMakeCompositeFromSelection() const
{
    // One level of nesting only: a composite cannot be made inside a composite, nor contain one.
    if (!m_project.activeSequenceId().isEmpty())
        return false;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    bool any = false;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (!m_project.tracks().at(pair.first).clips.at(pair.second).sequenceId.isEmpty())
            return false;
        any = true;
    }
    return any;
}

void AppController::makeCompositeFromSelection()
{
    if (!canMakeCompositeFromSelection())
        return;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty())
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    // Linked A/V partners and the adjustments pinned to a clip travel with it.
    QSet<QString> ids;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        for (const QPair<int, int> &linked : selectionWithLinkedPartners(m_project, pair.first, pair.second))
            ids.insert(m_project.tracks().at(linked.first).clips.at(linked.second).id);
    }
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (!clip.linkedClipId.isEmpty() && ids.contains(clip.linkedClipId))
                ids.insert(clip.id);
        }
    }

    drift::TimeUs startUs = std::numeric_limits<drift::TimeUs>::max();
    drift::TimeUs endUs = 0;
    int bottomVideoTrack = -1;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const drift::Track &track = m_project.tracks().at(t);
        for (const drift::Clip &clip : track.clips) {
            if (!ids.contains(clip.id))
                continue;
            if (!clip.sequenceId.isEmpty())
                return;
            startUs = qMin(startUs, clip.timelineStart);
            endUs = qMax(endUs, clip.timelineEnd());
            if (track.type == drift::TrackType::Video)
                bottomVideoTrack = t;
        }
    }
    if (endUs <= startUs)
        return;

    setPlaying(false);
    const drift::Project before = m_project.detachedCopy();

    // The composite keeps the selection's track layout, shifted to start at zero. Only tracks that
    // contribute a clip come along; a lane whose parent stays behind becomes a standalone
    // adjustment track (ensureTrackIds unparents it).
    drift::TrackList inner;
    QHash<QString, QString> innerTrackIds;
    for (const drift::Track &source : m_project.tracks()) {
        drift::Track copy = source;
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.locked = false;
        copy.clips.clear();
        copy.transitions.clear();
        for (drift::Clip clip : source.clips) {
            if (!ids.contains(clip.id))
                continue;
            clip.timelineStart -= startUs;
            copy.clips.append(clip);
        }
        if (copy.clips.isEmpty())
            continue;
        for (const drift::Transition &transition : source.transitions) {
            if (ids.contains(transition.fromClipId) && ids.contains(transition.toClipId))
                copy.transitions.append(transition);
        }
        innerTrackIds.insert(source.id, copy.id);
        inner.append(copy);
    }
    for (drift::Track &track : inner)
        track.parentTrackId = innerTrackIds.value(track.parentTrackId);
    const QString sequenceId = m_project.addSequence(inner);

    for (drift::Track &track : m_project.tracks()) {
        for (int i = track.clips.size() - 1; i >= 0; --i) {
            if (ids.contains(track.clips.at(i).id))
                track.clips.removeAt(i);
        }
        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = track.transitions.at(i);
            if (ids.contains(transition.fromClipId) || ids.contains(transition.toClipId))
                track.transitions.removeAt(i);
        }
    }

    int compositeCount = 0;
    for (const drift::MediaAsset &existing : m_project.assets()) {
        if (existing.kind == drift::MediaKind::Composite)
            ++compositeCount;
    }
    drift::MediaAsset asset;
    asset.kind = drift::MediaKind::Composite;
    asset.name = tr("Composite %1").arg(compositeCount + 1);
    asset.sequenceId = sequenceId;
    asset.durationUs = endUs - startUs;
    asset.width = m_project.width();
    asset.height = m_project.height();
    asset.fps = m_project.fps();
    asset.folderId = m_bin->currentFolderId();
    const QString assetId = m_project.addAsset(asset);

    drift::Clip composite;
    composite.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    composite.type = drift::ClipType::Composite;
    composite.assetId = assetId;
    composite.sequenceId = sequenceId;
    composite.name = asset.name;
    composite.timelineStart = startUs;
    composite.timelineDuration = endUs - startUs;
    composite.srcIn = 0;
    composite.srcOut = composite.timelineDuration;

    // The lowest video track the selection came from, now that it is cleared — unless something
    // left behind still occupies that span.
    int trackIndex = bottomVideoTrack;
    if (trackIndex >= 0) {
        for (const drift::Clip &clip : m_project.tracks().at(trackIndex).clips) {
            if (clip.timelineStart < endUs && startUs < clip.timelineEnd()) {
                trackIndex = -1;
                break;
            }
        }
    }
    if (trackIndex < 0) {
        trackIndex = drift::ensureFreeTrackForClipType(m_project, drift::ClipType::Composite, startUs,
                                                       composite.timelineDuration, true);
    }
    QList<drift::Clip> &clips = m_project.tracks()[trackIndex].clips;
    int insertAt = 0;
    while (insertAt < clips.size() && clips.at(insertAt).timelineStart < startUs)
        ++insertAt;
    clips.insert(insertAt, composite);

    if (m_assetLibrary)
        m_assetLibrary->syncToProject();
    pushProjectEdit(before, tr("Composite created"));
    finishEdit(tr("Composite created"));
    selectClip(trackIndex, insertAt);
    emit sequenceTabsChanged();
}
namespace {


bool isTransformClipData(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Adjustment
           && clip.adjustmentKind == drift::AdjustmentKind::Transform;
}

// The track a clip on `trackIndex` is covered through: lanes ride with their parent.
int coveredTrackFor(const drift::Project &project, int trackIndex)
{
    const int parent = drift::adjustmentLaneParentIndex(project, trackIndex);
    return parent >= 0 ? parent : trackIndex;
}

QString lowestTransformableTrackId(const QList<drift::Track> &tracks, int below = -1)
{
    QString id;
    for (int i = below + 1; i < tracks.size(); ++i) {
        if (drift::isTransformableTrack(tracks.at(i)))
            id = tracks.at(i).id;
    }
    return id;
}

} // namespace

void AppController::addTransformTrack()
{
    const drift::Project before = m_project;
    m_project.ensureTrackIds();
    const int layer = drift::insertTransformTrack(m_project.tracks(), 0,
                                                  lowestTransformableTrackId(m_project.tracks()));
    const drift::Clip clip = drift::makeTransformClip(
        0, qMax(m_project.durationUs(), drift::imageClipDurationUs()));
    m_project.tracks()[layer].clips.append(clip);
    if (m_selectedTransitionTrack >= 0)
        ++m_selectedTransitionTrack;
    pushProjectEdit(before, tr("Add transform layer"));
    finishEdit(tr("Transform layer added"));
    selectClipById(clip.id);
}

void AppController::addTransformClip(int trackIndex, double atSeconds, double durationSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size()
        || !m_project.tracks().at(trackIndex).isTransformLayer())
        return;
    const drift::TimeUs durUs = durationSeconds > 0.0 ? drift::secondsToUs(durationSeconds)
                                                      : drift::imageClipDurationUs();
    const drift::TimeUs wanted = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::Project before = m_project;
    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, wanted, durUs,
                                                        m_snapEnabled, m_playheadUs);
    const drift::Clip clip = drift::makeTransformClip(start, durUs);
    track.clips.append(clip);
    pushProjectEdit(before, tr("Add transform clip"));
    finishEdit(tr("Transform clip added"));
    selectClipById(clip.id);
}

void AppController::addTransformLayerAbove(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const int covered = coveredTrackFor(m_project, trackIndex);
    if (covered < 0 || !drift::isTransformableTrack(m_project.tracks().at(covered)))
        return;
    const drift::Track &target = m_project.tracks().at(covered);
    drift::TimeUs start = std::numeric_limits<drift::TimeUs>::max();
    drift::TimeUs end = 0;
    for (const drift::Clip &clip : target.clips) {
        start = qMin(start, clip.timelineStart);
        end = qMax(end, clip.timelineEnd());
    }
    if (end <= start) {
        start = m_playheadUs;
        end = start + drift::imageClipDurationUs();
    }

    const drift::Project before = m_project;
    m_project.ensureTrackIds();
    const int layer = drift::insertTransformTrack(m_project.tracks(), covered,
                                                  m_project.tracks().at(covered).id);
    const drift::Clip clip = drift::makeTransformClip(start, end - start);
    m_project.tracks()[layer].clips.append(clip);
    pushProjectEdit(before, tr("Add transform layer"));
    finishEdit(tr("Transform layer added"));
    selectClipById(clip.id);
}

void AppController::addTransformLayerForSelection()
{
    if (canTransformSelectionTogether()) {
        makeTransformLayerFromSelection();
        return;
    }
    const drift::Project before = m_project;
    m_project.ensureTrackIds();
    const int layer = drift::insertTransformTrack(m_project.tracks(), 0,
                                                  lowestTransformableTrackId(m_project.tracks()));
    const drift::Clip clip = drift::makeTransformClip(m_playheadUs, drift::imageClipDurationUs());
    m_project.tracks()[layer].clips.append(clip);
    if (m_selectedTransitionTrack >= 0)
        ++m_selectedTransitionTrack;
    pushProjectEdit(before, tr("Add transform layer"));
    finishEdit(tr("Transform layer added"));
    selectClipById(clip.id);
}

bool AppController::hasCameraTrack() const
{
    return drift::cameraTrackIndex(m_project.tracks()) >= 0;
}

void AppController::addCameraTrack()
{
    // With the lane already there, a whole-sequence clip would land on top of the ones it holds:
    // add a new shot at the playhead instead, in the next free slot.
    if (drift::cameraTrackIndex(m_project.tracks()) >= 0) {
        addCameraClip();
        return;
    }
    const drift::Project before = m_project;
    m_project.ensureTrackIds();
    const int index = drift::insertCameraTrack(m_project.tracks());
    // The camera goes in above everything, so every stored track index below it shifts by one.
    if (m_selectedTransitionTrack >= 0)
        ++m_selectedTransitionTrack;
    // Long enough to cover the whole sequence, so the first camera frames every shot rather than
    // snapping back to the default viewpoint partway through. An empty timeline still gets a
    // usable clip to drag keys onto.
    const drift::TimeUs span = qMax(m_project.durationUs(), drift::imageClipDurationUs());
    const drift::Clip clip = drift::makeCameraClip(0, span);
    m_project.tracks()[index].clips.append(clip);
    pushProjectEdit(before, tr("Add camera"));
    finishEdit(tr("Camera added"));
    selectClipById(clip.id);
}

void AppController::addCameraClip(double atSeconds, double durationSeconds)
{
    const drift::TimeUs durUs = durationSeconds > 0.0 ? drift::secondsToUs(durationSeconds)
                                                      : drift::imageClipDurationUs();
    const drift::TimeUs wanted = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::Project before = m_project;
    m_project.ensureTrackIds();
    int index = drift::cameraTrackIndex(m_project.tracks());
    if (index < 0) {
        // Make the lane here rather than delegating to addCameraTrack, which spans the whole
        // sequence: a clip asked for at a particular time has to land there.
        index = drift::insertCameraTrack(m_project.tracks());
        if (m_selectedTransitionTrack >= 0)
            ++m_selectedTransitionTrack;
    }
    drift::Track &track = m_project.tracks()[index];
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, wanted, durUs,
                                                        m_snapEnabled, m_playheadUs);
    const drift::Clip clip = drift::makeCameraClip(start, durUs);
    track.clips.append(clip);
    pushProjectEdit(before, tr("Add camera clip"));
    finishEdit(tr("Camera clip added"));
    selectClipById(clip.id);
}

bool AppController::canTransformSelectionTogether() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const int covered = coveredTrackFor(m_project, pair.first);
        if (covered >= 0 && drift::isTransformableTrack(m_project.tracks().at(covered)))
            return true;
    }
    return false;
}

void AppController::makeTransformLayerFromSelection()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    QStringList ids;
    for (const QPair<int, int> &pair : pairs) {
        if (isValidClipIndex(pair.first, pair.second))
            ids << m_project.tracks().at(pair.first).clips.at(pair.second).id;
    }
    makeTransformLayerForClips(ids);
}

QVariantMap AppController::makeTransformLayerForClips(const QStringList &clipIds, double atSeconds,
                                                      double durationSeconds)
{
    int top = -1;
    int bottom = -1;
    drift::TimeUs start = std::numeric_limits<drift::TimeUs>::max();
    drift::TimeUs end = 0;
    const QList<drift::Track> &tracks = m_project.tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        const int covered = coveredTrackFor(m_project, t);
        if (covered < 0 || !drift::isTransformableTrack(tracks.at(covered)))
            continue;
        for (const drift::Clip &clip : tracks.at(t).clips) {
            if (!clipIds.contains(clip.id))
                continue;
            top = top < 0 ? covered : qMin(top, covered);
            bottom = qMax(bottom, covered);
            start = qMin(start, clip.timelineStart);
            end = qMax(end, clip.timelineEnd());
        }
    }
    if (top < 0 || end <= start)
        return {};
    if (atSeconds >= 0.0)
        start = drift::secondsToUs(atSeconds);
    const drift::TimeUs durUs = durationSeconds > 0.0 ? drift::secondsToUs(durationSeconds)
                                                      : end - start;
    if (durUs <= 0)
        return {};

    const drift::Project before = m_project;
    m_project.ensureTrackIds();
    const QString endId = m_project.tracks().at(bottom).id;
    const int layer = drift::insertTransformTrack(m_project.tracks(), top, endId);
    drift::Clip clip = drift::makeTransformClip(start, durUs);
    m_project.tracks()[layer].clips.append(clip);
    if (m_selectedTransitionTrack >= layer)
        ++m_selectedTransitionTrack;
    pushProjectEdit(before, tr("Transform together"));
    finishEdit(tr("Transform layer added"));
    selectClipById(clip.id);

    int trackIndex = -1;
    int clipIndex = -1;
    findClipById(m_project, clip.id, &trackIndex, &clipIndex);
    return {{QStringLiteral("track"), trackIndex},
            {QStringLiteral("clip"), clipIndex},
            {QStringLiteral("id"), clip.id}};
}

QVariantList AppController::transformSpanOptions(int trackIndex) const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    if (trackIndex < 0 || trackIndex >= tracks.size() || !tracks.at(trackIndex).isTransformLayer())
        return {};

    // An enclosing layer bounds how far down this one may reach.
    int limit = int(tracks.size()) - 1;
    for (const int outer : drift::transformLayersCovering(tracks, trackIndex))
        limit = qMin(limit, drift::transformSpanEndIndex(tracks, outer));

    const int current = drift::transformSpanEndIndex(tracks, trackIndex);
    QVariantList out;
    int first = -1;
    int count = 0;
    for (int i = trackIndex + 1; i <= limit; ++i) {
        if (!drift::isTransformableTrack(tracks.at(i)))
            continue;
        if (first < 0)
            first = i;
        ++count;
        // Ending here must not cut through a nested layer's span.
        bool cuts = false;
        for (int nested = trackIndex + 1; nested < i && !cuts; ++nested) {
            if (tracks.at(nested).isTransformLayer())
                cuts = drift::transformSpanEndIndex(tracks, nested) > i;
        }
        if (cuts)
            continue;
        out.append(QVariantMap{
            {QStringLiteral("endId"), tracks.at(i).id},
            {QStringLiteral("endIndex"), i},
            {QStringLiteral("firstIndex"), first},
            {QStringLiteral("count"), count},
            {QStringLiteral("kind"), count == 1 ? QStringLiteral("only") : QStringLiteral("range")},
            {QStringLiteral("current"), i == current},
        });
    }
    if (!out.isEmpty()) {
        QVariantMap last = out.constLast().toMap();
        if (last.value(QStringLiteral("count")).toInt() > 1
            && lowestTransformableTrackId(tracks, trackIndex) == last.value(QStringLiteral("endId"))) {
            last.insert(QStringLiteral("kind"), QStringLiteral("all"));
            out.last() = last;
        }
    }
    return out;
}

bool AppController::setTransformSpan(int trackIndex, const QString &endTrackId)
{
    bool valid = false;
    for (const QVariant &option : transformSpanOptions(trackIndex))
        valid = valid || option.toMap().value(QStringLiteral("endId")).toString() == endTrackId;
    if (!valid)
        return false;
    if (m_project.tracks().at(trackIndex).spanEndTrackId == endTrackId)
        return true;
    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].spanEndTrackId = endTrackId;
    pushProjectEdit(before, tr("Change transform span"));
    finishEdit(tr("Transform layer now covers %n track(s)", "",
                  int(drift::transformSpanTrackIndexes(m_project.tracks(), trackIndex).size())));
    return true;
}

QVariantMap AppController::transformLayerCoverage(int trackIndex) const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    if (trackIndex < 0 || trackIndex >= tracks.size() || !tracks.at(trackIndex).isTransformLayer())
        return {};
    QVariantList covers;
    for (const int i : drift::transformSpanTrackIndexes(tracks, trackIndex))
        covers.append(i);
    const int end = drift::transformSpanEndIndex(tracks, trackIndex);
    return {{QStringLiteral("endIndex"), end},
            {QStringLiteral("endId"), end >= 0 ? tracks.at(end).id : QString()},
            {QStringLiteral("covers"), covers},
            {QStringLiteral("count"), covers.size()},
            {QStringLiteral("depth"), drift::transformLayersCovering(tracks, trackIndex).size()}};
}

QVariantList AppController::transformLayerCoveredClips(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const QList<drift::Track> &tracks = m_project.tracks();
    const drift::Clip &layerClip = tracks.at(trackIndex).clips.at(clipIndex);
    if (!isTransformClipData(layerClip))
        return {};
    QVariantList out;
    for (const int t : drift::transformSpanTrackIndexes(tracks, trackIndex)) {
        for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
            const drift::Clip &clip = tracks.at(t).clips.at(c);
            if (clip.timelineStart < layerClip.timelineEnd()
                && clip.timelineEnd() > layerClip.timelineStart) {
                out.append(QVariantMap{{QStringLiteral("track"), t}, {QStringLiteral("clip"), c}});
            }
        }
    }
    return out;
}

QVariantList AppController::transformLayersCovering(int trackIndex) const
{
    QVariantList out;
    for (const int layer : drift::transformLayersCovering(m_project.tracks(), trackIndex))
        out.append(layer);
    return out;
}

QVariantMap AppController::transformParentAt(int trackIndex) const
{
    return transformParentToMap(transformParentFor(trackIndex));
}

void AppController::selectTransformParent()
{
    if (!isValidClipIndex(m_selectedTrack, m_selectedClip))
        return;
    const QVariantList parents = clipAt(m_selectedTrack, m_selectedClip)
                                     .value(QStringLiteral("transformParents"))
                                     .toList();
    if (parents.isEmpty())
        return;
    const QVariantMap innermost = parents.constFirst().toMap();
    selectClip(innermost.value(QStringLiteral("track")).toInt(),
               innermost.value(QStringLiteral("clip")).toInt());
}

void AppController::selectTransformChildren(int trackIndex, int clipIndex)
{
    const QVariantList children = transformLayerCoveredClips(trackIndex, clipIndex);
    if (!children.isEmpty())
        setSelection(children);
}

QVariantList AppController::sequenceTabs() const
{
    QHash<QString, QString> names;
    for (const drift::MediaAsset &asset : m_project.assets()) {
        if (asset.kind == drift::MediaKind::Composite)
            names.insert(asset.sequenceId, asset.name);
    }
    QVariantList tabs;
    for (const QString &id : m_project.openSequenceTabs()) {
        tabs.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), names.value(id, tr("Composite"))},
        });
    }
    return tabs;
}

QVariantList AppController::compositeSequences() const
{
    QVariantList sequences;
    for (const drift::MediaAsset &asset : m_project.assets()) {
        if (asset.kind != drift::MediaKind::Composite || !m_project.hasSequence(asset.sequenceId))
            continue;
        sequences.append(QVariantMap{
            {QStringLiteral("id"), asset.sequenceId},
            {QStringLiteral("name"), asset.name},
        });
    }
    return sequences;
}

void AppController::openSequence(const QString &sequenceId)
{
    if (!m_project.hasSequence(sequenceId))
        return;
    if (!sequenceId.isEmpty() && !m_project.openSequenceTabs().contains(sequenceId)) {
        m_project.openSequenceTabs().append(sequenceId);
        emit sequenceTabsChanged();
    }
    if (sequenceId == m_project.activeSequenceId())
        return;

    setPlaying(false);
    if (m_multicamActive)
        endMulticamSession();
    clearTransitionSelection();
    clearSelection();
    m_sequencePlayheads.insert(m_project.activeSequenceId(), m_playheadUs);
    m_project.activateSequence(sequenceId);
    // Every index-keyed cache (the MCP clip index among them) describes the timeline just left.
    m_mcpController->bumpEditRevision();
    notifyTracksChanged();
    setPlayheadUs(m_sequencePlayheads.value(sequenceId, 0));
    emit sequenceTabsChanged();
    emit workAreaChanged();
}

void AppController::openCompositeClip(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    openSequence(m_project.tracks().at(trackIndex).clips.at(clipIndex).sequenceId);
}

void AppController::openCompositeAsset(const QString &assetId)
{
    const drift::MediaAsset *asset = m_project.asset(assetId);
    if (asset && asset->kind == drift::MediaKind::Composite)
        openSequence(asset->sequenceId);
}

void AppController::closeSequenceTab(const QString &sequenceId)
{
    if (sequenceId.isEmpty())
        return;
    if (sequenceId == m_project.activeSequenceId())
        openSequence(QString());
    m_project.openSequenceTabs().removeAll(sequenceId);
    m_sequencePlayheads.remove(sequenceId);
    emit sequenceTabsChanged();
}

bool AppController::flattenComposite(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;
    const drift::Clip clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Composite || !m_project.hasSequence(clip.sequenceId))
        return false;
    if (m_exportInProgress) {
        setLastMessage(tr("Export already in progress"), QStringLiteral("warning"));
        return false;
    }
    const QString outputPath = drift::newEditedMediaPath(m_project.id(), QStringLiteral("video"));
    if (outputPath.isEmpty()) {
        setLastMessage(tr("Could not create an output file"), QStringLiteral("error"));
        return false;
    }

    setPlaying(false);
    m_exportCancel.storeRelaxed(0);
    m_exportProgress = 0.0;
    emit exportProgressChanged();
    m_exportInProgress = true;
    m_exportByAgent = m_exportRequestedByAgent;
    emit exportInProgressChanged();
    setLastMessage(tr("Flattening composite…"));

    // Exactly the part of the composite this clip plays. A file cannot carry the transparency
    // around the composite's content, so the project background fills it.
    const drift::Project source = m_project.sequenceView(clip.sequenceId);
    ExportSettings settings = Exporter::defaultSettings();
    settings.startUs = clip.srcIn;
    settings.endUs = clip.srcOut;
    const QString clipId = clip.id;
    const QString sequenceId = clip.sequenceId;
    const QString name = clip.name;
    const drift::TimeUs srcInUs = clip.srcIn;
    const drift::TimeUs durationUs = clip.srcOut - clip.srcIn;

    (void)QtConcurrent::run([this, source, settings, outputPath, clipId, sequenceId, name, srcInUs,
                             durationUs]() {
        Exporter::BackgroundHold hold(QStringLiteral("Flattening composite"), /*cancellable=*/true);
        QString error;
        const auto report = [this](double fraction) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_exportProgress = fraction;
                    emit exportProgressChanged();
                },
                Qt::QueuedConnection);
            return m_exportCancel.loadRelaxed() == 0 && !Exporter::BackgroundHold::cancelRequested();
        };
        const bool ok = Exporter::run(source, settings, outputPath, &error, report);
        QMetaObject::invokeMethod(
            this,
            [this, ok, error, outputPath, clipId, sequenceId, name, srcInUs, durationUs]() {
                m_exportInProgress = false;
                m_exportProgress = ok ? 1.0 : 0.0;
                emit exportInProgressChanged();
                emit exportProgressChanged();
                if (!ok) {
                    QFile::remove(outputPath);
                    setLastMessage(error.isEmpty() ? tr("Flattening was cancelled")
                                                   : tr("Could not flatten the composite: %1").arg(error),
                                   QStringLiteral("error"));
                    return;
                }
                finishFlatten(clipId, sequenceId, outputPath, name, srcInUs, durationUs);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void AppController::finishFlatten(const QString &clipId, const QString &sequenceId,
                                  const QString &path, const QString &name, drift::TimeUs srcInUs,
                                  drift::TimeUs durationUs)
{
    // The instance lives on the main timeline; the user may have opened a tab meanwhile.
    openSequence(QString());
    int trackIndex = -1;
    int clipIndex = -1;
    if (!findClipById(m_project, clipId, &trackIndex, &clipIndex)) {
        QFile::remove(path);
        setLastMessage(tr("The composite clip was removed before flattening finished"),
                       QStringLiteral("warning"));
        return;
    }

    const drift::Clip &composite = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    // The file holds the range the clip played when the render started.
    if (composite.srcIn != srcInUs || composite.srcOut - composite.srcIn != durationUs) {
        QFile::remove(path);
        setLastMessage(tr("The composite clip was trimmed while flattening; try again"),
                       QStringLiteral("warning"));
        return;
    }
    const drift::Project before = m_project.detachedCopy();
    const drift::MediaAsset *compositeAsset = m_project.asset(composite.assetId);

    drift::MediaAsset asset;
    asset.kind = drift::MediaKind::Video;
    asset.path = path;
    asset.name = tr("%1 (flattened)").arg(name);
    asset.durationUs = durationUs;
    asset.width = m_project.width();
    asset.height = m_project.height();
    asset.fps = m_project.fps();
    asset.hasAudio = true;
    asset.hasAudioKnown = true;
    asset.frameRateKnown = true;
    asset.folderId = compositeAsset ? compositeAsset->folderId : m_bin->currentFolderId();
    const QString assetId = m_project.addAsset(asset);

    // The file starts where the clip's source range did, so every other property of the clip —
    // effects, transform, fades, the A/V link — carries over unchanged.
    const auto rebase = [&](drift::Clip &clip) {
        clip.assetId = assetId;
        clip.path = path;
        clip.sequenceId.clear();
        clip.srcOut -= clip.srcIn;
        clip.srcIn = 0;
    };
    const QString linkId = composite.linkId;
    for (drift::Track &track : m_project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (clip.id == clipId) {
                clip.type = drift::ClipType::Video;
                rebase(clip);
            } else if (!linkId.isEmpty() && clip.linkId == linkId && clip.sequenceId == sequenceId) {
                rebase(clip);
            }
        }
    }

    if (m_assetLibrary) {
        m_assetLibrary->syncToProject();
        m_assetLibrary->ensureMedia(m_assetLibrary->indexOfId(assetId));
    }
    pushProjectEdit(before, tr("Composite flattened"));
    finishEdit(tr("Composite flattened"));
    setLastMessage(tr("Composite flattened"));
}

void AppController::reconcileSequenceTabs()
{
    QStringList &tabs = m_project.openSequenceTabs();
    tabs.erase(std::remove_if(tabs.begin(), tabs.end(),
                              [this](const QString &id) { return !m_project.hasSequence(id); }),
               tabs.end());
    const QString active = m_project.activeSequenceId();
    if (!active.isEmpty() && !tabs.contains(active))
        tabs.append(active);
    emit sequenceTabsChanged();
}

void AppController::pruneOrphanSequences()
{
    QSet<QString> referenced;
    for (const drift::MediaAsset &asset : m_project.assets()) {
        if (asset.kind == drift::MediaKind::Composite)
            referenced.insert(asset.sequenceId);
    }
    const QString active = m_project.activeSequenceId();
    bool changed = false;
    for (const QString &id : m_project.sequenceIds()) {
        if (referenced.contains(id))
            continue;
        if (id == active) {
            clearSelection();
            m_project.activateSequence(QString());
            m_mcpController->bumpEditRevision();
            notifyTracksChanged();
        }
        m_project.removeSequence(id);
        changed = true;
    }
    if (changed)
        emit sequenceTabsChanged();
}

void AppController::syncCompositeAssetDurations()
{
    bool changed = false;
    for (drift::MediaAsset &asset : m_project.assets()) {
        if (asset.kind != drift::MediaKind::Composite)
            continue;
        const drift::TimeUs durationUs = m_project.sequenceDurationUs(asset.sequenceId);
        if (asset.durationUs != durationUs) {
            asset.durationUs = durationUs;
            changed = true;
        }
    }
    if (changed && m_assetLibrary)
        m_assetLibrary->syncToProject();
}

void AppController::separateAllAudioTracks(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (clip.type != drift::ClipType::Video)
        return;
    const QString videoId = clip.id;

    if (!detachAllAudioTracksFromVideo(
            m_project,
            m_assetLibrary,
            trackIndex,
            clipIndex))
        return;

    // New companion tracks can shift the original indexes, so resolve by id.
    if (findClipById(m_project, videoId, &trackIndex, &clipIndex)) {
        m_selectedTrack = trackIndex;
        m_selectedClip = clipIndex;
        m_selection = selectionWithLinkedPartners(m_project, trackIndex, clipIndex);
    }
    pushProjectEdit(before, tr("All audio tracks separated"));
    finishEdit(tr("All audio tracks separated"));
}

void AppController::separateAllAudioTracksFromSelection()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;

    QStringList videoIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        if (clip.type == drift::ClipType::Video)
            videoIds.append(clip.id);
    }
    QString selectedId;
    if (isValidClipIndex(m_selectedTrack, m_selectedClip))
        selectedId = m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).id;

    const drift::Project before = m_project;
    bool changed = false;
    QSet<QString> detachedVideoIds;
    for (const QString &videoId : videoIds) {
        if (detachedVideoIds.contains(videoId))
            continue;
        int trackIndex = -1;
        int clipIndex = -1;
        if (!findClipById(m_project, videoId, &trackIndex, &clipIndex))
            continue;
        if (detachAllAudioTracksFromVideo(
                m_project,
                m_assetLibrary,
                trackIndex,
                clipIndex)) {
            detachedVideoIds.insert(videoId);
            changed = true;
        }
    }

    if (!changed)
        return;

    if (!selectedId.isEmpty()
        && findClipById(m_project, selectedId, &m_selectedTrack, &m_selectedClip))
        m_selection = selectionWithLinkedPartners(m_project, m_selectedTrack, m_selectedClip);

    pushProjectEdit(before, tr("All audio tracks separated"));
    finishEdit(tr("All audio tracks separated"));
}

QVariantList AppController::clipAudioStreams(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};

    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.path.isEmpty())
        return {};

    const QList<StreamInfo> streams = cachedAudioStreams(clip.path);
    QVariantList out;
    for (int i = 0; i < streams.size(); ++i) {
        const StreamInfo &s = streams.at(i);
        QString label;
        if (!s.title.isEmpty()) {
            label = QStringLiteral("Track %1: %2 (%3 ch, %4 Hz)")
                        .arg(i + 1)
                        .arg(s.title)
                        .arg(s.channels)
                        .arg(s.sampleRate);
        } else {
            label = QStringLiteral("Track %1: %2 ch, %3 Hz (%4)")
                        .arg(i + 1)
                        .arg(s.channels)
                        .arg(s.sampleRate)
                        .arg(s.codecName);
        }
        if (!s.language.isEmpty())
            label += QStringLiteral(" [%1]").arg(s.language);

        QVariantMap map;
        map.insert(QStringLiteral("index"), i);
        map.insert(QStringLiteral("streamIndex"), s.streamIndex);
        map.insert(QStringLiteral("label"), label);
        map.insert(QStringLiteral("title"), s.title);
        map.insert(QStringLiteral("language"), s.language);
        map.insert(QStringLiteral("codec"), s.codecName);
        map.insert(QStringLiteral("channels"), s.channels);
        map.insert(QStringLiteral("sampleRate"), s.sampleRate);
        out.append(map);
    }
    return out;
}

int AppController::clipAudioStreamCount(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return 0;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.path.isEmpty())
        return 0;
    return cachedAudioStreams(clip.path).size();
}

void AppController::setClipAudioStreamIndex(int trackIndex, int clipIndex, int streamIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex) || streamIndex < 0)
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (clip.audioStreamIndex == streamIndex)
        return;

    clip.audioStreamIndex = streamIndex;
    pushProjectEdit(before, tr("Change audio track"));
    finishEdit(tr("Change audio track"));
}

void AppController::unlinkSelectedClips()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;

    const drift::Project before = m_project;
    bool changed = false;
    QSet<QString> clearedLinkIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;

        // A copy: clearing the clip's own linkId mid-loop would stop later partners matching.
        const QString linkId = m_project.tracks().at(pair.first).clips.at(pair.second).linkId;
        if (linkId.isEmpty() || clearedLinkIds.contains(linkId))
            continue;

        clearedLinkIds.insert(linkId);
        for (drift::Track &track : m_project.tracks()) {
            for (drift::Clip &candidate : track.clips) {
                if (candidate.linkId == linkId) {
                    candidate.linkId.clear();
                    candidate.linkLoose = false;
                }
            }
        }
        changed = true;
    }

    if (!changed)
        return;

    if (m_selectedTrack >= 0 && m_selectedClip >= 0)
        m_selection = {qMakePair(m_selectedTrack, m_selectedClip)};

    pushProjectEdit(before, tr("Clips unlinked"));
    finishEdit(tr("Audio unlinked"));
}

bool AppController::canLinkSelection() const
{
    // Two or more selected clips, none of them already part of a link.
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    int valid = 0;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (!m_project.tracks().at(pair.first).clips.at(pair.second).linkId.isEmpty())
            return false;
        ++valid;
    }
    return valid >= 2;
}

void AppController::linkSelectedClips()
{
    if (!canLinkSelection())
        return;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    const drift::Project before = m_project;
    const QString linkId = newClipId();
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        clip.linkId = linkId;
        // Manually linked clips usually come from different media, so the link is loose: it
        // never copies one clip's duration, trim or speed onto the other.
        clip.linkLoose = true;
    }

    pushProjectEdit(before, tr("Clips linked"));
    finishEdit(tr("Clips linked"));
}

void AppController::setClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::TimeUs fin = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeInSeconds));
    drift::TimeUs fout = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeOutSeconds));
    fin = qMin(fin, clip.timelineDuration);
    fout = qMin(fout, clip.timelineDuration - fin);
    if (clip.fadeInUs == fin && clip.fadeOutUs == fout)
        return;

    const drift::Project before = m_project;
    // First fade on an audio clip defaults to equal-power (constant loudness).
    const bool hadFade = clip.fadeInUs > 0 || clip.fadeOutUs > 0;
    if (!hadFade && (fin > 0 || fout > 0) && clip.type == drift::ClipType::Audio)
        clip.fadeCurve = drift::FadeCurve::EqualPower;
    clip.fadeInUs = fin;
    clip.fadeOutUs = fout;

    // Timeline fade handles are the CapCut Fade In/Out animation for visual clips.
    if (clip.type != drift::ClipType::Audio && clip.type != drift::ClipType::Subtitle) {
        if (fin > 0) {
            clip.animIn.kind = drift::ClipAnimKind::Fade;
            clip.animIn.durationUs = fin;
            clip.animIn.curve = clip.fadeCurve;
            clip.animIn.shape = clip.fadeShape;
            clip.animIn.ease = drift::clipAnimCurveToEase(clip.fadeCurve);
        } else if (clip.animIn.kind == drift::ClipAnimKind::Fade) {
            clip.animIn.kind = drift::ClipAnimKind::None;
        }
        if (fout > 0) {
            clip.animOut.kind = drift::ClipAnimKind::Fade;
            clip.animOut.durationUs = fout;
            clip.animOut.curve = clip.fadeCurve;
            clip.animOut.shape = clip.fadeShape;
            clip.animOut.ease = drift::clipAnimCurveToEase(clip.fadeCurve);
        } else if (clip.animOut.kind == drift::ClipAnimKind::Fade) {
            clip.animOut.kind = drift::ClipAnimKind::None;
        }
    }

    syncLinkedPartnersFrom(m_project, clip);
    pushProjectEdit(before, tr("Fade updated"));
    finishEdit(tr("Fade updated"));
}

void AppController::setClipFadeCurve(int trackIndex, int clipIndex, const QString &curve)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::FadeCurve newCurve = drift::fadeCurveFromString(curve);
    if (clip.fadeCurve == newCurve
        && (newCurve != drift::FadeCurve::Custom || !clip.fadeShape.isEmpty()))
        return;

    const drift::Project before = m_project;
    clip.fadeCurve = newCurve;
    if (newCurve == drift::FadeCurve::Custom && clip.fadeShape.isEmpty())
        clip.fadeShape = drift::FadeShape::smoothPreset();
    // Keep CapCut Fade animations on the same style as the timeline curve editor.
    if (clip.animIn.kind == drift::ClipAnimKind::Fade) {
        clip.animIn.curve = newCurve;
        clip.animIn.shape = clip.fadeShape;
        clip.animIn.ease = drift::clipAnimCurveToEase(newCurve);
    }
    if (clip.animOut.kind == drift::ClipAnimKind::Fade) {
        clip.animOut.curve = newCurve;
        clip.animOut.shape = clip.fadeShape;
        clip.animOut.ease = drift::clipAnimCurveToEase(newCurve);
    }
    syncLinkedPartnersFrom(m_project, clip);
    pushProjectEdit(before, tr("Fade curve changed"));
    finishEdit(tr("Fade curve updated"));
}

void AppController::setClipAnimation(int trackIndex, int clipIndex, const QString &which,
                                     const QVariantMap &patch)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type == drift::ClipType::Audio || clip.type == drift::ClipType::Subtitle)
        return;
    if (patch.isEmpty())
        return;

    const bool isIn = which == QLatin1String("animIn");
    const bool isOut = which == QLatin1String("animOut");
    if (!isIn && !isOut)
        return;

    drift::ClipAnimation next = isIn ? clip.animIn : clip.animOut;
    if (patch.contains(QStringLiteral("kind")))
        next.kind = drift::clipAnimKindFromString(patch.value(QStringLiteral("kind")).toString());
    if (patch.contains(QStringLiteral("duration")))
        next.durationUs =
            drift::secondsToUs(qBound(0.0, patch.value(QStringLiteral("duration")).toDouble(), 10.0));
    if (patch.contains(QStringLiteral("curve"))) {
        next.curve = drift::fadeCurveFromString(patch.value(QStringLiteral("curve")).toString());
        next.ease = drift::clipAnimCurveToEase(next.curve);
        if (next.curve == drift::FadeCurve::Custom && next.shape.isEmpty())
            next.shape = clip.fadeShape.isEmpty() ? drift::FadeShape::smoothPreset() : clip.fadeShape;
    } else if (patch.contains(QStringLiteral("ease"))) {
        next.ease = drift::clipAnimEaseFromString(patch.value(QStringLiteral("ease")).toString());
        next.curve = drift::clipAnimEaseToCurve(next.ease);
    }

    if (next.kind != drift::ClipAnimKind::None && next.durationUs <= 0)
        next.durationUs = 500000;
    next.durationUs = qMin(next.durationUs, clip.timelineDuration);

    const drift::ClipAnimation &current = isIn ? clip.animIn : clip.animOut;
    if (next.kind == current.kind && next.durationUs == current.durationUs
        && next.curve == current.curve && next.ease == current.ease)
        return;

    const drift::Project before = m_project;
    drift::Clip &mutableClip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (isIn)
        mutableClip.animIn = next;
    else
        mutableClip.animOut = next;

    // CapCut: Fade kind owns the timeline edge fade on that side; motion clears it.
    if (isIn) {
        if (next.kind == drift::ClipAnimKind::Fade) {
            mutableClip.fadeInUs = next.durationUs;
            mutableClip.fadeCurve = next.curve;
            if (next.curve == drift::FadeCurve::Custom)
                mutableClip.fadeShape = next.shape;
        } else {
            mutableClip.fadeInUs = 0;
        }
    } else {
        if (next.kind == drift::ClipAnimKind::Fade) {
            mutableClip.fadeOutUs = next.durationUs;
            mutableClip.fadeCurve = next.curve;
            if (next.curve == drift::FadeCurve::Custom)
                mutableClip.fadeShape = next.shape;
        } else {
            mutableClip.fadeOutUs = 0;
        }
    }

    pushProjectEdit(before, tr("Clip animation changed"));
    finishEdit(tr("Clip animation updated"));
}
