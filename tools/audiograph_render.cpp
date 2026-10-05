// Renders interleaved stereo float32 through an audio-effect.json with the native DSP, using the
// same C API Drift Forge's WebAssembly preview exports. Forge's golden tests compare its wasm build
// against what this writes (scripts/update-audio-goldens.sh in Forge).
//
//   audiograph_render <audio-effect.json> <in.f32> <out.f32> [--rate 48000] [--chunk 128]
//
// Files the manifest references ("ir": "ir/plate.wav") are read relative to the manifest.

#include "engine/audio/DriftGraphApi.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <audio-effect.json> <in.f32> <out.f32> [--rate N] [--chunk N]\n", argv[0]);
        return 2;
    }
    int rate = 48000;
    int chunk = 128;
    for (int i = 4; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--rate") == 0)
            rate = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--chunk") == 0)
            chunk = std::atoi(argv[i + 1]);
    }

    const QString manifestPath = QString::fromLocal8Bit(argv[1]);
    const QByteArray manifest = readAll(manifestPath);
    const QByteArray input = readAll(QString::fromLocal8Bit(argv[2]));
    if (manifest.isEmpty() || input.isEmpty()) {
        std::fprintf(stderr, "cannot read the manifest or the input\n");
        return 1;
    }

    // Stage every "ir" the manifest names, the way Forge stages its assets.
    const QDir base = QFileInfo(manifestPath).absoluteDir();
    static const QRegularExpression irRef(QStringLiteral("\"ir\"\\s*:\\s*\"([^\"]+)\""));
    for (auto it = irRef.globalMatch(QString::fromUtf8(manifest)); it.hasNext();) {
        const QString path = it.next().captured(1);
        const QByteArray bytes = readAll(base.filePath(path));
        dg_stage_file(path.toUtf8().constData(), reinterpret_cast<const uint8_t *>(bytes.constData()), int(bytes.size()));
    }

    DriftGraph *graph = dg_create(manifest.constData(), rate);
    if (!graph) {
        std::fprintf(stderr, "%s\n", dg_last_error());
        return 1;
    }
    dg_reset(graph, 0.0);

    std::vector<float> samples(size_t(input.size()) / sizeof(float));
    std::memcpy(samples.data(), input.constData(), samples.size() * sizeof(float));
    const int frames = int(samples.size() / 2);
    for (int offset = 0; offset < frames; offset += chunk) {
        const int count = std::min(chunk, frames - offset);
        std::memcpy(dg_io(graph), samples.data() + offset * 2, sizeof(float) * size_t(count) * 2);
        dg_process(graph, count);
        std::memcpy(samples.data() + offset * 2, dg_io(graph), sizeof(float) * size_t(count) * 2);
    }
    dg_destroy(graph);

    QFile out(QString::fromLocal8Bit(argv[3]));
    if (!out.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "cannot write the output\n");
        return 1;
    }
    out.write(reinterpret_cast<const char *>(samples.data()), qint64(samples.size() * sizeof(float)));
    return 0;
}
