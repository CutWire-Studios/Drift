#include <QtTest>

#include "core/edit/BeatCut.h"
#include "engine/audio/analysis/AudioOnsets.h"

#include <cmath>
#include <random>
#include <vector>

class BeatSyncTest : public QObject
{
    Q_OBJECT

private slots:
    void tracksJitteredClickTrack_data();
    void tracksJitteredClickTrack();
    void everyNthBeatAnchorsOnDownbeat();
    void planLandsCutsOnBeats();
    void planSyncsToBarLines();
    void planStopsWhenClipsRunOut();
    void planMarksShortClips();
};

namespace {

constexpr int kRate = 22050;

std::vector<float> clickTrack(double bpm, double seconds, double jitterSec, float noiseLevel,
                              std::vector<double> *truth)
{
    const double period = 60.0 / bpm;
    const int frames = static_cast<int>(seconds * kRate);
    std::vector<float> pcm(frames, 0.0f);
    std::mt19937 rng(777);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::uniform_real_distribution<double> jitter(-jitterSec, jitterSec);
    for (int i = 0; i < frames; ++i)
        pcm[size_t(i)] = noiseLevel * noise(rng);
    for (int c = 0; c * period < seconds - 0.1; ++c) {
        const double at = c * period + (c == 0 ? 0.0 : jitter(rng));
        if (truth)
            truth->push_back(at);
        const int start = int(at * kRate);
        const float accent = (c % 4 == 0) ? 1.0f : 0.7f;
        for (int i = 0; i < kRate / 20 && start + i < frames; ++i)
            pcm[size_t(start + i)] += accent * noise(rng) * std::exp(-i / (kRate * 0.01f));
    }
    return pcm;
}

} // namespace

void BeatSyncTest::tracksJitteredClickTrack_data()
{
    QTest::addColumn<double>("bpm");
    QTest::newRow("90") << 90.0;
    QTest::newRow("120") << 120.0;
    QTest::newRow("140") << 140.0;
}

void BeatSyncTest::tracksJitteredClickTrack()
{
    QFETCH(double, bpm);
    std::vector<double> truth;
    const std::vector<float> pcm = clickTrack(bpm, 30.0, 0.008, 0.05f, &truth);

    const AudioBeatAnalysis a = AudioOnsets::analyze(pcm.data(), int(pcm.size()), kRate, 0.0);
    QVERIFY2(std::abs(a.bpm - bpm) < bpm * 0.02, qPrintable(QStringLiteral("bpm %1").arg(a.bpm)));
    QVERIFY(a.beats.size() >= truth.size() - 2);

    int matched = 0;
    for (const double t : truth) {
        double nearest = 1e9;
        for (const double b : a.beats)
            nearest = std::min(nearest, std::abs(b - t));
        if (nearest < 0.03)
            ++matched;
    }
    QVERIFY2(matched >= int(truth.size()) - 2,
             qPrintable(QStringLiteral("%1 of %2 beats within 30 ms").arg(matched).arg(truth.size())));
}

void BeatSyncTest::everyNthBeatAnchorsOnDownbeat()
{
    const QList<double> beats{0, 1, 2, 3, 4, 5, 6, 7, 8};
    QCOMPARE(drift::beatsEvery(beats, 2, 1), (QList<double>{1, 3, 5, 7}));
    QCOMPARE(drift::beatsEvery(beats, 4, 2), (QList<double>{2, 6}));
    QCOMPARE(drift::beatsEvery(beats, 1, 3).size(), 9);
}

void BeatSyncTest::planLandsCutsOnBeats()
{
    QList<double> beats;
    for (int i = 0; i < 32; ++i)
        beats.append(0.5 + i * 0.5);
    drift::BeatCutOptions o;
    o.beatsPerCut = 2;
    o.syncToDownbeats = false;
    o.startSeconds = 0.0;
    o.endSeconds = 12.0;
    const QList<double> avail(20, 10.0);
    const auto plan = drift::planBeatCut(beats, avail, o);
    QVERIFY(plan.size() >= 3);
    QCOMPARE(plan.first().start, 0.0);
    // After the first clip every start is a beat, one beat-pair apart.
    for (int i = 1; i < plan.size(); ++i) {
        QVERIFY(std::abs(plan[i].start - plan[i - 1].start - plan[i - 1].duration) < 1e-9);
        const double rel = std::fmod(plan[i].start - 0.5, 0.5);
        QVERIFY(std::abs(rel) < 1e-9);
    }
    QVERIFY(plan.last().start + plan.last().duration <= 12.0 + 1e-9);
}

void BeatSyncTest::planSyncsToBarLines()
{
    QList<double> beats;
    for (int i = 0; i < 40; ++i)
        beats.append(i * 0.5);
    drift::BeatCutOptions o;
    o.beatsPerCut = 4;
    o.syncToDownbeats = true;
    o.firstDownbeat = 1;
    o.startSeconds = 0.0;
    o.endSeconds = 18.0;
    const auto plan = drift::planBeatCut(beats, QList<double>(10, 100.0), o);
    QVERIFY(plan.size() >= 3);
    for (int i = 1; i < plan.size(); ++i) {
        const int beatIndex = int(std::lround(plan[i].start / 0.5));
        QCOMPARE((beatIndex - 1) % 4, 0);
    }
}

void BeatSyncTest::planStopsWhenClipsRunOut()
{
    QList<double> beats;
    for (int i = 0; i < 40; ++i)
        beats.append(i * 0.5);
    drift::BeatCutOptions o;
    o.beatsPerCut = 1;
    o.syncToDownbeats = false;
    o.endSeconds = 19.0;
    QCOMPARE(drift::planBeatCut(beats, QList<double>(3, 5.0), o).size(), 3);
}

void BeatSyncTest::planMarksShortClips()
{
    QList<double> beats;
    for (int i = 0; i < 40; ++i)
        beats.append(i * 0.5);
    drift::BeatCutOptions o;
    o.beatsPerCut = 4;
    o.syncToDownbeats = false;
    o.endSeconds = 19.0;
    const auto plan = drift::planBeatCut(beats, QList<double>{0.3, 100.0, 100.0}, o);
    QVERIFY(plan.size() >= 2);
    QVERIFY(plan[0].shorterThanSlot);
    QCOMPARE(plan[0].duration, 0.3);
    QVERIFY(!plan[1].shorterThanSlot);
}

QTEST_APPLESS_MAIN(BeatSyncTest)
#include "tst_beatsync.moc"
