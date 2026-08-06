cmake_minimum_required(VERSION 3.21)
# C is enabled for JUCE only: its modules compile a handful of C sources (zlib, flac, oggvorbis)
# and its CMake refuses to configure without it.
project(Drift VERSION 0.1.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(WITH_BGREMOVAL "Enable OpenCV-based background removal" OFF)

# Fonts, stickers and the Whisper model used to be fetched or extracted here and shipped inside
# the build tree — roughly 800 MB of assets every user paid for whether or not they used them.
# They are now addons: signed .driftpkg archives downloaded at runtime into <AppDataLocation>/
# addons/ (src/engine/AddonPackage, src/models/AddonManager). Nothing asset-shaped is bundled,
# so a fresh build starts with system fonts, an empty sticker tab and no transcriber, and the
# user installs what they want from the Addon Manager. The packaging and publishing tooling
# lives in the drift-addons repo.
#
# Effects and transitions are the exception: they are small, they are source (GLSL reviewed
# alongside the engine that runs them), and an editor with an empty effects browser is broken
# rather than merely bare — so they still ship next to the binary. They are *also* addons, and an
# installed package outranks the bundled one of the same id (see defaultSearchPaths), which is
# what lets a shader fix go out without an app release.
#
# For development without downloading, point DRIFT_EFFECTS_DIR, DRIFT_TRANSITIONS_DIR,
# DRIFT_FONTS_DIR, DRIFT_STICKERS_DIR, DRIFT_EMOJI_FONT_DIR or DRIFT_WHISPER_MODEL_DIR at a local
# directory; those outrank everything, including installed addons.

# --- Addon service ----------------------------------------------------------
# Where the Addon Manager fetches from. These are the only place either value is defined —
# src/models/AddonEndpoint.h just reads them — so a fork can point at its own Worker with:
#
#   cmake -B build -DDRIFT_ADDON_INDEX_URL=https://addons.example.com/v1/index \
#                  -DDRIFT_ADDON_CLIENT_TOKEN=...
#
# Set DRIFT_ADDON_INDEX_URL to an empty string to build with the addon service disabled: the
# manager then lists nothing and installs nothing, and the app runs on locally installed or
# side-loaded content only.
#
# The token is NOT a secret. It ships inside every binary and is trivially extractable; it exists
# so the bucket cannot be crawled or hotlinked, which would cost operations for someone else's
# benefit. That is also why it is fine in CMakeCache.txt.
set(DRIFT_ADDON_INDEX_URL "https://drift-addons.cutwire.org/v1/index"
    CACHE STRING "Addon index endpoint; empty disables the addon service")
set(DRIFT_ADDON_CLIENT_TOKEN "79a388bba9301cacc81cc6eb0724c81e948e1b27c69aee1b"
    CACHE STRING "Client token sent to the addon service as X-Drift-Client")

# --- Update check -----------------------------------------------------------
# Where src/models/UpdateChecker looks for a newer release, once a day, to raise the badge in the
# header. GitHub's releases/latest is the source of truth because the release tag already is: it
# excludes drafts and pre-releases, and needs no service of our own.
#
# Set it to an empty string for builds a package manager owns — the Flatpak manifests and the Arch
# PKGBUILD all do. There, pacman or flatpak fetches the update on its own schedule and an in-app
# nag is noise the user cannot act on (Flathub's quality guidelines say as much).
set(DRIFT_UPDATE_FEED_URL "https://api.github.com/repos/CutWire-Studios/Drift/releases/latest"
    CACHE STRING "Release feed polled for updates; empty disables the update check")

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Multimedia Test Concurrent Widgets OpenGL Network QuickEffects)
find_package(FFmpeg REQUIRED)

# ONNX Runtime powers auto subtitles, object segmentation, face tracking and noise removal. The
# app links none of it: it compiles against the headers and dlopens a runtime at startup
# (src/engine/OrtRuntime.cpp), so the CPU / CUDA / WebGPU decision belongs to the user and is made
# by installing an addon, not by whoever configured this build. Only the headers are needed here.
option(DRIFT_FETCH_ONNXRUNTIME
    "Download prebuilt ONNX Runtime headers when they are not installed (cmake/FetchOnnxRuntime.cmake)" ON)
# Stage a CPU runtime into <appDir>/onnxruntime so the build tree can transcribe without going
# through the addon service. Packaged builds turn this off — they ship no runtime at all.
option(DRIFT_BUNDLE_ONNXRUNTIME
    "Stage a CPU ONNX Runtime into the build tree for development" ON)
find_package(OnnxRuntime QUIET)
if(DRIFT_BUNDLE_ONNXRUNTIME AND OnnxRuntime_FOUND AND NOT ONNXRUNTIME_RUNTIME_LIBS
   AND DRIFT_FETCH_ONNXRUNTIME)
    # Headers with no library beside them (a headers-only system package) leave the build tree
    # with nothing to load, which looks exactly like a broken install at runtime.
    message(STATUS "ONNX Runtime headers found without a library; downloading one to bundle")
    set(OnnxRuntime_FOUND FALSE)
endif()
if(NOT OnnxRuntime_FOUND AND DRIFT_FETCH_ONNXRUNTIME)
    include(FetchOnnxRuntime)
    find_package(OnnxRuntime REQUIRED)
endif()
if(NOT OnnxRuntime_FOUND)
    message(FATAL_ERROR
        "ONNX Runtime headers not found. Install them and set ONNXRUNTIME_ROOT, "
        "or enable DRIFT_FETCH_ONNXRUNTIME to download them automatically.")
endif()
message(STATUS "ONNX Runtime headers: ${OnnxRuntime_INCLUDE_DIRS}")
if(DRIFT_BUNDLE_ONNXRUNTIME AND NOT ONNXRUNTIME_RUNTIME_LIBS)
    message(WARNING
        "DRIFT_BUNDLE_ONNXRUNTIME is on but no ONNX Runtime library was found to stage. "
        "The build tree will have no runtime; install an Acceleration addon or point "
        "DRIFT_ONNXRUNTIME_DIR at an extracted release.")
endif()

if(WITH_BGREMOVAL)
    # Link only modules needed for background removal — not the full OpenCV
    # distribution (opencv_viz pulls in VTK and breaks linking on many systems).
    find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
endif()

# Addon packages (.driftpkg) are solid-zstd compressed and Ed25519 signed. zstd upstream only
# ships a CMake config in recent releases, so fall back to pkg-config.
find_package(PkgConfig QUIET)
find_package(zstd CONFIG QUIET)
if(TARGET zstd::libzstd_shared)
    set(DRIFT_ZSTD_TARGET zstd::libzstd_shared)
elseif(TARGET zstd::libzstd)
    set(DRIFT_ZSTD_TARGET zstd::libzstd)
elseif(PkgConfig_FOUND)
    pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
    set(DRIFT_ZSTD_TARGET PkgConfig::ZSTD)
else()
    message(FATAL_ERROR "libzstd not found: no zstd CMake config and no pkg-config to fall back on")
endif()
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# SoundTouch does the pitch shifting behind the voice effects. juce_dsp has no pitch shifter, and a
# hand-rolled granular one cancels the signal outright wherever the tap spacing lands on an odd
# number of half-periods of the output. LGPL-2.1, whose section 3 allows use under GPL v2 or later,
# so it is compatible with this project's GPL-3.0.
# pkg-config first: SoundTouch's installed CMake config names include directories that do not
# exist, and a non-existent INTERFACE_INCLUDE_DIRECTORIES entry on an imported target is a hard
# generate-time error rather than something we can fall back from. Where there is no pkg-config at
# all (MSVC), the CMake config below is the only path, so it gets repaired instead.
if(PkgConfig_FOUND)
    pkg_check_modules(SOUNDTOUCH QUIET IMPORTED_TARGET soundtouch)
endif()
if(TARGET PkgConfig::SOUNDTOUCH)
    set(DRIFT_SOUNDTOUCH_TARGET PkgConfig::SOUNDTOUCH)
    # soundtouch.pc often exports -I.../include/soundtouch (for <SoundTouch.h>), but our sources
    # use <soundtouch/SoundTouch.h> and need .../include. Prefer the parent when that layout is
    # present (vcpkg on Windows with pkgconf).
    get_target_property(_st_pc_includes PkgConfig::SOUNDTOUCH INTERFACE_INCLUDE_DIRECTORIES)
    if(_st_pc_includes)
        set(_st_usable "")
        foreach(_dir IN LISTS _st_pc_includes)
            if(EXISTS "${_dir}/soundtouch/SoundTouch.h")
                list(APPEND _st_usable "${_dir}")
            elseif(EXISTS "${_dir}/SoundTouch.h")
                get_filename_component(_parent "${_dir}" DIRECTORY)
                if(EXISTS "${_parent}/soundtouch/SoundTouch.h")
                    list(APPEND _st_usable "${_parent}")
                else()
                    list(APPEND _st_usable "${_dir}")
                endif()
            elseif(IS_DIRECTORY "${_dir}")
                list(APPEND _st_usable "${_dir}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _st_usable)
        set_target_properties(PkgConfig::SOUNDTOUCH PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_st_usable}")
    endif()
else()
    find_package(SoundTouch CONFIG REQUIRED)
    set(DRIFT_SOUNDTOUCH_TARGET SoundTouch::SoundTouch)
    # Upstream 2.4.x exports "<prefix>/include;<prefix>/include;<prefix>/COMPONENT;<prefix>/
    # SoundTouch" — two install() keywords that leaked into INCLUDES DESTINATION, plus a duplicate.
    # Every packaging of that release carries it, vcpkg's included. <prefix>/include is the real
    # one and the only one <soundtouch/SoundTouch.h> needs.
    get_target_property(_st_includes SoundTouch::SoundTouch INTERFACE_INCLUDE_DIRECTORIES)
    if(_st_includes)
        set(_st_usable "")
        foreach(_dir IN LISTS _st_includes)
            if(IS_DIRECTORY "${_dir}")
                list(APPEND _st_usable "${_dir}")
            else()
                message(STATUS "SoundTouch: ignoring exported include dir that does not exist: ${_dir}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _st_usable)
        set_target_properties(SoundTouch::SoundTouch PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_st_usable}")
    endif()
endif()

# --- JUCE -------------------------------------------------------------------
# Audio effects are JUCE DSP processors (src/engine/audio). Only juce_dsp and the modules it pulls
# in are used — deliberately NOT juce_audio_processors, which depends on juce_gui_extra ->
# juce_gui_basics and would drag X11, freetype and fontconfig into an engine that is headless on
# purpose. JUCE never opens the sound card here either: Qt Multimedia owns the sink, and JUCE only
# processes float blocks inside AudioMixer.
#
# Acquisition mirrors ONNX Runtime above: a local tree wins, otherwise fetch. flatpak-builder runs
# offline, so packaged builds set DRIFT_JUCE_DIR at a source it vendored itself and turn the fetch
# off (see flatpak/org.cutwire.Drift.yml).
# Drift links exactly one JUCE module and calls none of JUCE's project helpers, so everything past
# add_subdirectory(modules) is dead weight: it configures and builds juceaide, and — the part that
# actually leaks — adds install() rules that drop JUCE's headers, its LV2 helper binary and its
# CMake package config into any `cmake --install` prefix, i.e. into the AppImage and the distro
# package. FORCE because it has to win over JUCE's own option() default either way it is acquired.
set(JUCE_MODULES_ONLY ON CACHE BOOL "Only configure the JUCE modules" FORCE)

option(DRIFT_FETCH_JUCE "Download JUCE when DRIFT_JUCE_DIR is not set" ON)
# STRING, not PATH, on purpose: CMake rewrites a relative PATH cache entry to an absolute one
# against the working directory of the cmake invocation — the *build* directory. The manifests
# vendor JUCE next to CMakeLists.txt and pass `-DDRIFT_JUCE_DIR=juce`, which a PATH would resolve
# to <build>/juce and fail. As a STRING the value arrives verbatim and is resolved below, against
# the source tree, which is what a relative path here can only sensibly mean.
set(DRIFT_JUCE_DIR "" CACHE STRING
    "JUCE source tree — absolute, or relative to this file; overrides DRIFT_FETCH_JUCE")
set(DRIFT_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag to fetch when DRIFT_JUCE_DIR is unset")

if(DRIFT_JUCE_DIR)
    if(IS_ABSOLUTE "${DRIFT_JUCE_DIR}")
        set(_drift_juce_dir "${DRIFT_JUCE_DIR}")
    else()
        set(_drift_juce_dir "${CMAKE_CURRENT_SOURCE_DIR}/${DRIFT_JUCE_DIR}")
    endif()
    if(NOT EXISTS "${_drift_juce_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "DRIFT_JUCE_DIR=${DRIFT_JUCE_DIR} does not point at a JUCE source tree "
            "(looked in ${_drift_juce_dir}).")
    endif()
    message(STATUS "JUCE: using ${_drift_juce_dir}")
    add_subdirectory("${_drift_juce_dir}" "${CMAKE_BINARY_DIR}/juce-build")
elseif(DRIFT_FETCH_JUCE)
    message(STATUS "JUCE: fetching ${DRIFT_JUCE_TAG}")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${DRIFT_JUCE_TAG}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(JUCE)
else()
    message(FATAL_ERROR
        "JUCE not available. Point DRIFT_JUCE_DIR at a JUCE source tree, "
        "or enable DRIFT_FETCH_JUCE to download it automatically.")
endif()

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

# --- core: domain model, no GUI ---------------------------------------------
add_library(driftcore STATIC
    src/core/Effect.h
    src/core/Effect.cpp
    src/core/EffectPreset.h
    src/core/MediaAsset.h
    src/core/MediaAsset.cpp
    src/core/Clip.h
    src/core/Clip.cpp
    src/core/Bezier.h
    src/core/SpeedCurve.h
    src/core/SpeedCurve.cpp
    src/core/SubtitleCue.h
    src/core/SubtitleCue.cpp
    src/core/SrtIO.h
    src/core/SrtIO.cpp
    src/core/Mask.h
    src/core/Mask.cpp
    src/core/Transition.h
    src/core/Transition.cpp
    src/core/TextStyle.h
    src/core/TextStyle.cpp
    src/core/ShapeStyle.h
    src/core/ShapeStyle.cpp
    src/core/ShapePath.h
    src/core/ShapePath.cpp
    src/core/Track.h
    src/core/Track.cpp
    src/core/Project.h
    src/core/Project.cpp
    src/core/TimelineOps.h
    src/core/TimelineOps.cpp
    src/core/commands/ProjectCommands.h
    src/core/commands/ProjectCommands.cpp
)
target_include_directories(driftcore PUBLIC src)
target_link_libraries(driftcore PUBLIC Qt6::Core Qt6::Gui)

# Audio effect DSP. JUCE modules compile into whatever target links them, so keeping them in a
# library of their own is what stops JUCE's headers, its compile definitions and its build time
# from spreading across driftengine. AudioEffectRack.h is the only header that leaves here and it
# mentions no JUCE type, so driftengine stays JUCE-free at the source level.
add_library(driftaudiodsp STATIC
    src/engine/audio/AudioEffectProcessor.h
    src/engine/audio/AudioEffectProcessor.cpp
    src/engine/audio/FilterProcessors.h
    src/engine/audio/FilterProcessors.cpp
    src/engine/audio/DynamicsProcessors.h
    src/engine/audio/DynamicsProcessors.cpp
    src/engine/audio/ModulationProcessors.h
    src/engine/audio/ModulationProcessors.cpp
    src/engine/audio/SoundTouchPitchProcessor.h
    src/engine/audio/SoundTouchPitchProcessor.cpp
    src/engine/audio/AudioEffectFactory.h
    src/engine/audio/AudioEffectFactory.cpp
    src/engine/audio/AudioEffectRack.h
    src/engine/audio/AudioEffectRack.cpp
    src/engine/audio/ClipAudioRetimer.h
    src/engine/audio/ClipAudioRetimer.cpp
)
# Nothing here is a QObject, and AUTOMOC would otherwise scan JUCE's amalgamated sources.
set_target_properties(driftaudiodsp PROPERTIES AUTOMOC OFF)
target_include_directories(driftaudiodsp PUBLIC src)
target_link_libraries(driftaudiodsp PUBLIC driftcore)
target_link_libraries(driftaudiodsp PRIVATE juce::juce_dsp juce::juce_recommended_config_flags
    ${DRIFT_SOUNDTOUCH_TARGET})
# JUCE_USE_CURL and JUCE_WEB_BROWSER matter: without them juce_core links libcurl on Linux, which
# is exactly the dependency creep this split exists to prevent.
target_compile_definitions(driftaudiodsp PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    # Otherwise every binary that links this, tools and tests included, opens with "JUCE v8.0.15".
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
)

# --- engine: FFmpeg-facing code, no QML deps -------------------------------
add_library(driftengine STATIC
    src/engine/AddonPackage.h
    src/engine/AddonPackage.cpp
    src/engine/AddonRegistry.h
    src/engine/AddonRegistry.cpp
    src/engine/AddonSigningKey.h
    src/engine/MediaProbe.h
    src/engine/MediaProbe.cpp
    src/engine/MediaThumbnail.h
    src/engine/MediaThumbnail.cpp
    src/engine/FilmstripTileCache.h
    src/engine/FilmstripTileCache.cpp
    src/engine/MediaWaveform.h
    src/engine/MediaWaveform.cpp
    src/engine/WaveformBlockCache.h
    src/engine/WaveformBlockCache.cpp
    src/engine/AudioOnsets.h
    src/engine/AudioOnsets.cpp
    src/engine/Exporter.h
    src/engine/Exporter.cpp
    src/engine/ClipReader.h
    src/engine/ClipReader.cpp
    src/engine/ClipReaderWorker.h
    src/engine/ClipReaderWorker.cpp
    src/engine/ClipReaderPool.h
    src/engine/ClipReaderPool.cpp
    src/engine/EffectProcessor.h
    src/engine/EffectProcessor.cpp
    src/engine/EffectCatalog.h
    src/engine/EffectCatalog.cpp
    src/engine/FontCatalog.h
    src/engine/FontCatalog.cpp
    src/engine/StickerCatalog.h
    src/engine/StickerCatalog.cpp
    src/engine/EmojiCatalog.h
    src/engine/EmojiCatalog.cpp
    src/engine/EmojiTable.h
    src/engine/EmojiTable.cpp
    src/engine/TextRaster.h
    src/engine/TextRaster.cpp
    src/engine/EffectPackageLoader.h
    src/engine/EffectPackageLoader.cpp
    src/engine/TransitionCatalog.h
    src/engine/TransitionCatalog.cpp
    src/engine/TransitionPackageLoader.h
    src/engine/TransitionPackageLoader.cpp
    src/engine/GpuPackageParse.h
    src/engine/GpuPackageParse.cpp
    src/engine/GpuEffectDefinition.h
    src/engine/GpuCompositor.h
    src/engine/GpuCompositor.cpp
    src/engine/GlRuntime.h
    src/engine/GlRuntime.cpp
    src/engine/GpuEffectExecutor.h
    src/engine/GpuEffectExecutor.cpp
    src/engine/CompositorFrameHistory.h
    src/engine/CompositorFrameHistory.cpp
    src/engine/FrameCompositor.h
    src/engine/FrameCompositor.cpp
    src/engine/MaskApplier.h
    src/engine/MaskApplier.cpp
    src/engine/AudioEffectCatalog.h
    src/engine/AudioEffectCatalog.cpp
    src/engine/EffectTemplateCatalog.h
    src/engine/EffectTemplateCatalog.cpp
    src/engine/AudioMixer.h
    src/engine/AudioMixer.cpp
    src/engine/WhisperTokenizer.h
    src/engine/WhisperTokenizer.cpp
    src/engine/WhisperTranscriber.h
    src/engine/WhisperTranscriber.cpp
    src/engine/OrtSupport.h
    src/engine/OrtRuntime.h
    src/engine/OrtRuntime.cpp
    src/engine/Sam2Segmenter.h
    src/engine/Sam2Segmenter.cpp
    src/engine/FaceLandmarker.h
    src/engine/FaceLandmarker.cpp
    src/engine/FaceTrack.h
    src/engine/FaceTrack.cpp
    src/engine/FaceModelTransform.h
    src/engine/FaceModelTransform.cpp
    src/engine/ModelAsset.h
    src/engine/ModelAsset.cpp
    src/engine/GlModelRenderer.h
    src/engine/GlModelRenderer.cpp
    src/engine/FacePropCatalog.h
    src/engine/FacePropCatalog.cpp
    src/engine/DeepFilterDenoiser.h
    src/engine/DeepFilterDenoiser.cpp
    src/engine/MatteWriter.h
    src/engine/MatteWriter.cpp
    src/engine/ReverseProxyCache.h
    src/engine/ReverseProxyCache.cpp
    src/engine/ReverseRenderer.h
    src/engine/ReverseRenderer.cpp
    src/engine/AudioFileWriter.h
    src/engine/AudioFileWriter.cpp
    src/engine/ProjectBundle.h
    src/engine/ProjectBundle.cpp
    src/engine/ProjectDependencies.h
    src/engine/ProjectDependencies.cpp
)
target_include_directories(driftengine PUBLIC src)
# cgltf is PRIVATE: ModelAsset.h exposes no cgltf type, so nothing outside ModelAsset.cpp includes it.
target_include_directories(driftengine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/cgltf)
target_link_libraries(driftengine PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL FFmpeg::FFmpeg driftcore
    driftaudiodsp onnxruntime::headers)
target_link_libraries(driftengine PRIVATE ${DRIFT_ZSTD_TARGET} OpenSSL::Crypto ${CMAKE_DL_LIBS})
target_compile_definitions(driftengine PRIVATE DRIFT_VERSION="${PROJECT_VERSION}")

if(WITH_BGREMOVAL)
    target_include_directories(driftengine PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(driftengine PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(driftengine PUBLIC DRIFT_WITH_BGREMOVAL)
endif()

# --- application ------------------------------------------------------------
qt_add_executable(drift
    src/main.cpp
    src/DriftImageProvider.h
    src/DriftImageProvider.cpp
    src/SegmentImageProvider.h
    src/SegmentImageProvider.cpp
    src/SegmentImageStore.h
    src/SegmentImageStore.cpp
    src/ClipPreviewImageProvider.h
    src/ClipPreviewImageProvider.cpp
    src/ClipPreviewImageStore.h
    src/ClipPreviewImageStore.cpp
    src/TextStylePreviewImageProvider.h
    src/TextStylePreviewImageProvider.cpp
    src/models/AddonEndpoint.h
    src/models/AddonManager.h
    src/models/AddonManager.cpp
    src/models/VersionCompare.h
    src/models/UpdateChecker.h
    src/models/UpdateChecker.cpp
    src/models/AppController.h
    src/models/AppController.cpp
    src/models/EditorState.h
    src/models/FileDialogs.h
    src/models/FileDialogs.cpp
    src/models/AssetLibrary.h
    src/models/AssetLibrary.cpp
    src/models/TimelineModel.h
    src/models/TimelineModel.cpp
    src/models/ClipListModel.h
    src/models/ClipListModel.cpp
    src/playback/PlaybackEngine.h
    src/playback/PlaybackEngine.cpp
    src/playback/PlaybackClock.h
    src/playback/PlaybackClock.cpp
    src/playback/ClipPreviewPlayer.h
    src/playback/ClipPreviewPlayer.cpp
    src/playback/CompositorService.h
    src/playback/CompositorService.cpp
    src/preview/PreviewItem.h
    src/preview/PreviewItem.cpp
)

# Windows needs the icon embedded in the PE resource section for Explorer and
# the taskbar; setWindowIcon alone is not enough for the .exe identity.
if(WIN32)
    # qt_add_executable() forwards to add_executable() without WIN32, so the linker defaults to the
    # console subsystem and Windows opens a console window before the GUI comes up. Qt6::EntryPoint
    # (linked in by Qt6::Core) supplies the WinMain that forwards to main().
    set_target_properties(drift PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(drift PRIVATE resources/windows/drift.rc)
    set_source_files_properties(resources/windows/drift.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/windows/drift.ico")
endif()

# App icon for QApplication::setWindowIcon (all platforms). Rendered from Drift_icon.png,
# as is resources/windows/drift.ico — keep the two in sync when the branding changes.
qt_add_resources(drift "drift_app_icon"
    PREFIX "/app"
    BASE resources
    FILES resources/drift.png
)

set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# App-wide notification queue; the visual host is components/ToastHost.qml.
set_source_files_properties(src/qml/Toasts.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

# Lucide icons: SVG sources in resources/icons/ (from lucide-icons-1.25.0), PNGs for QML.
file(GLOB DRIFT_LUCIDE_ICONS RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
     "resources/icons/*.png")

qt_add_qml_module(drift
    URI Drift
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/Theme.qml
        src/qml/Toasts.qml
        src/qml/EditorHeader.qml
        src/qml/AssetsPanel.qml
        src/qml/PreviewPanel.qml
        src/qml/components/preview/TransformOverlay.qml
        src/qml/components/preview/CropOverlay.qml
        src/qml/components/preview/PreviewToolbar.qml
        src/qml/SegmentationWindow.qml
        src/qml/DenoiseWindow.qml
        src/qml/SpeedCurveWindow.qml
        src/qml/PropertiesPanel.qml
        src/qml/components/properties/GeneralInspector.qml
        src/qml/components/properties/TextInspector.qml
        src/qml/components/properties/TransformInspector.qml
        src/qml/components/properties/AudioInspector.qml
        src/qml/components/properties/SpeedFadeInspector.qml
        src/qml/components/properties/TransitionInspector.qml
        src/qml/components/properties/BlendingInspector.qml
        src/qml/components/properties/ShapeInspector.qml
        src/qml/components/properties/MasksInspector.qml
        src/qml/components/properties/EffectsInspector.qml
        src/qml/components/assets/TextAssetsTab.qml
        src/qml/components/assets/StickersTab.qml
        src/qml/components/assets/ShapesTab.qml
        src/qml/components/assets/SoundsTab.qml
        src/qml/components/assets/SettingsTab.qml
        src/qml/components/assets/ShortcutsTab.qml
        src/qml/components/assets/MediaAssetsTab.qml
        src/qml/TimelinePanel.qml
        src/qml/components/timeline/TimelineToolbar.qml
        src/qml/components/timeline/TrackHeaderColumn.qml
        src/qml/components/timeline/TimelineClipItem.qml
        src/qml/components/IconGlyph.qml
        src/qml/components/IconButton.qml
        src/qml/components/PanelFrame.qml
        src/qml/components/ClipFilmstrip.qml
        src/qml/components/AppScrollBar.qml
        src/qml/components/KeyframeDiamond.qml
        src/qml/components/PropertyKeyframeRow.qml
        src/qml/components/KeyframeGraph.qml
        src/qml/components/SubtitleCueLane.qml
        src/qml/components/ShortcutCaptureField.qml
        src/qml/components/ShapePreview.qml
        src/qml/components/ColorSwatchField.qml
        src/qml/components/EffectBrowser.qml
        src/qml/components/AudioEffectBrowser.qml
        src/qml/components/EffectTemplateBrowser.qml
        src/qml/components/ThemedSlider.qml
        src/qml/components/ThemedTextField.qml
        src/qml/components/ThemedNumberField.qml
        src/qml/components/ThemedTextArea.qml
        src/qml/components/SubtitleEditor.qml
        src/qml/components/ThemedComboBox.qml
        src/qml/components/FontPicker.qml
        src/qml/components/EmojiPicker.qml
        src/qml/components/ThemedButton.qml
        src/qml/components/ThemedChip.qml
        src/qml/components/ThemedProgressBar.qml
        src/qml/components/ThemedLabel.qml
        src/qml/components/ThemedDialog.qml
        src/qml/components/AddonManagerDialog.qml
        src/qml/components/AddonStartupDialog.qml
        src/qml/components/MissingAddonsDialog.qml
        src/qml/components/UpdateDialog.qml
        src/qml/components/ProjectSetupDialog.qml
        src/qml/components/LayoutChooserDialog.qml
        src/qml/components/ProjectPropertiesDialog.qml
        src/qml/components/PackageProgressDialog.qml
        src/qml/components/ExportDialog.qml
        src/qml/components/ExportProgressDialog.qml
        src/qml/components/ReverseProgressDialog.qml
        src/qml/components/SubtitleProgressDialog.qml
        src/qml/components/CircularProgress.qml
        src/qml/components/RecoveryDialog.qml
        src/qml/components/UnsavedChangesDialog.qml
        src/qml/components/RecentProjectsPopup.qml
        src/qml/components/NewTrackMenu.qml
        src/qml/components/ToastHost.qml
        src/qml/components/EmptyState.qml
        src/qml/components/SkeletonBox.qml
        src/qml/components/ThemedSwitch.qml
        src/qml/components/ThemedCheckBox.qml
        src/qml/components/ThemedToggleButton.qml
        src/qml/components/ThemedToolTip.qml
        src/qml/components/ThemedContextMenu.qml
        src/qml/components/ThemedMenuItem.qml
        src/qml/components/ThemedMenuSeparator.qml
        src/qml/components/LabelledProgressRing.qml
    RESOURCES
        # UI chrome: interface font and Lucide SVG icons (always needed, including before addons).
        resources/fonts/Inter.ttf
        ${DRIFT_LUCIDE_ICONS}
)

target_link_libraries(drift PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickEffects
    Qt6::Multimedia
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    driftcore
    driftengine
)
target_include_directories(drift PRIVATE src src/preview)

# Defined here rather than in the header so there is exactly one place to change them.
target_compile_definitions(drift PRIVATE
    DRIFT_ADDON_INDEX_URL="${DRIFT_ADDON_INDEX_URL}"
    DRIFT_ADDON_CLIENT_TOKEN="${DRIFT_ADDON_CLIENT_TOKEN}"
    DRIFT_UPDATE_FEED_URL="${DRIFT_UPDATE_FEED_URL}"
    DRIFT_VERSION="${PROJECT_VERSION}"
)

# Ship a file-based package directory next to the binaries that load the catalog.
#
# Several targets share one output directory (everything under tools/, the two engine tests), so
# the copy is registered once per destination and depended on, rather than as a POST_BUILD step per
# target: two `cmake -E copy_directory` processes writing the same tree concurrently — which is
# what `cmake --build --parallel` does — fail with "Error copying directory".
function(drift_stage_packages target assetdir)
    get_target_property(_outdir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _outdir)
        set(_outdir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    file(RELATIVE_PATH _key "${CMAKE_BINARY_DIR}" "${_outdir}/${assetdir}")
    string(MAKE_C_IDENTIFIER "drift_stage_${_key}" _stage)
    # Multi-config generators (the Windows job uses Visual Studio) put binaries in <dir>/<config>,
    # which is where the packaging step expects the staged packages as well.
    get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_multi_config)
        set(_outdir "${_outdir}/$<CONFIG>")
    endif()
    if(NOT TARGET ${_stage})
        add_custom_target(${_stage}
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/${assetdir}"
                    "${_outdir}/${assetdir}"
            COMMENT "Copy ${assetdir} packages into ${_outdir}"
        )
    endif()
    add_dependencies(${target} ${_stage})
endfunction()

function(drift_copy_effects target)
    drift_stage_packages(${target} effects)
endfunction()

function(drift_copy_transitions target)
    drift_stage_packages(${target} transitions)
endfunction()

# Audio effects ship as the "audio-effects" addon, so this only makes the build tree
# self-contained — <appDir>/audio-effects is already a search root.
function(drift_copy_audio_effects target)
    drift_stage_packages(${target} audio-effects)
endfunction()

# Stage an ONNX Runtime into <appDir>/onnxruntime/lib, which is one of the roots
# drift::ort::ensureLoaded() searches — the same relationship the bundled effects have with
# installed effect addons, and ranked the same way: an installed Acceleration addon wins over this.
# It is a development convenience only; packaged builds set DRIFT_BUNDLE_ONNXRUNTIME=OFF and ship
# no runtime at all.
function(drift_bundle_onnxruntime target)
    if(DRIFT_BUNDLE_ONNXRUNTIME AND ONNXRUNTIME_RUNTIME_LIBS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${target}>/onnxruntime/lib"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${ONNXRUNTIME_RUNTIME_LIBS}
                    "$<TARGET_FILE_DIR:${target}>/onnxruntime/lib"
            COMMENT "Stage onnxruntime into ${target}'s build tree"
        )
    endif()
endfunction()

function(drift_copy_effect_templates target)
    drift_stage_packages(${target} effect-templates)
endfunction()

drift_copy_effects(drift)
drift_copy_effect_templates(drift)
drift_copy_transitions(drift)
drift_copy_audio_effects(drift)
drift_bundle_onnxruntime(drift)

# --- install (used by packaging, e.g. Flatpak; the dev workflow runs in-place) ---
include(GNUInstallDirs)

# Everything Drift installs is tagged with the "drift" component, so packaging can ask for the app
# and nothing else: `cmake --install build --component drift`. JUCE's modules install ~50 MB of
# their own sources into include/JUCE-<version>, and a plain `cmake --install` has no way to say no
# to them. (The Flatpak manifests get the same result the other way round, by cleaning up /include.)
install(TARGETS drift RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT drift)

# File-based effect/transition/template/audio-effect packages ship next to the binary;
# the app searches <appDir>/<subdir> (GpuPackageParse::defaultSearchPaths). Installing
# them here (rather than via the DRIFT_*_DIR env vars) keeps installed addons able to
# supersede the bundled copies.
foreach(_assetdir effects transitions effect-templates audio-effects)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/${_assetdir}
            DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT drift)
endforeach()

# Desktop integration for Flatpak / freedesktop.
install(FILES flatpak/org.cutwire.Drift.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT drift)
install(FILES flatpak/org.cutwire.Drift.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo COMPONENT drift)
# 1024px source is intentionally skipped: flatpak build-export rejects icons
# larger than 512x512.
foreach(_size 64 128 256 512)
    install(FILES
        flatpak/share/icons/hicolor/${_size}x${_size}/apps/org.cutwire.Drift.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps COMPONENT drift)
endforeach()

# --- CLI tools (headless engine testing, no GUI) ----------------------------
add_subdirectory(tools)

# --- tests -------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
