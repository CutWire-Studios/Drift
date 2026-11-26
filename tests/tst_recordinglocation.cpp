#include <QtTest>

#include "engine/capture/RecordingLocation.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace drift;

class RecordingLocationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void modeRoundTrips();
    void fileNameIsSortableAndLegal();
    void fileNameToleratesEmptyParts();
    void uniquePathAvoidsCollisions();
    void folderModeUsesTheFolder();
    void missingFolderFallsBackToMovies();
    void projectModeMakesARecordingsSubfolder();
    void unsavedProjectFallsBackToMovies();
    void moviesModeNeedsNoProject();
    void emptyFileNameYieldsNothing();
};

void RecordingLocationTest::initTestCase()
{
    // The Movies fallback resolves a real writable location, so keep it inside the test sandbox
    // rather than writing into the developer's own ~/Videos.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(QStringLiteral("DriftRecordingTest"));
}

void RecordingLocationTest::modeRoundTrips()
{
    for (auto mode : {RecordingLocation::Movies, RecordingLocation::Project,
                      RecordingLocation::Folder}) {
        const QString text = recordingLocationToString(mode);
        QVERIFY(!text.isEmpty());
        QCOMPARE(recordingLocationFromString(text), std::optional<RecordingLocation>(mode));
    }
    // An unknown setting value is not silently a mode: the caller keeps its own default.
    QVERIFY(!recordingLocationFromString(QStringLiteral("cache")).has_value());
    QVERIFY(!recordingLocationFromString(QString()).has_value());
}

void RecordingLocationTest::fileNameIsSortableAndLegal()
{
    const QDateTime when(QDate(2026, 11, 24), QTime(14, 3, 9));
    const QString name = recordingFileName(QStringLiteral("Screen"), when, QStringLiteral("mp4"));
    QCOMPARE(name, QStringLiteral("Screen 2026-11-24 14-03-09.mp4"));
    // No colon or slash anywhere: Windows refuses the first, macOS displays the second as the
    // other one. A leading dot on the extension is the caller's habit, not an error.
    QVERIFY(!name.contains(QLatin1Char(':')));
    QVERIFY(!name.contains(QLatin1Char('/')));
    QCOMPARE(recordingFileName(QStringLiteral("Camera"), when, QStringLiteral(".mkv")),
             QStringLiteral("Camera 2026-11-24 14-03-09.mkv"));
}

void RecordingLocationTest::fileNameToleratesEmptyParts()
{
    const QDateTime when(QDate(2026, 1, 2), QTime(3, 4, 5));
    QCOMPARE(recordingFileName(QString(), when, QStringLiteral("mp4")),
             QStringLiteral("Recording 2026-01-02 03-04-05.mp4"));
    QCOMPARE(recordingFileName(QStringLiteral("Screen"), when, QString()),
             QStringLiteral("Screen 2026-01-02 03-04-05"));
}

void RecordingLocationTest::uniquePathAvoidsCollisions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = uniqueRecordingPath(dir.path(), QStringLiteral("Screen.mp4"));
    QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("Screen.mp4"));

    QFile(first).open(QIODevice::WriteOnly);
    const QString second = uniqueRecordingPath(dir.path(), QStringLiteral("Screen.mp4"));
    QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("Screen 2.mp4"));

    QFile(second).open(QIODevice::WriteOnly);
    const QString third = uniqueRecordingPath(dir.path(), QStringLiteral("Screen.mp4"));
    QCOMPARE(QFileInfo(third).fileName(), QStringLiteral("Screen 3.mp4"));

    QVERIFY(uniqueRecordingPath(QString(), QStringLiteral("a.mp4")).isEmpty());
    QVERIFY(uniqueRecordingPath(dir.path(), QString()).isEmpty());
}

void RecordingLocationTest::folderModeUsesTheFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const RecordingTarget target = chooseRecordingTarget(
        RecordingLocation::Folder, dir.path(), QString(), QStringLiteral("Screen.mp4"));
    QCOMPARE(target.where, RecordingTarget::Where::Folder);
    QVERIFY(!target.folderUnavailable);
    QCOMPARE(QFileInfo(target.path).absolutePath(), QFileInfo(dir.path()).absoluteFilePath());
}

void RecordingLocationTest::missingFolderFallsBackToMovies()
{
    const RecordingTarget target =
        chooseRecordingTarget(RecordingLocation::Folder,
                              QStringLiteral("/nonexistent/unmounted/drive"), QString(),
                              QStringLiteral("Screen.mp4"));
    // Reported, not silent: the user chose that folder and needs to know it was not used.
    QVERIFY(target.folderUnavailable);
    // And never the cache — a recording that a cache sweep deletes is worse than one in the
    // wrong folder.
    QCOMPARE(target.where, RecordingTarget::Where::Movies);
    QVERIFY(!target.path.isEmpty());
}

void RecordingLocationTest::projectModeMakesARecordingsSubfolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString project = QDir(dir.path()).filePath(QStringLiteral("cut.drift"));
    QFile file(project);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    const RecordingTarget target = chooseRecordingTarget(
        RecordingLocation::Project, QString(), project, QStringLiteral("Screen.mp4"));
    QCOMPARE(target.where, RecordingTarget::Where::Project);
    QVERIFY(!target.projectUnavailable);
    const QString parent = QFileInfo(target.path).absolutePath();
    QCOMPARE(QFileInfo(parent).fileName(), QStringLiteral("Recordings"));
    // Created on demand: the first recording of a project is the normal case.
    QVERIFY(QDir(parent).exists());
}

void RecordingLocationTest::unsavedProjectFallsBackToMovies()
{
    // Hitting record on a project that has never been saved is ordinary, so this is a reported
    // fallback rather than a refusal.
    const RecordingTarget target = chooseRecordingTarget(
        RecordingLocation::Project, QString(), QString(), QStringLiteral("Screen.mp4"));
    QVERIFY(target.projectUnavailable);
    QCOMPARE(target.where, RecordingTarget::Where::Movies);
    QVERIFY(!target.path.isEmpty());
}

void RecordingLocationTest::moviesModeNeedsNoProject()
{
    const RecordingTarget target = chooseRecordingTarget(
        RecordingLocation::Movies, QString(), QString(), QStringLiteral("Screen.mp4"));
    QCOMPARE(target.where, RecordingTarget::Where::Movies);
    QVERIFY(!target.folderUnavailable);
    QVERIFY(!target.projectUnavailable);
    // Grouped under an app-named subfolder rather than loose among the user's own videos.
    QCOMPARE(QFileInfo(QFileInfo(target.path).absolutePath()).fileName(),
             QCoreApplication::applicationName());
}

void RecordingLocationTest::emptyFileNameYieldsNothing()
{
    const RecordingTarget target =
        chooseRecordingTarget(RecordingLocation::Movies, QString(), QString(), QString());
    QVERIFY(target.path.isEmpty());
}

QTEST_MAIN(RecordingLocationTest)
#include "tst_recordinglocation.moc"
