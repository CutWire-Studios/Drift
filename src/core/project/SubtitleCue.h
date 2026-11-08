#pragma once

#include "core/base/Time.h"

#include <QList>
#include <QString>

namespace drift {

struct SubtitleCue
{
    TimeUs startUs = 0; // relative to the parent clip's timeline start
    TimeUs endUs = 0;
    QString text;
    // subtitleSourceKey() of the media this cue was transcribed from; empty for hand-written cues.
    // Lets the cue follow its words when that clip is retimed (retimeLinkedSubtitles).
    QString source;
};

const SubtitleCue *activeSubtitleCueAt(const QList<SubtitleCue> &cues, TimeUs localUs);

// Index of the word being spoken at localUs inside a [startUs, endUs) window, or -1 when the
// window is empty or has not started. Drives the Karaoke accent rule. The timings are the same
// proportional ones packSubtitleCues uses — true word timestamps aren't available from our ONNX
// Whisper export — so this is exact at cue boundaries and interpolated in between.
int activeWordIndexAt(const QString &text, TimeUs startUs, TimeUs endUs, TimeUs localUs);
int subtitleCueIndexAt(const QList<SubtitleCue> &cues, TimeUs localUs);
void sortSubtitleCues(QList<SubtitleCue> &cues);

// Timeline label for a subtitle clip, e.g. "Subtitles (12)".
QString subtitleClipName(const QList<SubtitleCue> &cues);
// Whether `name` is one subtitleClipName() made (or empty), as opposed to one the user or an
// agent chose. Only a generated name follows the cue count as cues change.
bool isGeneratedSubtitleClipName(const QString &name);
// Recount a subtitle clip's name from its cues, unless it carries a chosen name.
void refreshSubtitleClipName(QString &name, const QList<SubtitleCue> &cues);

// Re-pack Whisper (or other) segment cues into shorter display lines, matching openai-whisper's
// VTT writer with word_timestamps + max_line_width / max_line_count. Words inside a segment get
// proportional timing (true attention-based word timestamps aren't available from our ONNX
// export); packing then walks those words and starts a new cue when the line would exceed
// maxLineWidth or when maxLineCount lines are already filled. A pause longer than 3s also
// forces a cue break.
// maxWordsPerCue caps how many words a single cue may hold; 0 or less leaves it uncapped. It
// composes with maxLineWidth — whichever limit is reached first ends the cue — so a small cap
// yields short cues whose boundaries are interpolated rather than measured, drifting slightly
// from the speech in between Whisper's own segment boundaries.
QList<SubtitleCue> packSubtitleCues(const QList<SubtitleCue> &cues, int maxLineWidth = 42,
                                    int maxLineCount = 1, int maxWordsPerCue = 0);

} // namespace drift
