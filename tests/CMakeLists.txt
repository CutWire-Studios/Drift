# qt_standard_project_setup points Windows executables at the top build dir, which would share
# drift's staged effects/ — core-only in packaged builds — and the tests need the full set.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

qt_add_executable(tst_mediaprobe tst_mediaprobe.cpp)
target_link_libraries(tst_mediaprobe PRIVATE Qt6::Test driftengine)
add_test(NAME MediaProbe COMMAND tst_mediaprobe)

qt_add_executable(tst_core tst_core.cpp)
target_include_directories(tst_core PRIVATE ../src)
target_link_libraries(tst_core PRIVATE Qt6::Test driftcore)
target_compile_definitions(tst_core PRIVATE DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME Core COMMAND tst_core)

qt_add_executable(tst_keyframeview
    tst_keyframeview.cpp
    ../src/app/editor/sessions/KeyframeCurves.cpp
    ../src/app/editor/sessions/KeyframeRows.cpp
    ../src/app/editor/sessions/KeyframeSelection.cpp
    ../src/app/editor/sessions/KeyframeTimeView.cpp)
target_include_directories(tst_keyframeview PRIVATE ../src)
target_link_libraries(tst_keyframeview PRIVATE Qt6::Test Qt6::Core driftcore)
add_test(NAME KeyframeView COMMAND tst_keyframeview)

qt_add_executable(tst_polylinedraft
    tst_polylinedraft.cpp
    ../src/app/editor/sessions/PolylineDraft.cpp)
target_include_directories(tst_polylinedraft PRIVATE ../src)
target_link_libraries(tst_polylinedraft PRIVATE Qt6::Test Qt6::Core)
add_test(NAME PolylineDraft COMMAND tst_polylinedraft)

qt_add_executable(tst_trackeroverlay tst_trackeroverlay.cpp)
target_include_directories(tst_trackeroverlay PRIVATE ../src)
target_link_libraries(tst_trackeroverlay PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME TrackerOverlay COMMAND tst_trackeroverlay)

qt_add_executable(tst_curveedit
    tst_curveedit.cpp
    ../src/ui/color/CurveEdit.cpp)
target_include_directories(tst_curveedit PRIVATE ../src)
target_link_libraries(tst_curveedit PRIVATE Qt6::Test Qt6::Core driftengine)
add_test(NAME CurveEdit COMMAND tst_curveedit)

qt_add_executable(tst_colorwheel tst_colorwheel.cpp)
target_include_directories(tst_colorwheel PRIVATE ../src)
target_link_libraries(tst_colorwheel PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME ColorWheel COMMAND tst_colorwheel)

qt_add_executable(tst_pdfsource tst_pdfsource.cpp)
target_include_directories(tst_pdfsource PRIVATE ../src)
target_link_libraries(tst_pdfsource PRIVATE Qt6::Test driftcore)
add_test(NAME PdfSource COMMAND tst_pdfsource)

qt_add_executable(tst_editorstate
    tst_editorstate.cpp
    ../src/ui/imageproviders/segment/SegmentImageStore.cpp
    ../src/playback/preview/ClipPreviewImageStore.cpp
    ../src/ui/imageproviders/multicam/MulticamImageStore.cpp
    # The provider, not just the store: the id QML addresses a tile by is parsed here, and a
    # store test cannot see that half. Costs a Qt6::Quick link, but needs no QML engine —
    # requestImage is called directly.
    ../src/ui/imageproviders/multicam/MulticamImageProvider.cpp
    ../src/app/editor/AppController.cpp
    ../src/app/mcp/McpController.cpp
    ../src/app/editor/PreferencesController.cpp
    ../src/app/GraphicsApiChoice.cpp
    ../src/app/editor/ProjectFileController.cpp
    ../src/app/bin/AssetPreviewController.cpp
    ../src/app/editor/sessions/SpeedCurveController.cpp
    ../src/app/editor/sessions/CurveEditorController.cpp
    ../src/app/editor/sessions/KeyframeController.cpp
    ../src/app/editor/sessions/KeyframeCurves.cpp
    ../src/app/editor/sessions/KeyframeCurvesModel.cpp
    ../src/app/editor/sessions/KeyframeRows.cpp
    ../src/app/editor/sessions/KeyframeRowsModel.cpp
    ../src/app/editor/sessions/KeyframeSelection.cpp
    ../src/app/editor/sessions/KeyframeTimeView.cpp
    ../src/app/editor/sessions/MaskEditSession.cpp
    ../src/app/editor/sessions/MaskStackModel.cpp
    ../src/app/editor/sessions/MaskingController.cpp
    ../src/app/editor/sessions/SubtitleController.cpp
    ../src/app/editor/sessions/SubtitleCueModel.cpp
    ../src/app/editor/sessions/SubtitleCueFilterModel.cpp
    ../src/app/jobs/vision/SegmentationController.cpp
    ../src/app/jobs/vision/SceneDetectController.cpp
    ../src/app/jobs/vision/DepthController.cpp
    ../src/app/jobs/audio/DenoiseController.cpp
    ../src/app/color/ColorController.cpp
    ../src/app/color/ScopesController.cpp
    ../src/app/color/LookThumbnailService.cpp
    ../src/ui/imageproviders/color/ColorImageProvider.cpp
    ../src/app/jobs/vision/RestoreController.cpp
    ../src/app/jobs/vision/FaceTrackingController.cpp
    ../src/app/jobs/vision/StabilizeController.cpp
    ../src/app/jobs/vision/TrackingController.cpp
    ../src/app/jobs/vision/TrackerListModel.cpp
    ../src/app/bin/MediaBinController.cpp
    ../src/app/bin/SourceLabController.cpp
    ../src/app/jobs/audio/TtsController.cpp
    ../src/app/jobs/audio/VoiceLibrary.cpp
    ../src/ui/preview/PreviewController.cpp
    ../src/app/online/addons/AddonManager.cpp
    ../src/app/jobs/JobRegistry.cpp
    ../src/app/online/CloudProviders.cpp
    ../src/app/bin/AssetLibrary.cpp
    ../src/app/bin/BinFolderListModel.cpp
    ../src/app/editor/TimelineClipsModel.cpp
    ../src/ui/timeline/TimelineLayout.cpp
    ../src/ui/timeline/TimelineTrackItem.cpp
    ../src/ui/timeline/TimelineViewState.cpp
    ../src/ui/timeline/ClipThumbnailStore.cpp
    ../src/playback/PlaybackEngine.cpp
    ../src/playback/PlaybackClock.cpp
    ../src/playback/AudioOutputChannel.cpp
    ../src/playback/preview/CompositorService.cpp
    ../src/playback/stats/PerfLog.cpp
    ../src/playback/stats/PlaybackStats.cpp
    ../src/playback/stats/PlaybackDiagnostics.cpp
    ../src/playback/preview/ClipPreviewPlayer.cpp
    ../src/app/mcp/protocol/McpSession.cpp
    ../src/app/platform/RunningInstances.cpp
    ../src/app/mcp/tools/McpCatalog.cpp
    ../src/app/mcp/protocol/McpProtocol.cpp
    ../src/app/mcp/transport/McpHttp.cpp
    ../src/app/mcp/tools/McpDispatcher.cpp
    ../src/app/mcp/tools/McpDispatcherExtended.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/app/mcp/tools/McpEffectAuthoring.cpp
    ../src/app/mcp/protocol/McpValidate.cpp
    ../src/app/mcp/tools/McpMarket.cpp
    ../src/app/online/market/MarketClient.cpp
    ../src/app/online/assets/DriftAssetStore.cpp
    ../src/app/online/market/MarketIdentity.cpp
    ../src/app/mcp/transport/McpServer.cpp
    ../src/app/mcp/transport/McpStdio.cpp
    ../src/app/mcp/transport/McpBridge.cpp
    ../src/app/mcp/transport/McpStdioServer.cpp
)
target_include_directories(tst_editorstate PRIVATE ../src)
target_link_libraries(tst_editorstate PRIVATE Qt6::Test Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Concurrent Qt6::Network driftcore driftengine)
target_compile_definitions(tst_editorstate PRIVATE
    DRIFT_TEST_DENOISE_MODEL_DIR="${CMAKE_SOURCE_DIR}/models/deepfilternet3"
    DRIFT_VERSION="${PROJECT_VERSION}")
if(DRIFT_WITH_SKIA)
    target_compile_definitions(tst_editorstate PRIVATE DRIFT_WITH_SKIA)
endif()
add_test(NAME EditorState COMMAND tst_editorstate)
drift_copy_effects(tst_editorstate)
drift_copy_audio_effects(tst_editorstate)
drift_copy_transitions(tst_editorstate)

qt_add_executable(tst_playback
    tst_playback.cpp
    ../src/playback/PlaybackClock.cpp
    ../src/playback/AudioOutputChannel.cpp
)
target_include_directories(tst_playback PRIVATE ../src)
target_link_libraries(tst_playback PRIVATE Qt6::Test Qt6::Multimedia driftcore)
add_test(NAME Playback COMMAND tst_playback)

qt_add_executable(tst_layoutstore tst_layoutstore.cpp ../src/app/editor/LayoutStore.cpp)
target_include_directories(tst_layoutstore PRIVATE ../src)
# Qt6::Gui for QScreen: restoring a window means checking it against the displays
# that are actually attached now.
target_link_libraries(tst_layoutstore PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME LayoutStore COMMAND tst_layoutstore)
# Screen-dependent assertions, and saveWindowState deliberately drops the window
# position under Wayland (the compositor owns it there). Pin the platform so the
# test means the same thing on a dev machine as it does headless in CI.
set_tests_properties(LayoutStore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_updateasset tst_updateasset.cpp ../src/app/online/updates/UpdateAsset.cpp)
target_include_directories(tst_updateasset PRIVATE ../src)
target_link_libraries(tst_updateasset PRIVATE Qt6::Test)
add_test(NAME UpdateAsset COMMAND tst_updateasset)

qt_add_executable(tst_uifont tst_uifont.cpp)
target_link_libraries(tst_uifont PRIVATE Qt6::Test Qt6::Gui)
target_compile_definitions(tst_uifont PRIVATE
    DRIFT_TEST_UI_FONT_DIR="${CMAKE_SOURCE_DIR}/resources/fonts")
add_test(NAME UiFont COMMAND tst_uifont)
# Font matching only, no window: offscreen keeps the result the same on a dev machine as in CI.
set_tests_properties(UiFont PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_addonpackage tst_addonpackage.cpp)
target_include_directories(tst_addonpackage PRIVATE ../src)
target_link_libraries(tst_addonpackage PRIVATE Qt6::Test driftengine)
target_compile_definitions(tst_addonpackage PRIVATE
    DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME AddonPackage COMMAND tst_addonpackage)

qt_add_executable(tst_marketidentity
    tst_marketidentity.cpp
    ../src/app/online/market/MarketIdentity.cpp
)
target_include_directories(tst_marketidentity PRIVATE ../src)
target_link_libraries(tst_marketidentity PRIVATE Qt6::Test Qt6::Core)
target_compile_definitions(tst_marketidentity PRIVATE
    DRIFT_MARKET_API_URL=""
    # Valid hex on purpose: it is the shape that used to be silently decoded.
    DRIFT_MARKET_CLIENT_KEY="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    DRIFT_VERSION="0")
add_test(NAME MarketIdentity COMMAND tst_marketidentity)

qt_add_executable(tst_projectbundle tst_projectbundle.cpp)
target_include_directories(tst_projectbundle PRIVATE ../src)
# zstd directly: the test forges a manifest to exercise the format version gate.
target_link_libraries(tst_projectbundle PRIVATE Qt6::Test driftengine ${DRIFT_ZSTD_TARGET})
add_test(NAME ProjectBundle COMMAND tst_projectbundle)

qt_add_executable(tst_engine
    tst_engine.cpp
    # Diagnostics live in the playback layer but are engine-shaped: a collector plus a
    # plain-text rendering, exactly like DebugReport, and tested alongside it.
    ../src/playback/stats/PlaybackStats.cpp
    ../src/playback/stats/PlaybackDiagnostics.cpp
)
target_include_directories(tst_engine PRIVATE ../src)
target_link_libraries(tst_engine PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL driftengine driftcore)
target_compile_definitions(tst_engine PRIVATE
    DRIFT_TEST_EFFECTS_DIR="${CMAKE_SOURCE_DIR}/effects"
    # Beauty / makeup packages live in the effects.core addon (drift-addons staging), not the
    # bundled VideoEd/effects tree. Optional so a checkout without the sibling repo still builds.
    DRIFT_TEST_ADDON_EFFECTS_DIR="${CMAKE_SOURCE_DIR}/../drift-addons/staging/effects-core/effects"
    DRIFT_TEST_TRANSITIONS_DIR="${CMAKE_SOURCE_DIR}/transitions"
    DRIFT_TEST_FONTS_DIR="${CMAKE_SOURCE_DIR}/fonts"
    DRIFT_TEST_EMOJI_FONT_DIR="${CMAKE_SOURCE_DIR}/emoji-fonts"
    DRIFT_TEST_AUDIO_EFFECTS_DIR="${CMAKE_SOURCE_DIR}/audio-effects"
    DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    # Not committed (models/ is gitignored). The denoise tests skip when it is absent so a
    # checkout without the model still passes.
    DRIFT_TEST_DENOISE_MODEL_DIR="${CMAKE_SOURCE_DIR}/models/deepfilternet3"
    # Staged by drift-addons/recipes/fetch-*.py; the model tests skip when absent.
    DRIFT_TEST_VAD_MODEL_DIR="${CMAKE_SOURCE_DIR}/../drift-addons/staging/silero-vad/models/silero-vad"
)
if(DRIFT_WITH_SKIA)
    target_compile_definitions(tst_engine PRIVATE DRIFT_WITH_SKIA)
endif()
add_test(NAME Engine COMMAND tst_engine)
drift_copy_effects(tst_engine)
drift_copy_transitions(tst_engine)

if(DRIFT_WITH_SKIA)
    qt_add_executable(tst_skia tst_skia.cpp)
    target_include_directories(tst_skia PRIVATE ../src)
    # Skia::Skia directly: the test's painters draw with SkCanvas themselves.
    target_link_libraries(tst_skia PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL driftengine Skia::Skia)
    target_compile_definitions(tst_skia PRIVATE
        DRIFT_TEST_FONTS_DIR="${CMAKE_SOURCE_DIR}/fonts"
        DRIFT_TEST_EMOJI_FONT_DIR="${CMAKE_SOURCE_DIR}/emoji-fonts")
    add_test(NAME Skia COMMAND tst_skia)

    qt_add_executable(tst_vector tst_vector.cpp)
    target_include_directories(tst_vector PRIVATE ../src)
    target_link_libraries(tst_vector PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL driftengine driftcore)
    target_compile_definitions(tst_vector PRIVATE DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME Vector COMMAND tst_vector)
endif()

qt_add_executable(tst_mcp
    tst_mcp.cpp
    ../src/app/mcp/protocol/McpSession.cpp
    ../src/app/platform/RunningInstances.cpp
    ../src/app/mcp/tools/McpCatalog.cpp
    ../src/app/mcp/protocol/McpProtocol.cpp
    ../src/app/mcp/transport/McpHttp.cpp
    ../src/ui/imageproviders/segment/SegmentImageStore.cpp
    ../src/playback/preview/ClipPreviewImageStore.cpp
    ../src/ui/imageproviders/multicam/MulticamImageStore.cpp
    ../src/app/editor/AppController.cpp
    ../src/app/mcp/McpController.cpp
    ../src/app/editor/PreferencesController.cpp
    ../src/app/GraphicsApiChoice.cpp
    ../src/app/editor/ProjectFileController.cpp
    ../src/app/bin/AssetPreviewController.cpp
    ../src/app/editor/sessions/SpeedCurveController.cpp
    ../src/app/editor/sessions/CurveEditorController.cpp
    ../src/app/editor/sessions/KeyframeController.cpp
    ../src/app/editor/sessions/KeyframeCurves.cpp
    ../src/app/editor/sessions/KeyframeCurvesModel.cpp
    ../src/app/editor/sessions/KeyframeRows.cpp
    ../src/app/editor/sessions/KeyframeRowsModel.cpp
    ../src/app/editor/sessions/KeyframeSelection.cpp
    ../src/app/editor/sessions/KeyframeTimeView.cpp
    ../src/app/editor/sessions/MaskEditSession.cpp
    ../src/app/editor/sessions/MaskStackModel.cpp
    ../src/app/editor/sessions/MaskingController.cpp
    ../src/app/editor/sessions/SubtitleController.cpp
    ../src/app/editor/sessions/SubtitleCueModel.cpp
    ../src/app/editor/sessions/SubtitleCueFilterModel.cpp
    ../src/app/jobs/vision/SegmentationController.cpp
    ../src/app/jobs/vision/SceneDetectController.cpp
    ../src/app/jobs/vision/DepthController.cpp
    ../src/app/jobs/audio/DenoiseController.cpp
    ../src/app/color/ColorController.cpp
    ../src/app/color/ScopesController.cpp
    ../src/app/color/LookThumbnailService.cpp
    ../src/ui/imageproviders/color/ColorImageProvider.cpp
    ../src/app/jobs/vision/RestoreController.cpp
    ../src/app/jobs/vision/FaceTrackingController.cpp
    ../src/app/jobs/vision/StabilizeController.cpp
    ../src/app/jobs/vision/TrackingController.cpp
    ../src/app/jobs/vision/TrackerListModel.cpp
    ../src/app/bin/MediaBinController.cpp
    ../src/app/bin/SourceLabController.cpp
    ../src/app/jobs/audio/TtsController.cpp
    ../src/app/jobs/audio/VoiceLibrary.cpp
    ../src/ui/preview/PreviewController.cpp
    ../src/app/online/addons/AddonManager.cpp
    ../src/app/jobs/JobRegistry.cpp
    ../src/app/online/CloudProviders.cpp
    ../src/app/bin/AssetLibrary.cpp
    ../src/app/bin/BinFolderListModel.cpp
    ../src/app/editor/TimelineClipsModel.cpp
    ../src/ui/timeline/ClipThumbnailStore.cpp
    ../src/playback/PlaybackEngine.cpp
    ../src/playback/PlaybackClock.cpp
    ../src/playback/AudioOutputChannel.cpp
    ../src/playback/preview/CompositorService.cpp
    ../src/playback/stats/PerfLog.cpp
    ../src/playback/stats/PlaybackStats.cpp
    ../src/playback/stats/PlaybackDiagnostics.cpp
    ../src/playback/preview/ClipPreviewPlayer.cpp
    ../src/app/mcp/tools/McpDispatcher.cpp
    ../src/app/mcp/tools/McpDispatcherExtended.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/app/mcp/tools/McpEffectAuthoring.cpp
    ../src/app/mcp/protocol/McpValidate.cpp
    ../src/app/mcp/tools/McpMarket.cpp
    ../src/app/online/market/MarketClient.cpp
    ../src/app/online/assets/DriftAssetStore.cpp
    ../src/app/online/market/MarketIdentity.cpp
    ../src/app/mcp/transport/McpServer.cpp
    ../src/app/mcp/transport/McpStdio.cpp
    ../src/app/mcp/transport/McpBridge.cpp
    ../src/app/mcp/transport/McpStdioServer.cpp
)
target_include_directories(tst_mcp PRIVATE ../src)
target_link_libraries(tst_mcp PRIVATE Qt6::Test Qt6::Gui Qt6::Quick Qt6::Network Qt6::Multimedia Qt6::Concurrent driftcore driftengine)
target_compile_definitions(tst_mcp PRIVATE DRIFT_VERSION="${PROJECT_VERSION}"
    DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME Mcp COMMAND tst_mcp)
drift_copy_effects(tst_mcp)
drift_copy_transitions(tst_mcp)

qt_add_executable(tst_thememanager
    tst_thememanager.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/core/base/ZipArchive.cpp
    ../src/engine/platform/AndroidUri.cpp)
target_include_directories(tst_thememanager PRIVATE ../src)
target_compile_definitions(tst_thememanager PRIVATE
    "DRIFT_MAIN_CPP=\"${CMAKE_SOURCE_DIR}/src/app/main.cpp\""
)
target_link_libraries(tst_thememanager PRIVATE Qt6::Test Qt6::Qml Qt6::Quick ZLIB::ZLIB)
qt_add_resources(tst_thememanager "drift_themes"
    PREFIX "/themes"
    BASE "${CMAKE_SOURCE_DIR}/resources/themes"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/themes/dark.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/light.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/graphite.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/frost.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/theme.schema.json"
)
add_test(NAME ThemeManager COMMAND tst_thememanager)
set_tests_properties(ThemeManager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Runs lupdate and fails if i18n/*.ts would change — i.e. someone added qsTr/tr and
# did not run `cmake --build build --target update_translations`. Skipped where lupdate cannot
# parse QML: there the test would rewrite the checkout's catalogs without their QML strings
# before failing on the diff it caused itself (#167).
if(DRIFT_LUPDATE_HAS_QML)
    add_test(NAME Translations
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -DCONFIG=$<CONFIG>
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckTranslations.cmake
    )
else()
    message(STATUS "lupdate has no QML support; skipping the Translations test.")
endif()

set(_drift_windowbackdrop_platform)
foreach(_src ${DRIFT_BACKDROP_PLATFORM_SOURCES})
    list(APPEND _drift_windowbackdrop_platform "${CMAKE_SOURCE_DIR}/${_src}")
endforeach()
qt_add_executable(tst_windowbackdrop
    tst_windowbackdrop.cpp
    ../src/ui/theme/backdrop/WindowBackdrop.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/core/base/ZipArchive.cpp
    ../src/engine/platform/AndroidUri.cpp
    ${_drift_windowbackdrop_platform}
)
target_include_directories(tst_windowbackdrop PRIVATE ../src)
target_compile_definitions(tst_windowbackdrop PRIVATE
    "DRIFT_MAIN_CPP=\"${CMAKE_SOURCE_DIR}/src/app/main.cpp\""
    ${DRIFT_BACKDROP_PLATFORM_DEFS}
)
target_link_libraries(tst_windowbackdrop PRIVATE Qt6::Test Qt6::Qml Qt6::Quick ZLIB::ZLIB ${DRIFT_BACKDROP_PLATFORM_LIBS})
qt_add_resources(tst_windowbackdrop "drift_themes"
    PREFIX "/themes"
    BASE "${CMAKE_SOURCE_DIR}/resources/themes"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/themes/dark.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/light.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/graphite.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/frost.json"
        "${CMAKE_SOURCE_DIR}/resources/themes/theme.schema.json"
)
add_test(NAME WindowBackdrop COMMAND tst_windowbackdrop)
set_tests_properties(WindowBackdrop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_workspacestore tst_workspacestore.cpp ../src/app/editor/WorkspaceStore.cpp)
target_include_directories(tst_workspacestore PRIVATE ../src)
target_link_libraries(tst_workspacestore PRIVATE Qt6::Test Qt6::Core)
qt_add_resources(tst_workspacestore "workspace_presets"
    PREFIX "/workspaces"
    BASE ../resources/workspaces
    FILES
        ../resources/workspaces/edit.json
        ../resources/workspaces/edit-portrait.json
        ../resources/workspaces/source-lab.json
        ../resources/workspaces/audio.json
        ../resources/workspaces/record.json
        ../resources/workspaces/subtitles.json
        ../resources/workspaces/ai.json
        ../resources/workspaces/color.json
        ../resources/workspaces/keyframes.json
        ../resources/workspaces/masking-tracking.json
)
add_test(NAME WorkspaceStore COMMAND tst_workspacestore)

qt_add_executable(tst_searchindex tst_searchindex.cpp ../src/app/editor/SearchIndex.cpp)
target_include_directories(tst_searchindex PRIVATE ../src)
target_link_libraries(tst_searchindex PRIVATE Qt6::Test Qt6::Core)
target_compile_definitions(tst_searchindex PRIVATE DRIFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME SearchIndex COMMAND tst_searchindex)

qt_add_executable(tst_tracking tst_tracking.cpp ../src/engine/tracking/MotionTracking.cpp)
target_include_directories(tst_tracking PRIVATE ../src)
target_link_libraries(tst_tracking PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME Tracking COMMAND tst_tracking)
