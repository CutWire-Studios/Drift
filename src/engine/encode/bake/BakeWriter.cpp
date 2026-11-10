#include "BakeWriter.h"

#include "BakeFile.h"
#include "core/project/Project.h"
#include "engine/encode/export/Exporter.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <memory>

extern "C" {
#include <libavcodec/packet.h>
}

namespace drift {

namespace {

struct PresetDef
{
    int crf;
    const char *x264Preset;
    int gop;
};

constexpr PresetDef kPresets[BakeWriter::kPresetCount] = {
    {23, "veryfast", 15},
    {18, "veryfast", 15},
    {14, "fast", 10},
};

const PresetDef &presetDef(int preset)
{
    return kPresets[std::clamp(preset, 0, BakeWriter::kPresetCount - 1)];
}

} // namespace

int BakeWriter::gopSize(int preset)
{
    return presetDef(preset).gop;
}

BakeWriter::Result BakeWriter::bake(BakeFile &file, const Project &project, qint64 startFrame,
                                    qint64 endFrame, int preset, const QByteArray &fingerprint,
                                    const ProgressFn &progress)
{
    Result result;
    result.startFrame = startFrame;
    result.endFrame = startFrame;
    if (startFrame < 0 || endFrame <= startFrame) {
        result.error = QStringLiteral("Invalid bake range");
        return result;
    }

    const int fps = std::max(1, project.fps());
    const PresetDef &def = presetDef(preset);

    // Exporter samples frame i at startUs + llround(i * 1e6 / fps), which is within a microsecond of
    // the timeline's own frame (startFrame + i) time, and counts round(duration * fps / 1e6) frames.
    const auto frameUs = [fps](qint64 frame) {
        return static_cast<TimeUs>(std::llround(static_cast<double>(frame) * 1e6 / fps));
    };

    struct State
    {
        QByteArray extradata;
        QByteArray gopBytes;
        QVector<quint32> gopSizes;
        qint64 gopFirst = 0;
        qint64 packets = 0;
        qint64 uncommitted = 0;
        QString error;
    } st;
    st.gopFirst = startFrame;

    bool cancelled = false;
    QString bakeId;
    qint64 bakedEnd = startFrame;

    const auto commit = [&]() {
        st.uncommitted = 0;
        if (!file.commit()) {
            st.error = QStringLiteral("Could not write the bake file");
            return false;
        }
        return true;
    };

    const auto flushGop = [&]() {
        if (st.gopSizes.isEmpty())
            return true;
        const qint64 count = st.gopSizes.size();
        file.removeRange(st.gopFirst, st.gopFirst + count);
        if (bakeId.isEmpty()) {
            bakeId = file.beginBake(st.gopFirst, fps, project.width(), project.height(), preset,
                                    st.extradata, fingerprint);
            if (bakeId.isEmpty()) {
                st.error = QStringLiteral("Could not start the bake");
                return false;
            }
        }
        if (!file.appendGop(bakeId, st.gopFirst, st.gopBytes, st.gopSizes)) {
            st.error = QStringLiteral("Could not store a baked GOP");
            return false;
        }
        bakedEnd = st.gopFirst + count;
        st.uncommitted += count;
        st.gopBytes.clear();
        st.gopSizes.clear();
        return st.uncommitted < qint64(fps) * 2 || commit();
    };

    auto sink = std::make_shared<ExportSettings::PacketSink>();
    sink->onStart = [&](const QByteArray &extradata) { st.extradata = extradata; };
    sink->onPacket = [&](const AVPacket *pkt) {
        if (st.extradata.isEmpty()) {
            st.error = QStringLiteral("The encoder produced no stream header");
            return false;
        }
        if ((pkt->flags & AV_PKT_FLAG_KEY) && !st.gopSizes.isEmpty() && !flushGop())
            return false;
        if (st.gopSizes.isEmpty())
            st.gopFirst = startFrame + st.packets;
        st.gopBytes.append(reinterpret_cast<const char *>(pkt->data), pkt->size);
        st.gopSizes.append(quint32(pkt->size));
        ++st.packets;
        return true;
    };

    ExportSettings settings;
    settings.videoCodecId = QStringLiteral("h264");
    settings.rateControl = QStringLiteral("crf");
    settings.crf = def.crf;
    settings.videoPreset = QString::fromLatin1(def.x264Preset);
    settings.fpsNum = fps;
    settings.fpsDen = 1;
    settings.startUs = frameUs(startFrame);
    settings.endUs = frameUs(endFrame);
    settings.useBakes = false;
    settings.bakeGop = def.gop;
    settings.packetSink = sink;

    QString error;
    const bool ran = Exporter::run(
        project, settings, QString(), &error, [&](double fraction) {
            if (progress && !progress(fraction)) {
                cancelled = true;
                return false;
            }
            return true;
        });

    if (st.error.isEmpty())
        flushGop();
    if (bakedEnd > startFrame)
        commit();

    result.bakeId = bakeId;
    result.endFrame = bakedEnd;
    result.cancelled = cancelled;
    if (!st.error.isEmpty()) {
        result.error = st.error;
    } else if (!ran && !cancelled) {
        result.error = error.isEmpty() ? QStringLiteral("Bake failed") : error;
    } else {
        result.ok = true;
    }
    return result;
}

} // namespace drift
