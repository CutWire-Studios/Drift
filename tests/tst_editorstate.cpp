#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>
#include <QByteArray>

#include <QScopeGuard>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>

#include "engine/HwAccel.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "MulticamImageProvider.h"
#include "MulticamImageStore.h"

#include "core/Clip.h"
#include "core/EffectStackStore.h"
#include "core/Project.h"
#include "core/Track.h"

class EditorStateTest : public QObject
{
    Q_OBJECT

private slots:
    void snapTimeEnabled();
    void retimeKeepsDisabledKeyframeTrackDisabled();
    void effectStackCopyPasteAppendsAndRescales();
    void pastedEffectsKeepTheKeyframeGraphSelection();
    void copiedSingleEffectRoutesToTheRightList();
    void pastedAudioEffectsAreDroppedOnClipsWithNoAudio();
    void pastedUnknownEffectIsKeptAndReported();
    void clipboardHasEffectsIgnoresOrdinaryText();
    void savedEffectPresetAppliesToAnotherClip();
    void multiTrackAudioSelectionAndExtraction();
    void addTextClip();
    void addTextClipEmptyUsesPlaceholder();
    void addTextClipWithTextDoesNotRequestEdit();
    void addTextClipWithPresetAppliesStyle();
    void undoRedoClipAdd();
    void undoLibraryClipDropOntoExistingTrack();
    void undoTrackMute();
    void packagedProjectCarriesDerivedArtifacts();
    void undoBookmarkAdd();
    void bookmarkNavigationAndToggle();
    void workAreaMarkClearAndUndo();
    void bookmarkSnapTarget();
    void renameClipAndAsset();
    void createRenameAndDeleteBinFolder();
    void undoingBinFolderRenameEmitsDataChanged();
    void importIntoDeletedFolderFallsBackToRoot();
    void importUnreadableUrlReportsFailed();
    void moveAssetToFolderAndUndo();
    void deleteBinFolderMovesChildrenAndUndo();
    void removeAssetsIsOneUndoStep();
    void removeAssetsRefusesBatchWithInUseAsset();
    void removeAssetsAndClipsRemovesReferencesAndUndoesAtomically();
    void moveAssetsToFolderIsOneUndoStep();
    void addClipsFromAssetsPlacesThemSequentially();
    void moveTrackReordersAndRemapsSelection();
    void addTrackInsertsEmptyTrackByType();
    void projectPersistenceRoundTrip();
    void projectJsonExportImportRoundTrip();
    void projectJsonImportRejectsGarbageAndLeavesTimeline();
    void newProjectClearsEverything();
    void projectSetupOnPristineProjectStaysClean();
    void projectFpsCanChangeAfterSetup();
    void darkModePreferencePersistsAcrossSessions();
    void uiScalePersistsAcrossSessions();
    void uiLanguagePersistsAcrossSessions();
    void invertTimelineScrollPersistsAcrossSessions();
    void decodeModePickerListsOnlyWorkingBackends();
    void exportFrameRatePersistsAcrossSessions();
    void lastExportSettingsNormalisesStringTypedValues();
    void textStyleBlendModeKeyframesAndEffects();
    void previewSetTextRectScalesPixelSize();
    void fontCatalogIsExposedToQml();
    void effectBrowserCategoriesAndApply();
    void multiSelectClipboardGuidesAndShortcuts();
    void addTransitionBetweenAdjacentClips();
    void addTransitionBetweenAdjacentTextClips();
    void clipAnimationUndoRestoresKind();
    void setTransitionKindAndDurationPersist();
    void replaceTransitionOnDrop();
    void overlapAutoAppliesCrossfade();
    void separateAudioFromCombinedClip();
    void linkedAudioUnlinkAndMove();
    void linkedFadeCurveSyncsPartner();
    void customFadeCurveSessionApplyAndCancel();
    void keyframeGraphPropertySelection();
    void keyframesCanBeDisabledPerProperty();
    void effectParamKeyframes();
    void effectRemovalRemapsGraphSelection();
    void denoiseAddsCleanedClipOnTrackAbove();
    void speedCurveOnAudioClipRetimesAndReplaces();
    void waveformPeaksForSourceRangeSlicesToTheTrimmedWindow();
    void speedCurveSessionExposesTrimmedSourceWindow();
    void shapeStylePartialUpdateAndUndo();
    void replaceAssetSourceRebindsClipsAndClampsTrim();
    void replaceAssetSourceRefusesADifferentKind();
    void exportAssetImageWritesPngAndJpeg();
    void startupProjectUrlFromArguments();
    void multicamSessionDoesNotMutateTheProject();
    void multicamRecutSplitsAnInterval();
    void multicamCancelIsIdentity();
    void multicamSaveSeparateWritesGappedClips();
    void multicamSaveCombinedFlattensOntoTopmost();
    void multicamSessionEndsWithTheProject();
    void multicamSessionPublishesADecodedTilePerAngle();
    void multicamProviderServesTilesByAngleIdWithRevisionQuery();
    void multicamSetUpBuildsAWorkingRigFromTheBin();
};

void EditorStateTest::snapTimeEnabled()
{
    AssetLibrary library;
    AppController state(&library);
    state.setSnapEnabled(true);
    QCOMPARE(state.snapTime(0.0), 0.0);
    QVERIFY(state.snapTime(1.234) >= 0.0);
}

void EditorStateTest::addTextClip()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    QVERIFY(state.durationSeconds() > 0.0);
    QCOMPARE(state.selectedClip(), 0);
}

// Adding with no text drops in a placeholder clip and asks the preview to open
// its inline editor, instead of the click doing nothing at all.
void EditorStateTest::addTextClipEmptyUsesPlaceholder()
{
    AssetLibrary library;
    AppController state(&library);
    QSignalSpy spy(&state, &AppController::inlineTextEditRequested);

    state.addTextClip(QString(), 0.0);

    QCOMPARE(spy.count(), 1);
    const int trackIndex = spy.at(0).at(0).toInt();
    const int clipIndex = spy.at(0).at(1).toInt();
    QCOMPARE(clipIndex, state.selectedClip());

    const QVariantMap clip = state.clipAt(trackIndex, clipIndex);
    QCOMPARE(clip.value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
    QVERIFY(!clip.value(QStringLiteral("textContent")).toString().isEmpty());

    // The playhead must sit inside the clip, or the preview cannot show it.
    const double start = clip.value(QStringLiteral("start")).toDouble();
    const double duration = clip.value(QStringLiteral("duration")).toDouble();
    QVERIFY(state.playheadSeconds() >= start);
    QVERIFY(state.playheadSeconds() < start + duration);
}

// Passing real text keeps the old behaviour: no placeholder, no editor request.
void EditorStateTest::addTextClipWithTextDoesNotRequestEdit()
{
    AssetLibrary library;
    AppController state(&library);
    QSignalSpy spy(&state, &AppController::inlineTextEditRequested);

    state.addTextClip(QStringLiteral("Hello"), 0.0);

    QCOMPARE(spy.count(), 0);
    QCOMPARE(state.clipAt(state.selectedTrack(), state.selectedClip())
                 .value(QStringLiteral("textContent"))
                 .toString(),
             QStringLiteral("Hello"));
}

void EditorStateTest::addTextClipWithPresetAppliesStyle()
{
    AssetLibrary library;
    AppController state(&library);
    QSignalSpy spy(&state, &AppController::inlineTextEditRequested);

    state.addTextClip(QString(), 0.0, QStringLiteral("neon"));

    QCOMPARE(spy.count(), 1);
    const QVariantMap clip = state.clipAt(state.selectedTrack(), state.selectedClip());
    const QVariantMap style = clip.value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("packId")).toString(), QStringLiteral("neon"));
    QCOMPARE(style.value(QStringLiteral("fontFamily")).toString(), QStringLiteral("Bebas Neue"));
}

void EditorStateTest::undoRedoClipAdd()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Undo me"), 0.0);
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.durationSeconds(), 0.0);
    QVERIFY(state.redoAvailable());
    state.redo();
    QVERIFY(state.durationSeconds() > 0.0);
}

// Library drop onto an existing track uses addClipFromAssetAt. Taking a Track&
// before the undo snapshot used to share the track list with `before`, so the
// append mutated both sides and Ctrl+Z left the clip in place.
void EditorStateTest::undoLibraryClipDropOntoExistingTrack()
{
    AssetLibrary library;
    AppController state(&library);

    drift::MediaAsset asset;
    asset.name = QStringLiteral("clip.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.path = QStringLiteral("/nonexistent/clip.mp4");
    asset.durationUs = drift::secondsToUs(5.0);
    state.project()->addAsset(asset);
    library.syncToProject();
    QCOMPARE(library.count(), 1);

    QCOMPARE(state.tracks().size(), 1);
    QVERIFY(state.project()->tracks().at(0).clips.isEmpty());

    state.addClipFromAssetAt(0, 0, 1.0);
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
    QVERIFY(state.undoAvailable());

    state.undo();
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 0);
    QVERIFY(state.redoAvailable());

    state.redo();
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
}

void EditorStateTest::undoTrackMute()
{
    AssetLibrary library;
    AppController state(&library);
    QVERIFY(!state.trackMuted(0));
    state.setTrackMuted(0, true);
    QVERIFY(state.trackMuted(0));
    QVERIFY(state.undoAvailable());
    state.undo();
    QVERIFY(!state.trackMuted(0));
}

void EditorStateTest::undoBookmarkAdd()
{
    AssetLibrary library;
    AppController state(&library);
    QCOMPARE(state.bookmarks().size(), 0);
    state.addBookmark(1.5, QStringLiteral("Test"));
    QCOMPARE(state.bookmarks().size(), 1);
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.bookmarks().size(), 0);
}

void EditorStateTest::bookmarkNavigationAndToggle()
{
    AssetLibrary library;
    AppController state(&library);
    // Project duration follows the longest clip; without one the playhead clamps to 0.
    state.addTextClip(QStringLiteral("Pad"), 0.0);
    state.setClipDuration(0, 0, 10.0);

    state.addBookmark(1.0, QStringLiteral("A"));
    state.addBookmark(3.0, QStringLiteral("B"));
    state.addBookmark(5.0, QStringLiteral("C"));
    QCOMPARE(state.bookmarks().size(), 3);

    state.setPlayheadSeconds(2.0);
    state.goToNextBookmark();
    QCOMPARE(state.playheadSeconds(), 3.0);

    state.goToNextBookmark();
    QCOMPARE(state.playheadSeconds(), 5.0);

    // Wrap to the earliest mark.
    state.goToNextBookmark();
    QCOMPARE(state.playheadSeconds(), 1.0);

    state.goToPreviousBookmark();
    QCOMPARE(state.playheadSeconds(), 5.0);

    state.updateBookmark(1, 3.0, QStringLiteral("Bridge"));
    QCOMPARE(state.bookmarks().at(1).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("Bridge"));

    // Toggle at an existing mark removes it; toggle elsewhere adds one.
    state.setPlayheadSeconds(3.0);
    state.toggleBookmarkAtPlayhead();
    QCOMPARE(state.bookmarks().size(), 2);

    state.setPlayheadSeconds(4.0);
    state.toggleBookmarkAtPlayhead();
    QCOMPARE(state.bookmarks().size(), 3);
}

void EditorStateTest::workAreaMarkClearAndUndo()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Pad"), 0.0);
    state.setClipDuration(0, 0, 10.0);

    QVERIFY(!state.workAreaActive());

    state.setPlayheadSeconds(2.0);
    state.markWorkAreaIn();
    QVERIFY(state.workAreaInSeconds() >= 0);
    QVERIFY(!state.workAreaActive());

    state.setPlayheadSeconds(6.0);
    state.markWorkAreaOut();
    QVERIFY(state.workAreaActive());
    QCOMPARE(state.workAreaInSeconds(), 2.0);
    QCOMPARE(state.workAreaOutSeconds(), 6.0);

    state.goToWorkAreaIn();
    QCOMPARE(state.playheadSeconds(), 2.0);
    state.goToWorkAreaOut();
    QCOMPARE(state.playheadSeconds(), 6.0);

    state.setLoopWorkAreaEnabled(true);
    QVERIFY(state.loopWorkAreaEnabled());

    state.clearWorkArea();
    QVERIFY(!state.workAreaActive());
    QVERIFY(state.undoAvailable());
    state.undo();
    QVERIFY(state.workAreaActive());
}

void EditorStateTest::bookmarkSnapTarget()
{
    AssetLibrary library;
    AppController state(&library);
    state.setSnapEnabled(true);
    state.addBookmark(2.0, QStringLiteral("Snap me"));
    // Within the 150ms snap window of the bookmark.
    QCOMPARE(state.snapTime(2.05), 2.0);
}

void EditorStateTest::renameClipAndAsset()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("name")).toString(), QStringLiteral("Hello"));

    state.setClipName(0, 0, QStringLiteral("Intro title"));
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("name")).toString(), QStringLiteral("Intro title"));
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("name")).toString(), QStringLiteral("Hello"));

    // Asset rename is independent of clip names that were copied at add time.
    // Seed a bin row through the project table the library is bound to.
    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-1");
    asset.name = QStringLiteral("clip.mp4");
    asset.path = QStringLiteral("/tmp/clip.mp4");
    asset.kind = drift::MediaKind::Video;
    state.project()->assets().insert(asset.id, asset);
    state.project()->assetOrder().append(asset.id);
    library.syncToProject();
    QCOMPARE(library.count(), 1);

    QVERIFY(state.renameAsset(0, QStringLiteral("A-roll")));
    QCOMPARE(library.assetAt(0).value(QStringLiteral("name")).toString(), QStringLiteral("A-roll"));
    // Existing timeline text clip is untouched.
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("name")).toString(), QStringLiteral("Hello"));
}

void EditorStateTest::createRenameAndDeleteBinFolder()
{
    AssetLibrary library;
    AppController state(&library);

    const QString id = state.createBinFolder(QStringLiteral("B-Roll"), QString());
    QVERIFY(!id.isEmpty());
    QCOMPARE(state.binFolderModel()->count(), 1);
    QCOMPARE(state.binFolderModel()->folderById(id).value(QStringLiteral("name")).toString(),
             QStringLiteral("B-Roll"));

    QVERIFY(state.renameBinFolder(id, QStringLiteral("A-Roll")));
    QCOMPARE(state.binFolderModel()->folderById(id).value(QStringLiteral("name")).toString(),
             QStringLiteral("A-Roll"));

    QVERIFY(state.deleteBinFolder(id));
    QCOMPARE(state.binFolderModel()->count(), 0);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.binFolderModel()->count(), 1);
    state.undo();
    QCOMPARE(state.binFolderModel()->folderById(id).value(QStringLiteral("name")).toString(),
             QStringLiteral("B-Roll"));
    state.undo();
    QCOMPARE(state.binFolderModel()->count(), 0);
}

void EditorStateTest::undoingBinFolderRenameEmitsDataChanged()
{
    AssetLibrary library;
    AppController state(&library);

    const QString id = state.createBinFolder(QStringLiteral("B-Roll"), QString());
    QVERIFY(state.renameBinFolder(id, QStringLiteral("A-Roll")));

    // Consumers bound to BinFolderModel (the breadcrumb, folder tiles) rely on dataChanged to
    // notice a rename that undo/redo reverted behind their backs — the row's own value changing
    // isn't enough if nothing signals that it did.
    QSignalSpy dataChangedSpy(state.binFolderModel(), &QAbstractItemModel::dataChanged);
    state.undo();
    QVERIFY(dataChangedSpy.count() >= 1);
    const QModelIndex changedIndex = state.binFolderModel()->index(state.binFolderModel()->indexOfId(id));
    bool sawNameRole = false;
    for (const QList<QVariant> &args : dataChangedSpy) {
        const QModelIndex topLeft = args.at(0).toModelIndex();
        const QList<int> roles = args.at(2).value<QList<int>>();
        if (topLeft == changedIndex && roles.contains(int(BinFolderListModel::NameRole)))
            sawNameRole = true;
    }
    QVERIFY(sawNameRole);
    QCOMPARE(state.binFolderModel()->folderById(id).value(QStringLiteral("name")).toString(),
             QStringLiteral("B-Roll"));
}

void EditorStateTest::importIntoDeletedFolderFallsBackToRoot()
{
    AssetLibrary library;
    AppController state(&library);

    const QString folderId = state.createBinFolder(QStringLiteral("Temp"), QString());
    library.setImportFolderId(folderId);
    // Mirrors a slow async import completing after its destination folder was deleted out from
    // under it — the captured id is stale by the time the placeholder row is created.
    QVERIFY(state.deleteBinFolder(folderId));

    QTemporaryFile file(QDir::tempPath() + QStringLiteral("/drift-import-XXXXXX.mp4"));
    QVERIFY(file.open());
    file.write("not a real media file");
    file.close();

    const QStringList ids = library.importLocalPaths({file.fileName()});
    QCOMPARE(ids.size(), 1);
    const int index = library.indexOfId(ids.first());
    QVERIFY(index >= 0);
    QCOMPARE(library.assetAt(index).value(QStringLiteral("folderId")).toString(), QString());

    // The probe this kicked off runs on a QtConcurrent worker thread that captures `library` by
    // raw pointer. Qt's queued-connection context-object safety only protects the case where the
    // object is destroyed after the worker has already posted its result; it does nothing if the
    // worker is still running and dereferences that pointer after `library` goes out of scope at
    // the end of this function. Wait for the probe to actually finish first.
    QTRY_VERIFY_WITH_TIMEOUT(!library.isImportPending(ids.first()), 5000);
}

void EditorStateTest::importUnreadableUrlReportsFailed()
{
    AssetLibrary library;
    AppController state(&library);

    // A dropped host path the sandbox cannot see used to land as a local URL, skip isFile(),
    // and toast "the format may be unsupported". Counting it as failed is what lets the UI
    // tell those two cases apart.
    QSignalSpy finished(&library, &AssetLibrary::importFinished);
    QVERIFY(library.importUrlsAsync(
        {QUrl::fromLocalFile(QStringLiteral("/no/such/drift-unreadable-import.mp4"))}));
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(0).toInt(), 0);
    QCOMPARE(finished.first().at(1).toInt(), 1);
    QCOMPARE(library.count(), 0);
}

void EditorStateTest::moveAssetToFolderAndUndo()
{
    AssetLibrary library;
    AppController state(&library);

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-1");
    asset.name = QStringLiteral("clip.mp4");
    asset.path = QStringLiteral("/tmp/clip.mp4");
    asset.kind = drift::MediaKind::Video;
    state.project()->assets().insert(asset.id, asset);
    state.project()->assetOrder().append(asset.id);
    library.syncToProject();
    QCOMPARE(library.count(), 1);

    const QString folderId = state.createBinFolder(QStringLiteral("B-Roll"), QString());
    QVERIFY(state.moveAssetToFolder(0, folderId));
    QCOMPARE(library.assetAt(0).value(QStringLiteral("folderId")).toString(), folderId);

    state.undo();
    QCOMPARE(library.assetAt(0).value(QStringLiteral("folderId")).toString(), QString());
}

void EditorStateTest::deleteBinFolderMovesChildrenAndUndo()
{
    AssetLibrary library;
    AppController state(&library);

    const QString parentId = state.createBinFolder(QStringLiteral("Interviews"), QString());
    const QString childId = state.createBinFolder(QStringLiteral("Day 1"), parentId);

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-1");
    asset.name = QStringLiteral("clip.mp4");
    asset.path = QStringLiteral("/tmp/clip.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.folderId = childId;
    state.project()->assets().insert(asset.id, asset);
    state.project()->assetOrder().append(asset.id);
    library.syncToProject();

    QVERIFY(state.deleteBinFolder(childId));
    QCOMPARE(state.binFolderModel()->count(), 1);
    QCOMPARE(library.assetAt(0).value(QStringLiteral("folderId")).toString(), parentId);

    state.undo();
    QCOMPARE(state.binFolderModel()->count(), 2);
    QCOMPARE(library.assetAt(0).value(QStringLiteral("folderId")).toString(), childId);
}

void EditorStateTest::removeAssetsIsOneUndoStep()
{
    AssetLibrary library;
    AppController state(&library);

    for (int i = 0; i < 3; ++i) {
        drift::MediaAsset asset;
        asset.id = QStringLiteral("asset-%1").arg(i);
        asset.name = QStringLiteral("clip-%1.mp4").arg(i);
        asset.path = QStringLiteral("/tmp/clip-%1.mp4").arg(i);
        asset.kind = drift::MediaKind::Video;
        state.project()->assets().insert(asset.id, asset);
        state.project()->assetOrder().append(asset.id);
    }
    library.syncToProject();
    QCOMPARE(library.count(), 3);

    // Removed out of position order deliberately — removeAssets must resolve each id fresh
    // rather than trusting indices captured before earlier removals shifted the rest down.
    const int removed = state.removeAssets({QStringLiteral("asset-2"), QStringLiteral("asset-0")});
    QCOMPARE(removed, 2);
    QCOMPARE(library.count(), 1);
    QCOMPARE(library.assetAt(0).value(QStringLiteral("id")).toString(), QStringLiteral("asset-1"));

    // One undo step restores both, not one step per asset.
    state.undo();
    QCOMPARE(library.count(), 3);
}

void EditorStateTest::removeAssetsRefusesBatchWithInUseAsset()
{
    AssetLibrary library;
    AppController state(&library);
    drift::Project &project = *state.project();

    for (int i = 0; i < 3; ++i) {
        drift::MediaAsset asset;
        asset.id = QStringLiteral("asset-%1").arg(i);
        asset.name = QStringLiteral("clip-%1.mp4").arg(i);
        asset.path = QStringLiteral("/tmp/clip-%1.mp4").arg(i);
        asset.kind = drift::MediaKind::Video;
        project.assets().insert(asset.id, asset);
        project.assetOrder().append(asset.id);
    }

    // asset-1 is still referenced by a clip on the timeline.
    drift::Clip clip;
    clip.id = QStringLiteral("clip-0");
    clip.assetId = QStringLiteral("asset-1");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);
    drift::Track track{.type = drift::TrackType::Video};
    track.clips.append(clip);
    project.tracks().append(track);
    library.syncToProject();
    QCOMPARE(library.count(), 3);

    // One in-use id anywhere in the batch must refuse the whole removal, not just skip
    // that one id — otherwise a caller that bypasses the QML confirmation flow's own
    // in-use check (AssetsPanel.qml's requestRemoveAsset) could orphan the clip above.
    const int removed = state.removeAssets(
        {QStringLiteral("asset-0"), QStringLiteral("asset-1"), QStringLiteral("asset-2")});
    QCOMPARE(removed, 0);
    QCOMPARE(library.count(), 3);
}

void EditorStateTest::removeAssetsAndClipsRemovesReferencesAndUndoesAtomically()
{
    AssetLibrary library;
    AppController state(&library);

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-used");
    asset.name = QStringLiteral("used.mp4");
    asset.path = QStringLiteral("/tmp/used.mp4");
    asset.kind = drift::MediaKind::Video;

    state.project()->assets().insert(asset.id, asset);
    state.project()->assetOrder().append(asset.id);

    library.syncToProject();

    QCOMPARE(library.count(), 1);

    // Build the timeline through the controller's public editing path instead
    // of manually mutating Project tracks. This mirrors actual application use.
    state.addClipsFromAssets({asset.id});

    int initialReferences = 0;
    for (const drift::Track &track : state.project()->tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.assetId == asset.id)
                ++initialReferences;
        }
    }
    QCOMPARE(initialReferences, 1);

    const int removed =
        state.removeAssetsAndClips({QStringLiteral("asset-used")});

    QCOMPARE(removed, 1);
    QCOMPARE(library.count(), 0);

    int remainingReferences = 0;
    for (const drift::Track &track : state.project()->tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.assetId == QStringLiteral("asset-used"))
                ++remainingReferences;
        }
    }
    QCOMPARE(remainingReferences, 0);

    // The destructive removal itself must be exactly one undo operation.
    state.undo();

    QCOMPARE(library.count(), 1);

    int restoredReferences = 0;
    for (const drift::Track &track : state.project()->tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.assetId == QStringLiteral("asset-used"))
                ++restoredReferences;
        }
    }

    QCOMPARE(restoredReferences, 1);
}


void EditorStateTest::moveAssetsToFolderIsOneUndoStep()
{
    AssetLibrary library;
    AppController state(&library);

    const QString folderId = state.createBinFolder(QStringLiteral("B-Roll"), QString());
    for (int i = 0; i < 2; ++i) {
        drift::MediaAsset asset;
        asset.id = QStringLiteral("asset-%1").arg(i);
        asset.name = QStringLiteral("clip-%1.mp4").arg(i);
        asset.path = QStringLiteral("/tmp/clip-%1.mp4").arg(i);
        asset.kind = drift::MediaKind::Video;
        state.project()->assets().insert(asset.id, asset);
        state.project()->assetOrder().append(asset.id);
    }
    library.syncToProject();

    const int moved = state.moveAssetsToFolder(
        {QStringLiteral("asset-0"), QStringLiteral("asset-1")}, folderId);
    QCOMPARE(moved, 2);
    QCOMPARE(library.assetAt(0).value(QStringLiteral("folderId")).toString(), folderId);
    QCOMPARE(library.assetAt(1).value(QStringLiteral("folderId")).toString(), folderId);

    state.undo();
    QCOMPARE(library.assetAt(0).value(QStringLiteral("folderId")).toString(), QString());
    QCOMPARE(library.assetAt(1).value(QStringLiteral("folderId")).toString(), QString());
}

void EditorStateTest::addClipsFromAssetsPlacesThemSequentially()
{
    AssetLibrary library;
    AppController state(&library);
    state.setPlayheadSeconds(0.0);

    drift::MediaAsset first;
    first.id = QStringLiteral("asset-0");
    first.name = QStringLiteral("first.mp4");
    first.path = QStringLiteral("/tmp/first.mp4");
    first.kind = drift::MediaKind::Video;
    first.durationUs = drift::secondsToUs(4.0);
    state.project()->assets().insert(first.id, first);
    state.project()->assetOrder().append(first.id);

    drift::MediaAsset second;
    second.id = QStringLiteral("asset-1");
    second.name = QStringLiteral("second.mp4");
    second.path = QStringLiteral("/tmp/second.mp4");
    second.kind = drift::MediaKind::Video;
    second.durationUs = drift::secondsToUs(2.0);
    state.project()->assets().insert(second.id, second);
    state.project()->assetOrder().append(second.id);
    library.syncToProject();

    state.addClipsFromAssets({QStringLiteral("asset-0"), QStringLiteral("asset-1")});

    // Both land on the same default video track, back to back in the order given — not both
    // sitting at the playhead on top of each other.
    int videoTrack = -1;
    for (int i = 0; i < state.project()->tracks().size(); ++i) {
        if (state.project()->tracks().at(i).clips.size() == 2) {
            videoTrack = i;
            break;
        }
    }
    QVERIFY(videoTrack >= 0);
    const drift::Track &track = state.project()->tracks().at(videoTrack);
    QCOMPARE(track.clips[0].assetId, QStringLiteral("asset-0"));
    QCOMPARE(track.clips[1].assetId, QStringLiteral("asset-1"));
    QCOMPARE(track.clips[0].timelineStart, drift::TimeUs(0));
    QCOMPARE(track.clips[1].timelineStart, track.clips[0].timelineEnd());

    // One undo step removes both clips added by the batch.
    state.undo();
    bool anyClipsLeft = false;
    for (const drift::Track &t : state.project()->tracks()) {
        if (!t.clips.isEmpty())
            anyClipsLeft = true;
    }
    QVERIFY(!anyClipsLeft);
}

void EditorStateTest::moveTrackReordersAndRemapsSelection()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Top"), 0.0);
    // addTextClip prepends a text track above the default video track.
    QCOMPARE(state.tracks().size(), 2);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
    QCOMPARE(state.selectedTrack(), 0);

    state.moveTrack(0, 1);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(state.selectedTrack(), 1);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
}

void EditorStateTest::addTrackInsertsEmptyTrackByType()
{
    AssetLibrary library;
    AppController state(&library);
    QCOMPARE(state.tracks().size(), 1);

    state.addTrack(QStringLiteral("audio"));
    QCOMPARE(state.tracks().size(), 2);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("audio"));
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("clips")).toList().size(), 0);

    state.addTrack(QStringLiteral("video"));
    QCOMPARE(state.tracks().size(), 3);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("audio"));

    state.addTrack(QStringLiteral("not-a-type"));
    QCOMPARE(state.tracks().size(), 3);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.tracks().size(), 2);
}

// Packaging embeds the derived artifacts and repoints the project at the extraction directory, so
// a matte survives its cache being swept — which is the whole reason the format exists.
void EditorStateTest::packagedProjectCarriesDerivedArtifacts()
{
    // Keeps the extraction directory out of the real app data location.
    QStandardPaths::setTestModeEnabled(true);
    const auto restore = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });

    QTemporaryDir sources;
    QVERIFY(sources.isValid());
    const QString mattePath = sources.filePath(QStringLiteral("matte.mp4"));
    {
        QFile matte(mattePath);
        QVERIFY(matte.open(QIODevice::WriteOnly));
        matte.write(QByteArray(1024, '\x7f'));
    }

    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Masked"), 0.0);
    state.setProjectMetadata(QStringLiteral("Packaged"), QStringLiteral("Ada"),
                             QStringLiteral("With a matte"));

    // No QML-facing setter carries a matte path; the segmentation job writes it directly.
    drift::Clip &clip = state.project()->tracks()[0].clips[0];
    clip.mask.shape = drift::MaskShape::Matte;
    clip.mask.mattePath = mattePath;

    QTemporaryDir out;
    QVERIFY(out.isValid());
    const QString bundlePath = out.filePath(QStringLiteral("packaged.drift"));

    QSignalSpy finished(&state, &AppController::packageFinished);
    state.packageProject(QUrl::fromLocalFile(bundlePath));
    QVERIFY(finished.wait(30000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));

    // The matte's own cache is gone, exactly as a sweep would leave it.
    QVERIFY(QFile::remove(mattePath));

    state.newProject();
    state.loadProject(QUrl::fromLocalFile(bundlePath));
    // Opening a bundle extracts its media on a worker thread and only applies the project
    // document once that finishes, so the timeline below is still the empty new project until
    // the load reports in.
    QTRY_COMPARE_WITH_TIMEOUT(state.lastMessage(), QStringLiteral("Project loaded"), 30000);

    QCOMPARE(state.projectMetadata().value(QStringLiteral("title")).toString(),
             QStringLiteral("Packaged"));
    QCOMPARE(state.projectMetadata().value(QStringLiteral("author")).toString(),
             QStringLiteral("Ada"));

    const drift::Clip &loaded = state.project()->tracks().at(0).clips.at(0);
    QVERIFY(loaded.mask.mattePath != mattePath);
    QVERIFY2(QFileInfo::exists(loaded.mask.mattePath), qPrintable(loaded.mask.mattePath));
    QCOMPARE(QFileInfo(loaded.mask.mattePath).size(), 1024);
}

void EditorStateTest::projectPersistenceRoundTrip()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Persist"), 0.0);
    state.setTrackMuted(0, true);
    state.addBookmark(2.0, QStringLiteral("Mark"));
    state.setMediaGridMode(false);
    QCOMPARE(state.tracks().size(), 2); // text + default video

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    tempFile.close();

    state.saveProject(QUrl::fromLocalFile(tempFile.fileName()));
    state.loadProject(QUrl::fromLocalFile(tempFile.fileName()));

    QVERIFY(state.durationSeconds() > 0.0);
    QCOMPARE(state.tracks().size(), 2);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QVERIFY(state.trackMuted(0));
    QCOMPARE(state.bookmarks().size(), 1);
    QCOMPARE(state.mediaGridMode(), false);
}

void EditorStateTest::projectJsonExportImportRoundTrip()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("JsonRoundTrip"), 0.0);
    state.setTrackMuted(0, true);
    state.addBookmark(2.0, QStringLiteral("Mark"));
    state.setMediaGridMode(false);
    state.setProjectMetadata(QStringLiteral("FromJson"), QStringLiteral("Ada"), QString());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString jsonPath = dir.filePath(QStringLiteral("project.json"));
    const QString driftPath = dir.filePath(QStringLiteral("project.drift"));

    state.saveProject(QUrl::fromLocalFile(driftPath));
    QVERIFY(!state.hasUnsavedChanges());
    QCOMPARE(state.currentProjectPath(), driftPath);

    state.saveProjectJson(QUrl::fromLocalFile(jsonPath));
    QVERIFY(QFileInfo::exists(jsonPath));
    // Export leaves the .drift association and dirty flag alone.
    QCOMPARE(state.currentProjectPath(), driftPath);
    QVERIFY(!state.hasUnsavedChanges());
    QCOMPARE(state.lastMessage(), QStringLiteral("Project JSON saved"));

    state.newProject();
    QVERIFY(state.currentProjectPath().isEmpty());

    state.loadProjectJson(QUrl::fromLocalFile(jsonPath));
    QCOMPARE(state.lastMessage(), QStringLiteral("Project JSON loaded"));
    QCOMPARE(state.projectMetadata().value(QStringLiteral("title")).toString(),
             QStringLiteral("FromJson"));
    QCOMPARE(state.projectMetadata().value(QStringLiteral("author")).toString(),
             QStringLiteral("Ada"));
    QCOMPARE(state.tracks().size(), 2);
    QVERIFY(state.trackMuted(0));
    QCOMPARE(state.bookmarks().size(), 1);
    QCOMPARE(state.mediaGridMode(), false);
    // Import is not a project of record: Save must ask for a .drift path.
    QVERIFY(state.currentProjectPath().isEmpty());
    QVERIFY(state.hasUnsavedChanges());

    // loadProject sniffs JSON so a dropped file, CLI arg or MCP load_project works.
    state.newProject();
    state.loadProject(QUrl::fromLocalFile(jsonPath));
    QCOMPARE(state.lastMessage(), QStringLiteral("Project JSON loaded"));
    QCOMPARE(state.projectMetadata().value(QStringLiteral("title")).toString(),
             QStringLiteral("FromJson"));
    QVERIFY(state.currentProjectPath().isEmpty());
    QVERIFY(state.hasUnsavedChanges());
}

void EditorStateTest::projectJsonImportRejectsGarbageAndLeavesTimeline()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("KeepMe"), 0.0);
    QCOMPARE(state.tracks().size(), 2);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("nope.json"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ \"hello\": true }");
    }

    state.loadProjectJson(QUrl::fromLocalFile(path));
    QCOMPARE(state.lastMessageSeverity(), QStringLiteral("error"));
    QCOMPARE(state.lastMessage(), QStringLiteral("This file isn’t a Drift project."));
    QCOMPARE(state.tracks().size(), 2);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("clips")).toList().size(), 1);
}

// resetToDefaultTimeline() only clears the tracks, so New Project used to keep the asset pool,
// name, canvas size, bookmarks and work area of the project it replaced.
void EditorStateTest::newProjectClearsEverything()
{
    AssetLibrary library;
    AppController state(&library);

    drift::MediaAsset asset;
    asset.name = QStringLiteral("clip.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.path = QStringLiteral("/nonexistent/clip.mp4");
    asset.durationUs = drift::secondsToUs(5.0);
    state.project()->addAsset(asset);
    library.syncToProject();
    QCOMPARE(library.count(), 1);

    state.addTextClip(QStringLiteral("Old"), 0.0);
    state.addBookmark(2.0, QStringLiteral("Mark"));
    state.setPlayheadSeconds(1.0);
    state.markWorkAreaIn();
    state.setPlayheadSeconds(3.0);
    state.markWorkAreaOut();
    state.setProjectMetadata(QStringLiteral("Old project"), QStringLiteral("Ada"),
                             QStringLiteral("Notes"));
    state.setProjectSetup(1080, 1920, 60);
    state.setMediaGridMode(false);
    QVERIFY(state.workAreaActive());
    QVERIFY(state.hasUnsavedChanges());

    state.newProject();

    QCOMPARE(library.count(), 0);
    QVERIFY(state.project()->assets().isEmpty());
    QVERIFY(state.project()->assetOrder().isEmpty());
    QCOMPARE(state.projectName(), QStringLiteral("Untitled Project"));
    QCOMPARE(state.projectMetadata().value(QStringLiteral("description")).toString(), QString());
    QCOMPARE(state.bookmarks().size(), 0);
    QVERIFY(!state.workAreaActive());
    QCOMPARE(state.projectWidth(), 1920);
    QCOMPARE(state.projectHeight(), 1080);
    QCOMPARE(state.projectFps(), 30);
    QCOMPARE(state.tracks().size(), 1);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
    QVERIFY(state.project()->tracks().at(0).clips.isEmpty());
    QCOMPARE(state.mediaGridMode(), true);
    QVERIFY(!state.hasUnsavedChanges());
    QVERIFY(!state.undoAvailable());
}

// Answering the first-run layout chooser is the project taking its initial shape, not an edit, so
// it must not leave a brand-new project dirty with an undo step.
void EditorStateTest::projectSetupOnPristineProjectStaysClean()
{
    AssetLibrary library;
    AppController state(&library);

    state.setProjectSetup(1080, 1920, 60);

    QCOMPARE(state.projectWidth(), 1080);
    QCOMPARE(state.projectHeight(), 1920);
    QCOMPARE(state.projectFps(), 60);
    QVERIFY(!state.hasUnsavedChanges());
    QVERIFY(!state.undoAvailable());

    // Once there is something to undo back to, it is a real edit again.
    state.addTextClip(QStringLiteral("Titled"), 0.0);
    state.setProjectSetup(1920, 1080, 30);
    QVERIFY(state.hasUnsavedChanges());
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.projectWidth(), 1080);
    QCOMPARE(state.projectHeight(), 1920);
}

void EditorStateTest::projectFpsCanChangeAfterSetup()
{
    AssetLibrary library;
    AppController state(&library);

    QCOMPARE(state.projectFps(), 30);
    QCOMPARE(state.projectWidth(), 1920);
    QCOMPARE(state.projectHeight(), 1080);

    state.setProjectFps(60);
    QCOMPARE(state.projectFps(), 60);
    QCOMPARE(state.projectWidth(), 1920);
    QCOMPARE(state.projectHeight(), 1080);

    state.setProjectFps(0);
    QCOMPARE(state.projectFps(), 1);
    state.setProjectFps(999);
    QCOMPARE(state.projectFps(), 240);
    state.setProjectFps(24);
    QCOMPARE(state.projectFps(), 24);
}

// The picker offers a backend only when its device opens here, and a mode naming one
// this machine lacks has to fall back to Auto rather than to a choice that would never
// engage — settings outlive the GPU they were written on.
void EditorStateTest::decodeModePickerListsOnlyWorkingBackends()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("preview/decodeMode"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });
    QSettings().remove(QStringLiteral("preview/decodeMode"));

    AssetLibrary library;
    AppController state(&library);
    PlaybackEngine *playback = state.playback();

    const QVariantList modes = playback->decodeModes();
    QVERIFY(modes.size() >= 2);
    QCOMPARE(modes.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("auto"));
    QCOMPARE(modes.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("software"));

    QStringList hardwareIds;
    for (qsizetype i = 2; i < modes.size(); ++i) {
        const QVariantMap row = modes.at(i).toMap();
        const QString id = row.value(QStringLiteral("id")).toString();
        QVERIFY(id.startsWith(QStringLiteral("hw:")));
        QVERIFY(!row.value(QStringLiteral("label")).toString().isEmpty());
        hardwareIds.append(id);
    }
    QCOMPARE(hardwareIds.size(), drift::hwaccel::availableDecodeBackends().size());

    QCOMPARE(playback->decodeMode(), QStringLiteral("auto"));

    playback->setDecodeMode(QStringLiteral("software"));
    QCOMPARE(playback->decodeMode(), QStringLiteral("software"));

    playback->setDecodeMode(QStringLiteral("hw:nosuchgpu"));
    QCOMPARE(playback->decodeMode(), QStringLiteral("auto"));

    // The previous two-state value resolves to whichever backend the probe would pick.
    playback->setDecodeMode(QStringLiteral("hardware"));
    QCOMPARE(playback->decodeMode(),
             hardwareIds.isEmpty() ? QStringLiteral("auto") : hardwareIds.first());

    for (const QString &id : std::as_const(hardwareIds)) {
        playback->setDecodeMode(id);
        QCOMPARE(playback->decodeMode(), id);
    }
}

void EditorStateTest::darkModePreferencePersistsAcrossSessions()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("ui/darkMode"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });
    QSettings().remove(QStringLiteral("ui/darkMode"));

    AssetLibrary library;
    {
        AppController state(&library);
        // Never toggled: no override, so the UI is free to follow the OS scheme.
        QVERIFY(!state.darkModeOverridden());

        QSignalSpy spy(&state, &AppController::darkModePreferenceChanged);
        state.setDarkModePreference(false);
        QCOMPARE(spy.count(), 1);
        QVERIFY(state.darkModeOverridden());
        QCOMPARE(state.darkModePreferred(), false);
    }

    AppController relaunched(&library);
    QVERIFY(relaunched.darkModeOverridden());
    QCOMPARE(relaunched.darkModePreferred(), false);
}

void EditorStateTest::uiScalePersistsAcrossSessions()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    const QByteArray previousScale = qgetenv("QT_SCALE_FACTOR");
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("ui/scale"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
        if (previousScale.isNull())
            qunsetenv("QT_SCALE_FACTOR");
        else
            qputenv("QT_SCALE_FACTOR", previousScale);
    });
    QSettings().remove(QStringLiteral("ui/scale"));

    AssetLibrary library;
    {
        AppController state(&library);
        QCOMPARE(state.uiScale(), 1.0);

        QSignalSpy spy(&state, &AppController::uiScaleChanged);
        state.setUiScale(1.3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(state.uiScale(), 1.25);

        state.setUiScale(9.0);
        QCOMPARE(state.uiScale(), 2.0);
        state.setUiScale(0.1);
        QCOMPARE(state.uiScale(), 1.0);
        QVERIFY(!QSettings().contains(QStringLiteral("ui/scale")));

        state.setUiScale(1.5);
        QCOMPARE(state.uiScale(), 1.5);
    }

    AppController relaunched(&library);
    QCOMPARE(relaunched.uiScale(), 1.5);

    qunsetenv("QT_SCALE_FACTOR");
    AppController::applyStoredUiScale();
    QCOMPARE(qgetenv("QT_SCALE_FACTOR"), QByteArray("1.5"));

    qputenv("QT_SCALE_FACTOR", "3");
    AppController::applyStoredUiScale();
    QCOMPARE(qgetenv("QT_SCALE_FACTOR"), QByteArray("3"));
}

void EditorStateTest::uiLanguagePersistsAcrossSessions()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    const auto restore = qScopeGuard([&] {
        QSettings settings;
        settings.remove(QStringLiteral("ui/language"));
        settings.remove(QStringLiteral("ui/languageChosen"));
        settings.remove(QStringLiteral("lastSessionPath"));
        settings.remove(QStringLiteral("recentProjects"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });
    {
        QSettings settings;
        settings.remove(QStringLiteral("ui/language"));
        settings.remove(QStringLiteral("ui/languageChosen"));
        settings.remove(QStringLiteral("lastSessionPath"));
        settings.remove(QStringLiteral("recentProjects"));
    }

    AssetLibrary library;
    {
        AppController state(&library);
        // Brand-new install: no session history, so the first-launch chooser should ask.
        QVERIFY(state.needsUiLanguagePrompt());
        QCOMPARE(state.uiLanguage(), QString());

        QSignalSpy spy(&state, &AppController::uiLanguageChanged);
        state.chooseUiLanguage(QStringLiteral("en"));
        QVERIFY(spy.count() >= 1);
        QCOMPARE(state.uiLanguage(), QStringLiteral("en"));
        QVERIFY(!state.needsUiLanguagePrompt());
        QCOMPARE(QSettings().value(QStringLiteral("ui/language")).toString(), QStringLiteral("en"));
        QVERIFY(QSettings().value(QStringLiteral("ui/languageChosen")).toBool());
    }

    {
        AppController relaunched(&library);
        QCOMPARE(relaunched.uiLanguage(), QStringLiteral("en"));
        QVERIFY(!relaunched.needsUiLanguagePrompt());

        relaunched.setUiLanguage(QStringLiteral("es"));
        QCOMPARE(relaunched.uiLanguage(), QStringLiteral("es"));
    }

    AppController afterSettingsChange(&library);
    QCOMPARE(afterSettingsChange.uiLanguage(), QStringLiteral("es"));
    QVERIFY(!afterSettingsChange.needsUiLanguagePrompt());

    {
        QSettings settings;
        settings.remove(QStringLiteral("ui/language"));
        settings.remove(QStringLiteral("ui/languageChosen"));
        settings.setValue(QStringLiteral("lastSessionPath"), QStringLiteral("/tmp/used.drift"));
    }
    AppController returningUser(&library);
    QVERIFY(!returningUser.needsUiLanguagePrompt());
}

void EditorStateTest::invertTimelineScrollPersistsAcrossSessions()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("timeline/invertScroll"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });
    QSettings().remove(QStringLiteral("timeline/invertScroll"));

    AssetLibrary library;
    {
        AppController state(&library);
        QVERIFY(!state.invertTimelineScroll());

        QSignalSpy spy(&state, &AppController::invertTimelineScrollChanged);
        state.setInvertTimelineScroll(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(state.invertTimelineScroll());
        state.setInvertTimelineScroll(true);
        QCOMPARE(spy.count(), 1);
    }

    AppController relaunched(&library);
    QVERIFY(relaunched.invertTimelineScroll());
}

void EditorStateTest::exportFrameRatePersistsAcrossSessions()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("export"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });
    QSettings().remove(QStringLiteral("export"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString outPath = dir.filePath(QStringLiteral("out.mp4"));

    AssetLibrary library;
    {
        AppController state(&library);
        QVERIFY(state.lastExportSettings().isEmpty());

        // The default entry follows the project rather than pinning a rate.
        const QVariantList options = state.exportFrameRateOptions();
        QVERIFY(!options.isEmpty());
        QCOMPARE(options.first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("project"));
        QCOMPARE(options.first().toMap().value(QStringLiteral("fpsNum")).toInt(), 0);

        QVariantMap settings;
        settings.insert(QStringLiteral("scaleId"), QStringLiteral("source"));
        settings.insert(QStringLiteral("videoCodecId"), QStringLiteral("h264"));
        settings.insert(QStringLiteral("audioCodecId"), QStringLiteral("aac"));
        settings.insert(QStringLiteral("fpsNum"), 30000);
        settings.insert(QStringLiteral("fpsDen"), 1001);

        // The choice is remembered before any encoding starts, so an empty timeline
        // still exercises the write path without needing an encoder.
        QSignalSpy finished(&state, &AppController::exportFinished);
        state.exportWithSettings(QUrl::fromLocalFile(outPath), settings);
        // Must not outlive the worker: it captures `state`.
        QVERIFY(finished.wait(15000));
    }

    AppController relaunched(&library);
    const QVariantMap remembered = relaunched.lastExportSettings();
    QCOMPARE(remembered.value(QStringLiteral("fpsNum")).toInt(), 30000);
    QCOMPARE(remembered.value(QStringLiteral("fpsDen")).toInt(), 1001);
    QCOMPARE(relaunched.lastExportFolder(), dir.path());
}

// A real relaunch re-parses the INI and every value comes back a QString. QML reads
// this map directly, and JavaScript treats the string "false" as truthy — so an
// untyped audioOnly opened the export dialog in audio mode on every launch after
// the first. Writing strings here reproduces that round-trip without a second
// process (same-process QSettings would otherwise serve typed values from cache).
void EditorStateTest::lastExportSettingsNormalisesStringTypedValues()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("export"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });

    {
        QSettings store;
        store.remove(QStringLiteral("export"));
        store.beginGroup(QStringLiteral("export"));
        store.setValue(QStringLiteral("scaleId"), QStringLiteral("source"));
        store.setValue(QStringLiteral("videoCodecId"), QStringLiteral("h264"));
        store.setValue(QStringLiteral("audioOnly"), QStringLiteral("false"));
        store.setValue(QStringLiteral("fpsNum"), QStringLiteral("30000"));
        store.setValue(QStringLiteral("fpsDen"), QStringLiteral("1001"));
        store.setValue(QStringLiteral("crf"), QStringLiteral("23"));
        store.endGroup();
    }

    AssetLibrary library;
    AppController state(&library);
    const QVariantMap remembered = state.lastExportSettings();

    // Types, not just values: QML branches on these directly.
    QCOMPARE(remembered.value(QStringLiteral("audioOnly")).typeId(), QMetaType::Bool);
    QCOMPARE(remembered.value(QStringLiteral("audioOnly")).toBool(), false);
    QCOMPARE(remembered.value(QStringLiteral("fpsNum")).typeId(), QMetaType::Int);
    QCOMPARE(remembered.value(QStringLiteral("fpsNum")).toInt(), 30000);
    QCOMPARE(remembered.value(QStringLiteral("fpsDen")).typeId(), QMetaType::Int);
    QCOMPARE(remembered.value(QStringLiteral("crf")).typeId(), QMetaType::Int);
    QCOMPARE(remembered.value(QStringLiteral("crf")).toInt(), 23);
}

void EditorStateTest::textStyleBlendModeKeyframesAndEffects()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QVERIFY(track >= 0);
    QVERIFY(clip >= 0);

    // Text style: partial update only touches the given keys.
    state.setTextStyle(track, clip,
                       QVariantMap{{"pixelSize", 120}, {"fontWeight", 300},
                                   {"color", QStringLiteral("#ffff0000")}});
    QVariantMap style = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("pixelSize")).toInt(), 120);
    QCOMPARE(style.value(QStringLiteral("fontWeight")).toInt(), 300);
    QCOMPARE(style.value(QStringLiteral("color")).toString(), QStringLiteral("#ffff0000"));

    // Nested animation maps patch-merge like any other key.
    state.setTextStyle(track, clip, QVariantMap{{"animIn", QVariantMap{{"kind", QStringLiteral("pop")},
                                                                       {"duration", 0.25}}}});
    style = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    const QVariantMap animIn = style.value(QStringLiteral("animIn")).toMap();
    QCOMPARE(animIn.value(QStringLiteral("kind")).toString(), QStringLiteral("pop"));
    QCOMPARE(animIn.value(QStringLiteral("duration")).toDouble(), 0.25);
    QCOMPARE(style.value(QStringLiteral("pixelSize")).toInt(), 120); // untouched

    // Presets overwrite the whole style.
    state.applyTextPreset(track, clip, QStringLiteral("title"));
    style = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("pixelSize")).toInt(), 96);
    QCOMPARE(style.value(QStringLiteral("fontWeight")).toInt(), 800);

    // Blend mode.
    state.setClipBlendMode(track, clip, QStringLiteral("multiply"));
    QCOMPARE(state.selectedClipData().value(QStringLiteral("blendMode")).toString(), QStringLiteral("multiply"));

    // Keyframes: add, list, remove.
    state.setClipKeyframe(track, clip, QStringLiteral("opacity"), 0.0, 0.5);
    QVariantList keyframes = state.clipKeyframes(track, clip, QStringLiteral("opacity"));
    QCOMPARE(keyframes.size(), 1);
    QCOMPARE(keyframes.first().toMap().value(QStringLiteral("value")).toDouble(), 0.5);

    // WYSIWYG-style preview updates must refresh selectedClipData for the inspector.
    QSignalSpy clipDataSpy(&state, &AppController::selectedClipDataChanged);
    state.beginPreviewDrag(QStringLiteral("Move clip"));
    state.previewSetClipPosition(track, clip, 100.0, 200.0);
    QVERIFY(clipDataSpy.count() >= 1);
    const QVariantMap keys = state.selectedClipData().value(QStringLiteral("keyframes")).toMap();
    const QVariantList xKeys = keys.value(QStringLiteral("x")).toMap().value(QStringLiteral("points")).toList();
    QVERIFY(!xKeys.isEmpty());
    QCOMPARE(xKeys.first().toMap().value(QStringLiteral("value")).toDouble(), 100.0);
    state.commitPreviewDrag();

    state.beginPreviewDrag(QStringLiteral("Resize clip"));
    state.previewSetClipSize(track, clip, 640.0, 360.0);
    state.commitPreviewDrag();
    state.resetClipTransform(track, clip);
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("opacity")).size(), 0);
    // Reset writes a full-canvas layout keyframe at t=0 for each layout track.
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("x")).size(), 1);
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("width")).size(), 1);

    state.removeClipKeyframe(track, clip, QStringLiteral("opacity"), 0.0);
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("opacity")).size(), 0);

    // Effect catalog wiring: add a known effect, tweak its param, remove it.
    const QVariantList catalog = state.effectCatalog();
    QVERIFY(!catalog.isEmpty());

    state.addEffect(track, clip, QStringLiteral("adjust.contrast"));
    QVariantList effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    QCOMPARE(effects.size(), 1);
    QCOMPARE(state.selectedClipEffects().size(), 1);
    QCOMPARE(effects.first().toMap().value(QStringLiteral("catalogId")).toString(),
             QStringLiteral("adjust.contrast"));

    state.setEffectParam(track, clip, 0, QStringLiteral("contrast"), 2.5);
    effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    const QVariantList params = effects.first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(params.first().toMap().value(QStringLiteral("value")).toDouble(), 2.5);

    // Preview drag coalesces many slider moves into a single undo step.
    state.beginPreviewDrag(QStringLiteral("Edit effect"));
    state.previewSetEffectParam(track, clip, 0, QStringLiteral("contrast"), 1.2);
    state.previewSetEffectParam(track, clip, 0, QStringLiteral("contrast"), 1.8);
    state.commitPreviewDrag();
    effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    const QVariantList previewParams = effects.first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(previewParams.first().toMap().value(QStringLiteral("value")).toDouble(), 1.8);
    QVERIFY(state.undoAvailable());
    state.undo();
    effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    const QVariantList undoneParams = effects.first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(undoneParams.first().toMap().value(QStringLiteral("value")).toDouble(), 2.5);

    state.removeEffect(track, clip, 0);
    QCOMPARE(state.selectedClipData().value(QStringLiteral("effects")).toList().size(), 0);
    QCOMPARE(state.selectedClipEffects().size(), 0);
}

void EditorStateTest::effectParamKeyframes()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Animate me"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    state.addEffect(track, clip, QStringLiteral("adjust.contrast"));

    const QString prop = QStringLiteral("fx.0.contrast");

    // Reading a never-keyed param must not mint a track behind the const accessor.
    QCOMPARE(state.clipKeyframes(track, clip, prop).size(), 0);
    QVariantList params = state.selectedClipData().value(QStringLiteral("effects")).toList()
                              .first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(params.first().toMap().value(QStringLiteral("keyframes")).toMap()
                 .value(QStringLiteral("points")).toList().size(), 0);
    QCOMPARE(params.first().toMap().value(QStringLiteral("prop")).toString(), prop);

    // Auto-key off: a slider drag moves the static value without animating the param.
    state.setAutoKeyEnabled(false);
    state.beginPreviewDrag(QStringLiteral("Edit effect"));
    state.previewSetClipKeyframe(track, clip, prop, 0.0, 1.4);
    state.commitPreviewDrag();
    QCOMPARE(state.clipKeyframes(track, clip, prop).size(), 0);
    params = state.selectedClipData().value(QStringLiteral("effects")).toList()
                 .first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(params.first().toMap().value(QStringLiteral("value")).toDouble(), 1.4);

    // The diamond forces a key, and the static value tracks it.
    state.setClipKeyframe(track, clip, prop, 0.0, 0.5);
    state.setClipKeyframe(track, clip, prop, 2.0, 2.5);
    QVariantList keys = state.clipKeyframes(track, clip, prop);
    QCOMPARE(keys.size(), 2);
    QCOMPARE(keys.first().toMap().value(QStringLiteral("value")).toDouble(), 0.5);
    QCOMPARE(keys.last().toMap().value(QStringLiteral("seconds")).toDouble(), 2.0);

    // Easing applies to the key at the playhead, not to the whole track, and is reported
    // back on that key rather than on the track it belongs to.
    state.setPlayheadSeconds(0.0);
    state.setKeyframeInterpolation(track, clip, prop, QStringLiteral("ease"));
    keys = state.clipKeyframes(track, clip, prop);
    QCOMPARE(keys.first().toMap().value(QStringLiteral("easing")).toString(),
             QStringLiteral("ease"));
    QVERIFY(!keys.first().toMap().value(QStringLiteral("custom")).toBool());
    // The key at 2.0s was not touched, so it keeps the straight-line default.
    QCOMPARE(keys.last().toMap().value(QStringLiteral("easing")).toString(),
             QStringLiteral("linear"));

    // Dragging a tangent puts the key into a shape no preset describes.
    state.setKeyframeTangents(track, clip, prop, 0.0, 0.0, 0.0, 0.4, 1.7, false);
    keys = state.clipKeyframes(track, clip, prop);
    QVERIFY(keys.first().toMap().value(QStringLiteral("custom")).toBool());
    QVERIFY(std::abs(keys.first().toMap().value(QStringLiteral("outDx")).toDouble() - 0.4) < 1e-6);

    // Hold overrides the tangents when evaluated but does not destroy them, so switching it
    // back off restores the shape the user drew instead of silently flattening it.
    state.setKeyframeHold(track, clip, prop, 0.0, true);
    keys = state.clipKeyframes(track, clip, prop);
    QVERIFY(keys.first().toMap().value(QStringLiteral("hold")).toBool());
    QCOMPARE(keys.first().toMap().value(QStringLiteral("easing")).toString(),
             QStringLiteral("hold"));

    state.setKeyframeHold(track, clip, prop, 0.0, false);
    keys = state.clipKeyframes(track, clip, prop);
    QVERIFY(!keys.first().toMap().value(QStringLiteral("hold")).toBool());
    QVERIFY(std::abs(keys.first().toMap().value(QStringLiteral("outDx")).toDouble() - 0.4) < 1e-6);
    QVERIFY(keys.first().toMap().value(QStringLiteral("custom")).toBool());

    state.removeClipKeyframe(track, clip, prop, 2.0);
    QCOMPARE(state.clipKeyframes(track, clip, prop).size(), 1);

    // An out-of-range effect index resolves to nothing rather than crashing.
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("fx.9.contrast")).size(), 0);
}

void EditorStateTest::effectRemovalRemapsGraphSelection()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Two effects"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    state.addEffect(track, clip, QStringLiteral("adjust.contrast"));
    state.addEffect(track, clip, QStringLiteral("adjust.brightness"));

    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("x"));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.0.contrast"));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.1.brightness"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("x"), QStringLiteral("fx.0.contrast"),
                            QStringLiteral("fx.1.brightness") }));

    // Dropping effect 0 must retire its entry and renumber the one above it, or brightness would
    // inherit contrast's folded-away state.
    state.removeEffect(track, clip, 0);
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("x"), QStringLiteral("fx.0.brightness") }));
}

void EditorStateTest::previewSetTextRectScalesPixelSize()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Resize me"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();

    const QVariantMap before = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    const int basePixelSize = before.value(QStringLiteral("pixelSize")).toInt();
    QVERIFY(basePixelSize > 0);

    // Dragging a corner handle scales the glyphs with the box, not just the wrap container.
    state.beginPreviewDrag(QStringLiteral("Resize text"));
    state.previewSetTextRect(track, clip, 10.0, 20.0, 800.0, 600.0, basePixelSize * 2);
    state.commitPreviewDrag();

    QVariantMap after = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(after.value(QStringLiteral("pixelSize")).toInt(), basePixelSize * 2);
    const QVariantMap keys = state.selectedClipData().value(QStringLiteral("keyframes")).toMap();
    const QVariantList widthKeys =
        keys.value(QStringLiteral("width")).toMap().value(QStringLiteral("points")).toList();
    QCOMPARE(widthKeys.first().toMap().value(QStringLiteral("value")).toDouble(), 800.0);

    // The size and the rect land in one undo entry, so a single undo restores both.
    state.undo();
    after = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(after.value(QStringLiteral("pixelSize")).toInt(), basePixelSize);
}

void EditorStateTest::fontCatalogIsExposedToQml()
{
    AssetLibrary library;
    AppController state(&library);

    const QVariantList catalog = state.fontCatalog();
    if (catalog.isEmpty())
        QSKIP("font bundle not present — see recipes/fetch-fonts.py in drift-addons");

    // Every key the FontPicker delegate and the weight combo read must actually arrive.
    for (const QVariant &entry : catalog) {
        const QVariantMap family = entry.toMap();
        QVERIFY(!family.value(QStringLiteral("family")).toString().isEmpty());
        QVERIFY(!family.value(QStringLiteral("qtFamily")).toString().isEmpty());
        QVERIFY(!family.value(QStringLiteral("categoryLabel")).toString().isEmpty());
        QVERIFY(!family.value(QStringLiteral("weights")).toList().isEmpty());
    }

    const auto findFamily = [&](const QString &name) {
        for (const QVariant &entry : catalog) {
            if (entry.toMap().value(QStringLiteral("family")).toString() == name)
                return entry.toMap();
        }
        return QVariantMap{};
    };

    const QVariantMap anton = findFamily(QStringLiteral("Anton"));
    QCOMPARE(anton.value(QStringLiteral("weights")).toList().size(), 1);
    QCOMPARE(anton.value(QStringLiteral("hasItalic")).toBool(), false);

    const QVariantMap montserrat = findFamily(QStringLiteral("Montserrat"));
    QVERIFY(montserrat.value(QStringLiteral("weights")).toList().size() >= 6);
    QCOMPARE(montserrat.value(QStringLiteral("hasItalic")).toBool(), true);

    QVERIFY(!state.fontCategories().isEmpty());
}

void EditorStateTest::effectBrowserCategoriesAndApply()
{
    AssetLibrary library;
    AppController state(&library);

    // Built-in categories (color first), plus extras contributed by bundled effect packages.
    // Counting exactly would just be a tally of how many packages ship today.
    const QVariantList categories = state.effectCategories();
    QVERIFY(categories.size() >= 5);
    QCOMPARE(categories.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("color"));
    QCOMPARE(categories.first().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("Color"));

    const QVariantList catalog = state.effectCatalog();
    QVERIFY(catalog.size() >= 16);

    QSet<QString> categoryIds;
    for (const QVariant &category : categories)
        categoryIds.insert(category.toMap().value(QStringLiteral("id")).toString());

    for (const QVariant &entry : catalog) {
        const QVariantMap preset = entry.toMap();
        QVERIFY(categoryIds.contains(preset.value(QStringLiteral("category")).toString()));
        QVERIFY(!preset.value(QStringLiteral("categoryLabel")).toString().isEmpty());
    }

    state.addTextClip(QStringLiteral("FX"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QVERIFY(track >= 0);
    QVERIFY(clip >= 0);

    state.addEffect(track, clip, QStringLiteral("rgb_split"));
    QVariantList effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    QCOMPARE(effects.size(), 1);
    QCOMPARE(state.selectedClipEffects().size(), 1);
    QCOMPARE(effects.first().toMap().value(QStringLiteral("catalogId")).toString(),
             QStringLiteral("rgb_split"));
    QCOMPARE(effects.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("RGB Split"));
    QCOMPARE(state.project()->tracks()[track].clips[clip].effects.size(), 1);

    state.removeEffect(track, clip, 0);
    QCOMPARE(state.selectedClipData().value(QStringLiteral("effects")).toList().size(), 0);
    QCOMPARE(state.selectedClipEffects().size(), 0);
    QCOMPARE(state.project()->tracks()[track].clips[clip].effects.size(), 0);
}

void EditorStateTest::multiSelectClipboardGuidesAndShortcuts()
{
    AssetLibrary library;
    AppController state(&library);

    state.addTextClip(QStringLiteral("A"), 0.0);
    state.addTextClip(QStringLiteral("B"), 2.0);

    const int track = state.selectedTrack();
    QVERIFY(track >= 0);

    // Build a two-clip selection.
    state.selectClip(track, 0);
    state.addToSelection(track, 1);
    QCOMPARE(state.selection().size(), 2);
    QVERIFY(state.selectionContains(track, 0));
    QVERIFY(state.selectionContains(track, 1));

    // Copy/paste at playhead keeps both clips.
    state.setPlayheadSeconds(10.0);
    state.copySelection();
    state.pasteAtPlayhead();
    QCOMPARE(state.tracks().at(track).toMap().value(QStringLiteral("clips")).toList().size(), 4);

    // Nudge and cut do not crash and remain undoable.
    state.nudgeSelection(0.25);
    QVERIFY(state.undoAvailable());
    state.cutSelection();
    QVERIFY(state.tracks().at(track).toMap().value(QStringLiteral("clips")).toList().size() <= 2);

    // Guides state is writable.
    state.setGuidesEnabled(true);
    QCOMPARE(state.guidesEnabled(), true);
    state.setGuideType(QStringLiteral("safe"));
    QCOMPARE(state.guideType(), QStringLiteral("safe"));

    // Shortcut/action layer wiring.
    state.setShortcut(QStringLiteral("nudgeRight"), QStringLiteral("Ctrl+Alt+Right"));
    QCOMPARE(state.shortcutFor(QStringLiteral("nudgeRight")), QStringLiteral("Ctrl+Alt+Right"));

    bool found = false;
    for (const QVariant &entry : state.actions()) {
        const QVariantMap action = entry.toMap();
        if (action.value(QStringLiteral("id")).toString() != QStringLiteral("nudgeRight"))
            continue;
        QCOMPARE(action.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Alt+Right"));
        found = true;
    }
    QVERIFY(found);

    state.triggerAction(QStringLiteral("toggleGuides"));
    QCOMPARE(state.guidesEnabled(), false);
}

static void appendAdjacentShapeClips(drift::Project &project, drift::TimeUs gapUs = 0)
{
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("clip-a");
    clipA.type = drift::ClipType::Shape;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);

    drift::Clip clipB;
    clipB.id = QStringLiteral("clip-b");
    clipB.type = drift::ClipType::Shape;
    clipB.timelineStart = clipA.timelineEnd() + gapUs;
    clipB.timelineDuration = drift::secondsToUs(2.0);

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);
}

static void appendCombinedVideoClip(drift::Project &project)
{
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-video");
    asset.path = QStringLiteral("/tmp/video.mp4");
    asset.name = QStringLiteral("Video");
    asset.kind = drift::MediaKind::Video;
    asset.durationUs = drift::secondsToUs(4.0);
    asset.sampleRate = 48000;
    asset.channels = 2;
    project.assets().insert(asset.id, asset);
    project.assetOrder().append(asset.id);

    drift::Clip video;
    video.id = QStringLiteral("clip-video");
    video.assetId = asset.id;
    video.type = drift::ClipType::Video;
    video.name = asset.name;
    video.path = asset.path;
    video.timelineStart = 0;
    video.timelineDuration = drift::secondsToUs(4.0);
    video.srcIn = 0;
    video.srcOut = drift::secondsToUs(4.0);

    project.tracks()[0].clips.append(video);
}

static void appendLinkedVideoAudioPair(drift::Project &project)
{
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks().append(drift::Track{.type = drift::TrackType::Audio});

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-video");
    asset.path = QStringLiteral("/tmp/video.mp4");
    asset.name = QStringLiteral("Video");
    asset.kind = drift::MediaKind::Video;
    asset.durationUs = drift::secondsToUs(4.0);
    asset.sampleRate = 48000;
    asset.channels = 2;
    project.assets().insert(asset.id, asset);
    project.assetOrder().append(asset.id);

    const QString linkId = QStringLiteral("link-1");

    drift::Clip video;
    video.id = QStringLiteral("clip-video");
    video.linkId = linkId;
    video.suppressEmbeddedAudio = true;
    video.assetId = asset.id;
    video.type = drift::ClipType::Video;
    video.name = asset.name;
    video.path = asset.path;
    video.timelineStart = 0;
    video.timelineDuration = drift::secondsToUs(4.0);
    video.srcIn = 0;
    video.srcOut = drift::secondsToUs(4.0);

    drift::Clip audio;
    audio.id = QStringLiteral("clip-audio");
    audio.linkId = linkId;
    audio.assetId = asset.id;
    audio.type = drift::ClipType::Audio;
    audio.name = asset.name;
    audio.path = asset.path;
    audio.timelineStart = 0;
    audio.timelineDuration = drift::secondsToUs(4.0);
    audio.srcIn = 0;
    audio.srcOut = drift::secondsToUs(4.0);

    project.tracks()[0].clips.append(video);
    project.tracks()[1].clips.append(audio);
}

// Separating audio and unlinking are two different operations: a combined clip carries its audio
// inside the video clip and has nothing to unlink, and separating it produces a linked pair that
// still moves together until the link itself is broken.
void EditorStateTest::separateAudioFromCombinedClip()
{
    AssetLibrary library;
    AppController state(&library);
    appendCombinedVideoClip(*state.project());

    QCOMPARE(state.project()->tracks().size(), 1);
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);

    state.selectClip(0, 0);
    QVERIFY(state.canSeparateAudioSelection());
    QVERIFY(!state.canUnlinkSelection());
    QVERIFY(state.selectionContains(0, 0));

    state.moveClip(0, 0, 2.0);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineStart, drift::secondsToUs(2.0));

    state.selectClip(0, 0);
    state.separateAudioFromSelection();
    QCOMPARE(state.project()->tracks().size(), 2);
    QCOMPARE(state.project()->tracks().at(1).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).timelineStart, drift::secondsToUs(2.0));
    // The video must stop playing the audio it just handed over.
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).suppressEmbeddedAudio, true);

    // The two halves come out linked, so there is now something to unlink.
    state.selectClip(0, 0);
    QVERIFY(state.canUnlinkSelection());
    QVERIFY(!state.canSeparateAudioSelection());
}

void EditorStateTest::linkedFadeCurveSyncsPartner()
{
    AssetLibrary library;
    AppController state(&library);
    appendLinkedVideoAudioPair(*state.project());

    state.setClipFade(0, 0, 0.8, 0.4);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).fadeInUs, drift::secondsToUs(0.8));
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).fadeInUs, drift::secondsToUs(0.8));
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).fadeOutUs, drift::secondsToUs(0.4));

    state.setClipFadeCurve(0, 0, QStringLiteral("equalPower"));
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).fadeCurve, drift::FadeCurve::EqualPower);
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).fadeCurve, drift::FadeCurve::EqualPower);

    state.beginPreviewDrag(QStringLiteral("Adjust fade"));
    state.previewSetClipFade(0, 0, 1.0, 0.5);
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).fadeInUs, drift::secondsToUs(1.0));
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).fadeOutUs, drift::secondsToUs(0.5));
    state.commitPreviewDrag();
}

void EditorStateTest::customFadeCurveSessionApplyAndCancel()
{
    AssetLibrary library;
    AppController state(&library);
    appendLinkedVideoAudioPair(*state.project());
    state.setClipFade(0, 0, 1.0, 0.0);
    state.setClipFadeCurve(0, 0, QStringLiteral("linear"));

    state.beginFadeCurveSession(0, 0);
    QVERIFY(state.fadeCurveSessionActive());
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).fadeCurve, drift::FadeCurve::Custom);

    state.setFadeCurvePoints(QVariantList{
        QVariantMap{{QStringLiteral("t"), 0.0}, {QStringLiteral("g"), 0.0}},
        QVariantMap{{QStringLiteral("t"), 0.5}, {QStringLiteral("g"), 0.2}},
        QVariantMap{{QStringLiteral("t"), 1.0}, {QStringLiteral("g"), 1.0}},
    });
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).fadeCurve, drift::FadeCurve::Custom);
    QVERIFY(qAbs(state.project()->tracks().at(0).clips.at(0).fadeShape.gainAt(0.5) - 0.2) < 1e-6);

    state.endFadeCurveSession();
    QVERIFY(!state.fadeCurveSessionActive());
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).fadeCurve, drift::FadeCurve::Linear);

    state.beginFadeCurveSession(0, 0);
    state.setFadeCurvePoints(QVariantList{
        QVariantMap{{QStringLiteral("t"), 0.0}, {QStringLiteral("g"), 0.0}},
        QVariantMap{{QStringLiteral("t"), 0.5}, {QStringLiteral("g"), 0.75}},
        QVariantMap{{QStringLiteral("t"), 1.0}, {QStringLiteral("g"), 1.0}},
    });
    state.applyFadeCurve();
    QVERIFY(!state.fadeCurveSessionActive());
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).fadeCurve, drift::FadeCurve::Custom);
    QVERIFY(qAbs(state.project()->tracks().at(0).clips.at(0).fadeShape.gainAt(0.5) - 0.75) < 1e-6);
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).fadeCurve, drift::FadeCurve::Custom);
    QVERIFY(qAbs(state.project()->tracks().at(1).clips.at(0).fadeShape.gainAt(0.5) - 0.75) < 1e-6);
}

void EditorStateTest::linkedAudioUnlinkAndMove()
{
    AssetLibrary library;
    AppController state(&library);
    appendLinkedVideoAudioPair(*state.project());

    QCOMPARE(state.project()->tracks().size(), 2);
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(1).clips.size(), 1);

    state.selectClip(0, 0);
    QVERIFY(state.canUnlinkSelection());
    QVERIFY(state.selectionContains(0, 0));

    // Linked: moving the video carries its audio along.
    state.moveClip(0, 0, 2.0);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineStart, drift::secondsToUs(2.0));
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).timelineStart, drift::secondsToUs(2.0));

    state.selectClip(0, 0);
    state.unlinkSelectedClips();
    QVERIFY(!state.canUnlinkSelection());

    // Unlinked: the audio stays where it was.
    state.moveClip(0, 0, 0.0);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineStart, drift::secondsToUs(0.0));
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).timelineStart, drift::secondsToUs(2.0));
}

void EditorStateTest::addTransitionBetweenAdjacentClips()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project(), 500);

    state.selectClip(0, 0);
    state.addTransition(0, 0, QStringLiteral("wipe_left"), 0.75);

    const QVariantMap transition = state.transitionBetweenClips(0, 0);
    QVERIFY(!transition.isEmpty());
    QCOMPARE(transition.value(QStringLiteral("kind")).toString(), QStringLiteral("wipe_left"));
    QCOMPARE(transition.value(QStringLiteral("duration")).toDouble(), 0.75);
}

void EditorStateTest::addTransitionBetweenAdjacentTextClips()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("One"), 0.0);
    state.setClipDuration(0, 0, 2.0);
    state.addTextClip(QStringLiteral("Two"), 2.0);
    state.setClipDuration(0, 1, 2.0);

    QCOMPARE(state.project()->tracks().at(0).type, drift::TrackType::Text);
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 2);

    state.addTransition(0, 0, QStringLiteral("crossfade"), 0.5);
    const QVariantMap transition = state.transitionBetweenClips(0, 0);
    QVERIFY(!transition.isEmpty());
    QCOMPARE(transition.value(QStringLiteral("kind")).toString(), QStringLiteral("crossfade"));
    QCOMPARE(transition.value(QStringLiteral("duration")).toDouble(), 0.5);
}

void EditorStateTest::clipAnimationUndoRestoresKind()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Title"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).animIn.kind, drift::ClipAnimKind::None);

    state.setClipAnimation(track, clip, QStringLiteral("animIn"),
                           QVariantMap{{QStringLiteral("kind"), QStringLiteral("pop")},
                                       {QStringLiteral("duration"), 0.4},
                                       {QStringLiteral("curve"), QStringLiteral("smooth")}});
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).animIn.kind, drift::ClipAnimKind::Pop);
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).animIn.durationUs,
             drift::secondsToUs(0.4));
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).animIn.curve, drift::FadeCurve::Smooth);
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).fadeInUs, drift::TimeUs{0});

    state.setClipAnimation(track, clip, QStringLiteral("animIn"),
                           QVariantMap{{QStringLiteral("kind"), QStringLiteral("fade")},
                                       {QStringLiteral("duration"), 0.6},
                                       {QStringLiteral("curve"), QStringLiteral("equalPower")}});
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).animIn.kind, drift::ClipAnimKind::Fade);
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).fadeInUs, drift::secondsToUs(0.6));
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).fadeCurve, drift::FadeCurve::EqualPower);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).animIn.kind, drift::ClipAnimKind::Pop);
    state.undo();
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).animIn.kind, drift::ClipAnimKind::None);
}

void EditorStateTest::setTransitionKindAndDurationPersist()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project());

    state.addTransition(0, 0, QStringLiteral("crossfade"), 0.5);
    const QString transitionId = state.transitionBetweenClips(0, 0).value(QStringLiteral("id")).toString();
    QVERIFY(!transitionId.isEmpty());

    state.setTransitionKind(0, transitionId, QStringLiteral("zoom_in"));
    state.setTransitionDuration(0, transitionId, 1.25);

    const QVariantMap transition = state.transitionBetweenClips(0, 0);
    QCOMPARE(transition.value(QStringLiteral("kind")).toString(), QStringLiteral("zoom_in"));
    QCOMPARE(transition.value(QStringLiteral("duration")).toDouble(), 1.25);
}

void EditorStateTest::replaceTransitionOnDrop()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project());

    state.addTransition(0, 0, QStringLiteral("crossfade"), 0.5);
    const QString firstId = state.transitionBetweenClips(0, 0).value(QStringLiteral("id")).toString();
    QVERIFY(!firstId.isEmpty());

    state.addTransition(0, 0, QStringLiteral("wipe_right"), 0.5);
    const QVariantMap replaced = state.transitionBetweenClips(0, 0);
    QCOMPARE(replaced.value(QStringLiteral("id")).toString(), firstId);
    QCOMPARE(replaced.value(QStringLiteral("kind")).toString(), QStringLiteral("wipe_right"));
    QCOMPARE(state.project()->tracks().at(0).transitions.size(), 1);
}

void EditorStateTest::overlapAutoAppliesCrossfade()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project(), -drift::secondsToUs(0.5)); // 0.5s physical overlap

    // Overlap is off by default; keep it on so the no-op move below does not push the
    // already-overlapping clips apart before sync can create the crossfade.
    state.setAllowClipOverlap(true);
    // Overlap sync runs on finishEdit; nudge via a no-op-ish move to trigger it.
    state.moveClip(0, 1, drift::usToSeconds(state.project()->tracks().at(0).clips.at(1).timelineStart));

    const QVariantMap transition = state.transitionBetweenClips(0, 0);
    QVERIFY(!transition.isEmpty());
    QCOMPARE(transition.value(QStringLiteral("kind")).toString(), QStringLiteral("crossfade"));
    QCOMPARE(transition.value(QStringLiteral("overlapping")).toBool(), true);
    QCOMPARE(transition.value(QStringLiteral("duration")).toDouble(), 0.5);

    state.addTransition(0, 0, QStringLiteral("dip"), 0.5);
    QCOMPARE(state.transitionBetweenClips(0, 0).value(QStringLiteral("kind")).toString(),
             QStringLiteral("dip"));
}

void EditorStateTest::keyframeGraphPropertySelection()
{
    AssetLibrary library;
    AppController state(&library);
    // Nothing is folded away to begin with: the strip shows every animated property of the clip.
    QVERIFY(state.keyframeGraphHiddenProperties().isEmpty());

    // A chip click hides that one curve and leaves the rest alone.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("y"));
    QCOMPARE(state.keyframeGraphHiddenProperties(), QStringList { QStringLiteral("y") });
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("opacity"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("y"), QStringLiteral("opacity") }));

    // Clicking the same chip again brings the curve back.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("y"));
    QCOMPARE(state.keyframeGraphHiddenProperties(), QStringList { QStringLiteral("opacity") });

    // Editing a property un-hides it, and is a no-op for one that was never hidden.
    state.showKeyframeGraphProperty(QStringLiteral("opacity"));
    QVERIFY(state.keyframeGraphHiddenProperties().isEmpty());
    state.showKeyframeGraphProperty(QStringLiteral("width"));
    QVERIFY(state.keyframeGraphHiddenProperties().isEmpty());

    // Effect param keys are verbatim manifest identifiers and are often camelCase, so they must
    // survive normalization intact while bare transform names stay case-insensitive.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.0.u_blurRadius"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             QStringList { QStringLiteral("fx.0.u_blurRadius") });
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("X"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("fx.0.u_blurRadius"), QStringLiteral("x") }));

    // Unknown and malformed keys never reach the strip.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("bogus"));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx."));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.a.b"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("fx.0.u_blurRadius"), QStringLiteral("x") }));
}

// The inspector row's label switches a property's animation off without discarding its keys: the
// property freezes at its first key, and switching it back on restores the animation exactly.
void EditorStateTest::keyframesCanBeDisabledPerProperty()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Animate me"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();

    state.setClipKeyframe(track, clip, QStringLiteral("opacity"), 0.0, 0.0);
    state.setClipKeyframe(track, clip, QStringLiteral("opacity"), 2.0, 1.0);
    QCOMPARE(state.clipAnimatedProperties(track, clip), QStringList { QStringLiteral("opacity") });
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity")));
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 2.0, 1.0), 1.0);

    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity"));
    QVERIFY(!state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity")));
    // Frozen at the first key, everywhere.
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 2.0, 1.0), 0.0);
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 1.0, 1.0), 0.0);
    // The keys themselves survive, so the strip still has a curve to draw.
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("opacity")).size(), 2);
    QCOMPARE(state.clipAnimatedProperties(track, clip), QStringList { QStringLiteral("opacity") });

    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity"));
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 2.0, 1.0), 1.0);

    // Undo takes the switch back with it.
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity"));
    state.undo();
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity")));

    // A property with no keys has no animation to switch off.
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation"));
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation")));
    QVERIFY(!state.clipAnimatedProperties(track, clip).contains(QStringLiteral("rotation")));

    // A single key is the case where switching off changes nothing on screen, so it is also the
    // one most likely to look like a dead click: it still has to toggle both ways.
    state.setClipKeyframe(track, clip, QStringLiteral("rotation"), 1.0, 45.0);
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation"));
    QVERIFY(!state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation")));
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation"));
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation")));
}

// End-to-end noise removal through the controller: the whole clip is rendered on a worker thread
// and the result lands on a new audio track directly above the source, as one undoable edit.
void EditorStateTest::denoiseAddsCleanedClipOnTrackAbove()
{
    const QString modelDir = QString::fromUtf8(DRIFT_TEST_DENOISE_MODEL_DIR);
    if (!QDir(modelDir).exists())
        QSKIP("DeepFilterNet3 model not installed");
    qputenv("DRIFT_DENOISE_MODEL_DIR", modelDir.toUtf8());

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("noisy.wav"));
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"),
                        QStringLiteral("anoisesrc=d=3:c=white:r=48000:a=0.1"),
                        QStringLiteral("-ac"), QStringLiteral("2"),
                        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), source});
    QVERIFY(proc.waitForFinished(60000) && proc.exitCode() == 0);

    AssetLibrary library;
    AppController state(&library);

    drift::Project &project = *state.project();
    drift::Track track{.type = drift::TrackType::Audio};
    drift::Clip clip;
    clip.id = QStringLiteral("src-clip");
    clip.type = drift::ClipType::Audio;
    clip.path = source;
    clip.name = QStringLiteral("Noisy");
    clip.timelineStart = drift::secondsToUs(1.5);
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(3.0);
    track.clips.append(clip);
    // A new project comes with default tracks; drop them so the indices below are the ones set up
    // here. A video track sits above the audio one, so "directly above the clip's own track" is
    // distinguishable from "at the very top of the timeline".
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks().append(track);
    const int sourceTrack = 1;

    QSignalSpy finished(&state, &AppController::denoiseFinished);
    state.applyDenoise(sourceTrack, 0);
    QVERIFY2(finished.wait(180000), "denoise did not finish");
    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.at(0).at(0).toBool(),
             qPrintable(finished.at(0).at(1).toString()));

    QCOMPARE(project.tracks().size(), 3);
    // Inserted at the source's index, pushing it down — not at the top of the timeline.
    QCOMPARE(project.tracks().at(0).type, drift::TrackType::Video);
    QCOMPARE(project.tracks().at(sourceTrack).type, drift::TrackType::Audio);
    QCOMPARE(project.tracks().at(sourceTrack + 1).clips.at(0).id, QStringLiteral("src-clip"));

    const drift::Clip &out = project.tracks().at(sourceTrack).clips.at(0);
    QVERIFY(out.id != clip.id);
    QVERIFY(out.name.contains(QStringLiteral("denoised")));
    QCOMPARE(out.type, drift::ClipType::Audio);
    QVERIFY(out.assetId.isEmpty());
    QVERIFY2(QFileInfo::exists(out.path), qPrintable(out.path));
    QVERIFY(out.path.endsWith(QStringLiteral(".flac")));
    // Stays put on the timeline, and the source window is rebased into the new media.
    QCOMPARE(out.timelineStart, clip.timelineStart);
    QCOMPARE(out.timelineDuration, clip.timelineDuration);
    QCOMPARE(out.srcIn, drift::TimeUs(0));
    QCOMPARE(out.srcOut, clip.srcOut - clip.srcIn);

    // One undoable edit: the track and the clip go together.
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(project.tracks().size(), 2);
    QCOMPARE(project.tracks().at(sourceTrack).clips.at(0).id, QStringLiteral("src-clip"));

    QFile::remove(out.path);
}

void EditorStateTest::shapeStylePartialUpdateAndUndo()
{
    AssetLibrary library;
    AppController state(&library);

    // "circle" is a catalog id, not a ShapeKind name — it must resolve to an ellipse in a square
    // box, since every shape now simply fills its layout rect.
    state.addShapeClip(QStringLiteral("circle"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QVERIFY(track >= 0);
    QVERIFY(clip >= 0);

    QVariantMap style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("kind")).toString(), QStringLiteral("ellipse"));
    QCOMPARE(state.selectedClipData().value(QStringLiteral("width")).toDouble(),
             state.selectedClipData().value(QStringLiteral("height")).toDouble());

    // Partial update only touches the given keys.
    state.setShapeStyle(track, clip,
                        QVariantMap{{"fillKind", QStringLiteral("linear")},
                                    {"fill", QStringLiteral("#ff00ff00")},
                                    {"strokeStyle", QStringLiteral("dash")}});
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("fillKind")).toString(), QStringLiteral("linear"));
    QCOMPARE(style.value(QStringLiteral("fill")).toString(), QStringLiteral("#ff00ff00"));
    QCOMPARE(style.value(QStringLiteral("strokeStyle")).toString(), QStringLiteral("dash"));
    QCOMPARE(style.value(QStringLiteral("strokeWidth")).toDouble(), 4.0); // untouched

    // Out-of-range values are clamped rather than stored.
    state.setShapeStyle(track, clip, QVariantMap{{"points", 900}, {"innerRatio", -3.0}});
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("points")).toInt(), 60);
    QCOMPARE(style.value(QStringLiteral("innerRatio")).toDouble(), 0.05);

    // Each committed change is its own undo step.
    state.undo();
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("points")).toInt(), 5);
    QCOMPARE(style.value(QStringLiteral("fillKind")).toString(), QStringLiteral("linear"));

    state.undo();
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("fillKind")).toString(), QStringLiteral("solid"));
}

// A ramp on an audio clip goes through exactly the same session, apply and replace flow a video
// clip does — the only difference is that the editor has a waveform to draw instead of a filmstrip.
void EditorStateTest::speedCurveOnAudioClipRetimesAndReplaces()
{
    AssetLibrary library;
    AppController state(&library);

    drift::Project &project = *state.project();
    drift::Clip clip;
    clip.id = QStringLiteral("clip-audio");
    clip.type = drift::ClipType::Audio;
    clip.name = QStringLiteral("Tone");
    clip.path = QStringLiteral("/tmp/tone.wav");
    clip.timelineStart = drift::secondsToUs(1.0);
    clip.timelineDuration = drift::secondsToUs(4.0);
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(4.0);
    clip.volume.setKeyframe(0, 1.0);
    clip.volume.setKeyframe(drift::secondsToUs(4.0), 0.0);

    drift::Track track{.type = drift::TrackType::Audio};
    track.clips.append(clip);
    // A video track above the audio one, so "directly above the clip's own track" is
    // distinguishable from "at the very top".
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks().append(track);
    const int sourceTrack = 1;

    state.beginSpeedCurveSession(sourceTrack, 0);
    QVERIFY2(state.speedCurveSessionActive(), qPrintable(state.lastMessage()));
    QCOMPARE(state.speedCurveClipPath(), clip.path);
    // No filmstrip on an audio clip, which is what makes the editor fall back to the waveform.
    QVERIFY(state.speedCurveFilmstripPath().isEmpty());

    // Ramp from half speed up to double, so the retimed duration is neither the source length nor
    // a simple scale of it.
    state.setSpeedCurvePoints(QVariantList{
        QVariantMap{{QStringLiteral("pos"), 0.0}, {QStringLiteral("speed"), 0.5}},
        QVariantMap{{QStringLiteral("pos"), 1.0}, {QStringLiteral("speed"), 2.0}},
    });
    // Timeline length is the integral of 1/speed over the source, (2/3)·ln4 ≈ 0.924 of it here —
    // not the endpoint speed and not the average of the two.
    const double retimedSeconds = state.speedCurveRetimedDuration();
    QVERIFY2(std::fabs(retimedSeconds - 3.697) < 0.12, qPrintable(QString::number(retimedSeconds)));

    state.applySpeedCurve();

    // The retimed copy lands on a new audio track directly above the source track.
    QCOMPARE(project.tracks().size(), 3);
    QCOMPARE(project.tracks().at(1).type, drift::TrackType::Audio);
    QCOMPARE(project.tracks().at(1).clips.size(), 1);

    const drift::Clip &retimed = project.tracks().at(1).clips.at(0);
    QVERIFY(retimed.hasSpeedCurve());
    QCOMPARE(retimed.type, drift::ClipType::Audio);
    QCOMPARE(retimed.timelineStart, clip.timelineStart);
    QCOMPARE(retimed.srcIn, clip.srcIn);
    QCOMPARE(retimed.srcOut, clip.srcOut);
    QCOMPARE(retimed.timelineDuration,
             retimed.speedCurve.retimedDurationUs(retimed.srcOut - retimed.srcIn));
    // Volume automation has to follow the retime, or the fade would land at the wrong moment. The
    // key that sat at the clip's old end belongs at its new one, not at the same wall-clock offset.
    QCOMPARE(retimed.volume.keyframes().size(), clip.volume.keyframes().size());
    QCOMPARE(retimed.volume.keyframes().firstKey(), drift::TimeUs{0});
    QVERIFY2(std::llabs(retimed.volume.keyframes().lastKey() - retimed.timelineDuration) < 100'000,
             qPrintable(QString::number(retimed.volume.keyframes().lastKey())));

    // The clip it was made from is gone rather than left playing underneath at the original rate.
    QCOMPARE(project.tracks().at(2).clips.size(), 0);

    state.undo();
    QCOMPARE(project.tracks().size(), 2);
    QCOMPARE(project.tracks().at(1).clips.size(), 1);
    QVERIFY(!project.tracks().at(1).clips.at(0).hasSpeedCurve());
}

// The speed-curve editor plots its ramp over the clip's trimmed source window, so the waveform
// behind it has to cover that window and nothing else. Drawing the whole file put the audio under
// the wrong part of the curve on any clip that had been trimmed.
void EditorStateTest::waveformPeaksForSourceRangeSlicesToTheTrimmedWindow()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("half-tone.wav"));
    // Four seconds: a tone for the first two, silence for the last two. Concatenated from two
    // sources rather than gated with volume=enable, which leaves the tail audible.
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-y"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("anullsrc=r=48000:cl=mono:d=2"),
                        QStringLiteral("-filter_complex"),
                        QStringLiteral("[0:a][1:a]concat=n=2:v=0:a=1[out]"),
                        QStringLiteral("-map"), QStringLiteral("[out]"),
                        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), source});
    QVERIFY(proc.waitForFinished(60000) && proc.exitCode() == 0);

    AssetLibrary library;
    AppController state(&library);

    // The first call kicks off the decode and comes back empty; the peaks land on waveformReady.
    QSignalSpy ready(&state, &AppController::waveformReady);
    QVERIFY(state.waveformPeaksForSourceRange(source, 0.0, 2.0).isEmpty());
    QVERIFY2(ready.wait(60000), "waveform decode did not finish");

    const QVariantList loud = state.waveformPeaksForSourceRange(source, 0.0, 2.0);
    const QVariantList quiet = state.waveformPeaksForSourceRange(source, 2.0, 2.0);
    QVERIFY(loud.size() > 10);
    QCOMPARE(quiet.size(), loud.size());

    // reduceDensePeaks floors silence at 0.05 so it still draws as a hairline.
    for (const QVariant &v : loud)
        QVERIFY2(v.toDouble() > 0.2, qPrintable(QString::number(v.toDouble())));
    for (const QVariant &v : quiet)
        QVERIFY2(v.toDouble() <= 0.06, qPrintable(QString::number(v.toDouble())));

    // The whole-file call is what the editor used to draw: it spans both halves, so it cannot be
    // standing in for either window.
    const QVariantList whole = state.waveformPeaks(source);
    QVERIFY(whole.size() > 10);
    double wholeMin = 1.0;
    double wholeMax = 0.0;
    for (const QVariant &v : whole) {
        wholeMin = qMin(wholeMin, v.toDouble());
        wholeMax = qMax(wholeMax, v.toDouble());
    }
    QVERIFY(wholeMax > 0.2);
    QVERIFY(wholeMin <= 0.06);
}

// The curve editor's filmstrip and waveform both span the clip's trimmed window while the strip's
// frames are sampled across the whole media file, so the session has to publish all three numbers.
// A zero media duration would silently drop the strip back to spreading the whole file across the
// clip, which is the bug this guards.
void EditorStateTest::speedCurveSessionExposesTrimmedSourceWindow()
{
    AssetLibrary library;
    AppController state(&library);

    drift::Project &project = *state.project();

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-video");
    asset.path = QStringLiteral("/tmp/video.mp4");
    asset.name = QStringLiteral("Video");
    asset.kind = drift::MediaKind::Video;
    asset.durationUs = drift::secondsToUs(20.0);
    project.assets().insert(asset.id, asset);
    project.assetOrder().append(asset.id);

    drift::Clip clip;
    clip.id = QStringLiteral("clip-video");
    clip.assetId = asset.id;
    clip.type = drift::ClipType::Video;
    clip.name = asset.name;
    clip.path = asset.path;
    clip.filmstripPath = QStringLiteral("/tmp/video.strip.jpg");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(7.0);
    clip.srcIn = drift::secondsToUs(5.0);
    clip.srcOut = drift::secondsToUs(12.0);

    drift::Track track{.type = drift::TrackType::Video};
    track.clips.append(clip);
    project.tracks().clear();
    project.tracks().append(track);

    state.beginSpeedCurveSession(0, 0);
    QVERIFY2(state.speedCurveSessionActive(), qPrintable(state.lastMessage()));

    QVERIFY(std::fabs(state.speedCurveSourceStart() - 5.0) < 1e-6);
    QVERIFY(std::fabs(state.speedCurveSourceDuration() - 7.0) < 1e-6);
    QVERIFY(std::fabs(state.speedCurveMediaDuration() - 20.0) < 1e-6);

    // What ClipFilmstrip.sourceMapped needs: a real source length and a non-empty window inside it.
    QVERIFY(state.speedCurveMediaDuration() > 0.0);
    QVERIFY(state.speedCurveSourceDuration() > 0.0);
    QVERIFY(state.speedCurveSourceStart() + state.speedCurveSourceDuration()
            <= state.speedCurveMediaDuration());

    // With no asset entry the length still has to come out positive, or the strip silently falls
    // back to spreading the whole file across the clip.
    project.assets().clear();
    project.assetOrder().clear();
    state.endSpeedCurveSession();
    state.beginSpeedCurveSession(0, 0);
    QVERIFY(state.speedCurveSessionActive());
    QVERIFY(state.speedCurveMediaDuration() >= state.speedCurveSourceStart()
                                                  + state.speedCurveSourceDuration());
}

namespace {

// Generates a silent test pattern of `seconds` at `path`. Returns false when ffmpeg failed.
bool renderTestVideo(const QString &ffmpeg, const QString &path, int seconds)
{
    QProcess proc;
    proc.start(ffmpeg,
               {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                QStringLiteral("-i"),
                QStringLiteral("testsrc=d=%1:s=320x240:r=25").arg(seconds),
                QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path});
    return proc.waitForFinished(60000) && proc.exitCode() == 0;
}

// Imports one file into the bin and blocks until its probe has landed.
bool importAndAwait(AssetLibrary &library, const QString &path)
{
    QSignalSpy probed(&library, &AssetLibrary::assetMetadataChanged);
    const int before = library.count();
    library.importUrls({QUrl::fromLocalFile(path)});
    if (library.count() != before + 1)
        return false;
    return probed.wait(60000);
}

} // namespace

// The whole point of the feature: a project set up once — music, outro, CTA — is re-pointed at
// the next video by swapping the file under its bin row, and every clip using it stays put.
void EditorStateTest::replaceAssetSourceRebindsClipsAndClampsTrim()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString original = dir.filePath(QStringLiteral("week1.mp4"));
    const QString replacement = dir.filePath(QStringLiteral("week2.mp4"));
    QVERIFY(renderTestVideo(ffmpeg, original, 10));
    QVERIFY(renderTestVideo(ffmpeg, replacement, 4));

    AssetLibrary library;
    AppController state(&library);
    QVERIFY(importAndAwait(library, original));

    state.addClipFromAsset(0);
    const int trackIndex = state.selectedTrack();
    const int clipIndex = state.selectedClip();
    QVERIFY(trackIndex >= 0 && clipIndex >= 0);

    drift::Project &project = *state.project();
    // Trim to a window that only the 10s original can satisfy, and drop an effect on it so the
    // work that must survive the swap is represented.
    {
        drift::Clip &clip = project.tracks()[trackIndex].clips[clipIndex];
        clip.timelineStart = drift::secondsToUs(2.0);
        clip.srcIn = drift::secondsToUs(1.0);
        clip.srcOut = drift::secondsToUs(9.0);
        clip.timelineDuration = drift::secondsToUs(8.0);
        clip.effects.append(drift::Effect{.name = QStringLiteral("gblur")});
        clip.faceTrackPath = QStringLiteral("/tmp/stale.landmarks");
        clip.faceTrackSrcOffsetUs = drift::secondsToUs(1.0);
    }
    const QString assetId = library.assetIdAt(0);
    const drift::Clip beforeSwap = project.tracks().at(trackIndex).clips.at(clipIndex);

    QSignalSpy finished(&state, &AppController::assetReplaceFinished);
    QVERIFY(state.replaceAssetSource(0, QUrl::fromLocalFile(replacement)));
    QVERIFY2(finished.wait(60000), "replace did not finish");
    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
    // The 9s out-point cannot survive a 4s file, so exactly this one clip is reported adjusted.
    QCOMPARE(finished.at(0).at(2).toInt(), 1);

    // The bin row is the same row, still the same id, now pointing somewhere else.
    QCOMPARE(library.count(), 1);
    QCOMPARE(library.assetIdAt(0), assetId);
    QCOMPARE(project.asset(assetId)->path, QFileInfo(replacement).absoluteFilePath());

    const drift::Clip &after = project.tracks().at(trackIndex).clips.at(clipIndex);
    QCOMPARE(after.id, beforeSwap.id);
    QCOMPARE(after.assetId, assetId);
    QCOMPARE(after.path, QFileInfo(replacement).absoluteFilePath());
    // Position on the timeline and the editing work on the clip both carry over untouched.
    QCOMPARE(after.timelineStart, beforeSwap.timelineStart);
    QCOMPARE(after.effects.size(), 1);
    // Landmarks were baked against the old pixels; keeping them would warp to a face the new
    // footage never had.
    QVERIFY(after.faceTrackPath.isEmpty());
    QCOMPARE(after.faceTrackSrcOffsetUs, drift::TimeUs(0));
    // The source window is pulled inside the shorter file, and the timeline duration follows so
    // the clip never addresses frames past the end of its media.
    const drift::TimeUs mediaDuration = project.asset(assetId)->durationUs;
    QVERIFY(mediaDuration > 0);
    QVERIFY(after.srcOut <= mediaDuration);
    QCOMPARE(after.srcIn, beforeSwap.srcIn);
    QVERIFY(after.timelineDuration < beforeSwap.timelineDuration);

    // One undo puts the asset and every clip bound to it back on the old file together.
    state.undo();
    QCOMPARE(project.asset(assetId)->path, QFileInfo(original).absoluteFilePath());
    const drift::Clip &undone = project.tracks().at(trackIndex).clips.at(clipIndex);
    QCOMPARE(undone.path, beforeSwap.path);
    QCOMPARE(undone.srcOut, beforeSwap.srcOut);
    QCOMPARE(undone.timelineDuration, beforeSwap.timelineDuration);
    QCOMPARE(undone.faceTrackPath, beforeSwap.faceTrackPath);
}

// A clip's type is fixed at creation and decides which track it may sit on, so audio cannot slot
// into a video row. The refusal has to leave the project exactly as it was.
void EditorStateTest::replaceAssetSourceRefusesADifferentKind()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("clip.mp4"));
    QVERIFY(renderTestVideo(ffmpeg, video, 4));

    const QString audio = dir.filePath(QStringLiteral("tone.wav"));
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=f=440:d=4:r=48000"),
                        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), audio});
    QVERIFY(proc.waitForFinished(60000) && proc.exitCode() == 0);

    AssetLibrary library;
    AppController state(&library);
    QVERIFY(importAndAwait(library, video));

    state.addClipFromAsset(0);
    drift::Project &project = *state.project();
    const QString assetId = library.assetIdAt(0);
    const QString originalPath = project.asset(assetId)->path;

    QSignalSpy undoStack(&state, &AppController::undoStackChanged);
    QSignalSpy finished(&state, &AppController::assetReplaceFinished);
    QVERIFY(state.replaceAssetSource(0, QUrl::fromLocalFile(audio)));
    QVERIFY2(finished.wait(60000), "replace did not finish");
    QVERIFY(!finished.at(0).at(0).toBool());
    QVERIFY(!finished.at(0).at(1).toString().isEmpty());

    QCOMPARE(project.asset(assetId)->path, originalPath);
    QCOMPARE(project.tracks().at(state.selectedTrack()).clips.at(0).path, originalPath);
    // A refused swap must not leave an empty step on the stack for the user to undo.
    QCOMPARE(undoStack.count(), 0);
}

// The way a freeze frame leaves the project: a PNG destination copies the bytes through
// untouched, while a .jpg destination re-encodes at the same size.
void EditorStateTest::exportAssetImageWritesPngAndJpeg()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::red);
    const QString sourcePath = dir.filePath(QStringLiteral("freeze.png"));
    QVERIFY(source.save(sourcePath, "PNG"));

    AssetLibrary library;
    AppController state(&library);

    drift::MediaAsset asset;
    asset.name = QStringLiteral("Freeze frame");
    asset.path = sourcePath;
    asset.kind = drift::MediaKind::Image;
    asset.width = source.width();
    asset.height = source.height();
    QVERIFY(!library.addGeneratedAsset(asset).isEmpty());

    const QString pngOut = dir.filePath(QStringLiteral("out.png"));
    QVERIFY(state.exportAssetImage(0, QUrl::fromLocalFile(pngOut)));
    QCOMPARE(QFileInfo(pngOut).size(), QFileInfo(sourcePath).size());

    const QString jpegOut = dir.filePath(QStringLiteral("out.jpg"));
    QVERIFY(state.exportAssetImage(0, QUrl::fromLocalFile(jpegOut)));
    const QImage written(jpegOut);
    QVERIFY(!written.isNull());
    QCOMPARE(written.size(), source.size());

    QVERIFY(!state.exportAssetImage(1, QUrl::fromLocalFile(dir.filePath(QStringLiteral("no.png")))));
}

void EditorStateTest::startupProjectUrlFromArguments()
{
    QCOMPARE(AppController::startupProjectUrlFromArguments({QStringLiteral("drift")}), QUrl());
    QCOMPARE(AppController::startupProjectUrlFromArguments(
                 {QStringLiteral("drift"), QStringLiteral("--verbose")}),
             QUrl());

    const QString spaced = QDir::temp().filePath(QStringLiteral("Untitled Project.drift"));
    const QUrl fromPath = AppController::startupProjectUrlFromArguments(
        {QStringLiteral("drift"), QStringLiteral("--verbose"), spaced});
    QCOMPARE(fromPath, QUrl::fromLocalFile(spaced));

    const QUrl fileUrl = QUrl::fromLocalFile(spaced);
    const QUrl fromFileUrl = AppController::startupProjectUrlFromArguments(
        {QStringLiteral("drift"), fileUrl.toString()});
    QCOMPARE(fromFileUrl.toLocalFile(), spaced);
}

// --- Multicam ------------------------------------------------------------------------------

namespace {

// Three stacked cameras, no empty program track. Camera 2's media is offset 100 s from
// timeline zero so a source time taken from the wrong clip is impossible to mistake.
void appendStackedCameras(drift::Project &project)
{
    project.tracks().clear();

    auto addAsset = [&project](const QString &id, const QString &path, double durationSeconds) {
        drift::MediaAsset asset;
        asset.id = id;
        asset.path = path;
        asset.name = id;
        asset.kind = drift::MediaKind::Video;
        asset.durationUs = drift::secondsToUs(durationSeconds);
        project.assets().insert(asset.id, asset);
        project.assetOrder().append(asset.id);
    };

    auto addClip = [](drift::Track &track, const QString &id, const QString &assetId,
                      const QString &path, double startSeconds, double durationSeconds,
                      double srcInSeconds) {
        drift::Clip clip;
        clip.id = id;
        clip.assetId = assetId;
        clip.path = path;
        clip.name = id;
        clip.type = drift::ClipType::Video;
        clip.timelineStart = drift::secondsToUs(startSeconds);
        clip.timelineDuration = drift::secondsToUs(durationSeconds);
        clip.srcIn = drift::secondsToUs(srcInSeconds);
        clip.srcOut = drift::secondsToUs(srcInSeconds + durationSeconds);
        track.clips.append(clip);
    };

    addAsset(QStringLiteral("asset-cam1"), QStringLiteral("/tmp/cam1.mp4"), 60.0);
    addAsset(QStringLiteral("asset-cam2"), QStringLiteral("/tmp/cam2.mp4"), 200.0);
    addAsset(QStringLiteral("asset-cam3"), QStringLiteral("/tmp/cam3.mp4"), 60.0);

    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    addClip(project.tracks()[0], QStringLiteral("cam1"), QStringLiteral("asset-cam1"),
            QStringLiteral("/tmp/cam1.mp4"), 0.0, 10.0, 0.0);

    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    addClip(project.tracks()[1], QStringLiteral("cam2"), QStringLiteral("asset-cam2"),
            QStringLiteral("/tmp/cam2.mp4"), 0.0, 10.0, 100.0);

    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    addClip(project.tracks()[2], QStringLiteral("cam3"), QStringLiteral("asset-cam3"),
            QStringLiteral("/tmp/cam3.mp4"), 0.0, 10.0, 50.0);
}

void selectStackedCameras(AppController &state)
{
    state.selectClip(0, 0);
    state.addToSelection(1, 0);
    state.addToSelection(2, 0);
}

drift::TimeUs frameSnapped(int fps, double seconds)
{
    const drift::TimeUs step = drift::frameDurationUs(fps);
    return ((drift::secondsToUs(seconds) + step / 2) / step) * step;
}

} // namespace

void EditorStateTest::multicamSessionDoesNotMutateTheProject()
{
    AssetLibrary library;
    AppController state(&library);
    appendStackedCameras(*state.project());
    selectStackedCameras(state);

    const drift::Project before = state.project()->detachedCopy();
    QVERIFY(state.beginMulticamSession());
    QCOMPARE(state.multicamAngles().size(), 3);
    QCOMPARE(state.multicamActiveAngle(), 0);

    state.setPlayheadSeconds(4.0);
    state.switchMulticamAngle(1);

    QCOMPARE(state.multicamActiveAngle(), 1);
    QCOMPARE(state.project()->toJson(), before.toJson());
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(1).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(2).clips.size(), 1);

    const QVariantList lane = state.multicamProgramClips();
    QCOMPARE(lane.size(), 2);
    QCOMPARE(lane.at(0).toMap().value(QStringLiteral("angle")).toInt(), 0);
    QCOMPARE(lane.at(1).toMap().value(QStringLiteral("angle")).toInt(), 1);
}

void EditorStateTest::multicamRecutSplitsAnInterval()
{
    AssetLibrary library;
    AppController state(&library);
    appendStackedCameras(*state.project());
    selectStackedCameras(state);
    QVERIFY(state.beginMulticamSession());

    state.setPlayheadSeconds(4.0);
    state.switchMulticamAngle(1);

    const QVariantList lane = state.multicamProgramClips();
    QCOMPARE(lane.size(), 2);
    QCOMPARE(lane.at(0).toMap().value(QStringLiteral("angle")).toInt(), 0);
    QCOMPARE(lane.at(1).toMap().value(QStringLiteral("angle")).toInt(), 1);

    state.setPlayheadSeconds(2.0);
    state.switchMulticamAngle(2);
    const QVariantList recut = state.multicamProgramClips();
    QCOMPARE(recut.size(), 3);
    QCOMPARE(recut.at(0).toMap().value(QStringLiteral("angle")).toInt(), 0);
    QCOMPARE(recut.at(1).toMap().value(QStringLiteral("angle")).toInt(), 2);
    QCOMPARE(recut.at(2).toMap().value(QStringLiteral("angle")).toInt(), 1);
}

void EditorStateTest::multicamCancelIsIdentity()
{
    AssetLibrary library;
    AppController state(&library);
    appendStackedCameras(*state.project());
    selectStackedCameras(state);
    const drift::Project before = state.project()->detachedCopy();

    QVERIFY(state.beginMulticamSession());
    state.setPlayheadSeconds(4.0);
    state.switchMulticamAngle(1);
    state.endMulticamSession();

    QVERIFY(!state.multicamActive());
    QCOMPARE(state.project()->toJson(), before.toJson());
}

void EditorStateTest::multicamSaveSeparateWritesGappedClips()
{
    AssetLibrary library;
    AppController state(&library);
    appendStackedCameras(*state.project());
    selectStackedCameras(state);
    QVERIFY(state.beginMulticamSession());

    const drift::TimeUs cutUs = frameSnapped(state.projectFps(), 4.0);
    state.setPlayheadSeconds(4.0);
    state.switchMulticamAngle(1);
    state.saveMulticamAsSeparateTracks();

    QVERIFY(!state.multicamActive());
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineEnd(), cutUs);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).path, QStringLiteral("/tmp/cam1.mp4"));

    QCOMPARE(state.project()->tracks().at(1).clips.size(), 1);
    const drift::Clip &cam2 = state.project()->tracks().at(1).clips.at(0);
    QCOMPARE(cam2.timelineStart, cutUs);
    QCOMPARE(cam2.timelineEnd(), drift::secondsToUs(10.0));
    QCOMPARE(cam2.path, QStringLiteral("/tmp/cam2.mp4"));
    QCOMPARE(cam2.srcIn, drift::secondsToUs(100.0) + cutUs);

    QCOMPARE(state.project()->tracks().at(2).clips.size(), 0);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(1).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(2).clips.size(), 1);
}

void EditorStateTest::multicamSaveCombinedFlattensOntoTopmost()
{
    AssetLibrary library;
    AppController state(&library);
    appendStackedCameras(*state.project());
    selectStackedCameras(state);
    QVERIFY(state.beginMulticamSession());

    const drift::TimeUs cutUs = frameSnapped(state.projectFps(), 4.0);
    state.setPlayheadSeconds(4.0);
    state.switchMulticamAngle(1);
    state.saveMulticamCombined();

    QVERIFY(!state.multicamActive());
    const drift::Track &top = state.project()->tracks().at(0);
    QCOMPARE(top.clips.size(), 2);
    QCOMPARE(top.clips.at(0).path, QStringLiteral("/tmp/cam1.mp4"));
    QCOMPARE(top.clips.at(0).timelineEnd(), cutUs);
    QCOMPARE(top.clips.at(1).path, QStringLiteral("/tmp/cam2.mp4"));
    QCOMPARE(top.clips.at(1).timelineStart, cutUs);
    QCOMPARE(top.clips.at(1).srcIn, drift::secondsToUs(100.0) + cutUs);

    QCOMPARE(state.project()->tracks().at(1).clips.size(), 0);
    QCOMPARE(state.project()->tracks().at(2).clips.size(), 0);
}

void EditorStateTest::multicamSessionEndsWithTheProject()
{
    AssetLibrary library;
    AppController state(&library);
    appendStackedCameras(*state.project());
    selectStackedCameras(state);
    QVERIFY(state.beginMulticamSession());
    QVERIFY(state.multicamActive());

    state.newProject();
    QVERIFY(!state.multicamActive());
}

void EditorStateTest::multicamSessionPublishesADecodedTilePerAngle()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate test clips");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString camA = dir.filePath(QStringLiteral("camA.mp4"));
    const QString camB = dir.filePath(QStringLiteral("camB.mp4"));
    QVERIFY(renderTestVideo(ffmpeg, camA, 5));
    QVERIFY(renderTestVideo(ffmpeg, camB, 5));

    AssetLibrary library;
    AppController state(&library);
    appendStackedCameras(*state.project());
    state.project()->tracks()[0].clips[0].path = camA;
    state.project()->tracks()[1].clips[0].path = camB;
    state.project()->tracks()[1].clips[0].srcIn = 0;
    state.project()->tracks()[1].clips[0].srcOut = drift::secondsToUs(5.0);
    selectStackedCameras(state);

    MulticamImageStore::clear();
    QVERIFY(MulticamImageStore::tile(0).isNull());

    QSignalSpy frames(&state, &AppController::multicamFramesChanged);
    state.setPlayheadSeconds(2.0);
    QVERIFY(state.beginMulticamSession());

    QVERIFY(frames.wait(60000));

    const QImage first = MulticamImageStore::tile(0);
    const QImage second = MulticamImageStore::tile(1);
    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
    QCOMPARE(first.size(), QSize(320, 240));

    state.endMulticamSession();
    QVERIFY(!state.multicamActive());
    QVERIFY(MulticamImageStore::tile(0).isNull());
}

void EditorStateTest::multicamProviderServesTilesByAngleIdWithRevisionQuery()
{
    MulticamImageStore::clear();

    QImage stored(4, 3, QImage::Format_RGB32);
    stored.fill(Qt::green);
    MulticamImageStore::setTile(1, stored);

    MulticamImageProvider provider;
    QSize size;

    const QImage served = provider.requestImage(QStringLiteral("1?rev=7"), &size, QSize());
    QVERIFY(!served.isNull());
    QCOMPARE(served.size(), QSize(4, 3));
    QCOMPARE(size, QSize(4, 3));

    QCOMPARE(provider.requestImage(QStringLiteral("1"), &size, QSize()).size(), QSize(4, 3));
    QVERIFY(provider.requestImage(QStringLiteral("0?rev=7"), &size, QSize()).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("bogus?rev=7"), &size, QSize()).isNull());

    MulticamImageStore::clear();
}

void EditorStateTest::multicamSetUpBuildsAWorkingRigFromTheBin()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate test clips");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QStringList cams = {dir.filePath(QStringLiteral("cam1.mp4")),
                              dir.filePath(QStringLiteral("cam2.mp4")),
                              dir.filePath(QStringLiteral("cam3.mp4"))};
    for (const QString &cam : cams)
        QVERIFY(renderTestVideo(ffmpeg, cam, 4));

    AssetLibrary library;
    AppController state(&library);
    for (const QString &cam : cams)
        QVERIFY(importAndAwait(library, cam));

    QVERIFY(state.multicamCanSetUp());
    QVERIFY(state.beginMulticamSession());
    state.setUpMulticamFromAssets();

    const QList<drift::Track> &tracks = state.project()->tracks();
    QCOMPARE(tracks.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(tracks.at(i).type, drift::TrackType::Video);
        QCOMPARE(tracks.at(i).clips.size(), 1);
        QCOMPARE(tracks.at(i).clips.at(0).timelineStart, drift::TimeUs{0});
        QVERIFY(tracks.at(i).clips.at(0).timelineDuration > 0);
    }
    QCOMPARE(tracks.at(0).muted, false);
    QCOMPARE(tracks.at(1).muted, true);
    QCOMPARE(tracks.at(2).muted, true);

    QCOMPARE(state.multicamAngles().size(), 3);
    QVERIFY(!state.multicamCanSetUp());

    state.undo();
    QVERIFY(!state.multicamActive());
    QVERIFY(state.multicamCanSetUp());
}

namespace {

// Two video clips on one track, the second twice as long, so a paste between them has to rescale.
void appendTwoVideoClips(drift::Project &project)
{
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    for (int i = 0; i < 2; ++i) {
        drift::Clip clip;
        clip.id = QStringLiteral("clip-%1").arg(i);
        clip.type = drift::ClipType::Video;
        clip.name = QStringLiteral("Shot %1").arg(i);
        clip.timelineStart = i == 0 ? 0 : drift::secondsToUs(2.0);
        clip.timelineDuration = drift::secondsToUs(i == 0 ? 2.0 : 4.0);
        clip.srcIn = 0;
        clip.srcOut = clip.timelineDuration;
        project.tracks()[0].clips.append(clip);
    }
}

QList<drift::TimeUs> keyTimes(const drift::Effect &effect, const QString &param)
{
    return effect.paramKeyframes.value(param).keyframes().keys();
}

} // namespace

// Retiming carries each key across through the moment of source it sat on, but it used to rebuild
// the track from scratch and lose the enabled flag with it — silently switching an animation the
// user had turned off back on.
void EditorStateTest::retimeKeepsDisabledKeyframeTrackDisabled()
{
    AssetLibrary library;
    AppController state(&library);

    drift::Clip clip;
    clip.id = QStringLiteral("clip-retime");
    clip.type = drift::ClipType::Video;
    clip.name = QStringLiteral("Shot");
    clip.path = QStringLiteral("/tmp/does-not-need-to-exist.mp4");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(4.0);
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(4.0);

    drift::Track track{.type = drift::TrackType::Video};
    track.clips.append(clip);
    state.project()->tracks().clear();
    state.project()->tracks().append(track);

    state.selectClip(0, 0);
    state.setClipKeyframe(0, 0, QStringLiteral("opacity"), 0.0, 1.0);
    state.setClipKeyframe(0, 0, QStringLiteral("opacity"), 4.0, 0.0);
    state.setClipPropertyKeyframesEnabled(0, 0, QStringLiteral("opacity"), false);
    QVERIFY(!state.clipPropertyKeyframesEnabled(0, 0, QStringLiteral("opacity")));

    state.beginSpeedCurveSession(0, 0);
    QVERIFY2(state.speedCurveSessionActive(), qPrintable(state.lastMessage()));
    state.setSpeedCurvePoints(QVariantList{
        QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.5}},
        QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.5}},
    });
    state.applySpeedCurve();

    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
    state.selectClip(0, 0);
    QVERIFY(!state.clipPropertyKeyframesEnabled(0, 0, QStringLiteral("opacity")));
}

// The headline behaviour: a copied stack lands on top of what the target already has, and its
// animation is stretched to the target's length rather than sliding against the picture.
void EditorStateTest::effectStackCopyPasteAppendsAndRescales()
{
    AssetLibrary library;
    AppController state(&library);
    appendTwoVideoClips(*state.project());

    state.selectClip(0, 0);
    state.addEffect(0, 0, QStringLiteral("adjust.contrast"));
    state.setClipKeyframe(0, 0, QStringLiteral("fx.0.contrast"), 0.0, 1.0);
    state.setClipKeyframe(0, 0, QStringLiteral("fx.0.contrast"), 2.0, 2.0);
    QCOMPARE(keyTimes(state.project()->tracks().at(0).clips.at(0).effects.at(0),
                      QStringLiteral("contrast")).size(), 2);

    state.copyClipEffectsToClipboard(0, 0);
    QVERIFY(state.clipboardHasEffects());

    state.selectClip(0, 1);
    state.addEffect(0, 1, QStringLiteral("adjust.brightness"));
    state.pasteEffectsFromClipboard(0, 1);

    const drift::Clip &target = state.project()->tracks().at(0).clips.at(1);
    QCOMPARE(target.effects.size(), 2);
    // Appended, not prepended and not replacing.
    QCOMPARE(target.effects.at(0).catalogId, QStringLiteral("adjust.brightness"));
    QCOMPARE(target.effects.at(1).catalogId, QStringLiteral("adjust.contrast"));
    // 2s of source stretched over a 4s clip.
    QCOMPARE(keyTimes(target.effects.at(1), QStringLiteral("contrast")),
             (QList<drift::TimeUs>{0, drift::secondsToUs(4.0)}));
    // The clip it was copied from is untouched.
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).effects.size(), 1);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.project()->tracks().at(0).clips.at(1).effects.size(), 1);
}

// The executable form of "appending never shifts an existing effect index": keyframe-graph
// properties are addressed "fx.<n>.<key>", so a paste that prepended or replaced would silently
// repoint every one of them.
void EditorStateTest::pastedEffectsKeepTheKeyframeGraphSelection()
{
    AssetLibrary library;
    AppController state(&library);
    appendTwoVideoClips(*state.project());

    state.selectClip(0, 0);
    state.addEffect(0, 0, QStringLiteral("adjust.contrast"));
    state.copyClipEffectsToClipboard(0, 0);

    state.selectClip(0, 1);
    state.addEffect(0, 1, QStringLiteral("adjust.brightness"));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.0.brightness"));
    const QStringList before = state.keyframeGraphHiddenProperties();
    QVERIFY(before.contains(QStringLiteral("fx.0.brightness")));

    state.pasteEffectsFromClipboard(0, 1);
    QCOMPARE(state.keyframeGraphHiddenProperties(), before);
}

void EditorStateTest::copiedSingleEffectRoutesToTheRightList()
{
    AssetLibrary library;
    AppController state(&library);
    appendTwoVideoClips(*state.project());

    state.selectClip(0, 0);
    state.addEffect(0, 0, QStringLiteral("adjust.contrast"));
    state.addAudioEffect(0, 0, QStringLiteral("space.autopan"));
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).audioEffects.size(), 1);

    // One video effect: video side only.
    state.copyEffectToClipboard(0, 0, 0);
    state.pasteEffectsFromClipboard(0, 1);
    QCOMPARE(state.project()->tracks().at(0).clips.at(1).effects.size(), 1);
    QCOMPARE(state.project()->tracks().at(0).clips.at(1).audioEffects.size(), 0);

    // One audio effect: audio side only.
    state.copyAudioEffectToClipboard(0, 0, 0);
    state.pasteEffectsFromClipboard(0, 1);
    QCOMPARE(state.project()->tracks().at(0).clips.at(1).effects.size(), 1);
    QCOMPARE(state.project()->tracks().at(0).clips.at(1).audioEffects.size(), 1);
    QCOMPARE(state.project()->tracks().at(0).clips.at(1).audioEffects.at(0).catalogId,
             QStringLiteral("space.autopan"));
}

// Audio effects run in the mixer and the audio inspector is hidden for clips with no audio, so
// pasting them onto a title would leave them invisible and inert.
void EditorStateTest::pastedAudioEffectsAreDroppedOnClipsWithNoAudio()
{
    AssetLibrary library;
    AppController state(&library);
    appendTwoVideoClips(*state.project());

    state.selectClip(0, 0);
    state.addEffect(0, 0, QStringLiteral("adjust.contrast"));
    state.addAudioEffect(0, 0, QStringLiteral("space.autopan"));
    state.copyClipEffectsToClipboard(0, 0);

    state.addTextClip(QStringLiteral("Title"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).type, drift::ClipType::Text);

    state.pasteEffectsFromClipboard(track, clip);
    QCOMPARE(state.project()->tracks().at(track).clips.at(clip).effects.size(), 1);
    QVERIFY(state.project()->tracks().at(track).clips.at(clip).audioEffects.isEmpty());
}

// An effect from an addon the user has not installed is kept, exactly as project load keeps it, so
// the stack survives the round-trip and starts working once the pack is installed.
void EditorStateTest::pastedUnknownEffectIsKeptAndReported()
{
    AssetLibrary library;
    AppController state(&library);
    appendTwoVideoClips(*state.project());

    drift::EffectStackPreset stack;
    stack.sourceDurationUs = drift::secondsToUs(2.0);
    drift::Effect known;
    known.catalogId = QStringLiteral("adjust.contrast");
    stack.effects.append(known);
    drift::Effect unknown;
    unknown.catalogId = QStringLiteral("nope.not_installed");
    stack.effects.append(unknown);

    QGuiApplication::clipboard()->setText(QString::fromUtf8(
        QJsonDocument(drift::effectStackToJson(stack)).toJson(QJsonDocument::Compact)));

    state.selectClip(0, 1);
    state.pasteEffectsFromClipboard(0, 1);

    const drift::Clip &target = state.project()->tracks().at(0).clips.at(1);
    QCOMPARE(target.effects.size(), 2);
    QCOMPARE(target.effects.at(1).catalogId, QStringLiteral("nope.not_installed"));
    // finishEdit() clears lastMessage, so the warning has to survive being emitted after it.
    QVERIFY(state.lastMessage().contains(QStringLiteral("nope.not_installed")));
}

void EditorStateTest::clipboardHasEffectsIgnoresOrdinaryText()
{
    AssetLibrary library;
    AppController state(&library);
    appendTwoVideoClips(*state.project());

    QGuiApplication::clipboard()->setText(QStringLiteral("just some prose about \"drift\" wood"));
    QVERIFY(!state.clipboardHasEffects());

    state.selectClip(0, 1);
    state.pasteEffectsFromClipboard(0, 1);
    QVERIFY(state.project()->tracks().at(0).clips.at(1).effects.isEmpty());

    state.selectClip(0, 0);
    state.addEffect(0, 0, QStringLiteral("adjust.contrast"));
    state.copyClipEffectsToClipboard(0, 0);
    QVERIFY(state.clipboardHasEffects());
}

void EditorStateTest::savedEffectPresetAppliesToAnotherClip()
{
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTestEffectPresetApply"));
    QFile::remove(drift::EffectStackStore::storePath());
    drift::EffectStackStore::instance().reload();

    AssetLibrary library;
    AppController state(&library);
    appendTwoVideoClips(*state.project());

    state.selectClip(0, 0);
    state.addEffect(0, 0, QStringLiteral("adjust.contrast"));
    state.setClipKeyframe(0, 0, QStringLiteral("fx.0.contrast"), 0.0, 1.0);
    state.setClipKeyframe(0, 0, QStringLiteral("fx.0.contrast"), 2.0, 2.0);

    const QString id = state.saveClipEffectsAsPreset(0, 0, QStringLiteral("Grade"));
    QVERIFY(!id.isEmpty());
    QCOMPARE(state.userEffectPresets().size(), 1);

    state.selectClip(0, 1);
    state.addEffect(0, 1, QStringLiteral("adjust.brightness"));
    state.applyEffectPreset(0, 1, id);

    const drift::Clip &target = state.project()->tracks().at(0).clips.at(1);
    QCOMPARE(target.effects.size(), 2);
    QCOMPARE(target.effects.at(0).catalogId, QStringLiteral("adjust.brightness"));
    QCOMPARE(keyTimes(target.effects.at(1), QStringLiteral("contrast")),
             (QList<drift::TimeUs>{0, drift::secondsToUs(4.0)}));

    QVERIFY(state.deleteUserEffectPreset(id));
    QFile::remove(drift::EffectStackStore::storePath());
    drift::EffectStackStore::instance().reload();
    QCoreApplication::setOrganizationName(org);
    QCoreApplication::setApplicationName(app);
    QStandardPaths::setTestModeEnabled(false);
}

void EditorStateTest::multiTrackAudioSelectionAndExtraction()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a multi-track test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString multiTrack = dir.filePath(QStringLiteral("obs_multitrack.mkv"));
    QProcess make;
    make.start(ffmpeg,
               {QStringLiteral("-y"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=blue:s=64x32:r=25:d=2"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:d=2"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=880:d=2"),
                QStringLiteral("-map"), QStringLiteral("0:v"),
                QStringLiteral("-map"), QStringLiteral("1:a"),
                QStringLiteral("-map"), QStringLiteral("2:a"),
                QStringLiteral("-metadata:s:a:0"), QStringLiteral("title=Desktop"),
                QStringLiteral("-metadata:s:a:1"), QStringLiteral("title=Mic"),
                QStringLiteral("-c:v"), QStringLiteral("libx264"),
                QStringLiteral("-c:a"), QStringLiteral("aac"),
                multiTrack});
    QVERIFY(make.waitForFinished(30000));
    QCOMPARE(make.exitCode(), 0);

    AssetLibrary library;
    AppController state(&library);

    drift::Clip clip;
    clip.id = QStringLiteral("clip-obs");
    clip.type = drift::ClipType::Video;
    clip.name = QStringLiteral("OBS Recording");
    clip.path = multiTrack;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.srcIn = 0;
    clip.srcOut = clip.timelineDuration;
    state.project()->tracks().clear();
    state.project()->tracks().append(drift::Track{.type = drift::TrackType::Video});
    state.project()->tracks()[0].clips.append(clip);
    state.selectClip(0, 0);

    // Check clip audio streams
    const QVariantList streams = state.clipAudioStreams(0, 0);
    QCOMPARE(streams.size(), 2);
    QCOMPARE(streams[0].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Desktop"));
    QCOMPARE(streams[1].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Mic"));
    QCOMPARE(state.clipAudioStreamCount(0, 0), 2);

    // Initial audioStreamIndex should be 0
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("audioStreamIndex")).toInt(), 0);

    // Select second audio stream
    state.setClipAudioStreamIndex(0, 0, 1);
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("audioStreamIndex")).toInt(), 1);

    // Undo should restore audioStreamIndex to 0
    state.undo();
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("audioStreamIndex")).toInt(), 0);

    state.redo();
    QCOMPARE(state.clipAt(0, 0).value(QStringLiteral("audioStreamIndex")).toInt(), 1);

    // Extract all audio tracks
    state.separateAllAudioTracks(0, 0);
    // Video clip should now have suppressEmbeddedAudio = true
    const drift::Clip &videoClip = state.project()->tracks().at(0).clips.at(0);
    QVERIFY(videoClip.suppressEmbeddedAudio);
    QVERIFY(!videoClip.linkId.isEmpty());

    // Should have created audio tracks with companion clips
    bool hasAudioTrack1 = false;
    bool hasAudioTrack2 = false;
    for (const drift::Track &track : state.project()->tracks()) {
        if (track.type != drift::TrackType::Audio)
            continue;
        for (const drift::Clip &clip : track.clips) {
            if (clip.linkId == videoClip.linkId) {
                if (clip.audioStreamIndex == 0)
                    hasAudioTrack1 = true;
                else if (clip.audioStreamIndex == 1)
                    hasAudioTrack2 = true;
            }
        }
    }
    QVERIFY(hasAudioTrack1);
    QVERIFY(hasAudioTrack2);
}

QTEST_MAIN(EditorStateTest)
#include "tst_editorstate.moc"
