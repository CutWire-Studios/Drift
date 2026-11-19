#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "core/interchange/VegasReader.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "core/project/clip/Clip.h"

class TestInterchangeVegas : public QObject
{
    Q_OBJECT

private slots:
    void detectsVegasProject();
    void invalidDataFailsGracefully();
    void readsFixtureVegasProject();
    void readsRealVegasProject();
    void readsScene2VegasProjectWithMts();
    void readsEpisode4VegasProjectWithOffset96AndSubdirectories();
    void readsEpisode4WorkprintWithNestedTimelines();
    void readsTrackMetadataStateAndProperties();
    void readsTimelineMarkersAndRegions();
    void detectsCrossfadesAndTransitions();
    void readsTitlesAndTextGenerators();
    void readsVideoEffectsLevelsAndColor();
    void readsVignetteAndSoftlightEffects();
};

void TestInterchangeVegas::detectsVegasProject()
{
    const QString fixtureVeg = QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg");
    if (QFile::exists(fixtureVeg)) {
        QVERIFY(drift::vegas::isVegasProject(fixtureVeg));
    }

    QString realVeg = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Scene 1/e3s1.veg");
    if (!QFile::exists(realVeg)) {
        realVeg = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Scene 1/e3s1.veg");
    }
    if (QFile::exists(realVeg)) {
        QVERIFY(drift::vegas::isVegasProject(realVeg));
    }

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
    const QString fixtureVeg = QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg");
    if (!QFile::exists(fixtureVeg)) {
        QSKIP("Fixture sample.veg not present");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(fixtureVeg, &error);
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
}

void TestInterchangeVegas::readsRealVegasProject()
{
    QString realVeg = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Scene 1/e3s1.veg");
    if (!QFile::exists(realVeg)) {
        realVeg = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Scene 1/e3s1.veg");
    }
    if (!QFile::exists(realVeg)) {
        QSKIP("Sample .veg file not present on Jellyfish or HAL");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(realVeg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Loaded VEGAS project:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size()
             << "Resolution:" << proj.width() << "x" << proj.height()
             << "FPS:" << proj.fps();

    QVERIFY(proj.tracks().size() >= 1);
    QVERIFY(proj.assets().size() >= 1);

    int totalClips = 0;
    bool foundVideoTrack = false;
    bool foundAudioTrack = false;

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

            if (proj.assets().contains(clip.assetId)) {
                const auto &asset = proj.assets().value(clip.assetId);
                // If it was relinked to ProRes, verify editFriendly is true
                if (asset.path.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive)) {
                    QVERIFY(asset.editFriendly);
                }
            }
        }
    }

    QVERIFY(foundVideoTrack);
    QVERIFY(foundAudioTrack);
    QVERIFY(totalClips > 0);

    int videoMovCount = 0;
    int videoMtsCount = 0;
    for (const auto &asset : proj.assets()) {
        if (asset.kind == drift::MediaKind::Video) {
            if (asset.path.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive))
                ++videoMovCount;
            else if (asset.path.endsWith(QLatin1String(".mts"), Qt::CaseInsensitive))
                ++videoMtsCount;
        }
    }
    qDebug() << "e3s1 Video assets .mov:" << videoMovCount << ".mts:" << videoMtsCount;
    QCOMPARE(videoMovCount, 9);
    QCOMPARE(videoMtsCount, 0);
}

void TestInterchangeVegas::readsScene2VegasProjectWithMts()
{
    QString scene2Veg = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Scene 2/E3S2.veg");
    if (!QFile::exists(scene2Veg)) {
        scene2Veg = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Scene 2/E3S2.veg");
    }
    if (!QFile::exists(scene2Veg)) {
        QSKIP("Scene 2 .veg file not present");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(scene2Veg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Loaded Scene 2 VEGAS project:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size();

    for (const auto &asset : proj.assets()) {
        qDebug() << "Asset:" << asset.id << "path:" << asset.path << "exists:" << QFile::exists(asset.path)
                 << "kind:" << (asset.kind == drift::MediaKind::Video ? "video" : "audio")
                 << "editFriendly:" << asset.editFriendly;
    }

    // Verify native VEGAS event grouping (P6-04)
    int linkedCount = 0;
    QHash<QString, int> linkIdCounts;
    for (const drift::Track &track : proj.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (!clip.linkId.isEmpty()) {
                ++linkedCount;
                ++linkIdCounts[clip.linkId];
            }
        }
    }
    qDebug() << "E3S2 linked clips:" << linkedCount << "unique link groups:" << linkIdCounts.size();
    QVERIFY(linkedCount >= 80);
    QVERIFY(linkIdCounts.size() >= 35);
    for (auto it = linkIdCounts.constBegin(); it != linkIdCounts.constEnd(); ++it) {
        QVERIFY2(it.value() >= 2, qPrintable(QStringLiteral("Link group %1 has fewer than 2 clips: %2")
                                                .arg(it.key()).arg(it.value())));
    }
}

void TestInterchangeVegas::readsEpisode4VegasProjectWithOffset96AndSubdirectories()
{
    QString ep4Veg = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/1.1.veg");
    if (!QFile::exists(ep4Veg)) {
        ep4Veg = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/1.1.veg");
    }
    if (!QFile::exists(ep4Veg)) {
        QSKIP("Episode 4 1.1.veg not present");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(ep4Veg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Loaded Episode 4 VEGAS project:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size();

    QVERIFY(proj.assets().size() >= 20);
    int unconvertedCount = 0;
    int resolvedCount = 0;

    int videoCount = 0;
    for (const auto &asset : proj.assets()) {
        qDebug() << "Ep4 Asset:" << asset.id << "path:" << asset.path << "exists:" << QFile::exists(asset.path)
                 << "kind:" << (asset.kind == drift::MediaKind::Video ? "video" : "audio")
                 << "editFriendly:" << asset.editFriendly;
        if (QFile::exists(asset.path)) {
            ++resolvedCount;
        }
        if (asset.kind == drift::MediaKind::Video) {
            ++videoCount;
            if (!asset.editFriendly) {
                ++unconvertedCount;
                QVERIFY(asset.path.endsWith(QLatin1String(".MTS"), Qt::CaseInsensitive));
            } else {
                QVERIFY(asset.path.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive));
            }
        }
    }

    // All assets should be resolved on disk thanks to universal offset scan and subdirectory indexing
    QCOMPARE(resolvedCount, proj.assets().size());
    // Seven video clips total in Ep 4 (six camera clips + one plate effect)
    QCOMPARE(videoCount, 7);

    // Verify track metadata & naming (P6-05)
    QCOMPARE(proj.tracks().size(), 4);
    QCOMPARE(proj.tracks()[0].name, QStringLiteral("Track 1 (video)"));
    QCOMPARE(proj.tracks()[0].type, drift::TrackType::Video);
    QCOMPARE(proj.tracks()[0].muted, false);
    QCOMPARE(proj.tracks()[0].solo, false);

    QCOMPARE(proj.tracks()[1].name, QStringLiteral("Sin"));
    QCOMPARE(proj.tracks()[1].type, drift::TrackType::Audio);
    QCOMPARE(proj.tracks()[1].muted, false);
    QCOMPARE(proj.tracks()[1].solo, false);
    QCOMPARE(proj.tracks()[1].volume, 1.0);
    QCOMPARE(proj.tracks()[1].pan, 0.0);

    QCOMPARE(proj.tracks()[2].name, QStringLiteral("Andrew"));
    QCOMPARE(proj.tracks()[2].type, drift::TrackType::Audio);

    QCOMPARE(proj.tracks()[3].name, QStringLiteral("Boom"));
    QCOMPARE(proj.tracks()[3].type, drift::TrackType::Audio);
}

void TestInterchangeVegas::readsEpisode4WorkprintWithNestedTimelines()
{
    QString sceneVeg = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/10.1b.veg");
    if (QFile::exists(sceneVeg)) {
        QString err;
        auto optScene = drift::vegas::readProject(sceneVeg, &err);
        QVERIFY2(optScene.has_value(), qPrintable(err));
        qDebug() << "10.1b.veg loaded! Tracks:" << optScene->tracks().size()
                 << "Assets:" << optScene->assets().size();
    }

    QString workprintVeg = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/Workprint.veg");
    if (!QFile::exists(workprintVeg)) {
        workprintVeg = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/Workprint.veg");
    }
    if (!QFile::exists(workprintVeg)) {
        QSKIP("Episode 4 Workprint.veg not present");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(workprintVeg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Workprint loaded:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size()
             << "Sequences:" << proj.sequenceIds();

    for (const auto &asset : proj.assets()) {
        qDebug() << "Workprint Asset:" << asset.id << "path:" << asset.path
                 << "kind:" << drift::mediaKindToString(asset.kind)
                 << "exists:" << QFile::exists(asset.path);
    }

    for (int t = 0; t < proj.tracks().size(); ++t) {
        const auto &track = proj.tracks()[t];
        qDebug() << "Track" << t << "type:" << drift::trackTypeToString(track.type)
                 << "clips:" << track.clips.size();
        for (const auto &clip : track.clips) {
            qDebug() << "  Clip:" << clip.id << "type:" << drift::clipTypeToString(clip.type)
                     << "assetId:" << clip.assetId << "sequenceId:" << clip.sequenceId
                     << "timelineStart:" << clip.timelineStart << "dur:" << clip.timelineDuration;
        }
    }

    // Verify native event grouping on master assembly
    int wpLinkedCount = 0;
    QHash<QString, int> wpLinkCounts;
    for (const drift::Track &track : proj.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (!clip.linkId.isEmpty()) {
                ++wpLinkedCount;
                ++wpLinkCounts[clip.linkId];
            }
        }
    }
    qDebug() << "Workprint linked clips:" << wpLinkedCount << "link groups:" << wpLinkCounts.size();
    QVERIFY(wpLinkedCount >= 20);
    QVERIFY(wpLinkCounts.size() >= 10);
    for (auto it = wpLinkCounts.constBegin(); it != wpLinkCounts.constEnd(); ++it) {
        QVERIFY(it.value() >= 2);
    }
}

void TestInterchangeVegas::readsTrackMetadataStateAndProperties()
{
    // 1. Test mute states, volume levels, and names on 6.1a.veg
    QString veg61a = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/OLD/6.1a.veg");
    if (!QFile::exists(veg61a)) {
        veg61a = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/OLD/6.1a.veg");
    }
    if (QFile::exists(veg61a)) {
        QString err;
        const auto optProj = drift::vegas::readProject(veg61a, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;
        QCOMPARE(proj.tracks().size(), 6);

        // Track 0 & 1: muted camera audio / reference tracks
        QVERIFY(proj.tracks()[0].muted);
        QVERIFY(!proj.tracks()[0].solo);
        QVERIFY(proj.tracks()[1].muted);
        QVERIFY(!proj.tracks()[1].solo);
        QVERIFY(!proj.tracks()[2].muted);

        // Track 3: Sin (unmuted, boosted volume ~1.603)
        QCOMPARE(proj.tracks()[3].name, QStringLiteral("Sin"));
        QVERIFY(!proj.tracks()[3].muted);
        QVERIFY(std::abs(proj.tracks()[3].volume - 1.6032) < 0.01);

        // Track 4: Diana (unmuted, boosted volume ~1.758)
        QCOMPARE(proj.tracks()[4].name, QStringLiteral("Diana"));
        QVERIFY(!proj.tracks()[4].muted);
        QVERIFY(std::abs(proj.tracks()[4].volume - 1.7579) < 0.01);

        // Track 5: Room Tone (muted, volume ~0.822)
        QCOMPARE(proj.tracks()[5].name, QStringLiteral("Room Tone"));
        QVERIFY(proj.tracks()[5].muted);
        QVERIFY(std::abs(proj.tracks()[5].volume - 0.8222) < 0.01);
    }

    // 2. Test solo states on BEACH PARTY B.veg
    QString vegBeach = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/BEACH PARTY B.veg");
    if (!QFile::exists(vegBeach)) {
        vegBeach = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/BEACH PARTY B.veg");
    }
    if (QFile::exists(vegBeach)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegBeach, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;
        QVERIFY(proj.tracks().size() >= 9);

        QVERIFY(!proj.tracks()[0].solo);
        QVERIFY(proj.tracks()[1].solo);
        QVERIFY(!proj.tracks()[2].solo);
        QVERIFY(proj.tracks()[6].solo);
        QVERIFY(proj.tracks()[7].solo);
        QVERIFY(proj.tracks()[8].solo);
    }

    // 3. Test stereo pan on 9.4c.veg
    QString vegPan = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/Intermediates/9.4c.veg");
    if (!QFile::exists(vegPan)) {
        vegPan = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/Intermediates/9.4c.veg");
    }
    if (QFile::exists(vegPan)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegPan, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;
        QCOMPARE(proj.tracks().size(), 3);

        // Track 1: panned full Left (-1.0)
        QVERIFY(std::abs(proj.tracks()[1].pan - (-1.0)) < 0.01);
        // Track 2: panned full Right (+1.0)
        QVERIFY(std::abs(proj.tracks()[2].pan - 1.0) < 0.01);
    }
}

void TestInterchangeVegas::readsTimelineMarkersAndRegions()
{
    // 1. Test Previously On.veg for take region (Record Take 1)
    QString vegPrev = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/Previously On.veg");
    if (!QFile::exists(vegPrev)) {
        vegPrev = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/Previously On.veg");
    }
    if (QFile::exists(vegPrev)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegPrev, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundIn = false;
        bool foundOut = false;
        for (const auto &bm : proj.bookmarks()) {
            if (bm.label == QStringLiteral("Record Take 1") && bm.timeUs == 0)
                foundIn = true;
            if (bm.label.contains(QStringLiteral("Record Take 1")) && bm.timeUs == 38671)
                foundOut = true;
        }
        QVERIFY(foundIn);
        QVERIFY(foundOut);
    }

    // 2. Test Acid's Story B.veg for marker (Marker 5) and source marker
    QString vegAcid = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 5 & 6/Episode 5/Acid's Story B.veg");
    if (!QFile::exists(vegAcid)) {
        vegAcid = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 5 & 6/Episode 5/Acid's Story B.veg");
    }
    if (QFile::exists(vegAcid)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegAcid, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundMarker5 = false;
        for (const auto &bm : proj.bookmarks()) {
            if (bm.label == QStringLiteral("Marker 5"))
                foundMarker5 = true;
        }
        QVERIFY(foundMarker5);

        // Verify SourceMarker was extracted on asset
        bool foundSourceMarker = false;
        for (const auto &asset : proj.assets()) {
            for (const auto &sm : asset.markers) {
                if (sm.name == QStringLiteral("Marker 5"))
                    foundSourceMarker = true;
            }
        }
        QVERIFY(foundSourceMarker);
    }

    // 3. Test synthetic Wave64 project with kTypeMarkerList (point markers and regions)
    {
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
        static const uint8_t kTypeMarkerList[16] = {
            0xb5, 0x00, 0x09, 0x1f, 0x84, 0xdc, 0x43, 0x47, 0x81, 0xcb, 0x08, 0x77, 0x44, 0x40, 0x45, 0x98
        };
        static const uint8_t kGuidMarker[16] = {
            0x56, 0x62, 0xf7, 0xab, 0x2d, 0x39, 0xd2, 0x11, 0x86, 0xc7, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };

        auto writeU32 = [](QByteArray &b, quint32 v) {
            b.append(reinterpret_cast<const char *>(&v), 4);
        };
        auto writeU64 = [](QByteArray &b, quint64 v) {
            b.append(reinterpret_cast<const char *>(&v), 8);
        };
        auto writeGuid = [](QByteArray &b, const uint8_t *g) {
            b.append(reinterpret_cast<const char *>(g), 16);
        };
        auto makeChunk = [&](const uint8_t *guid, const QByteArray &payload) {
            QByteArray chunk;
            writeGuid(chunk, guid);
            writeU64(chunk, 24 + payload.size());
            chunk.append(payload);
            return chunk;
        };
        auto makeList = [&](const uint8_t *typeGuid, const QByteArray &subChunks) {
            QByteArray payload;
            writeGuid(payload, typeGuid);
            payload.append(subChunks);
            return makeChunk(kW64List, payload);
        };

        // Project settings chunk
        QByteArray c0Payload(48, 0);
        static const uint8_t kGuidSettings[16] = {
            0xb2, 0x8f, 0x2d, 0x5a, 0x23, 0x0f, 0x11, 0xd2, 0xaf, 0x86, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        QByteArray c0 = makeChunk(kGuidSettings, c0Payload);

        // Track with 1 event
        QByteArray timePayload;
        timePayload.append(16, 0);
        writeU64(timePayload, 0);        // start 0
        writeU64(timePayload, 10000000); // dur 1 sec
        QByteArray timeChunk = makeChunk(kChunkEventTime, timePayload);
        QByteArray eventList = makeList(kTypeEvent, timeChunk);
        QByteArray eventContainer = makeList(kTypeEventContainer, eventList);
        QByteArray trackList = makeList(kTypeTrack, eventContainer);
        QByteArray timelineList = makeList(kTypeTimeline, trackList);

        // Marker chunk helper
        auto makeMarker = [&](quint32 id, qint64 posTicks, qint64 lenTicks, const QString &name) {
            QByteArray chunk;
            writeGuid(chunk, kGuidMarker);
            QByteArray nameBytes;
            if (!name.isEmpty()) {
                const auto *u16 = name.utf16();
                nameBytes.append(reinterpret_cast<const char *>(u16), (name.size() + 1) * sizeof(char16_t));
            }
            const quint32 hdrSize = 28;
            const quint32 dataSize = hdrSize + nameBytes.size();
            writeU32(chunk, dataSize);
            writeU32(chunk, hdrSize);
            writeU32(chunk, id);
            writeU64(chunk, posTicks);
            writeU64(chunk, lenTicks);
            writeU32(chunk, nameBytes.size());
            chunk.append(nameBytes);
            return chunk;
        };

        QByteArray markerData;
        markerData.append(makeMarker(1, 10000000, -1, QStringLiteral("Intro")));        // Point marker at 1.0s
        markerData.append(makeMarker(2, 20000000, -1, QString()));                      // Unnamed marker at 2.0s -> "Marker 2"
        markerData.append(makeMarker(3, 30000000, 15000000, QStringLiteral("Chorus"))); // Region at 3.0s, len 1.5s -> "Chorus" and "Chorus (Out)"

        QByteArray markerList = makeList(kTypeMarkerList, markerData);

        QByteArray body = c0 + timelineList + markerList;
        QByteArray synthData;
        writeGuid(synthData, kW64Riff);
        writeU64(synthData, 40 + body.size());
        writeGuid(synthData, kW64Wave);
        synthData.append(body);

        QString err;
        const auto optProj = drift::vegas::readProjectData(synthData, QString(), &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        QCOMPARE(proj.bookmarks().size(), 4);
        // 1. timeUs = 1,000,000 -> "Intro"
        QCOMPARE(proj.bookmarks()[0].timeUs, 1000000);
        QCOMPARE(proj.bookmarks()[0].label, QStringLiteral("Intro"));

        // 2. timeUs = 2,000,000 -> "Marker 2"
        QCOMPARE(proj.bookmarks()[1].timeUs, 2000000);
        QCOMPARE(proj.bookmarks()[1].label, QStringLiteral("Marker 2"));

        // 3. timeUs = 3,000,000 -> "Chorus"
        QCOMPARE(proj.bookmarks()[2].timeUs, 3000000);
        QCOMPARE(proj.bookmarks()[2].label, QStringLiteral("Chorus"));

        // 4. timeUs = 4,500,000 -> "Chorus (Out)"
        QCOMPARE(proj.bookmarks()[3].timeUs, 4500000);
        QCOMPARE(proj.bookmarks()[3].label, QStringLiteral("Chorus (Out)"));
    }
}

void TestInterchangeVegas::detectsCrossfadesAndTransitions()
{
    // 1. Test e3s5.veg for track transitions
    QString vegE3S5 = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Scene 5/e3s5.veg");
    if (!QFile::exists(vegE3S5)) {
        vegE3S5 = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Scene 5/e3s5.veg");
    }
    if (QFile::exists(vegE3S5)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegE3S5, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        int totalTransitions = 0;
        for (const auto &track : proj.tracks()) {
            totalTransitions += track.transitions.size();
        }
        QCOMPARE(totalTransitions, 13);
        QCOMPARE(proj.tracks()[1].transitions.size(), 7);
        QCOMPARE(proj.tracks()[2].transitions.size(), 6);

        // Verify the 1.810s crossfade between clip 0 and clip 1 on track 1 (video)
        const auto &tr0 = proj.tracks()[1].transitions[0];
        QCOMPARE(tr0.kindId, QStringLiteral("crossfade"));
        QCOMPARE(tr0.durationUs, 1809956);
        QCOMPARE(tr0.fromClipId, proj.tracks()[1].clips[0].id);
        QCOMPARE(tr0.toClipId, proj.tracks()[1].clips[1].id);

        drift::TimeUs winStart = 0, winEnd = 0;
        QVERIFY(drift::transitionWindow(proj.tracks()[1], tr0, winStart, winEnd));
        QCOMPARE(winStart, 20212044);
        QCOMPARE(winEnd, 22022000);
        QVERIFY(drift::clipsPhysicallyOverlap(proj.tracks()[1].clips[0], proj.tracks()[1].clips[1]));
    }

    // 2. Test e3s8-9-10.veg for crossfades
    QString vegE3S8 = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Scene 8,9,10/e3s8-9-10.veg");
    if (!QFile::exists(vegE3S8)) {
        vegE3S8 = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Scene 8,9,10/e3s8-9-10.veg");
    }
    if (QFile::exists(vegE3S8)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegE3S8, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        int totalTransitions = 0;
        for (const auto &track : proj.tracks()) {
            totalTransitions += track.transitions.size();
        }
        QCOMPARE(totalTransitions, 13);
        QCOMPARE(proj.tracks()[0].transitions.size(), 5);

        // Check first transition duration on Track 0
        const auto &tr0 = proj.tracks()[0].transitions[0];
        QCOMPARE(tr0.kindId, QStringLiteral("crossfade"));
        QCOMPARE(tr0.durationUs, 667332);
    }

    // 3. Test synthetic Wave64 project with overlapping clips
    {
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

        auto writeU64 = [](QByteArray &b, quint64 v) {
            b.append(reinterpret_cast<const char *>(&v), 8);
        };
        auto writeGuid = [](QByteArray &b, const uint8_t *g) {
            b.append(reinterpret_cast<const char *>(g), 16);
        };
        auto makeChunk = [&](const uint8_t *guid, const QByteArray &payload) {
            QByteArray chunk;
            writeGuid(chunk, guid);
            writeU64(chunk, 24 + payload.size());
            chunk.append(payload);
            return chunk;
        };
        auto makeList = [&](const uint8_t *typeGuid, const QByteArray &subChunks) {
            QByteArray payload;
            writeGuid(payload, typeGuid);
            payload.append(subChunks);
            return makeChunk(kW64List, payload);
        };

        QByteArray c0Payload(48, 0);
        static const uint8_t kGuidSettings[16] = {
            0xb2, 0x8f, 0x2d, 0x5a, 0x23, 0x0f, 0x11, 0xd2, 0xaf, 0x86, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        QByteArray c0 = makeChunk(kGuidSettings, c0Payload);

        // Make event 1: start 0, dur 3s (30,000,000 ticks)
        QByteArray timePayload1;
        timePayload1.append(16, 0);
        writeU64(timePayload1, 0);
        writeU64(timePayload1, 30000000);
        QByteArray ev1 = makeList(kTypeEvent, makeChunk(kChunkEventTime, timePayload1));

        // Make event 2: start 2s (20,000,000 ticks), dur 3s (30,000,000 ticks) -> 1.0s overlap
        QByteArray timePayload2;
        timePayload2.append(16, 0);
        writeU64(timePayload2, 20000000);
        writeU64(timePayload2, 30000000);
        QByteArray ev2 = makeList(kTypeEvent, makeChunk(kChunkEventTime, timePayload2));

        QByteArray eventContainer = makeList(kTypeEventContainer, ev1 + ev2);
        QByteArray trackList = makeList(kTypeTrack, eventContainer);
        QByteArray timelineList = makeList(kTypeTimeline, trackList);

        QByteArray body = c0 + timelineList;
        QByteArray synthData;
        writeGuid(synthData, kW64Riff);
        writeU64(synthData, 40 + body.size());
        writeGuid(synthData, kW64Wave);
        synthData.append(body);

        QString err;
        const auto optProj = drift::vegas::readProjectData(synthData, QString(), &err);
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
}

void TestInterchangeVegas::readsTitlesAndTextGenerators()
{
    // 1. Test Workprint.veg for Titles & Text generator clips
    QString vegWorkprint = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/Workprint.veg");
    if (!QFile::exists(vegWorkprint)) {
        vegWorkprint = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/Workprint.veg");
    }
    if (QFile::exists(vegWorkprint)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegWorkprint, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        QList<drift::Clip> textClips;
        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (clip.type == drift::ClipType::Text)
                    textClips.append(clip);
            }
        }

        qDebug() << "Workprint Text Clips found:" << textClips.size();
        QVERIFY(textClips.size() >= 2);

        bool foundThreeMonths = false;
        bool foundWardEight = false;
        for (const auto &clip : textClips) {
            qDebug() << "  Text clip text:" << clip.textContent
                     << "font:" << clip.textStyle.fontFamily
                     << "pixelSize:" << clip.textStyle.pixelSize
                     << "align:" << drift::textAlignToString(clip.textStyle.align);

            QCOMPARE(clip.textStyle.fontFamily, QStringLiteral("Courier New"));
            QCOMPARE(clip.textStyle.align, drift::TextAlign::Center);
            QVERIFY(clip.textStyle.pixelSize > 0);

            if (clip.textContent.contains(QStringLiteral("THREE"))
                && clip.textContent.contains(QStringLiteral("MONTHS"))) {
                foundThreeMonths = true;
            }
            if (clip.textContent.contains(QStringLiteral("WARD"))
                && clip.textContent.contains(QStringLiteral("EIGHT"))) {
                foundWardEight = true;
            }
        }
        QVERIFY(foundThreeMonths);
        QVERIFY(foundWardEight);
    }

    // 2. Test Previously On.veg for "PREVIOUSLY ON" title card and solid color
    QString vegPrev = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/Previously On.veg");
    if (!QFile::exists(vegPrev)) {
        vegPrev = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/Previously On.veg");
    }
    if (QFile::exists(vegPrev)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegPrev, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundPreviouslyOn = false;
        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (clip.type == drift::ClipType::Text && clip.textContent.contains(QStringLiteral("PREVIOUSLY ON"))) {
                    foundPreviouslyOn = true;
                    QCOMPARE(clip.textStyle.fontFamily, QStringLiteral("Franklin Gothic Medium"));
                    QVERIFY(clip.textStyle.pixelSize > 0);
                }
            }
        }
        QVERIFY(foundPreviouslyOn);
    }
}

void TestInterchangeVegas::readsVideoEffectsLevelsAndColor()
{
    // Test e3s3.veg for media-level Video FX (Levels, Color Corrector, Sharpen)
    QString vegScene3 = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Scene 3/e3s3.veg");
    if (!QFile::exists(vegScene3)) {
        vegScene3 = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Scene 3/e3s3.veg");
    }
    if (QFile::exists(vegScene3)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegScene3, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        int totalClipsWithEffects = 0;
        bool foundLevels = false;
        bool foundColorGrade = false;
        bool foundSharpen = false;

        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (!clip.effects.isEmpty()) {
                    ++totalClipsWithEffects;
                    for (const auto &fx : clip.effects) {
                        if (fx.catalogId == QStringLiteral("adjust.levels")) {
                            foundLevels = true;
                            QVERIFY(fx.parameters.contains(QStringLiteral("gamma")));
                            QVERIFY(fx.parameters.value(QStringLiteral("gamma")).toDouble() > 1.0);
                        } else if (fx.catalogId == QStringLiteral("color.grade")) {
                            foundColorGrade = true;
                            QVERIFY(fx.parameters.contains(QStringLiteral("saturation")));
                            QVERIFY(fx.parameters.value(QStringLiteral("saturation")).toDouble() > 1.0);
                        } else if (fx.catalogId == QStringLiteral("stylize.sharpen")) {
                            foundSharpen = true;
                            QVERIFY(fx.parameters.contains(QStringLiteral("luma_amount")));
                            QVERIFY(fx.parameters.value(QStringLiteral("luma_amount")).toDouble() > 0.0);
                        }
                    }
                }
            }
        }

        qDebug() << "e3s3.veg clips with effects:" << totalClipsWithEffects
                 << "foundLevels:" << foundLevels
                 << "foundColorGrade:" << foundColorGrade
                 << "foundSharpen:" << foundSharpen;

        QVERIFY(totalClipsWithEffects >= 5);
        QVERIFY(foundLevels);
        QVERIFY(foundColorGrade);
        QVERIFY(foundSharpen);
    }
}

void TestInterchangeVegas::readsVignetteAndSoftlightEffects()
{
    // 1. Test BEACH PARTY B.veg for Media Pool Sony Vignette
    QString vegBeach = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 4/BEACH PARTY B.veg");
    if (!QFile::exists(vegBeach)) {
        vegBeach = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 4/BEACH PARTY B.veg");
    }
    if (QFile::exists(vegBeach)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegBeach, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundVignette = false;
        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                for (const auto &fx : clip.effects) {
                    if (fx.catalogId == QStringLiteral("stylize.vignette")) {
                        foundVignette = true;
                        QVERIFY(fx.parameters.contains(QStringLiteral("amount")));
                        QVERIFY(fx.parameters.value(QStringLiteral("amount")).toDouble() > 0.0);
                        QVERIFY(fx.parameters.contains(QStringLiteral("feather")));
                        QVERIFY(fx.parameters.value(QStringLiteral("feather")).toDouble() > 0.0);
                        QVERIFY(fx.parameters.contains(QStringLiteral("center.x")));
                        QCOMPARE(fx.parameters.value(QStringLiteral("center.x")).toDouble(), 0.5);
                        QCOMPARE(fx.parameters.value(QStringLiteral("center.y")).toDouble(), 0.5);
                    }
                }
            }
        }
        qDebug() << "BEACH PARTY B.veg foundVignette:" << foundVignette;
        QVERIFY(foundVignette);
    }

    // 2. Test Credits Episode 3.veg for Event-level Softlight XML and Levels
    QString vegCredits = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Credits Episode 3.veg");
    if (!QFile::exists(vegCredits)) {
        vegCredits = QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Credits Episode 3.veg");
    }
    if (QFile::exists(vegCredits)) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegCredits, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundSoftlightGlow = false;
        bool foundSoftlightVignette = false;
        bool foundTakeLevels = false;

        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                for (const auto &fx : clip.effects) {
                    if (fx.catalogId == QStringLiteral("bloom_glow")) {
                        foundSoftlightGlow = true;
                        QVERIFY(fx.parameters.contains(QStringLiteral("intensity")));
                        QVERIFY(fx.parameters.value(QStringLiteral("intensity")).toDouble() > 0.0);
                        QVERIFY(fx.parameters.contains(QStringLiteral("blurRadius")));
                    } else if (fx.catalogId == QStringLiteral("stylize.vignette")) {
                        foundSoftlightVignette = true;
                        QVERIFY(fx.parameters.contains(QStringLiteral("amount")));
                        QVERIFY(fx.parameters.contains(QStringLiteral("feather")));
                    } else if (fx.catalogId == QStringLiteral("adjust.levels")) {
                        foundTakeLevels = true;
                        QVERIFY(fx.parameters.contains(QStringLiteral("gamma")));
                        QVERIFY(fx.parameters.value(QStringLiteral("gamma")).toDouble() > 2.0);
                    }
                }
            }
        }

        qDebug() << "Credits Episode 3.veg foundSoftlightGlow:" << foundSoftlightGlow
                 << "foundSoftlightVignette:" << foundSoftlightVignette
                 << "foundTakeLevels:" << foundTakeLevels;

        QVERIFY(foundSoftlightGlow);
        QVERIFY(foundSoftlightVignette);
        QVERIFY(foundTakeLevels);
    }
}

QTEST_MAIN(TestInterchangeVegas)
#include "tst_interchange_vegas.moc"
