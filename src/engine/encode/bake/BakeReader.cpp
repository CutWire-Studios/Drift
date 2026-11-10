#include "engine/encode/bake/BakeReader.h"

#include "engine/encode/bake/BakeFile.h"

#include <QFile>

#include <cstring>
#include <list>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace drift {

namespace {
constexpr size_t kCachedFrames = 8;

struct CodecDeleter
{
    void operator()(AVCodecContext *c) const { avcodec_free_context(&c); }
};

AVFrame *toNv12(const AVFrame *src)
{
    AVFrame *out = av_frame_alloc();
    if (!out)
        return nullptr;
    out->format = AV_PIX_FMT_NV12;
    out->width = src->width;
    out->height = src->height;
    if (av_frame_get_buffer(out, 0) < 0) {
        av_frame_free(&out);
        return nullptr;
    }
    for (int y = 0; y < src->height; ++y)
        std::memcpy(out->data[0] + y * out->linesize[0], src->data[0] + y * src->linesize[0], size_t(src->width));
    const int cw = (src->width + 1) / 2;
    const int ch = (src->height + 1) / 2;
    for (int y = 0; y < ch; ++y) {
        const uint8_t *u = src->data[1] + y * src->linesize[1];
        const uint8_t *v = src->data[2] + y * src->linesize[2];
        uint8_t *dst = out->data[1] + y * out->linesize[1];
        for (int x = 0; x < cw; ++x) {
            dst[2 * x] = u[x];
            dst[2 * x + 1] = v[x];
        }
    }
    out->colorspace = AVCOL_SPC_BT709;
    out->color_range = AVCOL_RANGE_MPEG;
    out->color_primaries = AVCOL_PRI_BT709;
    out->color_trc = AVCOL_TRC_BT709;
    return out;
}
} // namespace

struct BakeReaderPool::Stream
{
    std::mutex mutex;
    std::shared_ptr<const BakeIndex> index;
    QFile file;
    quint64 epoch = ~quint64(0);
    QString path;

    std::unique_ptr<AVCodecContext, CodecDeleter> codec;
    QByteArray extradata;

    bool positioned = false;
    quint64 gopOffset = 0;
    qint64 gopFirst = 0;
    QByteArray gopBytes;
    std::vector<qsizetype> packetOffsets;
    int nextPacket = 0;
    int decoded = 0;

    struct Cached
    {
        qint64 frame;
        PreviewVideoFrame video;
    };
    std::list<Cached> cache;

    bool openCodec(const Bake &bake);
    bool loadGop(const BakeGop &gop);
    PreviewVideoFrame decodeTo(const BakeGop &gop, int target);
    PreviewVideoFrame cached(qint64 frame);
    void remember(qint64 frame, const PreviewVideoFrame &v);
};

bool BakeReaderPool::Stream::openCodec(const Bake &bake)
{
    if (codec && extradata == bake.extradata)
        return true;
    positioned = false;
    codec.reset();
    const AVCodec *h264 = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!h264)
        return false;
    std::unique_ptr<AVCodecContext, CodecDeleter> ctx(avcodec_alloc_context3(h264));
    if (!ctx)
        return false;
    ctx->extradata = static_cast<uint8_t *>(av_mallocz(size_t(bake.extradata.size()) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!ctx->extradata)
        return false;
    std::memcpy(ctx->extradata, bake.extradata.constData(), size_t(bake.extradata.size()));
    ctx->extradata_size = int(bake.extradata.size());
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx->thread_type = FF_THREAD_SLICE;
    ctx->thread_count = 4;
    if (avcodec_open2(ctx.get(), h264, nullptr) < 0)
        return false;
    codec = std::move(ctx);
    extradata = bake.extradata;
    return true;
}

bool BakeReaderPool::Stream::loadGop(const BakeGop &gop)
{
    if (!readGop(file, gop, gopBytes))
        return false;
    packetOffsets.clear();
    qsizetype off = 0;
    for (quint32 size : gop.packetSizes) {
        packetOffsets.push_back(off);
        off += qsizetype(size);
    }
    gopBytes.append(QByteArray(AV_INPUT_BUFFER_PADDING_SIZE, '\0'));
    avcodec_flush_buffers(codec.get());
    nextPacket = 0;
    decoded = 0;
    gopOffset = gop.offset;
    gopFirst = gop.firstFrame;
    positioned = true;
    return true;
}

PreviewVideoFrame BakeReaderPool::Stream::decodeTo(const BakeGop &gop, int target)
{
    PreviewVideoFrame result;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        positioned = false;
        return result;
    }
    const int count = int(gop.packetSizes.size());
    bool drained = false;
    while (decoded <= target) {
        int rc;
        if (nextPacket < count) {
            pkt->data = reinterpret_cast<uint8_t *>(gopBytes.data()) + packetOffsets[size_t(nextPacket)];
            pkt->size = int(gop.packetSizes[nextPacket]);
            ++nextPacket;
            rc = avcodec_send_packet(codec.get(), pkt);
        } else if (!drained) {
            drained = true;
            rc = avcodec_send_packet(codec.get(), nullptr);
        } else {
            break;
        }
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            positioned = false;
            break;
        }
        while (avcodec_receive_frame(codec.get(), frame) >= 0) {
            if (decoded == target) {
                if (AVFrame *nv12 = toNv12(frame))
                    result = takePreviewFrame(nv12, 0);
            }
            ++decoded;
            av_frame_unref(frame);
        }
    }
    if (drained || !result.isValid())
        positioned = false;
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return result;
}

PreviewVideoFrame BakeReaderPool::Stream::cached(qint64 frame)
{
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->frame == frame) {
            cache.splice(cache.begin(), cache, it);
            return it->video;
        }
    }
    return {};
}

void BakeReaderPool::Stream::remember(qint64 frame, const PreviewVideoFrame &v)
{
    cache.push_front({frame, v});
    while (cache.size() > kCachedFrames)
        cache.pop_back();
}

BakeReaderPool::BakeReaderPool() = default;
BakeReaderPool::~BakeReaderPool() = default;

void BakeReaderPool::setSource(const QString &path, std::shared_ptr<const BakeIndex> index)
{
    std::lock_guard lock(m_mutex);
    m_path = path;
    m_index = std::move(index);
}

std::shared_ptr<const BakeIndex> BakeReaderPool::index() const
{
    std::lock_guard lock(m_mutex);
    return m_index;
}

qint64 BakeReaderPool::frameForTime(TimeUs timeUs, int fps)
{
    if (timeUs < 0 || fps <= 0)
        return -1;
    return (qint64(timeUs) * fps + 500000) / 1000000;
}

bool BakeReaderPool::covers(qint64 frame, int width, int height, int fps, QString *bakeIdOut) const
{
    const auto idx = index();
    if (!idx || frame < 0)
        return false;
    const BakeLocation loc = idx->locate(frame);
    if (!loc || loc.bake->width != width || loc.bake->height != height || loc.bake->fps != fps)
        return false;
    if (bakeIdOut)
        *bakeIdOut = loc.bake->id;
    return true;
}

BakeReaderPool::Stream &BakeReaderPool::stream(int key)
{
    std::lock_guard lock(m_mutex);
    auto &slot = m_streams[key];
    if (!slot)
        slot = std::make_unique<Stream>();
    return *slot;
}

void BakeReaderPool::release(int streamKey)
{
    std::unique_ptr<Stream> dropped;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_streams.find(streamKey);
        if (it == m_streams.end())
            return;
        dropped = std::move(it->second);
        m_streams.erase(it);
    }
    std::lock_guard streamLock(dropped->mutex);
}

PreviewVideoFrame BakeReaderPool::read(qint64 frame, int streamKey, bool approximate, int width, int height,
                                       int fps, QString *bakeIdOut)
{
    std::shared_ptr<const BakeIndex> idx;
    QString path;
    Stream *s;
    {
        std::lock_guard lock(m_mutex);
        idx = m_index;
        path = m_path;
        auto &slot = m_streams[streamKey];
        if (!slot)
            slot = std::make_unique<Stream>();
        s = slot.get();
    }
    if (!idx || frame < 0)
        return {};
    const BakeLocation loc = idx->locate(frame);
    if (!loc || loc.bake->width != width || loc.bake->height != height || loc.bake->fps != fps)
        return {};
    if (bakeIdOut)
        *bakeIdOut = loc.bake->id;

    std::lock_guard lock(s->mutex);
    if (s->index != idx || s->path != path) {
        s->cache.clear();
        s->positioned = false;
        s->index = idx;
        if (s->path != path || s->epoch != idx->epoch || !s->file.isOpen()) {
            s->file.close();
            s->file.setFileName(path);
            if (!s->file.open(QIODevice::ReadOnly))
                return {};
            s->epoch = idx->epoch;
            s->path = path;
        }
    }

    if (PreviewVideoFrame hit = s->cached(frame); hit.isValid())
        return hit;

    const BakeGop &gop = *loc.gop;
    if (!s->openCodec(*loc.bake))
        return {};

    const bool inGop = s->positioned && s->gopOffset == gop.offset && s->gopFirst == gop.firstFrame;
    int target = loc.indexInGop;
    qint64 resultFrame = frame;
    if (!inGop || s->decoded > target) {
        if (approximate) {
            target = 0;
            resultFrame = gop.firstFrame;
            if (PreviewVideoFrame hit = s->cached(resultFrame); hit.isValid())
                return hit;
        }
        if (!s->loadGop(gop))
            return {};
    }
    PreviewVideoFrame out = s->decodeTo(gop, target);
    if (out.isValid())
        s->remember(resultFrame, out);
    return out;
}

} // namespace drift
