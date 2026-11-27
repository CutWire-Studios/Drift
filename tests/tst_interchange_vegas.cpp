#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/interchange/VegasReader.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "core/project/clip/Clip.h"

namespace {

static const uint8_t kW64Riff[16] = {
    0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kW64Wave[16] = {
    0xef, 0x29, 0xc4, 0x46, 0x4a, 0x90, 0xd2, 0x11, 0x87, 0x22, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kW64List[16] = {
    0x6c, 0x69, 0x73, 0x74, 0x2f, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kTypeTimeline[16] = {
    0x47, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeTrack[16] = {
    0x48, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventContainer[16] = {
    0x49, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEvent[16] = {
    0x4a, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTime[16] = {
    0x6a, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkSettings[16] = {
    0xb2, 0x8f, 0x2d, 0x5a, 0x23, 0x0f, 0x11, 0xd2, 0xaf, 0x86, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventGroupList[16] = {
    0x18, 0x3e, 0xe7, 0xb8, 0x4c, 0x7f, 0x6b, 0x45, 0x93, 0xcd, 0x68, 0x66, 0xb9, 0x5e, 0x5f, 0x2b
};
static const uint8_t kChunkEventGroup[16] = {
    0x32, 0xe1, 0x01, 0xe9, 0x77, 0xc6, 0x04, 0x46, 0xac, 0x9d, 0xdb, 0x0b, 0x87, 0xb4, 0xee, 0xbd
};
static const uint8_t kTypeMarkerList[16] = {
    0xb5, 0x00, 0x09, 0x1f, 0x84, 0xdc, 0x43, 0x47, 0x81, 0xcb, 0x08, 0x77, 0x44, 0x40, 0x45, 0x98
};
static const uint8_t kTypeMediaPool[16] = {
    0x5b, 0x2d, 0x8f, 0xb2, 0x0f, 0x23, 0xd2, 0x11, 0x86, 0xaf, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTake[16] = {
    0x6d, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kGuidMarker[16] = {
    0x56, 0x62, 0xf7, 0xab, 0x2d, 0x39, 0xd2, 0x11, 0x86, 0xc7, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};

void writeU32(QByteArray &b, quint32 v)
{
    b.append(reinterpret_cast<const char *>(&v), 4);
}

void writeU64(QByteArray &b, quint64 v)
{
    b.append(reinterpret_cast<const char *>(&v), 8);
}

void writeGuid(QByteArray &b, const uint8_t *g)
{
    b.append(reinterpret_cast<const char *>(g), 16);
}

QByteArray makeChunk(const uint8_t *guid, const QByteArray &payload)
{
    QByteArray chunk;
    writeGuid(chunk, guid);
    writeU64(chunk, 24 + payload.size());
    chunk.append(payload);
    return chunk;
}

QByteArray makeList(const uint8_t *typeGuid, const QByteArray &subChunks)
{
    QByteArray payload;
    writeGuid(payload, typeGuid);
    payload.append(subChunks);
    return makeChunk(kW64List, payload);
}

QByteArray makeEvent(quint64 startTicks, quint64 durTicks, quint32 mediaId = 0)
{
    QByteArray time;
    time.append(16, 0);
    writeU64(time, startTicks);
    writeU64(time, durTicks);
    QByteArray chunks = makeChunk(kChunkEventTime, time);
    if (mediaId != 0) {
        QByteArray take(36, 0);
        writeU32(take, mediaId);
        chunks.append(makeChunk(kChunkEventTake, take));
    }
    return makeList(kTypeEvent, chunks);
}

QByteArray makeMediaEntry(quint32 mediaId, const QString &path)
{
    QByteArray payload(60, 0);
    writeU32(payload, mediaId);
    payload.append(reinterpret_cast<const char *>(path.utf16()), path.size() * 2 + 2);
    payload.append(QByteArray((8 - payload.size() % 8) % 8, 0));
    return makeChunk(kChunkSettings, payload);
}

QByteArray makeProject(const QByteArray &extraChunks, const QByteArray &events)
{
    QByteArray body = makeChunk(kChunkSettings, QByteArray(48, 0));
    body.append(makeList(kTypeTimeline, makeList(kTypeTrack, makeList(kTypeEventContainer, events))));
    body.append(extraChunks);
    QByteArray data;
    writeGuid(data, kW64Riff);
    writeU64(data, 40 + body.size());
    writeGuid(data, kW64Wave);
    data.append(body);
    return data;
}

QByteArray readFixture()
{
    QFile file(QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void patchU64(QByteArray &data, qsizetype offset, quint64 value)
{
    std::memcpy(data.data() + offset, &value, 8);
}

} // namespace

class TestInterchangeVegas : public QObject
{
    Q_OBJECT

private slots:
    void detectsVegasProject();
    void invalidDataFailsGracefully();
    void readsFixtureVegasProject();
    void detectsCrossfadesAndTransitions();
    void readsTimelineMarkers();
    void absurdChunkSizesFailCleanly();
    void absurdEventCountFailsCleanly();
    void truncatedProjectFailsCleanly();
    void absurdTickValuesAreClamped();
    void missingMediaKeepsStoredPath();
    void nestedProjectIsParsedOncePerImport();
    void selfReferencingProjectIsCounted();
};

void TestInterchangeVegas::detectsVegasProject()
{
    QVERIFY(drift::vegas::isVegasProject(QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg")));
    QVERIFY(!drift::vegas::isVegasProject(QStringLiteral("/nonexistent/file.txt")));
    QVERIFY(!drift::vegas::isVegasData(QByteArray("This is not a Wave64 RIFF file at all.")));
}

void TestInterchangeVegas::invalidDataFailsGracefully()
{
    QString err;
    const auto result = drift::vegas::readProjectData(QByteArray("random truncated junk"), QString(), &err);
    QVERIFY(!result.has_value());
    QVERIFY(!err.isEmpty());
}

void TestInterchangeVegas::readsFixtureVegasProject()
{
    QString error;
    const auto optProj = drift::vegas::readProject(QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg"), &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    QCOMPARE(proj.tracks().size(), 5);
    QCOMPARE(proj.assets().size(), 19);
    QCOMPARE(proj.width(), 1920);
    QCOMPARE(proj.height(), 1080);
    QCOMPARE(proj.fps(), 24);

    bool foundVideoTrack = false;
    bool foundAudioTrack = false;
    int totalClips = 0;

    for (const drift::Track &track : proj.tracks()) {
        if (track.type == drift::TrackType::Video)
            foundVideoTrack = true;
        if (track.type == drift::TrackType::Audio)
            foundAudioTrack = true;

        totalClips += track.clips.size();
        for (const drift::Clip &clip : track.clips) {
            QVERIFY(!clip.id.isEmpty());
            QVERIFY(clip.timelineDuration > 0);
            QVERIFY(clip.srcOut >= clip.srcIn);
        }
    }

    QVERIFY(foundVideoTrack);
    QVERIFY(foundAudioTrack);
    QVERIFY(totalClips > 0);

    QCOMPARE(proj.tracks()[0].type, drift::TrackType::Video);
    QCOMPARE(proj.tracks()[0].clips.size(), 52);
    QCOMPARE(proj.tracks()[1].type, drift::TrackType::Audio);
    QCOMPARE(proj.tracks()[1].clips.size(), 20);

    const drift::Clip &first = proj.tracks()[0].clips[0];
    QCOMPARE(first.timelineStart, 0);
    QCOMPARE(first.timelineDuration, 26178095);
    QCOMPARE(first.srcIn, 63022323);
    QCOMPARE(first.srcOut, 89200418);
    QCOMPARE(proj.tracks()[0].clips[1].timelineStart, 26178095);
}

void TestInterchangeVegas::detectsCrossfadesAndTransitions()
{
    const QByteArray data = makeProject({}, makeEvent(0, 30000000) + makeEvent(20000000, 30000000));

    QString err;
    const auto optProj = drift::vegas::readProjectData(data, QString(), &err);
    QVERIFY2(optProj.has_value(), qPrintable(err));
    const drift::Project &proj = *optProj;

    QCOMPARE(proj.tracks().size(), 1);
    QCOMPARE(proj.tracks()[0].clips.size(), 2);
    QCOMPARE(proj.tracks()[0].transitions.size(), 1);

    const auto &tr = proj.tracks()[0].transitions[0];
    QCOMPARE(tr.fromClipId, proj.tracks()[0].clips[0].id);
    QCOMPARE(tr.toClipId, proj.tracks()[0].clips[1].id);
    QCOMPARE(tr.kindId, QStringLiteral("crossfade"));
    QCOMPARE(tr.durationUs, 1000000);

    drift::TimeUs startUs = 0, endUs = 0;
    QVERIFY(drift::transitionWindow(proj.tracks()[0], tr, startUs, endUs));
    QCOMPARE(startUs, 2000000);
    QCOMPARE(endUs, 3000000);
}

void TestInterchangeVegas::readsTimelineMarkers()
{
    QByteArray marker;
    writeGuid(marker, kGuidMarker);
    writeU64(marker, 0);
    writeU32(marker, 28);
    writeU32(marker, 7);
    writeU64(marker, 50000000);
    writeU64(marker, 0);
    const QString name = QStringLiteral("Intro");
    writeU32(marker, quint32(name.size() * 2 + 2));
    marker.append(reinterpret_cast<const char *>(name.utf16()), name.size() * 2 + 2);
    marker.append(QByteArray(8 - marker.size() % 8, 0));

    const QByteArray data = makeProject(makeList(kTypeMarkerList, marker), makeEvent(0, 30000000));
    QString err;
    const auto optProj = drift::vegas::readProjectData(data, QString(), &err);
    QVERIFY2(optProj.has_value(), qPrintable(err));
    QCOMPARE(optProj->bookmarks().size(), 1);
    QCOMPARE(optProj->bookmarks()[0].timeUs, 5000000);
    QCOMPARE(optProj->bookmarks()[0].label, name);
}

void TestInterchangeVegas::absurdChunkSizesFailCleanly()
{
    const quint64 absurd = 0xFFFFFFFFFFFFFFF8ULL;

    QByteArray header = makeProject({}, makeEvent(0, 10000000));
    patchU64(header, 56, absurd);
    QVERIFY(!drift::vegas::readProjectData(header).has_value());

    // Every chunk header in the fixture, one at a time, claims to be larger than the address space.
    const QByteArray fixture = readFixture();
    QVERIFY(!fixture.isEmpty());
    qsizetype pos = 40;
    int patched = 0;
    while (pos + 24 <= fixture.size()) {
        quint64 size = 0;
        std::memcpy(&size, fixture.constData() + pos + 16, 8);
        QByteArray copy = fixture;
        patchU64(copy, pos + 16, absurd);
        drift::vegas::readProjectData(copy);
        ++patched;
        if (size < 24 || size > quint64(fixture.size() - pos))
            break;
        pos += (size + 7) & ~quint64(7);
    }
    QVERIFY(patched > 3);

    // Nested chunk headers too: each 8-byte aligned slot that looks like a size field.
    for (qsizetype off = 56; off + 8 <= fixture.size(); off += off < 16384 ? 8 : 4096) {
        QByteArray copy = fixture;
        patchU64(copy, off, absurd);
        drift::vegas::readProjectData(copy);
    }
}

void TestInterchangeVegas::absurdEventCountFailsCleanly()
{
    QByteArray group;
    group.append(8, 0);
    writeU32(group, 0xFFFFFFFFu);
    writeU32(group, 0);
    const QByteArray groups = makeList(kTypeEventGroupList, makeChunk(kChunkEventGroup, group));

    const QByteArray data = makeProject(groups, makeEvent(0, 10000000));
    QString err;
    const auto optProj = drift::vegas::readProjectData(data, QString(), &err);
    QVERIFY2(optProj.has_value(), qPrintable(err));
    QCOMPARE(optProj->tracks()[0].clips.size(), 1);
}

void TestInterchangeVegas::truncatedProjectFailsCleanly()
{
    const QByteArray fixture = readFixture();
    QVERIFY(!fixture.isEmpty());
    for (qsizetype len = 0; len < fixture.size(); len += 4099)
        drift::vegas::readProjectData(fixture.left(len));
    for (qsizetype len = fixture.size() - 40; len < fixture.size(); ++len)
        drift::vegas::readProjectData(fixture.left(len));
}

void TestInterchangeVegas::absurdTickValuesAreClamped()
{
    const QByteArray data = makeProject({}, makeEvent(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFF0ULL));
    QString err;
    const auto optProj = drift::vegas::readProjectData(data, QString(), &err);
    QVERIFY2(optProj.has_value(), qPrintable(err));
    QCOMPARE(optProj->tracks()[0].clips.size(), 1);
    const drift::Clip &clip = optProj->tracks()[0].clips[0];
    QVERIFY(clip.timelineStart >= 0 && clip.timelineStart < Q_INT64_C(1) << 50);
    QVERIFY(clip.timelineDuration > 0 && clip.timelineDuration < Q_INT64_C(1) << 50);
    QVERIFY(clip.timelineEnd() > clip.timelineStart);
}

void TestInterchangeVegas::missingMediaKeepsStoredPath()
{
    QString error;
    const auto optProj = drift::vegas::readProject(QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg"), &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));
    for (const drift::MediaAsset &asset : optProj->assets()) {
        if (asset.kind == drift::MediaKind::Composite)
            continue;
        QVERIFY2(asset.path.contains(QLatin1Char('/')), qPrintable(asset.path));
        QVERIFY2(!asset.path.contains(QLatin1Char('\\')), qPrintable(asset.path));
    }
}

void TestInterchangeVegas::nestedProjectIsParsedOncePerImport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto write = [&](const QString &name, const QByteArray &data) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
    };

    write(QStringLiteral("inner.veg"), makeProject({}, makeEvent(0, 20000000)));

    const QByteArray pool = makeList(kTypeMediaPool, makeMediaEntry(1, QStringLiteral("C:\\Footage\\inner.veg"))
                                                         + makeMediaEntry(2, QStringLiteral("D:\\Other\\inner.veg")));
    write(QStringLiteral("outer.veg"), makeProject(pool, makeEvent(0, 10000000, 1) + makeEvent(10000000, 10000000, 2)));

    QString err;
    drift::interchange::InterchangeReport report;
    const auto optProj = drift::vegas::readProject(dir.filePath(QStringLiteral("outer.veg")), &err, &report);
    QVERIFY2(optProj.has_value(), qPrintable(err));
    QCOMPARE(report.droppedCount(), 0);
    QCOMPARE(optProj->sequenceIds().size(), 1);
    const auto &clips = optProj->tracks()[0].clips;
    QCOMPARE(clips.size(), 2);
    QVERIFY(!clips[0].sequenceId.isEmpty());
    QCOMPARE(clips[0].sequenceId, clips[1].sequenceId);
    QCOMPARE(clips[0].sequenceId, optProj->sequenceIds().first());
}

void TestInterchangeVegas::selfReferencingProjectIsCounted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray pool = makeList(kTypeMediaPool, makeMediaEntry(1, QStringLiteral("C:\\Footage\\self.veg")));
    QFile file(dir.filePath(QStringLiteral("self.veg")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(makeProject(pool, makeEvent(0, 10000000, 1)));
    file.close();

    QString err;
    drift::interchange::InterchangeReport report;
    const auto optProj = drift::vegas::readProject(file.fileName(), &err, &report);
    QVERIFY2(optProj.has_value(), qPrintable(err));
    QCOMPARE(report.droppedCount(), 1);
    QVERIFY(optProj->sequenceIds().isEmpty());
}

QTEST_MAIN(TestInterchangeVegas)
#include "tst_interchange_vegas.moc"
