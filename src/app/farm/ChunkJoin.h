#pragma once

#include <QString>
#include <QStringList>

namespace drift::farm {

// Joins the video of `chunks` (each starting on a keyframe, in order) and the audio of `audioFile`
// into `output` by copying packets, so nothing is re-encoded. `chunkFrames[i]` is how many frames
// chunk i holds; its packets are shifted to start where the previous chunk's frames end. Chunk
// files may carry their own (silent) audio; it is ignored. `audioFile` may be empty.
bool joinChunks(const QStringList &chunks, const QList<qint64> &chunkFrames, int fpsNum, int fpsDen,
                const QString &audioFile, const QString &output, QString *error);

} // namespace drift::farm
