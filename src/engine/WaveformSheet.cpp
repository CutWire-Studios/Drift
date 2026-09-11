#include "WaveformSheet.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

namespace drift::waveformsheet {
namespace {

constexpr int kNFft = 512;
constexpr int kHop = 256;
constexpr int kNBins = kNFft / 2 + 1;
constexpr float kLogGain = 4.0f;

// Value for column x of `width`: nearest bucket when there are fewer buckets than columns,
// the loudest of the bucket span otherwise, so a single transient still shows.
float sampleAt(const QVector<float> &buckets, int x, int width)
{
    const int count = buckets.size();
    if (count == 0)
        return 0.0f;
    if (count == width)
        return buckets.at(x);
    const int from = int(qint64(x) * count / width);
    const int to = std::max(from + 1, int(qint64(x + 1) * count / width));
    float peak = 0.0f;
    for (int i = from; i < to && i < count; ++i)
        peak = std::max(peak, buckets.at(i));
    return peak;
}

void drawLane(QPainter &p, const QVector<float> &buckets, int top, int height, int width,
              QRgb colour)
{
    const int mid = top + height / 2;
    const double halfMax = height / 2.0 - 1.0;
    p.setPen(QColor(colour));
    for (int x = 0; x < width; ++x) {
        const float v = std::clamp(sampleAt(buckets, x, width), 0.0f, 1.0f);
        const int half = int(std::lround(v * halfMax));
        if (half <= 0)
            continue;
        p.drawLine(x, mid - half, x, mid + half);
    }
}

QRgb rampColour(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    const int r = int(std::lround(255.0f * std::min(1.0f, v * 1.6f)));
    const int g = int(std::lround(255.0f * std::clamp((v - 0.35f) * 1.7f, 0.0f, 1.0f)));
    const int b = int(std::lround(255.0f * (v < 0.5f ? 0.25f + v : std::max(0.0f, 1.5f - v * 2.0f))));
    return qRgb(r, g, b);
}

QString axisLabel(double seconds, double step)
{
    return QString::number(seconds, 'f', step < 1.0 ? 1 : 0) + QStringLiteral("s");
}

} // namespace

QImage render(const Input &in, const Options &opt)
{
    const int width = std::max(1, opt.width);
    const int height = std::max(1, opt.height);
    QImage img(width, height, QImage::Format_RGB888);
    img.fill(Qt::black);

    const bool spec = !in.spectrogram.isEmpty();
    const int specH = spec ? std::max(0, opt.spectrogramHeight) : 0;
    const int axisH = std::max(0, opt.axisHeight);
    const int laneArea = std::max(0, height - axisH - specH);
    const int mixedH = in.speech.isEmpty() ? laneArea : int(std::lround(laneArea * 0.55));
    const int speechH = laneArea - mixedH;
    const double duration = in.durationSeconds > 0.0 ? in.durationSeconds : 1.0;
    const auto xAt = [&](double seconds) {
        return int(std::lround((seconds - in.startSeconds) / duration * width));
    };

    QPainter p(&img);

    for (const auto &range : in.silence) {
        const int x0 = std::clamp(xAt(range.first), 0, width);
        const int x1 = std::clamp(xAt(range.second), 0, width);
        if (x1 > x0)
            p.fillRect(x0, 0, x1 - x0, laneArea, QColor(kSilenceShade));
    }

    p.setPen(QColor(70, 70, 82));
    for (double t : in.beats) {
        const int x = xAt(t);
        if (x >= 0 && x < width)
            p.drawLine(x, 0, x, laneArea - 1);
    }

    drawLane(p, in.mixed, 0, mixedH, width, qRgb(150, 180, 230));
    if (speechH > 0)
        drawLane(p, in.speech, mixedH, speechH, width, qRgb(235, 165, 80));

    p.setPen(QColor(255, 230, 60));
    const int tick = std::max(1, mixedH / 4);
    for (double t : in.onsets) {
        const int x = xAt(t);
        if (x >= 0 && x < width)
            p.drawLine(x, 0, x, tick);
    }

    if (spec && specH > 0) {
        const int columns = in.spectrogram.size();
        const int bins = in.spectrogram.first().size();
        const int top = laneArea;
        for (int x = 0; x < width; ++x) {
            const QVector<float> &column = in.spectrogram.at(int(qint64(x) * columns / width));
            for (int y = 0; y < specH; ++y) {
                const int bin = int(qint64(specH - 1 - y) * bins / specH);
                const float v = bin < column.size() ? column.at(bin) : 0.0f;
                img.setPixel(x, top + y, rampColour(v));
            }
        }
    }

    if (axisH > 0) {
        const int top = height - axisH;
        p.fillRect(0, top, width, axisH, QColor(24, 24, 24));
        QFont font;
        font.setPixelSize(std::clamp(axisH - 10, 8, 14));
        p.setFont(font);
        const QFontMetrics metrics(font);
        const double step = axisStepSeconds(in.durationSeconds, width);
        const double end = in.startSeconds + in.durationSeconds;
        for (double t = std::ceil(in.startSeconds / step) * step; t <= end + 1e-9; t += step) {
            const int x = xAt(t);
            if (x < 0 || x >= width)
                continue;
            p.setPen(QColor(150, 150, 150));
            p.drawLine(x, top, x, top + 4);
            const QString label = axisLabel(t, step);
            if (x + 2 + metrics.horizontalAdvance(label) > width)
                continue;
            p.setPen(QColor(200, 200, 200));
            p.drawText(x + 2, top + axisH - 4, label);
        }
    }

    return img;
}

double axisStepSeconds(double durationSeconds, int width, int minPixels)
{
    static constexpr double kSteps[] = {0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    if (durationSeconds <= 0.0 || width <= 0)
        return 1.0;
    const double pxPerSec = width / durationSeconds;
    for (double step : kSteps) {
        if (step * pxPerSec >= minPixels)
            return step;
    }
    return 600.0;
}

QVector<QVector<float>> spectrogram(const float *mono, qint64 frameCount, int sampleRate,
                                    int bins, int columns, double minHz)
{
    if (!mono || sampleRate <= 0 || bins <= 0 || columns <= 0)
        return {};
    const qint64 frames = (frameCount - kNFft) / kHop + 1;
    if (frames < 1)
        return {};

    AVTXContext *tx = nullptr;
    av_tx_fn fn = nullptr;
    float scale = 1.0f;
    if (av_tx_init(&tx, &fn, AV_TX_FLOAT_RDFT, 0, kNFft, &scale, 0) < 0)
        return {};
    float *in = static_cast<float *>(av_malloc(sizeof(float) * kNFft));
    auto *out = static_cast<float *>(av_malloc(sizeof(float) * 2 * (kNBins + 1)));

    std::vector<float> hann(kNFft);
    for (int n = 0; n < kNFft; ++n)
        hann[n] = 0.5f * (1.0f - std::cos(2.0 * M_PI * n / kNFft)); // periodic Hann

    // FFT bin -> log band, or -1 below minHz.
    const double nyquist = sampleRate / 2.0;
    minHz = std::clamp(minHz, 1.0, nyquist / 2.0);
    const double span = std::log(nyquist / minHz);
    std::vector<int> band(kNBins, -1);
    for (int k = 0; k < kNBins; ++k) {
        const double hz = double(k) * sampleRate / kNFft;
        if (hz < minHz)
            continue;
        band[k] = std::min(bins - 1, int(std::log(hz / minHz) / span * bins));
    }

    QVector<QVector<float>> result(columns, QVector<float>(bins, 0.0f));
    for (qint64 t = 0; t < frames; ++t) {
        const float *src = mono + t * kHop;
        for (int i = 0; i < kNFft; ++i)
            in[i] = src[i] * hann[i];
        fn(tx, out, in, sizeof(float));
        QVector<float> &column = result[int(t * columns / frames)];
        for (int k = 0; k < kNBins; ++k) {
            if (band[k] < 0)
                continue;
            const float re = out[2 * k];
            const float im = out[2 * k + 1];
            const float mag = std::sqrt(re * re + im * im);
            column[band[k]] = std::max(column[band[k]], mag);
        }
    }

    av_tx_uninit(&tx);
    av_free(in);
    av_free(out);

    float peak = 0.0f;
    for (QVector<float> &column : result) {
        for (float &v : column) {
            v = std::log1p(kLogGain * v);
            peak = std::max(peak, v);
        }
    }
    if (peak > 1e-9f) {
        for (QVector<float> &column : result) {
            for (float &v : column)
                v /= peak;
        }
    }
    return result;
}

} // namespace drift::waveformsheet
