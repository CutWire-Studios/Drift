cmake_minimum_required(VERSION 3.21)

# Must be set before project(). Left unset, the binary is stamped with the build machine's OS
# version and will not launch on anything older.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Oldest macOS release Drift runs on")

# C is enabled for JUCE only: its modules compile a handful of C sources (zlib, flac, oggvorbis)
# and its CMake refuses to configure without it.
project(Drift VERSION 0.5.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(WITH_BGREMOVAL "Enable OpenCV-based background removal" OFF)

# Defaults off under CI so the Translations test still compares the committed catalogs.
if(DEFINED ENV{CI})
    set(_drift_auto_ts OFF)
else()
    set(_drift_auto_ts ON)
endif()
option(DRIFT_AUTO_UPDATE_TRANSLATIONS "Run lupdate as part of the default build" ${_drift_auto_ts})

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

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Multimedia Test Concurrent Widgets OpenGL Network QuickEffects Svg)
find_package(Qt6 COMPONENTS LinguistTools QUIET)

# IconImage loads Lucide SVGs from qrc. The SVG imageformat plugin is refused when it was
# built for a different Qt minor (6.10 plugin into 6.11 → "Unsupported image format", blank icons).
string(REGEX REPLACE "^([0-9]+\\.[0-9]+).*" "\\1" _drift_qt_mm "${Qt6_VERSION}")
string(REGEX REPLACE "^([0-9]+\\.[0-9]+).*" "\\1" _drift_svg_mm "${Qt6Svg_VERSION}")
if(NOT _drift_qt_mm STREQUAL _drift_svg_mm)
    message(FATAL_ERROR
        "Qt SVG ${Qt6Svg_VERSION} does not match Qt ${Qt6_VERSION}. "
        "Install a qt6-svg package built for the same Qt as qt6-base, or toolbar icons will not render.")
endif()

# --- Android prebuilt dependencies -------------------------------------------
# Cross-compiling to Android has no pkg-config and no system prefix, so FFmpeg, dav1d, zstd,
# OpenSSL and SoundTouch come from static PIC archives under third_party/prebuilt/android/<abi>,
# built by third_party/build-android.sh. They are declared here as the same targets the host
# branches below produce, so nothing downstream needs to know which way it was satisfied.
#
# Static, not shared: the app is a single .so that QtLoader dlopens, and every extra shared library
# would have to be listed in ANDROID_EXTRA_LIBS and bundled by hand. PIC is mandatory for the same
# reason — a non-PIC archive linked into a .so fails with an R_AARCH64_ADR_PREL_PG_HI21 relocation
# error. Drift is GPL-3.0, so statically linking the LGPL pieces is fine.
if(ANDROID)
    set(DRIFT_PREBUILT "${CMAKE_SOURCE_DIR}/third_party/prebuilt/android/${ANDROID_ABI}")
    if(NOT EXISTS "${DRIFT_PREBUILT}/lib/libavcodec.a")
        message(FATAL_ERROR
            "No prebuilt dependencies for ${ANDROID_ABI}. Run:\n"
            "    third_party/build-android.sh ${ANDROID_ABI}")
    endif()

    # Link order is dependency order; static archives are resolved left to right.
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    target_include_directories(FFmpeg::FFmpeg INTERFACE "${DRIFT_PREBUILT}/include")
    target_link_libraries(FFmpeg::FFmpeg INTERFACE
        "${DRIFT_PREBUILT}/lib/libavfilter.a"
        "${DRIFT_PREBUILT}/lib/libavformat.a"
        "${DRIFT_PREBUILT}/lib/libavcodec.a"
        "${DRIFT_PREBUILT}/lib/libswscale.a"
        "${DRIFT_PREBUILT}/lib/libswresample.a"
        "${DRIFT_PREBUILT}/lib/libavutil.a"
        "${DRIFT_PREBUILT}/lib/libx264.a"
        # libavcodec's only software AV1 decoder — the native "av1" decoder needs a hwaccel.
        "${DRIFT_PREBUILT}/lib/libdav1d.a"
        # mediandk resolves the AMediaCodec_*/AMediaFormat_* the --enable-mediacodec decoders call.
        m z mediandk)

    add_library(drift_zstd INTERFACE IMPORTED)
    target_include_directories(drift_zstd INTERFACE "${DRIFT_PREBUILT}/include")
    target_link_libraries(drift_zstd INTERFACE "${DRIFT_PREBUILT}/lib/libzstd.a")
    set(DRIFT_ZSTD_TARGET drift_zstd)

    add_library(OpenSSL::Crypto INTERFACE IMPORTED)
    target_include_directories(OpenSSL::Crypto INTERFACE "${DRIFT_PREBUILT}/include")
    target_link_libraries(OpenSSL::Crypto INTERFACE "${DRIFT_PREBUILT}/lib/libcrypto.a")

    add_library(drift_soundtouch INTERFACE IMPORTED)
    target_include_directories(drift_soundtouch INTERFACE "${DRIFT_PREBUILT}/include")
    target_link_libraries(drift_soundtouch INTERFACE "${DRIFT_PREBUILT}/lib/libSoundTouch.a")
    set(DRIFT_SOUNDTOUCH_TARGET drift_soundtouch)
endif()

if(NOT ANDROID)
    find_package(FFmpeg REQUIRED)
endif()

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
if(NOT ANDROID)
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
endif()

# SoundTouch does the pitch shifting behind the voice effects. juce_dsp has no pitch shifter, and a
# hand-rolled granular one cancels the signal outright wherever the tap spacing lands on an odd
# number of half-periods of the output. LGPL-2.1, whose section 3 allows use under GPL v2 or later,
# so it is compatible with this project's GPL-3.0.
# pkg-config first: SoundTouch's installed CMake config names include directories that do not
# exist, and a non-existent INTERFACE_INCLUDE_DIRECTORIES entry on an imported target is a hard
# generate-time error rather than something we can fall back from. Where there is no pkg-config at
# all (MSVC), the CMake config below is the only path, so it gets repaired instead.
if(NOT ANDROID)
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
    src/core/BinFolder.h
    src/core/MediaAsset.h
    src/core/MediaAsset.cpp
    src/core/Clip.h
    src/core/Clip.cpp
    src/core/Stabilize.h
    src/core/Stabilize.cpp
    src/core/Bezier.h
    src/core/SpeedCurve.h
    src/core/SpeedCurve.cpp
    src/core/FadeShape.h
    src/core/FadeShape.cpp
    src/core/ClipAnimation.h
    src/core/ClipAnimation.cpp
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
    src/core/TextPresetStore.h
    src/core/TextPresetStore.cpp
    src/core/EffectStackStore.h
    src/core/EffectStackStore.cpp
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
    src/engine/AndroidUri.h
    src/engine/AndroidUri.cpp
    src/engine/HwAccel.h
    src/engine/HwAccel.cpp
    src/engine/MediaProbe.h
    src/engine/MediaProbe.cpp
    src/engine/MediaThumbnail.h
    src/engine/MediaThumbnail.cpp
    src/engine/FilmstripTileCache.h
    src/engine/FilmstripTileCache.cpp
    src/engine/MediaWaveform.h
    src/engine/MediaWaveform.cpp
    src/engine/LoudnessMeter.h
    src/engine/LoudnessMeter.cpp
    src/engine/WaveformBlockCache.h
    src/engine/WaveformBlockCache.cpp
    src/engine/AudioOnsets.h
    src/engine/AudioOnsets.cpp
    src/engine/SceneDetect.h
    src/engine/SceneDetect.cpp
    src/engine/ObjectDetector.h
    src/engine/ObjectDetector.cpp
    src/engine/Exporter.h
    src/engine/Exporter.cpp
    src/engine/PreviewVideoFrame.h
    src/engine/ClipReader.h
    src/engine/ClipReader.cpp
    src/engine/DebugReport.h
    src/engine/DebugReport.cpp
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
    src/engine/GpuStatus.h
    src/engine/GpuStatus.cpp
    src/engine/VaapiZeroCopy.h
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
    src/engine/FaceMesh.h
    src/engine/FaceMesh.cpp
    src/engine/ModelAsset.h
    src/engine/ModelAsset.cpp
    src/engine/GlFaceSwapRenderer.h
    src/engine/GlFaceSwapRenderer.cpp
    src/engine/GlModelRenderer.h
    src/engine/GlModelRenderer.cpp
    src/engine/FaceSwapSource.h
    src/engine/FaceSwapSource.cpp
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
    src/engine/MediaEditor.h
    src/engine/MediaEditor.cpp
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
    src/MulticamImageProvider.h
    src/MulticamImageProvider.cpp
    src/MulticamImageStore.h
    src/MulticamImageStore.cpp
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
    src/models/LayoutStore.h
    src/models/LayoutStore.cpp
    src/models/FileDialogs.h
    src/models/FileDialogs.cpp
    src/models/Haptics.h
    src/models/Haptics.cpp
    src/models/AssetLibrary.h
    src/models/AssetLibrary.cpp
    src/models/BinFolderListModel.h
    src/models/BinFolderListModel.cpp
    src/models/TimelineModel.h
    src/models/TimelineModel.cpp
    src/models/ClipListModel.h
    src/models/ClipListModel.cpp
    src/playback/PlaybackEngine.h
    src/playback/PlaybackEngine.cpp
    src/playback/PlaybackClock.h
    src/playback/PlaybackClock.cpp
    src/playback/AudioOutputChannel.h
    src/playback/AudioOutputChannel.cpp
    src/playback/ClipPreviewPlayer.h
    src/playback/ClipPreviewPlayer.cpp
    src/playback/CompositorService.h
    src/playback/CompositorService.cpp
    src/playback/PlaybackStats.h
    src/playback/PlaybackStats.cpp
    src/playback/PlaybackDiagnostics.h
    src/playback/PlaybackDiagnostics.cpp
    src/preview/PreviewItem.h
    src/preview/PreviewItem.cpp
)

# MCP is a desktop agent protocol (stdio/HTTP). Skip it on Android APK builds.
if(NOT ANDROID)
    target_sources(drift PRIVATE
        src/mcp/McpSession.h
        src/mcp/McpSession.cpp
        src/mcp/McpCatalog.h
        src/mcp/McpCatalog.cpp
        src/mcp/McpProtocol.h
        src/mcp/McpProtocol.cpp
        src/mcp/McpHttp.h
        src/mcp/McpHttp.cpp
        src/mcp/McpDispatcher.h
        src/mcp/McpDispatcher.cpp
        src/mcp/McpDispatcherExtended.cpp
        src/mcp/McpServer.h
        src/mcp/McpServer.cpp
        src/mcp/McpStdio.h
        src/mcp/McpStdio.cpp
        src/mcp/McpJson.h
    )
endif()

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

# Without a bundle macOS runs the app as a background process, with no Dock tile or menu bar.
# It also collides with the <build>/Drift/ directory qt_add_qml_module(URI Drift) writes, which
# on case-insensitive APFS fails the link with EISDIR.
if(APPLE)
    set(DRIFT_MACOS_EXECUTABLE "Drift")
    set(DRIFT_MACOS_BUNDLE_ID "org.cutwire.Drift")
    set(DRIFT_MACOS_ICON_FILE "Drift.icns")

    # Not CMake's MACOSX_BUNDLE_* properties: the NS* and LSMinimumSystemVersion keys have none.
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY)

    set(DRIFT_MACOS_ICNS "${CMAKE_SOURCE_DIR}/resources/macos/${DRIFT_MACOS_ICON_FILE}")
    target_sources(drift PRIVATE "${DRIFT_MACOS_ICNS}")
    set_source_files_properties("${DRIFT_MACOS_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")

    set_target_properties(drift PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "${DRIFT_MACOS_EXECUTABLE}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
endif()

# App icon for QApplication::setWindowIcon (all platforms). Rendered from Drift_icon.png,
# as is resources/windows/drift.ico — keep the two in sync when the branding changes.
qt_add_resources(drift "drift_app_icon"
    PREFIX "/app"
    BASE resources
    FILES resources/drift.png
)

# Fixed input for the playback diagnostics benchmark, so numbers from two different bug
# reports are comparable. See resources/diagnostics/README.md for how it is generated.
qt_add_resources(drift "drift_diagnostics"
    PREFIX "/diagnostics"
    BASE resources/diagnostics
    FILES resources/diagnostics/bench1080p60.mp4
)

set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# App-wide notification queue; the visual host is components/ToastHost.qml.
set_source_files_properties(src/qml/Toasts.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# In-flight lift-and-drop state for the phone asset sheet; the ghost is drawn by
# AndroidBottomSheet and the drop is performed by AndroidTimeline.
set_source_files_properties(src/qml/TouchDrag.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

# Lucide icons: use the original scalable SVG sources in QML.
file(GLOB DRIFT_LUCIDE_ICONS RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
     "resources/icons/*.svg")

qt_add_qml_module(drift
    URI Drift
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        # Touch entry point, loaded instead of Main.qml on Android (see src/main.cpp).
        src/qml/AndroidMain.qml
        src/qml/AndroidHome.qml
        src/qml/AndroidEditor.qml
        src/qml/AndroidPreview.qml
        src/qml/AndroidTimeline.qml
        src/qml/AndroidLayoutPicker.qml
        src/qml/AndroidTopBar.qml
        src/qml/AndroidBottomRail.qml
        src/qml/AndroidBottomSheet.qml
        src/qml/AndroidAddMenu.qml
        src/qml/AndroidEditActions.qml
        src/qml/AndroidMediaPreview.qml
        src/qml/Theme.qml
        src/qml/Toasts.qml
        src/qml/TouchDrag.qml
        src/qml/EditorHeader.qml
        src/qml/AssetsPanel.qml
        src/qml/PreviewPanel.qml
        src/qml/components/preview/TransformOverlay.qml
        src/qml/components/preview/CropOverlay.qml
        src/qml/components/preview/PreviewToolbar.qml
        src/qml/components/preview/AndroidTransformOverlay.qml
        src/qml/components/preview/AndroidCropOverlay.qml
        src/qml/SegmentationWindow.qml
        src/qml/MulticamWindow.qml
        src/qml/DenoiseWindow.qml
        src/qml/SpeedCurveWindow.qml
        src/qml/FadeCurveWindow.qml
        src/qml/MediaPreviewWindow.qml
        src/qml/PropertiesPanel.qml
        src/qml/components/properties/GeneralInspector.qml
        src/qml/components/properties/TextInspector.qml
        src/qml/components/properties/TransformInspector.qml
        src/qml/components/properties/StabilizeInspector.qml
        src/qml/components/properties/AudioInspector.qml
        src/qml/components/properties/AudioEffectsInspector.qml
        src/qml/components/properties/SpeedFadeInspector.qml
        src/qml/components/properties/AnimationInspector.qml
        src/qml/components/properties/TransitionInspector.qml
        src/qml/components/properties/BlendingInspector.qml
        src/qml/components/properties/ShapeInspector.qml
        src/qml/components/properties/MasksInspector.qml
        src/qml/components/properties/EffectsInspector.qml
        src/qml/components/assets/TextAssetsTab.qml
        src/qml/components/assets/ScenesTab.qml
        src/qml/components/assets/SubtitlesTab.qml
        src/qml/components/assets/StickersTab.qml
        src/qml/components/assets/ShapesTab.qml
        src/qml/components/assets/SoundsTab.qml
        src/qml/components/assets/SettingsTab.qml
        src/qml/components/assets/ShortcutsTab.qml
        src/qml/components/assets/MediaAssetsTab.qml
        src/qml/components/assets/BinBreadcrumb.qml
        src/qml/components/assets/AssetCategoryChips.qml
        src/qml/components/assets/EffectStacksSection.qml
        src/qml/components/assets/AssetFavoriteButton.qml
        src/qml/TimelinePanel.qml
        src/qml/components/timeline/TimelineToolbar.qml
        src/qml/components/timeline/TrackHeaderColumn.qml
        src/qml/components/timeline/TimelineClipItem.qml
        src/qml/components/IconGlyph.qml
        src/qml/components/IconButton.qml
        src/qml/components/PanelFrame.qml
        src/qml/components/PanelSplitHandle.qml
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
        src/qml/components/CollapsibleSection.qml
        src/qml/components/TextStylePackThumb.qml
        src/qml/components/TextStylePackPicker.qml
        src/qml/components/NameDialog.qml
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
        src/qml/components/DebugInfoDialog.qml
        src/qml/components/preview/PlaybackStatsOverlay.qml
        src/qml/components/ProjectSetupDialog.qml
        src/qml/components/LayoutChooserDialog.qml
        src/qml/components/LanguageChooserDialog.qml
        src/qml/components/AgentAccessDialog.qml
        src/qml/components/VideoSizeDialog.qml
        src/qml/components/VideoSizeControls.qml
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
        src/qml/components/TouchLiftArea.qml
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
    Qt6::Svg
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    driftcore
    driftengine
)
target_include_directories(drift PRIVATE src src/preview)

if(ANDROID)
    # FileDialogs registers a QtAndroidPrivate::NewIntentListener so a second ACTION_VIEW reaches a
    # running instance (singleTop delivers it to onNewIntent, which the public API does not expose).
    # CorePrivate is not pulled in by the plain Qt6 Core package, hence the extra find_package.
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    target_link_libraries(drift PRIVATE Qt6::CorePrivate)

    # applicationDirPath() on Android is the APK's lib directory and contains nothing but .so
    # files, so the file-based package roots the desktop build stages next to the binary have
    # nowhere to live. The packages ride inside the binary as Qt resources instead;
    # GpuPackageParse::defaultSearchPaths adds ":/packages/<subdir>" as the bundled root.
    file(GLOB_RECURSE DRIFT_PACKAGE_FILES RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
         "effects/*/effect.json"
         "effects/*/*.frag"
         "effects/*/thumbnail.png"
         "effects/*/*.bin"
         "transitions/*/*.json"
         "transitions/*/*.frag"
         "transitions/*/*.png"
         "audio-effects/*/audio-effect.json"
         "audio-effects/*/thumbnail.png"
         "effect-templates/*/template.json"
         "effect-templates/*/thumbnail.png")
    list(REMOVE_DUPLICATES DRIFT_PACKAGE_FILES)
    qt_add_resources(drift "drift_packages"
        PREFIX "/packages"
        BASE "${CMAKE_SOURCE_DIR}"
        FILES ${DRIFT_PACKAGE_FILES}
    )

    # Qt Network's OpenSSL TLS plugin dlopens libcrypto_3.so / libssl_3.so at runtime.
    # androiddeployqt files each extra .so under lib/<abi>/ from its ELF architecture, so a
    # multi-ABI AAB must list the OpenSSL pair for every ABI in QT_ANDROID_ABIS — not only
    # the toolchain ABI CMake is currently compiling.
    if(QT_ANDROID_ABIS)
        set(_drift_packaged_abis ${QT_ANDROID_ABIS})
    else()
        set(_drift_packaged_abis ${ANDROID_ABI})
    endif()
    set(_drift_android_extra_libs "")
    foreach(_abi IN LISTS _drift_packaged_abis)
        set(_prebuilt "${CMAKE_SOURCE_DIR}/third_party/prebuilt/android/${_abi}")
        set(_crypto "${_prebuilt}/lib/libcrypto_3.so")
        set(_ssl "${_prebuilt}/lib/libssl_3.so")
        if(NOT EXISTS "${_crypto}" OR NOT EXISTS "${_ssl}")
            message(FATAL_ERROR
                "Missing OpenSSL shared libs for Qt Network TLS under ${_prebuilt}/lib.\n"
                "Expected libcrypto_3.so and libssl_3.so (KDAB android_openssl ssl_3 layout).\n"
                "Rebuild deps with third_party/build-android.sh ${_abi}, or copy the\n"
                "ssl_3/${_abi}/*.so files from https://github.com/KDAB/android_openssl.")
        endif()
        list(APPEND _drift_android_extra_libs "${_crypto}" "${_ssl}")
    endforeach()

    set(DRIFT_ANDROID_PACKAGE_NAME "org.cutwire.drift" CACHE STRING
        "Android application id (use org.cutwire.drift.ci for side-by-side test builds)")
    set(DRIFT_ANDROID_APP_NAME "Drift" CACHE STRING
        "Launcher label")
    # versionName is PROJECT_VERSION — same string the desktop binary embeds as DRIFT_VERSION.
    # versionCode is the Play-Store integer; default tracks that semver so a desktop bump is an
    # Android bump too (0.5.1 → 501). CI may override with -DDRIFT_ANDROID_VERSION_CODE=.
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _drift_android_ver "${PROJECT_VERSION}")
    if(_drift_android_ver)
        math(EXPR _drift_android_version_code_default
            "${CMAKE_MATCH_1} * 10000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}")
    else()
        set(_drift_android_version_code_default 1)
    endif()
    set(DRIFT_ANDROID_VERSION_CODE "${_drift_android_version_code_default}" CACHE STRING
        "Android versionCode (defaults to major*10000+minor*100+patch from PROJECT_VERSION)")

    set_target_properties(drift PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "${DRIFT_ANDROID_PACKAGE_NAME}"
        QT_ANDROID_APP_NAME "${DRIFT_ANDROID_APP_NAME}"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE "${DRIFT_ANDROID_VERSION_CODE}"
        QT_ANDROID_EXTRA_LIBS "${_drift_android_extra_libs}"
    )
endif()

# MCP tool names, JSON schemas, agent errors and copied configs are a machine protocol and must
# stay English. QT_EXCLUDE_FROM_TRANSLATION is a no-op until src/mcp/ exists (the mcp branch);
# lupdate also ignores QStringLiteral, so those files only leak if someone later adds tr().
file(GLOB DRIFT_MCP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/mcp/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/mcp/*.h")
if(DRIFT_MCP_SOURCES)
    set_source_files_properties(${DRIFT_MCP_SOURCES} PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)
endif()

if(Qt6LinguistTools_FOUND)
    # Catalogs, project/SRT errors and reverse-proxy messages live on driftengine/driftcore, which
    # qt_add_translations(drift) does not scan on its own.
    file(GLOB DRIFT_TS_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/i18n/drift_*.ts")
    qt_add_translations(drift
        TS_FILES
            "${CMAKE_SOURCE_DIR}/i18n/drift.ts"
            ${DRIFT_TS_FILES}
        RESOURCE_PREFIX "/i18n"
        SOURCES
            src/engine/EffectCatalog.cpp
            src/engine/EffectTemplateCatalog.cpp
            src/engine/AudioEffectCatalog.cpp
            src/engine/TransitionCatalog.cpp
            src/engine/FontCatalog.cpp
            src/engine/ReverseRenderer.cpp
            src/engine/MediaEditor.cpp
            src/engine/ProjectBundle.cpp
            src/core/Project.cpp
            src/core/SrtIO.cpp
            src/core/ShapeStyle.cpp
            src/core/TextStyle.cpp
            src/core/SubtitleCue.cpp
        # -locations none: source line numbers in .ts files churn on every edit
        # and make i18n/*.ts conflict across PRs. Translators still see context
        # and source text; they lose jump-to-source in Qt Linguist.
        LUPDATE_OPTIONS -no-obsolete -locations none
    )
    if(NOT TARGET update_translations AND TARGET drift_lupdate)
        add_custom_target(update_translations DEPENDS drift_lupdate)
    endif()

    # Qt keeps lupdate out of the default build, so a new qsTr() only reaches i18n/*.ts when
    # someone remembers the manual target -- which is what keeps breaking the Translations test.
    # Order it ahead of lrelease so the .qm files come from the refreshed catalogs rather than
    # racing them. DEFER because qt_add_translations only creates these targets at the end of
    # the directory scope. Off under CI, where the test must see the committed catalogs and not
    # ones this build just repaired.
    if(DRIFT_AUTO_UPDATE_TRANSLATIONS)
        cmake_language(DEFER CALL add_dependencies ${PROJECT_NAME}_lrelease update_translations)
    endif()
else()
    message(STATUS "Qt6 LinguistTools not found; translations will not be built.")
endif()

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
    # Only drift is a bundle, so the shared output directory guarded against below cannot happen.
    get_target_property(_bundle ${target} MACOSX_BUNDLE)
    if(_bundle)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/${assetdir}"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/${assetdir}"
            COMMENT "Copy ${assetdir} packages into ${target}.app/Contents/Resources")
        return()
    endif()

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
        get_target_property(_bundle ${target} MACOSX_BUNDLE)
        if(_bundle)
            set(_ortdir "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/onnxruntime/lib")
        else()
            set(_ortdir "$<TARGET_FILE_DIR:${target}>/onnxruntime/lib")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ortdir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${ONNXRUNTIME_RUNTIME_LIBS}
                    "${_ortdir}"
            COMMENT "Stage onnxruntime into ${target}'s build tree"
        )
    endif()
endfunction()

function(drift_copy_effect_templates target)
    drift_stage_packages(${target} effect-templates)
endfunction()

# Android gets these through qt_add_resources above; there is no <appDir> to stage into.
if(NOT ANDROID)
    drift_copy_effects(drift)
    drift_copy_effect_templates(drift)
    drift_copy_transitions(drift)
    drift_copy_audio_effects(drift)
    drift_bundle_onnxruntime(drift)
endif()

# Desktop packaging, the headless CLI tools and the QTest binaries. None of it applies to an
# APK, so the Android configure skips it wholesale.
if(NOT ANDROID)
# --- install (used by packaging, e.g. Flatpak; the dev workflow runs in-place) ---
include(GNUInstallDirs)

# Everything Drift installs is tagged with the "drift" component, so packaging can ask for the app
# and nothing else: `cmake --install build --component drift`. JUCE's modules install ~50 MB of
# their own sources into include/JUCE-<version>, and a plain `cmake --install` has no way to say no
# to them. (The Flatpak manifests get the same result the other way round, by cleaning up /include.)
if(APPLE)
    # The bundle is complete after the build. Use -DCMAKE_INSTALL_PREFIX=/Applications.
    install(TARGETS drift BUNDLE DESTINATION . COMPONENT drift)
else()
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
    install(FILES flatpak/org.cutwire.Drift.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages COMPONENT drift)
    # 1024px source is intentionally skipped: flatpak build-export rejects icons
    # larger than 512x512.
    foreach(_size 64 128 256 512)
        install(FILES
            flatpak/share/icons/hicolor/${_size}x${_size}/apps/org.cutwire.Drift.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps COMPONENT drift)
    endforeach()
endif()

# --- CLI tools (headless engine testing, no GUI) ----------------------------
add_subdirectory(tools)

# --- tests -------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
endif() # NOT ANDROID
