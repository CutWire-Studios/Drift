qt_add_executable(tst_mediaprobe tst_mediaprobe.cpp)
target_link_libraries(tst_mediaprobe PRIVATE Qt6::Test driftengine)
add_test(NAME MediaProbe COMMAND tst_mediaprobe)

qt_add_executable(tst_core tst_core.cpp)
target_include_directories(tst_core PRIVATE ../src)
target_link_libraries(tst_core PRIVATE Qt6::Test driftcore)
add_test(NAME Core COMMAND tst_core)

qt_add_executable(tst_editorstate
    tst_editorstate.cpp
    ../src/SegmentImageStore.cpp
    ../src/ClipPreviewImageStore.cpp
    ../src/MulticamImageStore.cpp
    # The provider, not just the store: the id QML addresses a tile by is parsed here, and a
    # store test cannot see that half. Costs a Qt6::Quick link, but needs no QML engine —
    # requestImage is called directly.
    ../src/MulticamImageProvider.cpp
    ../src/models/AppController.cpp
    ../src/models/AddonManager.cpp
    ../src/models/AssetLibrary.cpp
    ../src/models/BinFolderListModel.cpp
    ../src/models/TimelineModel.cpp
    ../src/models/ClipListModel.cpp
    ../src/playback/PlaybackEngine.cpp
    ../src/playback/PlaybackClock.cpp
    ../src/playback/AudioOutputChannel.cpp
    ../src/playback/CompositorService.cpp
    ../src/playback/PlaybackStats.cpp
    ../src/playback/PlaybackDiagnostics.cpp
    ../src/playback/ClipPreviewPlayer.cpp
    ../src/mcp/McpSession.cpp
    ../src/mcp/McpCatalog.cpp
    ../src/mcp/McpProtocol.cpp
    ../src/mcp/McpHttp.cpp
    ../src/mcp/McpDispatcher.cpp
    ../src/mcp/McpDispatcherExtended.cpp
    ../src/mcp/McpServer.cpp
    ../src/mcp/McpStdio.cpp
)
target_include_directories(tst_editorstate PRIVATE ../src)
target_link_libraries(tst_editorstate PRIVATE Qt6::Test Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Concurrent Qt6::Network driftcore driftengine)
target_compile_definitions(tst_editorstate PRIVATE
    DRIFT_TEST_DENOISE_MODEL_DIR="${CMAKE_SOURCE_DIR}/models/deepfilternet3"
    DRIFT_VERSION="${PROJECT_VERSION}")
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

qt_add_executable(tst_layoutstore tst_layoutstore.cpp ../src/models/LayoutStore.cpp)
target_include_directories(tst_layoutstore PRIVATE ../src)
# Qt6::Gui for QScreen: restoring a window means checking it against the displays
# that are actually attached now.
target_link_libraries(tst_layoutstore PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME LayoutStore COMMAND tst_layoutstore)
# Screen-dependent assertions, and saveWindowState deliberately drops the window
# position under Wayland (the compositor owns it there). Pin the platform so the
# test means the same thing on a dev machine as it does headless in CI.
set_tests_properties(LayoutStore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_addonpackage tst_addonpackage.cpp)
target_include_directories(tst_addonpackage PRIVATE ../src)
target_link_libraries(tst_addonpackage PRIVATE Qt6::Test driftengine)
target_compile_definitions(tst_addonpackage PRIVATE
    DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME AddonPackage COMMAND tst_addonpackage)

qt_add_executable(tst_projectbundle tst_projectbundle.cpp)
target_include_directories(tst_projectbundle PRIVATE ../src)
# zstd directly: the test forges a manifest to exercise the format version gate.
target_link_libraries(tst_projectbundle PRIVATE Qt6::Test driftengine ${DRIFT_ZSTD_TARGET})
add_test(NAME ProjectBundle COMMAND tst_projectbundle)

qt_add_executable(tst_engine
    tst_engine.cpp
    # Diagnostics live in the playback layer but are engine-shaped: a collector plus a
    # plain-text rendering, exactly like DebugReport, and tested alongside it.
    ../src/playback/PlaybackStats.cpp
    ../src/playback/PlaybackDiagnostics.cpp
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
)
add_test(NAME Engine COMMAND tst_engine)
drift_copy_effects(tst_engine)
drift_copy_transitions(tst_engine)

qt_add_executable(tst_mcp
    tst_mcp.cpp
    ../src/mcp/McpSession.cpp
    ../src/mcp/McpCatalog.cpp
    ../src/mcp/McpProtocol.cpp
    ../src/mcp/McpHttp.cpp
    ../src/SegmentImageStore.cpp
    ../src/ClipPreviewImageStore.cpp
    ../src/MulticamImageStore.cpp
    ../src/models/AppController.cpp
    ../src/models/AddonManager.cpp
    ../src/models/AssetLibrary.cpp
    ../src/models/BinFolderListModel.cpp
    ../src/models/TimelineModel.cpp
    ../src/models/ClipListModel.cpp
    ../src/playback/PlaybackEngine.cpp
    ../src/playback/PlaybackClock.cpp
    ../src/playback/AudioOutputChannel.cpp
    ../src/playback/CompositorService.cpp
    ../src/playback/PlaybackStats.cpp
    ../src/playback/PlaybackDiagnostics.cpp
    ../src/playback/ClipPreviewPlayer.cpp
    ../src/mcp/McpDispatcher.cpp
    ../src/mcp/McpDispatcherExtended.cpp
    ../src/mcp/McpServer.cpp
    ../src/mcp/McpStdio.cpp
)
target_include_directories(tst_mcp PRIVATE ../src)
target_link_libraries(tst_mcp PRIVATE Qt6::Test Qt6::Gui Qt6::Network Qt6::Multimedia Qt6::Concurrent driftcore driftengine)
target_compile_definitions(tst_mcp PRIVATE DRIFT_VERSION="${PROJECT_VERSION}")
add_test(NAME Mcp COMMAND tst_mcp)
drift_copy_effects(tst_mcp)
drift_copy_transitions(tst_mcp)

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
