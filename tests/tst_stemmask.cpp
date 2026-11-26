#include <QtTest>

#include "engine/ml/audio/StemMask.h"
#include "engine/audio/analysis/Stft.h"

#include <cmath>
#include <random>

using namespace drift;

class StemMaskTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMismatchedSizes();
    void stemsSumBackToTheMixture();
    void oneStemClaimingEverythingGetsItAll();
    void aSilentStemGetsSilence();
    void unclaimedBinsAreSplitEqually();
    void unpredictedTopBinsAreSplitEqually();
    void phaseIsReused();
    void negativeEstimatesAreClamped();
    void separationRoundTripsThroughTheStft();
};

namespace {

std::vector<float> ramp(int n, float start, float step)
{
    std::vector<float> out(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        out[size_t(i)] = start + step * float(i);
    return out;
}

} // namespace

void StemMaskTest::rejectsMismatchedSizes()
{
    const std::vector<float> mix(2 * 4 * 2, 1.0f); // 2 frames, 4 bins
    QVERIFY(stemSpectraFromMagnitudes(mix, 2, 4, {}, 4).empty());
    QVERIFY(stemSpectraFromMagnitudes(mix, 0, 4, {{1.0f}}, 4).empty());
    // A magnitude buffer that is not frames * modelBins is a contract violation, not something to
    // paper over with a partial result.
    QVERIFY(stemSpectraFromMagnitudes(mix, 2, 4, {std::vector<float>(7, 1.0f)}, 4).empty());
    // modelBins may not exceed the spectrum.
    QVERIFY(stemSpectraFromMagnitudes(mix, 2, 4, {std::vector<float>(10, 1.0f)}, 5).empty());
}

void StemMaskTest::stemsSumBackToTheMixture()
{
    const int frames = 3;
    const int bins = 6;
    const int modelBins = 4; // leaves two unpredicted bins on top
    const std::vector<float> mix = ramp(frames * bins * 2, -1.5f, 0.37f);

    std::vector<std::vector<float>> magnitudes{ramp(frames * modelBins, 0.1f, 0.21f),
                                               ramp(frames * modelBins, 2.0f, -0.11f)};
    const auto stems = stemSpectraFromMagnitudes(mix, frames, bins, magnitudes, modelBins);
    QCOMPARE(int(stems.size()), 2);

    for (size_t i = 0; i < mix.size(); ++i) {
        const float summed = stems[0][i] + stems[1][i];
        QVERIFY2(std::abs(summed - mix[i]) < 1e-5f,
                 qPrintable(QStringLiteral("index %1: %2 vs %3").arg(i).arg(summed).arg(mix[i])));
    }
}

void StemMaskTest::oneStemClaimingEverythingGetsItAll()
{
    const int frames = 2;
    const int bins = 4;
    const std::vector<float> mix = ramp(frames * bins * 2, 0.5f, 0.25f);
    std::vector<std::vector<float>> magnitudes{std::vector<float>(frames * bins, 1.0f),
                                               std::vector<float>(frames * bins, 0.0f)};
    const auto stems = stemSpectraFromMagnitudes(mix, frames, bins, magnitudes, bins);
    for (size_t i = 0; i < mix.size(); ++i) {
        QVERIFY(std::abs(stems[0][i] - mix[i]) < 1e-6f);
        QVERIFY(std::abs(stems[1][i]) < 1e-6f);
    }
}

void StemMaskTest::aSilentStemGetsSilence()
{
    const int frames = 2;
    const int bins = 4;
    const std::vector<float> mix = ramp(frames * bins * 2, 1.0f, 0.1f);
    std::vector<std::vector<float>> magnitudes{std::vector<float>(frames * bins, 3.0f),
                                               std::vector<float>(frames * bins, 0.0f)};
    const auto stems = stemSpectraFromMagnitudes(mix, frames, bins, magnitudes, bins);
    for (float v : stems[1])
        QVERIFY(std::abs(v) < 1e-6f);
}

void StemMaskTest::unclaimedBinsAreSplitEqually()
{
    // Every estimate zero: a silent passage, or a model declining to commit. The bin must still
    // add up rather than divide by zero.
    const int frames = 1;
    const int bins = 2;
    const std::vector<float> mix{2.0f, -4.0f, 1.0f, 0.0f};
    std::vector<std::vector<float>> magnitudes{std::vector<float>(bins, 0.0f),
                                               std::vector<float>(bins, 0.0f)};
    const auto stems = stemSpectraFromMagnitudes(mix, frames, bins, magnitudes, bins);
    QCOMPARE(stems[0][0], 1.0f);
    QCOMPARE(stems[1][0], 1.0f);
    QCOMPARE(stems[0][1], -2.0f);
    QCOMPARE(stems[1][1], -2.0f);
    for (size_t i = 0; i < mix.size(); ++i)
        QVERIFY(std::abs(stems[0][i] + stems[1][i] - mix[i]) < 1e-6f);
}

void StemMaskTest::unpredictedTopBinsAreSplitEqually()
{
    const int frames = 1;
    const int bins = 3;
    const int modelBins = 1;
    const std::vector<float> mix{1.0f, 1.0f, 4.0f, 2.0f, 6.0f, -8.0f};
    // The one predicted bin goes entirely to stem 0; the two above it are outside the graph.
    std::vector<std::vector<float>> magnitudes{{1.0f}, {0.0f}};
    const auto stems = stemSpectraFromMagnitudes(mix, frames, bins, magnitudes, modelBins);

    QCOMPARE(stems[0][0], 1.0f); // predicted bin: all of it
    QCOMPARE(stems[1][0], 0.0f);
    QCOMPARE(stems[0][2], 2.0f); // unpredicted: half each
    QCOMPARE(stems[1][2], 2.0f);
    QCOMPARE(stems[0][5], -4.0f);
    QCOMPARE(stems[1][5], -4.0f);
}

void StemMaskTest::phaseIsReused()
{
    // The mask is real, so each stem keeps the mixture's phase: the ratio of imaginary to real
    // must be untouched wherever the stem is not silent.
    const int frames = 1;
    const int bins = 2;
    const std::vector<float> mix{3.0f, 4.0f, -1.0f, 2.0f};
    std::vector<std::vector<float>> magnitudes{{1.0f, 1.0f}, {3.0f, 1.0f}};
    const auto stems = stemSpectraFromMagnitudes(mix, frames, bins, magnitudes, bins);
    for (const auto &stem : stems) {
        QVERIFY(std::abs(stem[1] / stem[0] - 4.0f / 3.0f) < 1e-5f);
        QVERIFY(std::abs(stem[3] / stem[2] - 2.0f / -1.0f) < 1e-5f);
    }
}

void StemMaskTest::negativeEstimatesAreClamped()
{
    // Nothing stops a graph emitting a small negative magnitude. Left alone it would make a mask
    // negative — a stem that inverts the mixture's phase — or make the total cancel to near zero
    // and blow the other stem's mask up past one.
    const int frames = 1;
    const int bins = 1;
    const std::vector<float> mix{2.0f, 0.0f};
    std::vector<std::vector<float>> magnitudes{{-1.0f}, {1.0f}};
    const auto stems = stemSpectraFromMagnitudes(mix, frames, bins, magnitudes, bins);
    QCOMPARE(stems[0][0], 0.0f);
    QCOMPARE(stems[1][0], 2.0f);
}

void StemMaskTest::separationRoundTripsThroughTheStft()
{
    // The property that matters end to end: analyse a signal, split it, resynthesise both stems,
    // and the two must add back to what went in. This is what makes "isolate voice" and "remove
    // vocals" two halves of one operation rather than two lossy approximations.
    Stft stft(1024, 256);
    QVERIFY(stft.ok());

    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> dist(-0.7f, 0.7f);
    std::vector<float> input(5000);
    for (float &v : input)
        v = dist(rng);

    std::vector<float> spectra;
    stft.analyze(input, &spectra);
    const int frames = stft.frameCount(int64_t(input.size()));
    const int bins = stft.bins();
    const int modelBins = bins / 2; // a graph that predicts only the lower half

    // Stand in for the model with something frequency-dependent, so the masks are not uniform.
    std::vector<std::vector<float>> magnitudes(2, std::vector<float>(size_t(frames) * size_t(modelBins)));
    for (int f = 0; f < frames; ++f) {
        for (int b = 0; b < modelBins; ++b) {
            const size_t cell = size_t(f) * size_t(modelBins) + size_t(b);
            magnitudes[0][cell] = float(b) / float(modelBins);
            magnitudes[1][cell] = 1.0f - float(b) / float(modelBins);
        }
    }

    const auto stems = stemSpectraFromMagnitudes(spectra, frames, bins, magnitudes, modelBins);
    QCOMPARE(int(stems.size()), 2);

    std::vector<float> a;
    std::vector<float> b;
    stft.synthesize(stems[0], int64_t(input.size()), &a);
    stft.synthesize(stems[1], int64_t(input.size()), &b);
    QCOMPARE(a.size(), input.size());

    double worst = 0.0;
    for (size_t i = 0; i < input.size(); ++i)
        worst = std::max(worst, double(std::abs(a[i] + b[i] - input[i])));
    QVERIFY2(worst < 1e-4, qPrintable(QString::number(worst)));
}

QTEST_MAIN(StemMaskTest)
#include "tst_stemmask.moc"
