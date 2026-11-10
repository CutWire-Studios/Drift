#pragma once

#include "core/base/Time.h"
#include "engine/decode/readers/PreviewVideoFrame.h"

#include <QString>

#include <map>
#include <memory>
#include <mutex>

namespace drift {

class BakeIndex;

// Decodes baked frames back out of a BakeFile for the compositor. The owner publishes the
// committed index with setSource(); each reader (a preview stream, an export run) passes its own
// key to read() and gets a private decoder, file handle and frame cache, so concurrent readers
// never share a seek position or a codec context. Thread safe.
class BakeReaderPool
{
public:
    BakeReaderPool();
    ~BakeReaderPool();

    // `index` is the file's last committed snapshot; pass an empty pointer to stop serving bakes.
    void setSource(const QString &path, std::shared_ptr<const BakeIndex> index);
    std::shared_ptr<const BakeIndex> index() const;

    // The project frame a timeline time falls on: the inverse of BakeWriter's llround(f*1e6/fps).
    static qint64 frameForTime(TimeUs timeUs, int fps);

    // The baked picture of `frame`, as NV12 tagged BT.709 limited, or an invalid frame when no bake
    // of the same width, height and fps covers it. `approximate` returns the GOP's keyframe unless
    // this key's decoder already sits in that GOP at or before `frame`. `bakeIdOut` receives the
    // covering bake's id.
    PreviewVideoFrame read(qint64 frame, int streamKey, bool approximate, int width, int height, int fps,
                           QString *bakeIdOut = nullptr);

    // Whether read() would be served, without decoding.
    bool covers(qint64 frame, int width, int height, int fps, QString *bakeIdOut = nullptr) const;

    // Frees a key's decoder and cached frames.
    void release(int streamKey);

private:
    struct Stream;
    Stream &stream(int key);

    mutable std::mutex m_mutex;
    QString m_path;
    std::shared_ptr<const BakeIndex> m_index;
    std::map<int, std::unique_ptr<Stream>> m_streams;
};

} // namespace drift
