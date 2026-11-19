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

QTEST_MAIN(TestInterchangeVegas)
#include "tst_interchange_vegas.moc"
