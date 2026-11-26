#include <QtTest>

#include "engine/audio/analysis/Stft.h"

#include <cmath>
#include <random>

// MSVC does not define M_PI in <cmath>, and this is the guard the audio analysis beside it uses.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace drift;

class StftTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsBadSizes();
    void frameCountCoversTheSignal();
    void roundTripIsExact();
    void roundTripIsExactAtHalfOverlap();
    void unityMaskChangesNothing();
    void complementaryMasksSumToTheOriginal();
    void zeroMaskIsSilence();
    void dcLevelSurvives();
    void shortSignalRoundTrips();
};

namespace {

std::vector<float> noise(int n, unsigned seed = 1234)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-0.8f, 0.8f);
    std::vector<float> out(static_cast<size_t>(n), 0.0f);
    for (float &v : out)
        v = dist(rng);
    return out;
}

std::vector<float> tone(int n, double freq, double rate)
{
    std::vector<float> out(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        out[size_t(i)] = 0.5f * float(std::sin(2.0 * M_PI * freq * i / rate));
    return out;
}

double maxError(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.size() != b.size())
        return 1e9;
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, double(std::abs(a[i] - b[i])));
    return worst;
}

} // namespace

void StftTest::rejectsBadSizes()
{
    QVERIFY(!Stft(1000, 250).ok()); // not a power of two
    QVERIFY(!Stft(1024, 0).ok());
    QVERIFY(!Stft(1024, 2048).ok()); // hop longer than the window leaves gaps
    QVERIFY(Stft(1024, 256).ok());
}

void StftTest::frameCountCoversTheSignal()
{
    Stft stft(1024, 256);
    QVERIFY(stft.ok());
    QCOMPARE(stft.bins(), 513);
    QCOMPARE(stft.frameCount(0), 0);
    // A length that is not a multiple of the hop still gets a frame whose window reaches the end.
    QCOMPARE(stft.frameCount(1024), 5);
    QCOMPARE(stft.frameCount(1025), 6);
}

void StftTest::roundTripIsExact()
{
    Stft stft(1024, 256);
    QVERIFY(stft.ok());

    const std::vector<float> input = noise(8000);
    std::vector<float> spectra;
    stft.analyze(input, &spectra);
    QCOMPARE(int(spectra.size()), stft.frameCount(8000) * stft.bins() * 2);

    std::vector<float> output;
    stft.synthesize(spectra, int64_t(input.size()), &output);
    QCOMPARE(output.size(), input.size());
    // Exact to float rounding, including the first and last half window where fewer frames
    // overlap — that is what the measured window-sum normalization buys.
    QVERIFY2(maxError(input, output) < 1e-4, qPrintable(QString::number(maxError(input, output))));
}

void StftTest::roundTripIsExactAtHalfOverlap()
{
    Stft stft(512, 256); // hop == fftSize / 2
    QVERIFY(stft.ok());
    const std::vector<float> input = noise(4000, 99);
    std::vector<float> spectra;
    std::vector<float> output;
    stft.analyze(input, &spectra);
    stft.synthesize(spectra, int64_t(input.size()), &output);
    QVERIFY(maxError(input, output) < 1e-4);
}

void StftTest::unityMaskChangesNothing()
{
    Stft stft(1024, 256);
    const std::vector<float> input = tone(6000, 440.0, 44100.0);
    std::vector<float> spectra;
    stft.analyze(input, &spectra);

    // Multiplying every bin by 1 is the degenerate separation: it must be audibly a no-op, or a
    // model that predicts "keep everything" would still colour the signal.
    for (float &v : spectra)
        v *= 1.0f;

    std::vector<float> output;
    stft.synthesize(spectra, int64_t(input.size()), &output);
    QVERIFY(maxError(input, output) < 1e-4);
}

void StftTest::complementaryMasksSumToTheOriginal()
{
    Stft stft(1024, 256);
    const std::vector<float> input = noise(6000, 7);
    std::vector<float> spectra;
    stft.analyze(input, &spectra);

    // A separation into two stems is two masks that sum to one per bin. If synthesis is exact the
    // stems must add back to the input, which is what stops "isolate voice" and "remove vocals"
    // from together losing or doubling energy.
    const int bins = stft.bins();
    std::vector<float> voice(spectra.size(), 0.0f);
    std::vector<float> rest(spectra.size(), 0.0f);
    for (size_t i = 0; i < spectra.size(); i += 2) {
        const int bin = int((i / 2) % size_t(bins));
        const float mask = 0.25f + 0.5f * float(bin) / float(bins); // varies across frequency
        voice[i] = spectra[i] * mask;
        voice[i + 1] = spectra[i + 1] * mask;
        rest[i] = spectra[i] * (1.0f - mask);
        rest[i + 1] = spectra[i + 1] * (1.0f - mask);
    }

    std::vector<float> voicePcm;
    std::vector<float> restPcm;
    stft.synthesize(voice, int64_t(input.size()), &voicePcm);
    stft.synthesize(rest, int64_t(input.size()), &restPcm);

    std::vector<float> summed(input.size(), 0.0f);
    for (size_t i = 0; i < summed.size(); ++i)
        summed[i] = voicePcm[i] + restPcm[i];
    QVERIFY(maxError(input, summed) < 1e-4);
}

void StftTest::zeroMaskIsSilence()
{
    Stft stft(1024, 256);
    const std::vector<float> input = noise(3000, 5);
    std::vector<float> spectra;
    stft.analyze(input, &spectra);
    std::fill(spectra.begin(), spectra.end(), 0.0f);

    std::vector<float> output;
    stft.synthesize(spectra, int64_t(input.size()), &output);
    QCOMPARE(output.size(), input.size());
    for (float v : output)
        QVERIFY(std::abs(v) < 1e-6f);
}

void StftTest::dcLevelSurvives()
{
    // A constant signal is all DC. It catches a bin-0 or Nyquist mistake that noise would hide,
    // because those two bins are real-only and easy to mis-handle in a real FFT layout.
    Stft stft(1024, 256);
    const std::vector<float> input(5000, 0.3f);
    std::vector<float> spectra;
    std::vector<float> output;
    stft.analyze(input, &spectra);
    stft.synthesize(spectra, int64_t(input.size()), &output);
    QVERIFY(maxError(input, output) < 1e-4);
}

void StftTest::shortSignalRoundTrips()
{
    // Shorter than one window: every frame overhangs an end, so this is purely a test of the
    // padding and the crop back.
    Stft stft(1024, 256);
    const std::vector<float> input = noise(300, 42);
    std::vector<float> spectra;
    std::vector<float> output;
    stft.analyze(input, &spectra);
    stft.synthesize(spectra, int64_t(input.size()), &output);
    QCOMPARE(output.size(), input.size());
    QVERIFY(maxError(input, output) < 1e-4);
}

QTEST_MAIN(StftTest)
#include "tst_stft.moc"
