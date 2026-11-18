#include "engine/audio/analysis/AudioDucking.h"

#include <QtTest>

#include <cmath>
#include <vector>

using namespace drift;

namespace {

Clip musicClip(double startSeconds, double durationSeconds)
{
    Clip clip;
    clip.timelineStart = secondsToUs(startSeconds);
    clip.timelineDuration = secondsToUs(durationSeconds);
    return clip;
}

double valueAt(const Clip &clip, double timelineSeconds)
{
    return clip.volume.evaluateAt(secondsToUs(timelineSeconds) - clip.timelineStart);
}

} // namespace

class TstAudioDucking : public QObject
{
    Q_OBJECT

private slots:
    void mergeBridgesPausesShorterThanHoldAndRamps()
    {
        DuckSettings s;
        s.holdSeconds = 0.5;
        s.attackSeconds = 0.2;
        s.releaseSeconds = 0.3; // gap threshold 1.0 s
        const QList<DuckSpan> out = mergeDuckSpans({{5.0, 6.0}, {1.0, 2.0}, {2.9, 3.5}, {-5.0, -4.98}}, s);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0).start, 1.0);
        QCOMPARE(out.at(0).end, 3.5);
        QCOMPARE(out.at(1).start, 5.0);
        QCOMPARE(out.at(1).end, 6.0);
    }

    void levelSpansFindToneBurstsAndBridgeShortGaps()
    {
        const int rate = 8000;
        std::vector<float> pcm(size_t(rate * 6), 0.0f);
        const auto tone = [&](double from, double to, float amp) {
            for (size_t i = size_t(from * rate); i < size_t(to * rate); ++i)
                pcm[i] = amp * float(std::sin(2.0 * M_PI * 220.0 * double(i) / rate));
        };
        tone(1.0, 2.0, 0.5f);
        tone(2.05, 2.5, 0.5f); // 50 ms gap is bridged
        tone(4.0, 4.05, 0.5f); // too short
        tone(5.0, 5.5, 0.01f); // about -43 dBFS: only a sensitive setting hears it

        const QList<DuckSpan> loud = levelSpeechSpans(pcm.data(), qint64(pcm.size()), rate, 10.0, 0.0);
        QCOMPARE(loud.size(), 1);
        QVERIFY(qAbs(loud.first().start - 11.0) < 0.03);
        QVERIFY(qAbs(loud.first().end - 12.5) < 0.03);

        const QList<DuckSpan> sensitive = levelSpeechSpans(pcm.data(), qint64(pcm.size()), rate, 10.0, 1.0);
        QCOMPARE(sensitive.size(), 2);
        QVERIFY(qAbs(sensitive.last().start - 15.0) < 0.03);
    }

    void vadThresholdFallsWithSensitivity()
    {
        QVERIFY(vadThresholdForSensitivity(0.0) > vadThresholdForSensitivity(1.0));
        QCOMPARE(vadThresholdForSensitivity(1.0), 0.2f);
        QCOMPARE(vadThresholdForSensitivity(0.0), 0.8f);
        QCOMPARE(vadThresholdForSensitivity(-5.0), 0.9f);
    }

    void envelopeDipsAndRestoresAroundSpeech()
    {
        Clip music = musicClip(0.0, 20.0);
        DuckSettings s;
        s.reduceDb = -20.0;
        s.attackSeconds = 0.5;
        s.releaseSeconds = 1.0;
        const int keys = applyDuckEnvelope(music, {{5.0, 8.0}}, {{3.0, 11.0}}, s, 1.0);
        QCOMPARE(keys, 4);
        QCOMPARE(valueAt(music, 4.0), 1.0);
        QVERIFY(qAbs(valueAt(music, 6.0) - 0.1) < 1e-9);
        QCOMPARE(valueAt(music, 9.0), 1.0);
    }

    void envelopeIsIdempotentAcrossSettings()
    {
        Clip music = musicClip(2.0, 20.0);
        DuckSettings first;
        first.attackSeconds = 0.2;
        first.releaseSeconds = 0.4;
        const QList<DuckSpan> owned{{3.0, 14.0}};
        applyDuckEnvelope(music, {{5.0, 8.0}, {10.0, 12.0}}, owned, first, 1.0);
        QCOMPARE(music.volume.keyframes().size(), 8);

        DuckSettings second;
        second.reduceDb = -6.0;
        second.attackSeconds = 0.6;
        second.releaseSeconds = 0.9;
        const int keys = applyDuckEnvelope(music, {{5.5, 8.5}}, owned, second, 1.0);
        QCOMPARE(keys, 4);
        QCOMPARE(music.volume.keyframes().size(), 4);
        QVERIFY(qAbs(valueAt(music, 6.0) - std::pow(10.0, -6.0 / 20.0)) < 1e-9);
        QCOMPARE(valueAt(music, 12.0), 1.0);

        applyDuckEnvelope(music, {}, owned, second, 1.0);
        QVERIFY(music.volume.isEmpty());
    }

    void restLevelComesFromTheEnvelopeNotAStaticDefault()
    {
        Clip music = musicClip(0.0, 20.0);
        music.volume.setKeyframe(secondsToUs(0.0), 0.8);
        music.volume.setKeyframe(secondsToUs(1.0), 0.8);
        DuckSettings s;
        s.reduceDb = -20.0;
        s.attackSeconds = 0.5;
        applyDuckEnvelope(music, {{5.0, 6.0}}, {{3.0, 8.0}}, s, 1.0);
        QVERIFY(qAbs(valueAt(music, 5.5) - 0.08) < 1e-9);
        QVERIFY(qAbs(valueAt(music, 0.5) - 0.8) < 1e-9);
    }

    void dipsOutsideOrAtTheEdgeOfTheClipClamp()
    {
        Clip music = musicClip(10.0, 5.0);
        DuckSettings s;
        s.attackSeconds = 1.0;
        s.releaseSeconds = 1.0;
        QCOMPARE(applyDuckEnvelope(music, {{0.0, 5.0}, {20.0, 25.0}}, {}, s, 1.0), 0);
        // Starts before the clip: no pre key, ducked from its first frame.
        const int keys = applyDuckEnvelope(music, {{8.0, 11.0}}, {}, s, 1.0);
        QCOMPARE(keys, 3);
        QVERIFY(valueAt(music, 10.0) < 0.3);
    }
};

QTEST_APPLESS_MAIN(TstAudioDucking)
#include "tst_audioducking.moc"
