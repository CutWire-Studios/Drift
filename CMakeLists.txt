cmake_minimum_required(VERSION 3.21)

# Must be set before project(). Left unset, the binary is stamped with the build machine's OS
# version and will not launch on anything older.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Oldest macOS release Drift runs on")

# C is enabled for JUCE only: its modules compile a handful of C sources (zlib, flac, oggvorbis)
# and its CMake refuses to configure without it.
project(Drift VERSION 0.9.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(WITH_BGREMOVAL "Enable OpenCV-based background removal" OFF)
# Skia (Ganesh/GL) draws text, shapes and Lottie/SVG clips on the GPU; see
# third_party/build-skia.sh for producing the prebuilt it links. OFF builds a video/image/audio
# only editor: text, shapes and Lottie/SVG clips draw nothing.
option(DRIFT_WITH_SKIA "Draw vector content (text, shapes, Lottie, SVG) with Skia" ON)
set(DRIFT_SKIA_DIR "" CACHE PATH "Directory containing SkiaConfig.cmake (default: third_party/prebuilt/skia/<target>)")

# Defaults off under CI so the Translations test still compares the committed catalogs.
if(DEFINED ENV{CI} OR EMSCRIPTEN)
    set(_drift_auto_ts OFF)
else()
    set(_drift_auto_ts ON)
endif()
option(DRIFT_AUTO_UPDATE_TRANSLATIONS "Run lupdate as part of the default build" ${_drift_auto_ts})

# Fonts, stickers and the Whisper model used to be fetched or extracted here and shipped inside
# the build tree — roughly 800 MB of assets every user paid for whether or not they used them.
# They are now addons: signed .driftpkg archives downloaded at runtime into <AppDataLocation>/
# addons/ (src/engine/addons/AddonPackage, src/app/online/addons/AddonManager). Nothing asset-shaped is bundled,
# so a fresh build starts with system fonts, an empty sticker tab and no transcriber, and the
# user installs what they want from the Addon Manager. The packaging and publishing tooling
# lives in the drift-addons repo.
#
# Effects and transitions are the exception: they are small, they are source (GLSL reviewed
# alongside the engine that runs them), and an editor with an empty effects browser is broken
# rather than merely bare — so a small core set still ships next to the binary. Packaged builds
# (DRIFT_BUNDLE_CORE_PACKAGES_ONLY=ON) ship only that set and the rest comes from addons; dev
# builds stage everything so the tests and tools have the full catalog. Every package is *also*
# an addon, and an installed package outranks the bundled one of the same id (see
# defaultSearchPaths), which is what lets a shader fix go out without an app release.
#
# For development without downloading, point DRIFT_EFFECTS_DIR, DRIFT_TRANSITIONS_DIR,
# DRIFT_FONTS_DIR, DRIFT_STICKERS_DIR, DRIFT_EMOJI_FONT_DIR or DRIFT_WHISPER_MODEL_DIR at a local
# directory; those outrank everything, including installed addons.

# --- Addon service ----------------------------------------------------------
# Where the Addon Manager fetches from. These are the only place either value is defined —
# src/app/online/addons/AddonEndpoint.h just reads them — so a fork can point at its own Worker with:
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

# --- Marketplace -----------------------------------------------------------
# Stock catalog at market.cutwire.org. Empty URL or key disables the Market tab.
# The HMAC key is not a user secret — it ships in the binary — and exists so requests can be
# signed and a stable client id derived. See docs/marketplace/README.md.
#
#   cmake -B build -DDRIFT_MARKET_API_URL=https://market.example.com/api/v1 \
#                  -DDRIFT_MARKET_CLIENT_KEY=...
#
#   cmake -B build -DDRIFT_MARKET_API_URL=      # no marketplace
set(DRIFT_MARKET_API_URL "https://market.cutwire.org/api/v1"
    CACHE STRING "Marketplace API base; empty disables the marketplace")
set(DRIFT_MARKET_CLIENT_KEY "5409a0bebab4446f88e51d06024a5cf9a694ee141a6a1c6be7a49e45b6687438"
    CACHE STRING "HMAC key for marketplace client identity and request signing")

# --- Update check -----------------------------------------------------------
# Where the installer for a newer release is downloaded. The version itself comes from the TXT
# record on drift-version.cutwire.org (see UpdateChecker); this URL is only how that version's
# release notes and installer are found.
#
# Set it to an empty string for builds a package manager owns — the Flatpak manifests and the Arch
# PKGBUILD all do. There, pacman or flatpak fetches the update on its own schedule and an in-app
# nag is noise the user cannot act on (Flathub's quality guidelines say as much).
set(DRIFT_UPDATE_FEED_URL "https://api.github.com/repos/CutWire-Studios/Drift/releases/latest"
    CACHE STRING "Release feed polled for updates; empty disables the update check")

# --- Release channel --------------------------------------------------------
# Nightly builds are the same source with a different identity: their own app id, their own
# launcher name, and a version string carrying the build stamp. That is what lets one install
# beside a stable one (a separate bundle id, Windows AppId and Android package) while still
# reading the same settings and the same downloaded addons, which is the whole point of testing
# one. The nightly workflow passes both of these; nothing else sets them.
#
# project(Drift VERSION ...) above stays strictly X.Y.Z — CMake rejects a pre-release suffix
# there — so the string the app displays is built here instead.
set(DRIFT_CHANNEL "stable" CACHE STRING "Release channel: stable or nightly")
set(DRIFT_BUILD_ID "" CACHE STRING "Nightly build stamp, YYYYMMDD.<short sha>")

if(NOT DRIFT_CHANNEL STREQUAL "stable" AND NOT DRIFT_CHANNEL STREQUAL "nightly")
    message(FATAL_ERROR "DRIFT_CHANNEL must be 'stable' or 'nightly', not '${DRIFT_CHANNEL}'")
endif()

set(DRIFT_APP_ID "org.cutwire.Drift")
set(DRIFT_APP_NAME "Drift")
set(DRIFT_VERSION_STRING "${PROJECT_VERSION}")
if(DRIFT_CHANNEL STREQUAL "nightly")
    set(DRIFT_APP_ID "org.cutwire.Drift.Nightly")
    set(DRIFT_APP_NAME "Drift Nightly")
    # A local nightly configure with no stamp is still legible rather than "0.7.0-nightly.".
    if(DRIFT_BUILD_ID STREQUAL "")
        set(DRIFT_BUILD_ID "dev")
    endif()
    set(DRIFT_VERSION_STRING "${PROJECT_VERSION}-nightly.${DRIFT_BUILD_ID}")
endif()
message(STATUS "Drift channel: ${DRIFT_CHANNEL} (${DRIFT_VERSION_STRING}, ${DRIFT_APP_ID})")

# Which package this build ships as. Sent to the marketplace so it can hide sources per store
# (e.g. Play Store moderation) without a separate client build. Every packaging path sets it.
set(DRIFT_DISTRIBUTIONS source windows windows-store macos appimage flatpak flathub arch android-github android-play)
set(DRIFT_DISTRIBUTION "source" CACHE STRING "Package type reported to the marketplace")
set_property(CACHE DRIFT_DISTRIBUTION PROPERTY STRINGS ${DRIFT_DISTRIBUTIONS})
if(NOT DRIFT_DISTRIBUTION IN_LIST DRIFT_DISTRIBUTIONS)
    message(FATAL_ERROR "DRIFT_DISTRIBUTION must be one of ${DRIFT_DISTRIBUTIONS}, not '${DRIFT_DISTRIBUTION}'")
endif()
message(STATUS "Drift distribution: ${DRIFT_DISTRIBUTION}")
# Google Play updates this build itself, and scripts/build.sh has no way to pass an empty feed.
if(DRIFT_DISTRIBUTION STREQUAL "android-play")
    set(DRIFT_UPDATE_FEED_URL "")
endif()
# A browser tab is always the latest deployment; there is no installer to point at.
if(EMSCRIPTEN)
    set(DRIFT_UPDATE_FEED_URL "")
endif()

# Qt for wasm is built with one exception model and every object linked into it must agree, so this
# has to match how Qt itself was configured. The official Qt 6.11 wasm builds use JS exceptions
# (config_qtbase.summary: "WebAssembly Exceptions ... no"), hence -fexceptions.
# Global rather than per target: the prebuilt archives and JUCE are compiled the same way.
if(EMSCRIPTEN)
    set(DRIFT_WASM_EH_FLAGS "-fexceptions" CACHE STRING
        "Exception-handling flag for wasm; must match the Qt for wasm build")
    add_compile_options(${DRIFT_WASM_EH_FLAGS})
    add_link_options(${DRIFT_WASM_EH_FLAGS})
endif()

find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2 Multimedia Test Concurrent Widgets OpenGL Network QuickEffects Svg Xml)
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

# qtimageformats supplies the qwebp and qtiff QImageReader plugins. Nothing links against them,
# so without this check a kit missing the module configures, builds and packages cleanly, and
# then every .webp/.tiff still decodes to a null QImage on the device: no thumbnail, a 0x0 asset,
# and a clip that renders as nothing. That is exactly how it shipped in the Android packages —
# the CI jobs installed only qtmultimedia and qtshadertools.
#
# The module declares no findable component, but it does install imported plugin targets that
# Qt6Gui pulls in via __qt_internal_include_plugin_packages(), so the targets are the one thing
# that can be tested at configure time. QT_SKIP_AUTO_PLUGIN_INCLUSION turns that inclusion off,
# in which case the targets are absent for a reason that has nothing to do with the module.
#
# Fatal on Android only: there the plugin can come from nowhere but the kit, so a missing one is
# a broken package. A desktop build can still pick it up from the system Qt at runtime.
if(NOT QT_SKIP_AUTO_PLUGIN_INCLUSION)
    set(_drift_missing_imageformats "")
    if(NOT TARGET Qt6::QWebpPlugin)
        list(APPEND _drift_missing_imageformats "qwebp (.webp)")
    endif()
    if(NOT TARGET Qt6::QTiffPlugin)
        list(APPEND _drift_missing_imageformats "qtiff (.tiff)")
    endif()
    if(_drift_missing_imageformats)
        string(JOIN ", " _drift_missing_list ${_drift_missing_imageformats})
        set(_drift_imageformats_msg
            "Qt ImageFormats is missing: ${_drift_missing_list}.\n"
            "Those stills will import as 0x0 and render as nothing.\n"
            "  aqtinstall: add qtimageformats to the -m module list\n"
            "  Arch:       pacman -S qt6-imageformats\n"
            "  Debian:     apt install qt6-imageformats-dev")
        if(ANDROID)
            message(FATAL_ERROR ${_drift_imageformats_msg})
        else()
            message(WARNING ${_drift_imageformats_msg})
        endif()
    endif()
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
        # vidstabdetect/vidstabtransform, which clip stabilization runs through libavfilter.
        "${DRIFT_PREBUILT}/lib/libvidstab.a"
        # mediandk resolves the AMediaCodec_*/AMediaFormat_* the --enable-mediacodec decoders call,
        # plus the AImageReader_*/AImage_* the zero-copy preview surface is built on. android is
        # for AHardwareBuffer_*, which lives in libandroid rather than libmediandk.
        m z mediandk android)

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

# --- WebAssembly prebuilt dependencies ---------------------------------------
# Same shape as the Android block above: no pkg-config and no system prefix, so the static archives
# built by third_party/build-wasm.sh stand in for the host lookups. Compiled with the same -pthread
# and exception flags as Qt for wasm, or the link fails on mismatched atomics/EH features.
if(EMSCRIPTEN)
    set(DRIFT_PREBUILT "${CMAKE_SOURCE_DIR}/third_party/prebuilt/wasm")
    if(NOT EXISTS "${DRIFT_PREBUILT}/lib/libavcodec.a")
        message(FATAL_ERROR
            "No prebuilt dependencies for wasm. Run:\n"
            "    third_party/build-wasm.sh")
    endif()

    # zlib is an emscripten port, not an archive: the flag has to reach both compile and link.
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    target_compile_options(ZLIB::ZLIB INTERFACE -sUSE_ZLIB=1)
    target_link_options(ZLIB::ZLIB INTERFACE -sUSE_ZLIB=1)
    set(ZLIB_FOUND TRUE)

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
        "${DRIFT_PREBUILT}/lib/libdav1d.a"
        "${DRIFT_PREBUILT}/lib/libvidstab.a"
        m ZLIB::ZLIB)

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

if(NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(FFmpeg REQUIRED)
endif()

# ONNX Runtime powers auto subtitles, object segmentation, face tracking and noise removal. The
# app links none of it: it compiles against the headers and dlopens a runtime at startup
# (src/engine/ml/runtime/OrtRuntime.cpp), so the CPU / CUDA / WebGPU decision belongs to the user and is made
# by installing an addon, not by whoever configured this build. Only the headers are needed here.
option(DRIFT_FETCH_ONNXRUNTIME
    "Download prebuilt ONNX Runtime headers when they are not installed (cmake/FetchOnnxRuntime.cmake)" ON)
# Stage a CPU runtime into <appDir>/onnxruntime so the build tree can transcribe without going
# through the addon service. Packaged builds turn this off — they ship no runtime at all.
# Not on wasm: only the headers are fetched, and there is no runtime to dlopen in a browser.
if(EMSCRIPTEN)
    set(_drift_bundle_ort OFF)
else()
    set(_drift_bundle_ort ON)
endif()
option(DRIFT_BUNDLE_ONNXRUNTIME
    "Stage a CPU ONNX Runtime into the build tree for development" ${_drift_bundle_ort})

option(DRIFT_BUNDLE_CORE_PACKAGES_ONLY
    "Ship only the core effect/transition packages in the drift app (the rest are addons)" OFF)
set(DRIFT_CORE_EFFECTS
    color_grade film_grain chromatic_aberration adjust_brightness adjust_contrast adjust_gamma
    adjust_hue adjust_saturation adjust_temperature gaussian_blur bloom_glow halation stylize_vignette)
set(DRIFT_CORE_TRANSITIONS crossfade dip dip_white)
set(DRIFT_CORE_AUDIO_EFFECTS "")
set(DRIFT_CORE_EFFECT_TEMPLATES "")
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
if(NOT ANDROID AND NOT EMSCRIPTEN)
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
if(NOT EMSCRIPTEN)
    find_package(ZLIB REQUIRED)
endif()

# SoundTouch does the pitch shifting behind the voice effects. juce_dsp has no pitch shifter, and a
# hand-rolled granular one cancels the signal outright wherever the tap spacing lands on an odd
# number of half-periods of the output. LGPL-2.1, whose section 3 allows use under GPL v2 or later,
# so it is compatible with this project's GPL-3.0.
# pkg-config first: SoundTouch's installed CMake config names include directories that do not
# exist, and a non-existent INTERFACE_INCLUDE_DIRECTORIES entry on an imported target is a hard
# generate-time error rather than something we can fall back from. Where there is no pkg-config at
# all (MSVC), the CMake config below is the only path, so it gets repaired instead.
if(NOT ANDROID AND NOT EMSCRIPTEN)
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
    if(EMSCRIPTEN)
        # The wasm fix-up edits a JUCE header, which is not something to do to a user's checkout,
        # so build from a copy in the build tree (rerun when the source path changes).
        include(PatchJuceWasm)
        set(_drift_juce_copied "")
        if(EXISTS "${CMAKE_BINARY_DIR}/juce-wasm-src/.src")
            file(READ "${CMAKE_BINARY_DIR}/juce-wasm-src/.src" _drift_juce_copied)
        endif()
        if(NOT "${_drift_juce_copied}" STREQUAL "${_drift_juce_dir}")
            file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/juce-wasm-src")
            file(COPY "${_drift_juce_dir}/" DESTINATION "${CMAKE_BINARY_DIR}/juce-wasm-src"
                 PATTERN ".git" EXCLUDE)
            file(WRITE "${CMAKE_BINARY_DIR}/juce-wasm-src/.src" "${_drift_juce_dir}")
        endif()
        drift_patch_juce_wasm("${CMAKE_BINARY_DIR}/juce-wasm-src/modules")
        set(_drift_juce_dir "${CMAKE_BINARY_DIR}/juce-wasm-src")
    endif()
    add_subdirectory("${_drift_juce_dir}" "${CMAKE_BINARY_DIR}/juce-build")
elseif(DRIFT_FETCH_JUCE)
    message(STATUS "JUCE: fetching ${DRIFT_JUCE_TAG}")
    include(FetchContent)
    set(_drift_juce_patch "")
    if(EMSCRIPTEN)
        # The download is ours alone, so patch it in place between populate and add_subdirectory.
        set(_drift_juce_patch PATCH_COMMAND ${CMAKE_COMMAND}
            -DDRIFT_JUCE_MODULES_DIR=<SOURCE_DIR>/modules
            -P "${CMAKE_SOURCE_DIR}/cmake/PatchJuceWasm.cmake")
    endif()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${DRIFT_JUCE_TAG}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        ${_drift_juce_patch})
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
    src/core/effects/Effect.h
    src/core/effects/Effect.cpp
    src/core/effects/EffectPreset.h
    src/core/project/BinFolder.h
    src/core/project/media/MediaAsset.h
    src/core/project/media/SourceLab.h
    src/core/project/media/MediaAsset.cpp
    src/core/project/media/ImageSequence.h
    src/core/project/media/ImageSequence.cpp
    src/core/effects/BlendMode.h
    src/core/project/clip/Clip.h
    src/core/project/clip/Clip.cpp
    src/core/project/clip/Stabilize.h
    src/core/project/clip/Stabilize.cpp
    src/core/base/Bezier.h
    src/core/animation/SpeedCurve.h
    src/core/animation/SpeedCurve.cpp
    src/core/animation/FadeShape.h
    src/core/animation/FadeShape.cpp
    src/core/animation/Easing.h
    src/core/animation/Easing.cpp
    src/core/animation/EasingPresetStore.h
    src/core/animation/EasingPresetStore.cpp
    src/core/animation/ClipAnimation.h
    src/core/animation/ClipAnimation.cpp
    src/core/project/SubtitleCue.h
    src/core/project/SubtitleCue.cpp
    src/core/project/SubtitleTiming.h
    src/core/project/SubtitleTiming.cpp
    src/core/project/Transcript.h
    src/core/project/Transcript.cpp
    src/core/project/GuideSet.h
    src/core/project/GuideSet.cpp
    src/core/interchange/subtitles/SubtitleParse.h
    src/core/interchange/subtitles/SrtIO.h
    src/core/interchange/subtitles/SrtIO.cpp
    src/core/interchange/subtitles/VttIO.h
    src/core/interchange/subtitles/VttIO.cpp
    src/core/effects/Mask.h
    src/core/effects/Mask.cpp
    src/core/tracking/MotionTracker.h
    src/core/project/Transition.h
    src/core/project/Transition.cpp
    src/core/text/animation/TextParamSpec.h
    src/core/text/animation/TextParamSpec.cpp
    src/core/text/animation/LottieTextImport.h
    src/core/text/animation/LottieTextImport.cpp
    src/core/text/TextLook.h
    src/core/text/TextLook.cpp
    src/core/text/TextShading.h
    src/core/text/TextShading.cpp
    src/core/text/animation/TextAnimationPreset.h
    src/core/text/animation/TextAnimationPreset.cpp
    src/core/text/animation/TextAnimator.h
    src/core/text/animation/TextAnimator.cpp
    src/core/text/TextStyle.h
    src/core/text/TextStyle.cpp
    src/core/text/TextPresetStore.h
    src/core/text/TextPresetStore.cpp
    src/core/effects/EffectStackStore.h
    src/core/effects/EffectStackStore.cpp
    src/core/shapes/ShapeStyle.h
    src/core/shapes/ShapeStyle.cpp
    src/core/shapes/ShapePath.h
    src/core/shapes/ShapePath.cpp
    src/core/project/sources/VectorSource.h
    src/core/project/sources/VectorSource.cpp
    src/core/project/sources/Model3dSource.h
    src/core/project/sources/Model3dSource.cpp
    src/core/project/sources/PdfSource.h
    src/core/project/sources/PdfSource.cpp
    src/core/base/ZipArchive.h
    src/core/base/ZipArchive.cpp
    src/core/project/sources/DotLottie.h
    src/core/project/sources/DotLottie.cpp
    src/core/project/Track.h
    src/core/project/Track.cpp
    src/core/project/Project.h
    src/core/project/Project.cpp
    src/core/interchange/premiere/PrprojReader.h
    src/core/interchange/premiere/PrprojReader.cpp
    src/core/interchange/premiere/MogrtReader.h
    src/core/interchange/premiere/MogrtReader.cpp
    src/core/interchange/KdenliveReader.h
    src/core/interchange/KdenliveReader.cpp
    src/core/interchange/ResolveReader.h
    src/core/interchange/ResolveReader.cpp
    src/core/interchange/EdlReader.h
    src/core/interchange/EdlReader.cpp
    src/core/interchange/OtioReader.h
    src/core/interchange/OtioReader.cpp
    src/core/edit/TimelineOps.h
    src/core/edit/TimelineOps.cpp
    src/core/edit/MaskSpace.h
    src/core/edit/MaskSpace.cpp
    src/core/edit/CutOps.h
    src/core/edit/CutOps.cpp
    src/core/edit/commands/ProjectCommands.h
    src/core/edit/commands/ProjectCommands.cpp
)
target_include_directories(driftcore PUBLIC src)
target_link_libraries(driftcore PUBLIC Qt6::Core Qt6::Gui Qt6::Xml ZLIB::ZLIB)

file(GLOB DRIFT_TEXT_ANIMATION_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/presets/text-animations/*.json")
qt_add_resources(driftcore "drift_text_animations"
    PREFIX "/text-animations"
    BASE "${CMAKE_SOURCE_DIR}/presets/text-animations"
    FILES ${DRIFT_TEXT_ANIMATION_FILES}
)

# Audio effect DSP. JUCE modules compile into whatever target links them, so keeping them in a
# library of their own is what stops JUCE's headers, its compile definitions and its build time
# from spreading across driftengine. AudioEffectRack.h is the only header that leaves here and it
# mentions no JUCE type, so driftengine stays JUCE-free at the source level.
add_library(driftaudiodsp STATIC
    src/engine/audio/dsp/AudioStage.h
    src/engine/audio/dsp/AudioEffectProcessor.h
    src/engine/audio/dsp/AudioEffectProcessor.cpp
    src/engine/audio/dsp/processors/FilterProcessors.h
    src/engine/audio/dsp/processors/FilterProcessors.cpp
    src/engine/audio/dsp/processors/DynamicsProcessors.h
    src/engine/audio/dsp/processors/DynamicsProcessors.cpp
    src/engine/audio/dsp/processors/ModulationProcessors.h
    src/engine/audio/dsp/processors/ModulationProcessors.cpp
    src/engine/audio/dsp/processors/SoundTouchPitchProcessor.h
    src/engine/audio/dsp/processors/SoundTouchPitchProcessor.cpp
    src/engine/audio/dsp/AudioEffectFactory.h
    src/engine/audio/dsp/AudioEffectFactory.cpp
    src/engine/audio/dsp/graph/PedalCatalog.h
    src/engine/audio/dsp/graph/PedalCatalog.cpp
    src/engine/audio/dsp/WavReader.h
    src/engine/audio/dsp/WavReader.cpp
    src/engine/audio/dsp/graph/AudioGraph.h
    src/engine/audio/dsp/graph/AudioGraph.cpp
    src/engine/audio/dsp/graph/GraphPedals.h
    src/engine/audio/dsp/graph/GraphPedals.cpp
    src/engine/audio/dsp/graph/GraphProcessor.h
    src/engine/audio/dsp/graph/GraphProcessor.cpp
    src/engine/audio/dsp/graph/DriftGraphApi.h
    src/engine/audio/dsp/graph/DriftGraphApi.cpp
    src/engine/audio/dsp/AudioEffectRack.h
    src/engine/audio/dsp/AudioEffectRack.cpp
    src/engine/audio/dsp/ClipAudioRetimer.h
    src/engine/audio/dsp/ClipAudioRetimer.cpp
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
if(EMSCRIPTEN)
    # juce_SystemStats_wasm.cpp calls emscripten_get_now() without including its header. JUCE's
    # sources reach this target as interface sources, so a per-file property would be keyed on a
    # path this file does not own; the flag is harmless for the rest of the target.
    target_compile_options(driftaudiodsp PRIVATE "SHELL:-include emscripten/emscripten.h")
    target_sources(driftaudiodsp PRIVATE wasm/juce_wasm_shim.cpp)
endif()
# The same sources are built to WebAssembly for Drift Forge's preview (wasm/). Fused multiply-adds
# would round differently from the wasm build, and on ARM the compiler contracts by default.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(driftaudiodsp PRIVATE -ffp-contract=off)
endif()

# --- engine: FFmpeg-facing code, no QML deps -------------------------------
add_library(driftengine STATIC
    src/engine/addons/AddonPackage.h
    src/engine/addons/AddonPackage.cpp
    src/engine/addons/AddonRegistry.h
    src/engine/addons/AddonRegistry.cpp
    src/engine/addons/AddonSigningKey.h
    src/engine/addons/PackageSearchPaths.h
    src/engine/addons/PackageSearchPaths.cpp
    src/engine/platform/AndroidUri.h
    src/engine/platform/AndroidUri.cpp
    src/engine/decode/hw/HwAccel.h
    src/engine/decode/hw/HwAccel.cpp
    src/engine/decode/SwsColorRange.h
    src/engine/decode/probe/MediaProbe.h
    src/engine/decode/probe/MediaProbe.cpp
    src/engine/media/thumbnails/MediaThumbnail.h
    src/engine/media/thumbnails/MediaThumbnail.cpp
    src/engine/decode/probe/StillImage.h
    src/engine/decode/probe/StillImage.cpp
    src/engine/decode/hw/MediaCodecImagePool.h
    src/engine/decode/hw/MediaCodecImagePool.cpp
    src/engine/media/thumbnails/FilmstripTileCache.h
    src/engine/media/thumbnails/FilmstripTileCache.cpp
    src/engine/media/waveforms/MediaWaveform.h
    src/engine/media/waveforms/MediaWaveform.cpp
    src/engine/audio/analysis/LoudnessMeter.h
    src/engine/audio/analysis/LoudnessMeter.cpp
    src/engine/media/waveforms/WaveformBlockCache.h
    src/engine/media/waveforms/WaveformBlockCache.cpp
    src/engine/audio/analysis/AudioOnsets.h
    src/engine/audio/analysis/AudioOnsets.cpp
    src/engine/audio/analysis/AudioFrameAnalysis.h
    src/engine/audio/analysis/AudioFrameAnalysis.cpp
    src/engine/ml/vision/detection/SceneDetect.h
    src/engine/ml/vision/detection/SceneDetect.cpp
    src/engine/diagnostics/sheets/FrameSheet.h
    src/engine/diagnostics/sheets/FrameSheet.cpp
    src/engine/diagnostics/sheets/WaveformSheet.h
    src/engine/diagnostics/sheets/WaveformSheet.cpp
    src/engine/ml/vision/detection/ObjectDetector.h
    src/engine/ml/vision/detection/ObjectDetector.cpp
    src/engine/encode/export/Exporter.h
    src/engine/encode/export/Exporter.cpp
    src/engine/decode/readers/PreviewVideoFrame.h
    src/engine/decode/readers/ClipReader.h
    src/engine/decode/readers/ClipReader.cpp
    src/engine/diagnostics/DebugReport.h
    src/engine/diagnostics/DebugReport.cpp
    src/engine/diagnostics/DiagnosticsProbe.h
    src/engine/diagnostics/DiagnosticsProbe.cpp
    src/engine/decode/readers/ClipReaderWorker.h
    src/engine/decode/readers/ClipReaderWorker.cpp
    src/engine/decode/readers/ClipReaderPool.h
    src/engine/decode/readers/ClipReaderPool.cpp
    src/engine/compositor/effects/EffectProcessor.h
    src/engine/compositor/effects/EffectProcessor.cpp
    src/engine/packages/effects/EffectCatalog.h
    src/engine/packages/effects/EffectCatalog.cpp
    src/engine/packages/assets/FontCatalog.h
    src/engine/packages/assets/FontCatalog.cpp
    src/engine/packages/assets/StickerCatalog.h
    src/engine/packages/assets/StickerCatalog.cpp
    src/engine/packages/assets/EmojiCatalog.h
    src/engine/packages/assets/EmojiCatalog.cpp
    src/engine/packages/assets/EmojiTable.h
    src/engine/packages/assets/EmojiTable.cpp
    src/engine/render2d/text/TextLayout.h
    src/engine/render2d/text/TextLayout.cpp
    src/engine/render2d/vector/VectorClipRenderer.h
    src/engine/render2d/vector/VectorClipRenderer.cpp
    src/engine/render2d/vector/VectorInspect.h
    src/engine/render2d/vector/VectorInspect.cpp
    src/engine/packages/effects/EffectPackageLoader.h
    src/engine/packages/effects/EffectPackageLoader.cpp
    src/engine/packages/transitions/TransitionCatalog.h
    src/engine/packages/transitions/TransitionCatalog.cpp
    src/engine/packages/transitions/TransitionPackageLoader.h
    src/engine/packages/transitions/TransitionPackageLoader.cpp
    src/engine/packages/GpuPackageParse.h
    src/engine/packages/GpuPackageParse.cpp
    src/engine/gpu/effects/GpuEffectDefinition.h
    src/engine/compositor/GpuCompositor.h
    src/engine/compositor/GpuCompositor.cpp
    src/engine/EngineTuning.h
    src/engine/gpu/GlRuntime.h
    src/engine/gpu/GlRuntime.cpp
    src/engine/gpu/device/GpuStatus.h
    src/engine/gpu/device/GpuStatus.cpp
    src/engine/decode/hw/VaapiZeroCopy.h
    src/engine/gpu/effects/GpuEffectExecutor.h
    src/engine/gpu/effects/GpuEffectExecutor.cpp
    src/engine/gpu/effects/LookupTextures.h
    src/engine/gpu/effects/LookupTextures.cpp
    src/engine/color/CubeLut.h
    src/engine/color/CubeLut.cpp
    src/engine/color/ScopeAnalysis.h
    src/engine/color/ScopeAnalysis.cpp
    src/engine/compositor/effects/CompositorFrameHistory.h
    src/engine/compositor/effects/CompositorFrameHistory.cpp
    src/engine/compositor/FrameCompositor.h
    src/engine/compositor/FrameCompositor.cpp
    src/engine/compositor/layers/MaskApplier.h
    src/engine/compositor/layers/MaskApplier.cpp
    src/engine/packages/audio/AudioEffectCatalog.h
    src/engine/packages/audio/AudioEffectCatalog.cpp
    src/engine/packages/effects/EffectTemplateCatalog.h
    src/engine/packages/effects/EffectTemplateCatalog.cpp
    src/engine/packages/looks/LookCatalog.h
    src/engine/packages/looks/LookCatalog.cpp
    src/engine/packages/looks/LookImport.h
    src/engine/packages/looks/LookImport.cpp
    src/engine/audio/AudioMixer.h
    src/engine/audio/AudioMixer.cpp
    src/engine/ml/speech/transcription/WhisperTokenizer.h
    src/engine/ml/speech/transcription/WhisperTokenizer.cpp
    src/engine/ml/speech/transcription/WhisperTranscriber.h
    src/engine/ml/speech/transcription/WhisperTranscriber.cpp
    src/engine/ml/speech/tts/ChatterboxTokenizer.h
    src/engine/ml/speech/tts/ChatterboxTokenizer.cpp
    src/engine/ml/speech/tts/ChatterboxTts.h
    src/engine/ml/speech/tts/ChatterboxTts.cpp
    src/engine/ml/runtime/OrtSupport.h
    src/engine/ml/runtime/OrtRuntime.h
    src/engine/ml/runtime/OrtRuntime.cpp
    src/engine/render2d/pdf/PdfClipRenderer.h
    src/engine/render2d/pdf/PdfClipRenderer.cpp
    src/engine/render2d/pdf/PdfiumRuntime.h
    src/engine/render2d/pdf/PdfiumRuntime.cpp
    src/engine/ml/vision/segmentation/Sam2Segmenter.h
    src/engine/ml/vision/segmentation/Sam2Segmenter.cpp
    src/engine/ml/vision/segmentation/RvmMatter.h
    src/engine/ml/vision/segmentation/RvmMatter.cpp
    src/engine/tracking/MotionTracking.h
    src/engine/tracking/MotionTracking.cpp
    src/engine/ml/vision/depth/VdaDepth.h
    src/engine/ml/vision/depth/VdaDepth.cpp
    src/engine/ml/vision/Restorer.h
    src/engine/ml/vision/Restorer.cpp
    src/engine/ml/vision/depth/DepthSidecar.h
    src/engine/ml/vision/depth/DepthSidecar.cpp
    src/engine/face/tracking/FaceLandmarker.h
    src/engine/face/tracking/FaceLandmarker.cpp
    src/engine/face/tracking/FaceTrack.h
    src/engine/face/tracking/FaceTrack.cpp
    src/engine/face/props/FaceModelTransform.h
    src/engine/face/props/FaceModelTransform.cpp
    src/engine/render3d/models/ModelAnimation.h
    src/engine/render3d/models/ModelAnimation.cpp
    src/engine/render3d/scene/ClipTransform3d.h
    src/engine/render3d/scene/ClipTransform3d.cpp
    src/engine/render3d/scene/SceneCamera3d.h
    src/engine/render3d/scene/SceneCamera3d.cpp
    src/engine/compositor/PreviewSnap.h
    src/engine/render3d/scene/EditorView3d.h
    src/engine/render3d/scene/EditorView3d.cpp
    src/engine/render3d/editor/EditorOverlay.h
    src/engine/render3d/editor/EditorOverlay.cpp
    src/engine/render3d/DepthRelief.h
    src/engine/render3d/DepthRelief.cpp
    src/engine/render3d/editor/GlEditorOverlay.h
    src/engine/render3d/editor/GlEditorOverlay.cpp
    src/engine/compositor/PreviewSnap.cpp
    src/engine/compositor/layers/TransformLayer.h
    src/engine/compositor/layers/TransformLayer.cpp
    src/engine/render3d/editor/ClipGizmo.h
    src/engine/render3d/editor/ClipGizmo.cpp
    src/engine/render3d/models/ModelClipTransform.h
    src/engine/render3d/models/ModelClipTransform.cpp
    src/engine/render3d/models/ModelClipRenderer.h
    src/engine/render3d/models/ModelClipRenderer.cpp
    src/engine/face/tracking/FaceMesh.h
    src/engine/face/tracking/FaceMesh.cpp
    src/engine/face/tracking/Face111.h
    src/engine/face/tracking/Face111.cpp
    src/engine/render3d/models/ModelAsset.h
    src/engine/render3d/models/ModelAsset.cpp
    src/engine/render3d/splats/SplatAsset.h
    src/engine/render3d/splats/SplatAsset.cpp
    src/engine/face/swap/GlFaceSwapRenderer.h
    src/engine/face/swap/GlFaceSwapRenderer.cpp
    src/engine/render3d/models/GlModelRenderer.h
    src/engine/render3d/models/GlModelRenderer.cpp
    src/engine/render3d/splats/GlSplatRenderer.h
    src/engine/render3d/splats/GlSplatRenderer.cpp
    src/engine/face/swap/FaceSwapSource.h
    src/engine/face/swap/FaceSwapSource.cpp
    src/engine/face/props/FacePropCatalog.h
    src/engine/face/props/FacePropCatalog.cpp
    src/engine/face/props/FacePropImport.h
    src/engine/face/props/FacePropImport.cpp
    src/engine/ml/speech/DeepFilterDenoiser.h
    src/engine/ml/speech/DeepFilterDenoiser.cpp
    src/engine/ml/speech/DenoiseAudio.h
    src/engine/ml/speech/DenoiseAudio.cpp
    src/engine/encode/writers/MatteWriter.h
    src/engine/encode/writers/MatteWriter.cpp
    src/engine/encode/proxies/ProxyNaming.h
    src/engine/encode/proxies/ProxyNaming.cpp
    src/engine/encode/proxies/ReverseProxyCache.h
    src/engine/encode/proxies/ReverseProxyCache.cpp
    src/engine/encode/proxies/ReverseRenderer.h
    src/engine/encode/proxies/ReverseRenderer.cpp
    src/engine/encode/proxies/ProxyEncoder.h
    src/engine/encode/proxies/ProxyEncoder.cpp
    src/engine/encode/proxies/PreviewProxyRenderer.h
    src/engine/encode/proxies/PreviewProxyRenderer.cpp
    src/engine/encode/MediaEditor.h
    src/engine/encode/MediaEditor.cpp
    src/engine/encode/writers/AudioFileWriter.h
    src/engine/encode/writers/AudioFileWriter.cpp
    src/engine/audio/AudioRecorder.h
    src/engine/audio/AudioRecorder.cpp
    src/engine/ml/speech/SpeechAudio.h
    src/engine/ml/speech/SpeechAudio.cpp
    src/engine/ml/speech/transcription/SileroVad.h
    src/engine/ml/speech/transcription/SileroVad.cpp
    src/engine/ml/speech/transcription/CtcAligner.h
    src/engine/ml/speech/transcription/CtcAligner.cpp
    src/engine/ml/speech/transcription/LocalTranscription.h
    src/engine/ml/speech/transcription/LocalTranscription.cpp
    src/engine/ml/speech/diarization/KaldiFbank.h
    src/engine/ml/speech/diarization/KaldiFbank.cpp
    src/engine/ml/speech/diarization/SpeakerDiarizer.h
    src/engine/ml/speech/diarization/SpeakerDiarizer.cpp
    src/engine/project/ProjectBundle.h
    src/engine/project/ProjectBundle.cpp
    src/engine/project/ProjectDependencies.h
    src/engine/project/ProjectDependencies.cpp
    src/engine/gpu/device/GpuPreference.h
    src/engine/gpu/device/GpuPreference.cpp
    src/engine/gpu/device/GpuDevice.h
    src/engine/gpu/device/GpuDevice.cpp
)
target_include_directories(driftengine PUBLIC src)
# cgltf is PRIVATE: ModelAsset.h exposes no cgltf type, so nothing outside ModelAsset.cpp includes it.
target_include_directories(driftengine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/cgltf)
target_link_libraries(driftengine PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Multimedia FFmpeg::FFmpeg driftcore
    driftaudiodsp onnxruntime::headers)
target_link_libraries(driftengine PRIVATE ${DRIFT_ZSTD_TARGET} OpenSSL::Crypto ${CMAKE_DL_LIBS})
if(WIN32)
    # Hybrid-graphics laptops: D3D11VA -> OpenGL preview interop, and DXGI adapter enumeration for
    # the GPU preference and for opening D3D11VA on the GPU that renders.
    target_sources(driftengine PRIVATE
        src/engine/decode/hw/D3d11GlInterop.h
        src/engine/decode/hw/D3d11GlInterop.cpp
    )
    target_link_libraries(driftengine PRIVATE d3d11 dxgi d3dcompiler opengl32)
endif()
target_compile_definitions(driftengine PRIVATE DRIFT_VERSION="${DRIFT_VERSION_STRING}")

if(WITH_BGREMOVAL)
    target_include_directories(driftengine PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(driftengine PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(driftengine PUBLIC DRIFT_WITH_BGREMOVAL)
endif()

if(DRIFT_WITH_SKIA)
    find_package(Skia REQUIRED)
    target_sources(driftengine PRIVATE
        src/engine/render2d/skia/SkiaRuntime.h
        src/engine/render2d/skia/SkiaRuntime.cpp
        src/engine/render2d/skia/SkiaPath.h
        src/engine/render2d/skia/SkiaPath.cpp
        src/engine/render2d/skia/SkiaShading.h
        src/engine/render2d/skia/SkiaShading.cpp
        src/engine/render2d/shapes/SkiaShapePainter.h
        src/engine/render2d/shapes/SkiaShapePainter.cpp
        src/engine/render2d/text/fonts/SkiaFonts.h
        src/engine/render2d/text/fonts/SkiaFonts.cpp
        src/engine/render2d/text/fonts/SkiaFontCollection.h
        src/engine/render2d/text/fonts/SkiaFontCollection.cpp
        src/engine/render2d/vector/SkiaVectorResources.h
        src/engine/render2d/vector/SkiaVectorResources.cpp
        src/engine/render2d/vector/SkiaVectorObservers.h
        src/engine/render2d/vector/SkiaVectorObservers.cpp
        src/engine/render2d/text/SkiaTextPainter.h
        src/engine/render2d/text/SkiaTextPainter.cpp
        src/engine/render2d/text/SkiaTextEffects.h
        src/engine/render2d/text/SkiaTextEffects.cpp
    )
    # build-skia.sh compiles Skia with RTTI (see the extra_cflags_cc note there), so Drift classes
    # may subclass Skia interfaces from any translation unit. vcpkg's port on Windows keeps /GR-,
    # which is harmless: MSVC emits type descriptors as COMDAT wherever they are needed, so a
    # derived class never depends on its base's archive carrying one.
    # PRIVATE: no header outside src/engine/render2d/**/Skia*.h includes Skia (Vector*.cpp do, under
    # DRIFT_WITH_SKIA), so the defines and the include root stay out of the rest of the build.
    target_link_libraries(driftengine PRIVATE Skia::Skia)
    target_compile_definitions(driftengine PUBLIC DRIFT_WITH_SKIA)
    # Qt's `foreach` keyword macro collides with SkTHash::foreach(), which the Skottie headers
    # pull in. Nothing in Drift writes foreach; Q_FOREACH stays available.
    target_compile_definitions(driftengine PRIVATE QT_NO_FOREACH)
endif()

# --- application ------------------------------------------------------------
qt_add_executable(drift
    src/app/main.cpp
    src/ui/imageproviders/DriftImageProvider.h
    src/ui/imageproviders/DriftImageProvider.cpp
    src/ui/imageproviders/segment/SegmentImageProvider.h
    src/ui/imageproviders/segment/SegmentImageProvider.cpp
    src/ui/imageproviders/segment/SegmentImageStore.h
    src/ui/imageproviders/segment/SegmentImageStore.cpp
    src/ui/imageproviders/clippreview/ClipPreviewImageProvider.h
    src/ui/imageproviders/clippreview/ClipPreviewImageProvider.cpp
    src/playback/preview/ClipPreviewImageStore.h
    src/playback/preview/ClipPreviewImageStore.cpp
    src/ui/imageproviders/multicam/MulticamImageProvider.h
    src/ui/imageproviders/multicam/MulticamImageProvider.cpp
    src/ui/imageproviders/multicam/MulticamImageStore.h
    src/ui/imageproviders/multicam/MulticamImageStore.cpp
    src/ui/imageproviders/TextStylePreviewImageProvider.h
    src/ui/imageproviders/TextStylePreviewImageProvider.cpp
    src/ui/imageproviders/ShapePreviewImageProvider.h
    src/ui/imageproviders/ShapePreviewImageProvider.cpp
    src/ui/imageproviders/color/ColorImageProvider.h
    src/ui/imageproviders/color/ColorImageProvider.cpp
    src/app/color/LookThumbnailService.h
    src/app/color/ColorController.cpp
    src/app/color/ColorController.h
    src/app/color/ScopesController.h
    src/app/color/ScopesController.cpp
    src/app/color/LookThumbnailService.cpp
    src/app/online/addons/AddonEndpoint.h
    src/app/online/addons/AddonManager.h
    src/app/online/addons/AddonManager.cpp
    src/app/jobs/JobRegistry.h
    src/app/jobs/JobRegistry.cpp
    src/app/online/CloudProviders.h
    src/app/online/CloudProviders.cpp
    src/app/online/market/MarketEndpoint.h
    src/app/online/market/MarketIdentity.h
    src/app/online/market/MarketIdentity.cpp
    src/app/online/market/MarketClient.h
    src/app/online/market/MarketClient.cpp
    src/app/online/assets/DriftAssetStore.h
    src/app/online/assets/DriftAssetStore.cpp
    src/app/online/assets/SfxLibrary.h
    src/app/online/assets/SfxLibrary.cpp
    src/core/base/VersionCompare.h
    src/app/online/updates/UpdateChecker.h
    src/app/online/updates/UpdateChecker.cpp
    src/app/online/updates/UpdateAsset.h
    src/app/online/updates/UpdateAsset.cpp
    src/app/editor/AppController.h
    src/app/editor/AppController.cpp
    src/app/mcp/McpController.h
    src/app/mcp/McpController.cpp
    src/app/editor/PreferencesController.h
    src/app/editor/PreferencesController.cpp
    src/app/editor/ProjectFileController.h
    src/app/editor/ProjectFileController.cpp
    src/app/bin/AssetPreviewController.h
    src/app/bin/AssetPreviewController.cpp
    src/app/editor/sessions/SpeedCurveController.h
    src/app/editor/sessions/SpeedCurveController.cpp
    src/app/editor/sessions/CurveEditorController.h
    src/app/editor/sessions/CurveEditorController.cpp
    src/app/editor/sessions/KeyframeController.h
    src/app/editor/sessions/KeyframeController.cpp
    src/app/editor/sessions/KeyframeRows.h
    src/app/editor/sessions/KeyframeRows.cpp
    src/app/editor/sessions/KeyframeRowsModel.h
    src/app/editor/sessions/KeyframeRowsModel.cpp
    src/app/editor/sessions/KeyframeCurves.h
    src/app/editor/sessions/KeyframeCurves.cpp
    src/app/editor/sessions/KeyframeCurvesModel.h
    src/app/editor/sessions/KeyframeCurvesModel.cpp
    src/app/editor/sessions/KeyframeSelection.h
    src/app/editor/sessions/KeyframeSelection.cpp
    src/app/editor/sessions/KeyframeTimeView.h
    src/app/editor/sessions/KeyframeTimeView.cpp
    src/app/editor/sessions/MaskEditSession.h
    src/app/editor/sessions/MaskEditSession.cpp
    src/app/editor/sessions/MaskStackModel.h
    src/app/editor/sessions/MaskStackModel.cpp
    src/app/editor/sessions/PolylineDraft.h
    src/app/editor/sessions/PolylineDraft.cpp
    src/app/editor/sessions/MaskingController.h
    src/app/editor/sessions/MaskingController.cpp
    src/app/editor/sessions/SubtitleController.h
    src/app/editor/sessions/SubtitleController.cpp
    src/app/editor/sessions/SubtitleCueModel.h
    src/app/editor/sessions/SubtitleCueModel.cpp
    src/app/editor/sessions/SubtitleCueFilterModel.h
    src/app/editor/sessions/SubtitleCueFilterModel.cpp
    src/app/jobs/vision/SegmentationController.h
    src/app/jobs/vision/SegmentationController.cpp
    src/app/jobs/vision/SegmentationPlan.h
    src/app/jobs/vision/SceneDetectController.h
    src/app/jobs/vision/SceneDetectController.cpp
    src/app/jobs/vision/DepthController.h
    src/app/jobs/vision/DepthController.cpp
    src/app/jobs/audio/DenoiseController.h
    src/app/jobs/audio/DenoiseController.cpp
    src/app/jobs/vision/RestoreController.h
    src/app/jobs/vision/RestoreController.cpp
    src/app/jobs/vision/FaceTrackingController.h
    src/app/jobs/vision/FaceTrackingController.cpp
    src/app/jobs/vision/StabilizeController.h
    src/app/jobs/vision/StabilizeController.cpp
    src/app/jobs/vision/TrackingController.h
    src/app/jobs/vision/TrackingController.cpp
    src/app/jobs/vision/TrackerListModel.h
    src/app/jobs/vision/TrackerListModel.cpp
    src/app/bin/MediaBinController.h
    src/app/bin/MediaBinController.cpp
    src/app/bin/SourceLabController.h
    src/app/bin/SourceLabController.cpp
    src/app/jobs/audio/TtsController.h
    src/app/jobs/audio/TtsController.cpp
    src/app/jobs/audio/VoiceLibrary.h
    src/app/jobs/audio/VoiceLibrary.cpp
    src/app/mcp/McpDetail.h
    src/app/editor/AppControllerDetail.h
    src/app/editor/EditorState.h
    src/app/editor/LayoutStore.h
    src/app/editor/LayoutStore.cpp
    src/app/editor/WorkspaceStore.h
    src/app/editor/WorkspaceStore.cpp
    src/app/platform/FileDialogs.h
    src/app/platform/FileDialogs.cpp
    src/app/platform/Haptics.h
    src/app/platform/Haptics.cpp
    src/app/platform/PointerDevices.h
    src/app/platform/PointerDevices.cpp
    src/app/bin/AssetLibrary.h
    src/app/bin/AssetLibrary.cpp
    src/app/bin/BinFolderListModel.h
    src/app/bin/BinFolderListModel.cpp
    src/app/editor/TimelineClipsModel.h
    src/app/editor/TimelineClipsModel.cpp
    src/playback/PlaybackEngine.h
    src/playback/PlaybackEngine.cpp
    src/playback/PlaybackClock.h
    src/playback/PlaybackClock.cpp
    src/playback/AudioOutputChannel.h
    src/playback/AudioOutputChannel.cpp
    src/playback/preview/ClipPreviewPlayer.h
    src/playback/preview/ClipPreviewPlayer.cpp
    src/playback/preview/CompositorService.h
    src/playback/preview/CompositorService.cpp
    src/playback/stats/PlaybackStats.h
    src/playback/stats/PlaybackStats.cpp
    src/playback/stats/PlaybackDiagnostics.h
    src/playback/stats/PlaybackDiagnostics.cpp
    src/playback/stats/PerfLog.h
    src/playback/stats/PerfLog.cpp
    src/ui/preview/PreviewItem.h
    src/ui/preview/PreviewItem.cpp
    src/ui/preview/BrushStrokeItem.h
    src/ui/preview/BrushStrokeItem.cpp
    src/ui/color/ColorWheelMath.h
    src/ui/color/ColorWheelPad.h
    src/ui/color/ColorWheelPad.cpp
    src/ui/color/CurveEdit.h
    src/ui/color/CurveEdit.cpp
    src/ui/keyframes/KeyframeDot.h
    src/ui/keyframes/KeyframeDot.cpp
    src/ui/keyframes/KeyframeLanesItem.h
    src/ui/keyframes/KeyframeLanesItem.cpp
    src/ui/keyframes/KeyframeCurvesItem.h
    src/ui/keyframes/KeyframeCurvesItem.cpp
    src/ui/color/CurvePlot.h
    src/ui/color/CurvePlot.cpp
    src/ui/preview/TrackerOverlayItem.h
    src/ui/preview/TrackerOverlayItem.cpp
    src/ui/preview/TrackerOverlayMath.h
    src/ui/preview/ConfidenceStripItem.h
    src/ui/preview/ConfidenceStripItem.cpp
    src/ui/preview/PreviewController.h
    src/ui/preview/PreviewController.cpp
    src/ui/timeline/ClipThumbnailStore.h
    src/ui/timeline/ClipThumbnailStore.cpp
    src/ui/timeline/TimelineLayout.h
    src/ui/timeline/TimelineLayout.cpp
    src/ui/timeline/TimelineTrackItem.h
    src/ui/timeline/TimelineTrackItem.cpp
    src/ui/timeline/TimelineViewState.h
    src/ui/timeline/TimelineViewState.cpp
)

# The MCP server itself ships everywhere. On Android it listens on loopback like it does on
# desktop, and an agent reaches it through `adb forward` — which is what makes the phone build
# drivable by tooling instead of by blind coordinate taps.
target_sources(drift PRIVATE
    src/app/mcp/protocol/McpSession.h
    src/app/mcp/protocol/McpSession.cpp
    src/app/platform/RunningInstances.h
    src/app/platform/RunningInstances.cpp
    src/app/mcp/tools/McpCatalog.h
    src/app/mcp/tools/McpCatalog.cpp
    src/app/mcp/protocol/McpProtocol.h
    src/app/mcp/protocol/McpProtocol.cpp
    src/app/mcp/tools/McpDispatcher.h
    src/app/mcp/tools/McpDispatcher.cpp
    src/app/mcp/tools/McpDispatcherExtended.cpp
    src/app/mcp/tools/McpEffectAuthoring.h
    src/app/mcp/tools/McpEffectAuthoring.cpp
    src/app/mcp/protocol/McpValidate.h
    src/app/mcp/protocol/McpValidate.cpp
    src/app/mcp/tools/McpMarket.cpp
    src/app/mcp/protocol/McpJson.h
)

# The HTTP listener needs QTcpServer, which a browser has no sockets for; McpController does not
# construct a McpServer under Q_OS_WASM.
if(NOT EMSCRIPTEN)
    target_sources(drift PRIVATE
        src/app/mcp/transport/McpHttp.h
        src/app/mcp/transport/McpHttp.cpp
        src/app/mcp/transport/McpServer.h
        src/app/mcp/transport/McpServer.cpp
    )
endif()

# The stdio transport and the headless entry point stay desktop-only: both are process entry
# points driven from a terminal, which an APK has no equivalent of.
if(NOT ANDROID AND NOT EMSCRIPTEN)
    target_sources(drift PRIVATE
        src/app/mcp/transport/McpStdio.h
        src/app/mcp/transport/McpStdio.cpp
        src/app/mcp/transport/McpBridge.h
        src/app/mcp/transport/McpBridge.cpp
        src/app/mcp/transport/McpStdioServer.h
        src/app/mcp/transport/McpStdioServer.cpp
        src/app/HeadlessApp.h
        src/app/HeadlessApp.cpp
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
    # Channel-dependent: a nightly is Drift Nightly.app with its own bundle id, so it installs
    # beside a stable copy instead of replacing it. "Drift Nightly" does not collide with the
    # Drift/ QML module directory either (see the note above).
    set(DRIFT_MACOS_EXECUTABLE "${DRIFT_APP_NAME}")
    set(DRIFT_MACOS_BUNDLE_ID "${DRIFT_APP_ID}")
    set(DRIFT_MACOS_ICON_FILE "Drift.icns")
    set(DRIFT_MACOS_ICON_NAME_ENTRY "")

    set(_drift_developer_dir "$ENV{DEVELOPER_DIR}")
    if(NOT _drift_developer_dir)
        execute_process(
            COMMAND /usr/bin/xcode-select -p
            OUTPUT_VARIABLE _drift_developer_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
    if(NOT EXISTS "${_drift_developer_dir}/usr/bin/actool"
       AND EXISTS "/Applications/Xcode.app/Contents/Developer/usr/bin/actool")
        set(_drift_developer_dir "/Applications/Xcode.app/Contents/Developer")
    endif()

    set(_drift_xcode_version "")
    if(EXISTS "${_drift_developer_dir}/usr/bin/actool"
       AND EXISTS "${_drift_developer_dir}/usr/bin/xcodebuild")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "DEVELOPER_DIR=${_drift_developer_dir}"
                "${_drift_developer_dir}/usr/bin/xcodebuild" -version
            OUTPUT_VARIABLE _drift_xcode_version_output
            RESULT_VARIABLE _drift_xcodebuild_result
            ERROR_QUIET)
        if(_drift_xcodebuild_result EQUAL 0)
            string(REGEX MATCH "Xcode[ \t]+([0-9]+(\\.[0-9]+)*)"
                _drift_xcode_version_match "${_drift_xcode_version_output}")
            set(_drift_xcode_version "${CMAKE_MATCH_1}")
        endif()
    endif()

    set(DRIFT_MACOS_ICNS "${CMAKE_SOURCE_DIR}/resources/macos/${DRIFT_MACOS_ICON_FILE}")
    if(NOT "${_drift_xcode_version}" STREQUAL ""
       AND "${_drift_xcode_version}" VERSION_GREATER_EQUAL "26.0")
        set(_drift_icon_assets_dir "${CMAKE_CURRENT_BINARY_DIR}/macos-icon-assets")
        set(_drift_icon_assets_car "${_drift_icon_assets_dir}/Assets.car")
        set(_drift_icon_fallback_icns "${_drift_icon_assets_dir}/Drift.icns")
        set(_drift_icon_partial_plist "${_drift_icon_assets_dir}/partial-info.plist")
        set(_drift_icon_source "${CMAKE_SOURCE_DIR}/resources/macos/Drift.icon")
        file(GLOB _drift_icon_inputs CONFIGURE_DEPENDS
            "${_drift_icon_source}/icon.json"
            "${_drift_icon_source}/Assets/*.png"
            "${_drift_icon_source}/Assets/*.svg")

        add_custom_command(
            OUTPUT "${_drift_icon_assets_car}" "${_drift_icon_fallback_icns}"
                "${_drift_icon_partial_plist}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_drift_icon_assets_dir}"
            COMMAND "${_drift_developer_dir}/usr/bin/actool" "${_drift_icon_source}"
                --compile "${_drift_icon_assets_dir}"
                --app-icon Drift
                --enable-on-demand-resources NO
                --target-device mac
                --platform macosx
                --minimum-deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                --output-partial-info-plist "${_drift_icon_partial_plist}"
                --output-format human-readable-text
                --errors --warnings
            # Icon-only rebuilds do not relink the executable or update the bundle's
            # timestamp. Invalidate its cached Dock appearances on the next launch.
            COMMAND "${CMAKE_COMMAND}" -E touch_nocreate
                "$<TARGET_BUNDLE_DIR:drift>/Contents/Info.plist"
                "$<TARGET_BUNDLE_DIR:drift>"
            DEPENDS ${_drift_icon_inputs}
            VERBATIM)
        set_source_files_properties(
            "${_drift_icon_assets_car}" "${_drift_icon_fallback_icns}"
            PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(drift PRIVATE "${_drift_icon_assets_car}" "${_drift_icon_fallback_icns}")
        set(DRIFT_MACOS_ICON_FILE "Drift")
        set(DRIFT_MACOS_ICON_NAME_ENTRY
            "    <key>CFBundleIconName</key>\n    <string>Drift</string>")
        message(STATUS "Compiling Drift's macOS icon stack with Xcode ${_drift_xcode_version}")
    else()
        target_sources(drift PRIVATE "${DRIFT_MACOS_ICNS}")
        set_source_files_properties("${DRIFT_MACOS_ICNS}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        message(WARNING "Xcode 26 or newer is unavailable; using the static macOS icon")
    endif()

    # Not CMake's MACOSX_BUNDLE_* properties: the NS* and LSMinimumSystemVersion keys have none.
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY)

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

# Built-in theme documents. ThemeManager reads these; Theme.qml is a facade over the
# resolved tokens, so the rest of the QML keeps using the same property names.
qt_add_resources(drift "drift_themes"
    PREFIX "/themes"
    BASE resources/themes
    FILES
        resources/themes/dark.json
        resources/themes/light.json
        resources/themes/graphite.json
        resources/themes/frost.json
        resources/themes/theme.schema.json
)

# Built-in workspace presets. WorkspaceStore lists this directory, so a new preset is a
# JSON file plus an entry here.
qt_add_resources(drift "drift_workspaces"
    PREFIX "/workspaces"
    BASE resources/workspaces
    FILES
        resources/workspaces/edit.json
        resources/workspaces/edit-portrait.json
        resources/workspaces/source-lab.json
        resources/workspaces/audio.json
        resources/workspaces/record.json
        resources/workspaces/subtitles.json
        resources/workspaces/ai.json
        resources/workspaces/color.json
        resources/workspaces/keyframes.json
        resources/workspaces/masking-tracking.json
)

set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# Canvas preset catalog + the (template, quality) -> pixels arithmetic, shared by the desktop
# layout dialog and the phone layout sheet so the two cannot drift apart again.
set_source_files_properties(src/qml/LayoutPresets.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# How a download job renders as words, shared by the desktop DownloadsWindow and the phone
# AndroidDownloadsSheet so the two hosts of the same jobs cannot word them differently.
set_source_files_properties(src/qml/DownloadFormat.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# Import + reporting policy, needed by surfaces that have no AssetsPanel to borrow it from.
set_source_files_properties(src/qml/MediaImport.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# App-wide notification queue; the visual host is components/shell/ToastHost.qml.
set_source_files_properties(src/qml/Toasts.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# In-flight lift-and-drop state for the phone asset sheet; the ghost is drawn by
# AndroidBottomSheet and the drop is performed by AndroidTimeline.
set_source_files_properties(src/qml/TouchDrag.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
set_source_files_properties(src/qml/AssetDrag.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
# The editor main window, for panels that may live in a secondary window.
set_source_files_properties(src/qml/ShellBridge.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

# Lucide icons: use the original scalable SVG sources in QML.
file(GLOB DRIFT_LUCIDE_ICONS RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
     "resources/icons/*.svg")

# Native window backdrops. Linux only makes the window translucent; blur is
# left to the compositor.
set(DRIFT_BACKDROP_PLATFORM_SOURCES)
set(DRIFT_BACKDROP_PLATFORM_LIBS)
set(DRIFT_BACKDROP_PLATFORM_DEFS)
if(WIN32)
    list(APPEND DRIFT_BACKDROP_PLATFORM_SOURCES src/ui/theme/backdrop/WindowBackdrop_win.cpp)
    list(APPEND DRIFT_BACKDROP_PLATFORM_LIBS dwmapi)
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # .mm sources are Objective-C++. The project() languages are CXX and C so
    # Linux configure does not ask for a language this host does not have.
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    enable_language(OBJCXX)
    list(APPEND DRIFT_BACKDROP_PLATFORM_SOURCES src/ui/theme/backdrop/WindowBackdrop_mac.mm)
    list(APPEND DRIFT_BACKDROP_PLATFORM_LIBS "-framework Cocoa")
endif()

qt_add_qml_module(drift
    URI Drift
    VERSION 1.0
    DISCARD_QML_CONTENTS
    SOURCES
        src/ui/theme/ThemeManager.h
        src/ui/theme/ThemeManager.cpp
        src/ui/theme/backdrop/WindowBackdrop.h
        src/ui/theme/backdrop/WindowBackdrop.cpp
        ${DRIFT_BACKDROP_PLATFORM_SOURCES}
    QML_FILES
        src/qml/Shell.qml
        src/qml/LayoutPresets.qml
        src/qml/MediaImport.qml
        src/qml/Main.qml
        src/qml/AndroidProjectsPage.qml
        src/qml/AndroidShareTargetSheet.qml
        src/qml/AndroidLinkImport.qml
        src/qml/DownloadFormat.qml
        src/qml/AndroidDownloadsSheet.qml
        src/qml/AndroidNotificationsSheet.qml
        src/qml/components/shell/NavRailButton.qml
        # Touch entry point, loaded instead of Main.qml on Android (see src/app/main.cpp).
        src/qml/AndroidLayoutSheet.qml
        src/qml/AndroidMain.qml
        src/qml/AndroidMoreToolsSheet.qml
        src/qml/AndroidFadeSheet.qml
        src/qml/AndroidEasingSheet.qml
        src/qml/AndroidProjectSheet.qml
        src/qml/AndroidHome.qml
        src/qml/AndroidEditor.qml
        src/qml/AndroidPreview.qml
        src/qml/AndroidTimeline.qml
        src/qml/AndroidTopBar.qml
        src/qml/AndroidBottomRail.qml
        src/qml/AndroidBottomSheet.qml
        src/qml/AndroidAddMenu.qml
        src/qml/AndroidClipToolbar.qml
        src/qml/AndroidTextComposer.qml
        src/qml/AndroidMediaPreview.qml
        src/qml/Theme.qml
        src/qml/Toasts.qml
        src/qml/TouchDrag.qml
        src/qml/AssetDrag.qml
        src/qml/ShellBridge.qml
        src/qml/EditorHeader.qml
        src/qml/AssetsPanel.qml
        src/qml/PreviewPanel.qml
        src/qml/components/preview/ViewAxisWidget.qml
        src/qml/components/preview/ViewNavPad.qml
        src/qml/components/preview/PreviewViewport.qml
        src/qml/components/preview/PreviewStatusLayer.qml
        src/qml/components/preview/PreviewToolHost.qml
        src/qml/components/preview/InlineTextEditor.qml
        src/qml/components/preview/PreviewOsd.qml
        src/qml/components/preview/PreviewModeSwitch.qml
        src/qml/components/preview/Preview3DTools.qml
        src/qml/components/preview/PreviewOptionsMenu.qml
        src/qml/components/preview/PreviewTransportBar.qml
        src/qml/components/preview/modes/Preview2DMode.qml
        src/qml/components/preview/modes/Preview3DMode.qml
        src/qml/components/preview/modes/PreviewModeSwitchSegment.qml
        src/qml/components/preview/modes/PreviewModeSwitchPill.qml
        src/qml/components/preview/modes/Preview3DFlyHint.qml
        src/qml/components/preview/modes/Preview3DFlyDeadZone.qml
        src/qml/components/preview/TransformGizmo.qml
        src/qml/components/preview/TransformOverlay.qml
        src/qml/components/preview/PreviewDropOverlay.qml
        src/qml/components/preview/CropOverlay.qml
        src/qml/components/preview/GuideLayer.qml
        src/qml/components/preview/GuidesPopover.qml
        src/qml/components/preview/GuideEditOverlay.qml
        src/qml/components/preview/MaskOverlay.qml
        src/qml/components/preview/masking/MaskDrawOverlay.qml
        src/qml/components/preview/masking/MaskBrushOverlay.qml
        src/qml/components/preview/masking/MaskAiOverlay.qml
        src/qml/components/preview/masking/TrackerOverlay.qml
        src/qml/components/preview/DepthEffectOverlay.qml
        src/qml/components/preview/DepthLight3dOverlay.qml
        src/qml/components/preview/Checkerboard.qml
        src/qml/components/android/AndroidTransformOverlay.qml
        src/qml/components/android/AndroidCropOverlay.qml
        src/qml/SegmentationWindow.qml
        src/qml/MulticamWindow.qml
        src/qml/DenoiseWindow.qml
        src/qml/RestoreWindow.qml
        src/qml/DownloadsWindow.qml
        src/qml/SpeedCurveWindow.qml
        src/qml/FadeCurveWindow.qml
        src/qml/MediaPreviewWindow.qml
        src/qml/PropertiesPanel.qml
        src/qml/components/properties/GeneralInspector.qml
        src/qml/components/properties/TextInspector.qml
        src/qml/components/properties/TextParamSlots.qml
        src/qml/components/properties/PaintEditor.qml
        src/qml/components/properties/GradientStopEditor.qml
        src/qml/components/properties/ShadingLayerRow.qml
        src/qml/components/properties/TextAnimPresetTile.qml
        src/qml/components/properties/TextLookPicker.qml
        src/qml/components/properties/TransformInspector.qml
        src/qml/components/properties/CameraInspector.qml
        src/qml/components/properties/TransformLayerCard.qml
        src/qml/components/properties/StabilizeInspector.qml
        src/qml/components/properties/AudioInspector.qml
        src/qml/components/properties/AudioEffectsInspector.qml
        src/qml/components/properties/SpeedFadeInspector.qml
        src/qml/components/properties/AnimationInspector.qml
        src/qml/components/properties/TransitionInspector.qml
        src/qml/components/properties/BlendingInspector.qml
        src/qml/components/properties/ShapeInspector.qml
        src/qml/components/properties/VoiceoverDialog.qml
        src/qml/components/properties/VectorInspector.qml
        src/qml/components/properties/Model3DInspector.qml
        src/qml/components/properties/PdfInspector.qml
        src/qml/components/properties/MasksInspector.qml
        src/qml/components/properties/EffectsInspector.qml
        src/qml/components/properties/FacePropPicker.qml
        src/qml/components/properties/VectorSvgColorRow.qml
        src/qml/components/properties/Vec2ParamPad.qml
        src/qml/components/properties/TextLookTile.qml
        src/qml/components/properties/TextInspectorTextPage.qml
        src/qml/components/properties/TextInspectorTabBar.qml
        src/qml/components/properties/TextInspectorStylePage.qml
        src/qml/components/properties/TextInspectorSpacingSection.qml
        src/qml/components/properties/TextInspectorPresetSection.qml
        src/qml/components/properties/TextInspectorLookSection.qml
        src/qml/components/properties/TextInspectorFontSection.qml
        src/qml/components/properties/TextInspectorDecorations.qml
        src/qml/components/properties/TextInspectorContentSection.qml
        src/qml/components/properties/TextInspectorAnimatePage.qml
        src/qml/components/properties/TextInspectorAnimateHint.qml
        src/qml/components/properties/TextInspectorAnimSlotBar.qml
        src/qml/components/properties/TextInspectorAnimPresetGrid.qml
        src/qml/components/properties/TextInspectorAnimControls.qml
        src/qml/components/properties/TextInspectorAnimCategoryStrip.qml
        src/qml/components/properties/TextInspectorAnimAdvanced.qml
        src/qml/components/properties/TextInspectorAccentSection.qml
        src/qml/components/properties/TextContentResizeGrip.qml
        src/qml/components/properties/TextAnimPresetFrame.qml
        src/qml/components/properties/TextAlignDivider.qml
        src/qml/components/properties/SubtitleEditorPanel.qml
        src/qml/components/properties/SubtitleEditorHeader.qml
        src/qml/components/properties/SubtitleCueEmptyState.qml
        src/qml/components/properties/SubtitleCueCard.qml
        src/qml/components/properties/SpeedCurveTangentHandle.qml
        src/qml/components/properties/SpeedCurvePreviewStage.qml
        src/qml/components/properties/SpeedCurveKnob.qml
        src/qml/components/properties/SpeedCurveGridLine.qml
        src/qml/components/properties/SpeedCurveGraphPlayhead.qml
        src/qml/components/properties/SpeedCurveFilmstrip.qml
        src/qml/components/properties/SpeedCurveCanvas.qml
        src/qml/components/properties/ShadingLayerHeader.qml
        src/qml/components/properties/PropertiesTabRailSeparator.qml
        src/qml/components/properties/PropertiesSheetTabChip.qml
        src/qml/components/properties/PropertiesInspectorHeading.qml
        src/qml/components/properties/PropertiesEmptyState.qml
        src/qml/components/properties/KeyframeGraphValueAxis.qml
        src/qml/components/properties/KeyframeGraphTangentHandle.qml
        src/qml/components/properties/KeyframeGraphResizeGrip.qml
        src/qml/components/properties/KeyframeGraphPlayhead.qml
        src/qml/components/properties/KeyframeGraphLaneStatus.qml
        src/qml/components/properties/KeyframeGraphKeyDot.qml
        src/qml/components/properties/KeyframeGraphHeaderBar.qml
        src/qml/components/properties/KeyframeGraphGutterRule.qml
        src/qml/components/properties/KeyframeGraphFrame.qml
        src/qml/components/properties/KeyframeGraphClipSpan.qml
        src/qml/components/properties/KeyframeGraphBeatCanvas.qml
        src/qml/components/properties/GradientStopChip.qml
        src/qml/components/properties/GradientPreviewBar.qml
        src/qml/components/properties/GradientPresetSwatch.qml
        src/qml/components/properties/FontPickerTrigger.qml
        src/qml/components/properties/FontPickerSectionHeader.qml
        src/qml/components/properties/FontPickerPopupSurface.qml
        src/qml/components/properties/FontPickerPackBanner.qml
        src/qml/components/properties/FontPickerFamilyRow.qml
        src/qml/components/properties/FontPickerAddRow.qml
        src/qml/components/properties/FadeCurveToolbarRule.qml
        src/qml/components/properties/FadeCurvePlotFrame.qml
        src/qml/components/properties/FadeCurveCanvas.qml
        src/qml/components/properties/FacePropTile.qml
        src/qml/components/properties/EmojiPickerSurface.qml
        src/qml/components/properties/EmojiPickerEmptyLabel.qml
        src/qml/components/properties/EmojiGridCell.qml
        src/qml/components/properties/EffectsStackActions.qml
        src/qml/components/properties/EffectsParamGroupHeader.qml
        src/qml/components/properties/EffectsOccludeSection.qml
        src/qml/components/properties/EffectsHueParam.qml
        src/qml/components/properties/EffectsFileParam.qml
        src/qml/components/properties/EffectsCurveParam.qml
        src/qml/components/properties/EffectsLookParam.qml
        src/qml/components/properties/EffectsFaceSection.qml
        src/qml/components/properties/EffectsFaceIndexParam.qml
        src/qml/components/properties/EffectsEnumParam.qml
        src/qml/components/properties/EffectsEffectHeader.qml
        src/qml/components/properties/EffectsDepthSection.qml
        src/qml/components/properties/EffectsDepthPanel.qml
        src/qml/components/properties/EffectsColorSwatchChip.qml
        src/qml/components/properties/EffectsColorParam.qml
        src/qml/components/properties/EffectsClipParam.qml
        src/qml/components/properties/EffectsBoolParam.qml
        src/qml/components/properties/EffectsAutoKeyHint.qml
        src/qml/components/properties/ColorSwatchButton.qml
        src/qml/components/properties/CollapsibleSectionRule.qml
        src/qml/components/properties/CollapsibleSectionHeader.qml
        src/qml/components/properties/AudioEffectHeader.qml
        src/qml/components/assets/TextAssetsTab.qml
        src/qml/components/assets/ScenesTab.qml
        src/qml/components/assets/SubtitlesTab.qml
        src/qml/components/assets/SpeechTab.qml
        src/qml/components/assets/StickersTab.qml
        src/qml/components/assets/ShapesTab.qml
        src/qml/components/assets/AssetSearchBar.qml
        src/qml/components/assets/AssetDragSource.qml
        src/qml/components/assets/MasksTab.qml
        src/qml/components/assets/SoundsTab.qml
        src/qml/components/assets/TransitionsTab.qml
        src/qml/components/assets/MediaAssetsTab.qml
        src/qml/components/assets/MarketConsentPanel.qml
        src/qml/components/assets/MarketTab.qml
        src/qml/components/assets/MarketSearchField.qml
        src/qml/components/assets/DriftAssetsHome.qml
        src/qml/components/assets/DriftAssetCard.qml
        src/qml/components/assets/DriftAssetDetail.qml
        src/qml/components/assets/StockBrowser.qml
        src/qml/components/assets/SfxBrowser.qml
        src/qml/components/assets/StockThumb.qml
        src/qml/components/assets/StockItemDetail.qml
        src/qml/components/assets/BinBreadcrumb.qml
        src/qml/components/assets/AssetCategoryChips.qml
        src/qml/components/assets/EffectStacksSection.qml
        src/qml/components/assets/AssetFavoriteButton.qml
        src/qml/components/assets/AssetQuickAddButton.qml
        src/qml/components/assets/TransitionPresetCard.qml
        src/qml/components/assets/TextStyleUserCard.qml
        src/qml/components/assets/TextStylePackCard.qml
        src/qml/components/assets/StockAudioRow.qml
        src/qml/components/assets/StickerPresetCard.qml
        src/qml/components/assets/ShapePresetCard.qml
        src/qml/components/assets/SfxSoundRow.qml
        src/qml/components/assets/ScenesShotRow.qml
        src/qml/components/assets/MediaAssetsViewToolbar.qml
        src/qml/components/assets/MediaAssetsSelectionRing.qml
        src/qml/components/assets/MediaAssetsListThumb.qml
        src/qml/components/assets/MediaAssetsListRow.qml
        src/qml/components/assets/MediaAssetsListLabels.qml
        src/qml/components/assets/MediaAssetsJobStatus.qml
        src/qml/components/assets/MediaAssetsItemMenu.qml
        src/qml/components/assets/MediaAssetsGridNameRow.qml
        src/qml/components/assets/MediaAssetsGridFrame.qml
        src/qml/components/assets/MediaAssetsGridCard.qml
        src/qml/components/assets/MediaAssetsGridBusyOverlay.qml
        src/qml/components/assets/MediaAssetsFormatRow.qml
        src/qml/components/assets/MediaAssetsFolderMenu.qml
        src/qml/components/assets/MediaAssetsEmptyState.qml
        src/qml/components/assets/MediaAssetsDurationBadge.qml
        src/qml/components/assets/MaskPresetCard.qml
        src/qml/components/assets/MarketSectionTab.qml
        src/qml/components/assets/EffectTemplateCard.qml
        src/qml/components/assets/EffectStackCard.qml
        src/qml/components/assets/EffectPresetCard.qml
        src/qml/components/assets/DownloadsJobRow.qml
        src/qml/components/assets/AudioEffectPresetCard.qml
        src/qml/components/assets/AssetsPanelTabRail.qml
        src/qml/components/assets/AssetsPanelTabButton.qml
        src/qml/components/assets/AssetsPanelImportSplit.qml
        src/qml/components/assets/AssetsPanelImportOverlay.qml
        src/qml/components/assets/AssetsPanelImportMenu.qml
        src/qml/components/assets/AssetsPanelHeader.qml
        src/qml/components/assets/AssetsPanelDropOverlay.qml
        src/qml/TimelinePanel.qml
        src/qml/components/timeline/TimelineToolbar.qml
        src/qml/components/timeline/TimelineToolbarActions.qml
        src/qml/components/timeline/TimelineToolbarCustomizeDialog.qml
        src/qml/components/timeline/TimelineToolbarCustomizer.qml
        src/qml/components/timeline/TimelineOverview.qml
        src/qml/components/timeline/TrackHeaderColumn.qml
        src/qml/components/timeline/TransformSpanBracket.qml
        src/qml/components/timeline/TransformCoverageOverlay.qml
        src/qml/components/timeline/VoiceoverControlBar.qml
        src/qml/components/timeline/AudioMixerStrip.qml
        src/qml/components/panels/AudioMixerPanel.qml
        src/qml/components/panels/SubtitlesPanel.qml
        src/qml/components/panels/RecorderPanel.qml
        src/qml/components/panels/DenoisePanel.qml
        src/qml/components/panels/ColorPanel.qml
        src/qml/components/panels/LooksPanel.qml
        src/qml/components/panels/ScopesPanel.qml
        src/qml/components/panels/KeyframesPanel.qml
        src/qml/components/panels/MaskingPanel.qml
        src/qml/components/panels/TrackingPanel.qml
        src/qml/components/panels/SubtitleLanePanel.qml
        src/qml/components/panels/SubtitleTextPanel.qml
        src/qml/components/subtitles/SubtitleLaneToolbar.qml
        src/qml/components/subtitles/SubtitleLaneWaveform.qml
        src/qml/components/subtitles/SubtitleLaneCueBlock.qml
        src/qml/components/subtitles/SubtitleLaneView.qml
        src/qml/components/subtitles/SubtitleLaneInteraction.qml
        src/qml/components/subtitles/SubtitleCueGridToolbar.qml
        src/qml/components/subtitles/SubtitleCueGridHeader.qml
        src/qml/components/subtitles/SubtitleCueGridRow.qml
        src/qml/components/subtitles/SubtitleTime.js
        src/qml/components/panels/SubtitleTimingPanel.qml
        src/qml/components/keyframes/KeyframesContextMenu.qml
        src/qml/components/keyframes/KeyframesCurvesInteraction.qml
        src/qml/components/keyframes/KeyframesCurvesView.qml
        src/qml/components/keyframes/KeyframesInteractionSurface.qml
        src/qml/components/keyframes/KeyframesDragReadout.qml
        src/qml/components/keyframes/KeyframesLanesView.qml
        src/qml/components/keyframes/KeyframesMarquee.qml
        src/qml/components/keyframes/KeyframesPlayheadLine.qml
        src/qml/components/keyframes/KeyframesRuler.qml
        src/qml/components/keyframes/KeyframesStretchBox.qml
        src/qml/components/keyframes/KeyframesToolbar.qml
        src/qml/components/keyframes/KeyframesTreeRow.qml
        src/qml/components/keyframes/KeyframesValueScrubber.qml
        src/qml/components/color/GradeSlider.qml
        src/qml/components/color/ColorWheel.qml
        src/qml/components/color/CurveEditor.qml
        src/qml/components/panels/HistoryPanel.qml
        src/qml/components/panels/SourceViewerPanel.qml
        src/qml/components/panels/sourcelab/SourceLabViewerPanel.qml
        src/qml/components/panels/sourcelab/SourceLabBinPanel.qml
        src/qml/components/panels/sourcelab/SourceLabBinRow.qml
        src/qml/components/panels/sourcelab/SourceLabBinBadge.qml
        src/qml/components/panels/sourcelab/SourceLabBinFilterRow.qml
        src/qml/components/panels/sourcelab/SourceLabTrimPanel.qml
        src/qml/components/panels/sourcelab/SourceLabRangesPanel.qml
        src/qml/components/panels/sourcelab/SourceLabProcessPanel.qml
        src/qml/components/panels/sourcelab/SourceLabOutputPanel.qml
        src/qml/components/panels/sourcelab/SourceLabTime.js
        src/qml/components/panels/sourcelab/SourceLabCropOverlay.qml
        src/qml/components/panels/sourcelab/SourceLabCompareView.qml
        src/qml/components/panels/sourcelab/SourceLabPaneLabel.qml
        src/qml/components/panels/sourcelab/SourceLabTimecodeField.qml
        src/qml/components/panels/sourcelab/SourceLabTransport.qml
        src/qml/components/panels/sourcelab/SourceLabHandle.qml
        src/qml/components/panels/sourcelab/SourceLabWaveformLane.qml
        src/qml/components/panels/sourcelab/SourceLabRangeSpan.qml
        src/qml/components/panels/sourcelab/SourceLabOverview.qml
        src/qml/components/panels/sourcelab/SourceLabStrip.qml
        src/qml/components/panels/sourcelab/SourceLabRangeRow.qml
        src/qml/components/panels/sourcelab/SourceLabSection.qml
        src/qml/components/panels/sourcelab/SourceLabSliderRow.qml
        src/qml/components/panels/sourcelab/SourceLabCropSection.qml
        src/qml/components/panels/sourcelab/SourceLabSpeedSection.qml
        src/qml/components/panels/sourcelab/SourceLabAudioSection.qml
        src/qml/components/panels/sourcelab/SourceLabStabilizeSection.qml
        src/qml/components/panels/sourcelab/SourceLabEnhanceSection.qml
        src/qml/components/panels/sourcelab/SourceLabPlanCard.qml
        src/qml/components/panels/sourcelab/SourceLabProgressCard.qml
        src/qml/components/panels/sourcelab/SourceLabExportOptions.qml
        src/qml/components/panels/AgentLogPanel.qml
        src/qml/components/timeline/TimelineClipItem.qml
        src/qml/components/timeline/PlayheadInterpolator.qml
        src/qml/components/timeline/TimelineTrackArea.qml
        src/qml/components/timeline/TimelineDropRouter.qml
        src/qml/components/timeline/VoiceoverTransportButton.qml
        src/qml/components/timeline/VoiceoverTransport.qml
        src/qml/components/timeline/VoiceoverTimerReadout.qml
        src/qml/components/timeline/VoiceoverStatus.qml
        src/qml/components/timeline/VoiceoverRecLabel.qml
        src/qml/components/timeline/VoiceoverRecDot.qml
        src/qml/components/timeline/VoiceoverMicPicker.qml
        src/qml/components/timeline/VoiceoverMicMenuItem.qml
        src/qml/components/timeline/VoiceoverLevelMeter.qml
        src/qml/components/timeline/VoiceoverGlow.qml
        src/qml/components/timeline/VoiceoverGainControl.qml
        src/qml/components/timeline/VoiceoverBarDivider.qml
        src/qml/components/timeline/TransformCoverageTint.qml
        src/qml/components/timeline/TrackHeaderWash.qml
        src/qml/components/timeline/TrackHeaderRow.qml
        src/qml/components/timeline/TrackHeaderResizeHandle.qml
        src/qml/components/timeline/TrackHeaderRenameDialog.qml
        src/qml/components/timeline/TrackHeaderMuteButton.qml
        src/qml/components/timeline/TrackHeaderMicButton.qml
        src/qml/components/timeline/TrackHeaderLaneEye.qml
        src/qml/components/timeline/TrackHeaderInsertLine.qml
        src/qml/components/timeline/TrackHeaderInlineName.qml
        src/qml/components/timeline/TrackHeaderHideButton.qml
        src/qml/components/timeline/TrackHeaderGrip.qml
        src/qml/components/timeline/TrackHeaderEdge.qml
        src/qml/components/timeline/TrackHeaderDeleteDialog.qml
        src/qml/components/timeline/TrackHeaderCoversMenu.qml
        src/qml/components/timeline/TrackHeaderContextMenu.qml
        src/qml/components/timeline/TrackHeaderCompactName.qml
        src/qml/components/timeline/TrackHeaderClipDisplayButton.qml
        src/qml/components/timeline/TrackHeaderChannelLanesButton.qml
        src/qml/components/timeline/TrackHeaderBandName.qml
        src/qml/components/timeline/TimelineWorkAreaShade.qml
        src/qml/components/timeline/TimelineWorkAreaOverlay.qml
        src/qml/components/timeline/TimelineWorkAreaEdge.qml
        src/qml/components/timeline/TimelineTrimHandle.qml
        src/qml/components/timeline/TimelineTrimGrip.qml
        src/qml/components/timeline/TimelineTransitionRegion.qml
        src/qml/components/timeline/TimelineTransitionLabel.qml
        src/qml/components/timeline/TimelineTransitionHatch.qml
        src/qml/components/timeline/TimelineTransitionDropOutline.qml
        src/qml/components/timeline/TimelineTrackTransformWash.qml
        src/qml/components/timeline/TimelineToolbarZoomSlider.qml
        src/qml/components/timeline/TimelineToolbarZoomReadout.qml
        src/qml/components/timeline/TimelineToolbarZoomControl.qml
        src/qml/components/timeline/TimelineToolbarToolButton.qml
        src/qml/components/timeline/TimelineToolbarTimecode.qml
        src/qml/components/timeline/TimelineToolbarSeparator.qml
        src/qml/components/timeline/TimelineToolbarSceneMenuItem.qml
        src/qml/components/timeline/TimelineToolbarSceneEmptyItem.qml
        src/qml/components/timeline/TimelineToolbarSceneBadge.qml
        src/qml/components/timeline/TimelineToolbarOverflowMenuItem.qml
        src/qml/components/timeline/TimelineToolbarOverflowButton.qml
        src/qml/components/timeline/TimelineToolbarHairline.qml
        src/qml/components/timeline/TimelineToolbarCustomizeRow.qml
        src/qml/components/timeline/TimelineToolbarCustomizeGrip.qml
        src/qml/components/timeline/TimelineToolbarCustomizeDivider.qml
        src/qml/components/timeline/TimelineToolbarActionSlot.qml
        src/qml/components/timeline/TimelineSpanBracketTick.qml
        src/qml/components/timeline/TimelineSpanBracketLine.qml
        src/qml/components/timeline/TimelineSpanBracketFoot.qml
        src/qml/components/timeline/TimelineSpanBracketDash.qml
        src/qml/components/timeline/TimelineSpanBracketChip.qml
        src/qml/components/timeline/TimelineSnapGuideLabel.qml
        src/qml/components/timeline/TimelineSnapGuide.qml
        src/qml/components/timeline/TimelineSeekStripFill.qml
        src/qml/components/timeline/TimelineSeekStripEdge.qml
        src/qml/components/timeline/TimelineSeekStrip.qml
        src/qml/components/timeline/TimelineRulerTick.qml
        src/qml/components/timeline/TimelineRuler.qml
        src/qml/components/timeline/TimelinePlayheadTip.qml
        src/qml/components/timeline/TimelinePlayheadLine.qml
        src/qml/components/timeline/TimelinePlayheadHead.qml
        src/qml/components/timeline/TimelinePlayhead.qml
        src/qml/components/timeline/TimelineOverviewShade.qml
        src/qml/components/timeline/TimelineOverviewPlayhead.qml
        src/qml/components/timeline/TimelineOverviewMap.qml
        src/qml/components/timeline/TimelineOverviewFrame.qml
        src/qml/components/timeline/TimelineOverviewEdge.qml
        src/qml/components/timeline/TimelineOverviewBackground.qml
        src/qml/components/timeline/TimelineNewTrackGhostLane.qml
        src/qml/components/timeline/TimelineNewTrackGhostClip.qml
        src/qml/components/timeline/TimelineNewTrackGhostBoundary.qml
        src/qml/components/timeline/TimelineNewTrackGhost.qml
        src/qml/components/timeline/TimelineMarqueeRect.qml
        src/qml/components/timeline/TimelineLiveRecordingBlock.qml
        src/qml/components/timeline/TimelineLandingOutline.qml
        src/qml/components/timeline/TimelineGapHover.qml
        src/qml/components/timeline/TimelineFadeHandle.qml
        src/qml/components/timeline/TimelineEmptyState.qml
        src/qml/components/timeline/TimelineCutOverlay.qml
        src/qml/components/timeline/TimelineCutDoomGradient.qml
        src/qml/components/timeline/TimelineCutDash.qml
        src/qml/components/timeline/TimelineClipContextMenu.qml
        src/qml/components/timeline/TimelineBookmarkMarker.qml
        src/qml/components/timeline/TimelineBookmarkLaneHighlight.qml
        src/qml/components/timeline/TimelineBookmarkContextMenu.qml
        src/qml/components/timeline/TimelineAdjustmentLaneStrip.qml
        src/qml/components/timeline/TimelineAdjustmentLaneChrome.qml
        src/qml/components/timeline/SubtitleCueWaveform.qml
        src/qml/components/timeline/SubtitleCueSpan.qml
        src/qml/components/timeline/SubtitleCuePlayhead.qml
        src/qml/components/timeline/SubtitleCueGutter.qml
        src/qml/components/timeline/SubtitleCueEdgeGrip.qml
        src/qml/components/timeline/SubtitleCueBlock.qml
        src/qml/components/timeline/NewTrackMenuEntry.qml
        src/qml/components/timeline/MixerZoneGradient.qml
        src/qml/components/timeline/MixerStripToggle.qml
        src/qml/components/timeline/MixerPanControl.qml
        src/qml/components/timeline/MixerMeterTick.qml
        src/qml/components/timeline/MixerMeterChannel.qml
        src/qml/components/timeline/MixerLevelReadout.qml
        src/qml/components/timeline/MixerLevelMeter.qml
        src/qml/components/timeline/MixerFader.qml
        src/qml/components/timeline/MixerDbScale.qml
        src/qml/components/timeline/MixerChannelStrip.qml
        src/qml/components/timeline/MixerChannelName.qml
        src/qml/components/timeline/MixerChannelDivider.qml
        src/qml/components/timeline/MixerChannelArmTint.qml
        src/qml/components/timeline/ClipFilmstripTile.qml
        src/qml/components/controls/IconGlyph.qml
        src/qml/components/controls/ThemeSlot.qml
        src/qml/components/controls/IconButton.qml
        src/qml/components/shell/PanelFrame.qml
        src/qml/components/shell/PanelSplitHandle.qml
        src/qml/components/ClipFilmstrip.qml
        src/qml/components/controls/AppScrollBar.qml
        src/qml/components/KeyframeDiamond.qml
        src/qml/components/ChannelKeyButton.qml
        src/qml/components/Vec2Param.qml
        src/qml/components/PropertyKeyframeRow.qml
        src/qml/components/EasingPicker.qml
        src/qml/components/EasingPickerPopup.qml
        src/qml/components/EasingThumb.qml
        src/qml/components/EasingTile.qml
        src/qml/components/KeyframeGraph.qml
        src/qml/components/SubtitleCueLane.qml
        src/qml/components/shell/ShortcutCaptureField.qml
        src/qml/components/ColorSwatchField.qml
        src/qml/components/controls/ThemedColorDialog.qml
        src/qml/components/ColorEyedropper.qml
        src/qml/components/EffectBrowser.qml
        src/qml/components/AudioEffectBrowser.qml
        src/qml/components/EffectTemplateBrowser.qml
        src/qml/components/controls/ThemedSlider.qml
        src/qml/components/controls/ThemedTextField.qml
        src/qml/components/controls/ThemedNumberField.qml
        src/qml/components/controls/ScrubArea.qml
        src/qml/components/controls/ThemedTextArea.qml
        src/qml/components/SubtitleEditor.qml
        src/qml/components/controls/ThemedComboBox.qml
        src/qml/components/FontPicker.qml
        src/qml/components/CollapsibleSection.qml
        src/qml/components/TextStylePackThumb.qml
        src/qml/components/TextStylePackPicker.qml
        src/qml/components/dialogs/NameDialog.qml
        src/qml/components/EmojiPicker.qml
        src/qml/components/controls/ThemedButton.qml
        src/qml/components/controls/ThemedChip.qml
        src/qml/components/controls/ThemedProgressBar.qml
        src/qml/components/MediaPill.qml
        src/qml/components/shell/VfrWarning.qml
        src/qml/components/controls/ThemedLabel.qml
        src/qml/components/controls/ThemedDialog.qml
        src/qml/components/shell/ToolWindow.qml
        src/qml/components/shell/LazyLoader.qml
        src/qml/components/android/AndroidLifecycle.qml
        src/qml/components/dialogs/AddonManagerDialog.qml
        src/qml/components/dialogs/AddonStartupDialog.qml
        src/qml/components/dialogs/MissingAddonsDialog.qml
        src/qml/components/dialogs/AboutDialog.qml
        src/qml/components/dialogs/UpdateDialog.qml
        src/qml/components/dialogs/DebugInfoDialog.qml
        src/qml/components/dialogs/ThemeTrustDialog.qml
        src/qml/components/preview/PlaybackStatsOverlay.qml
        src/qml/components/preview/transport/PreviewTransportTimecode.qml
        src/qml/components/preview/transport/PreviewTransportStepForwardButton.qml
        src/qml/components/preview/transport/PreviewTransportStepBackButton.qml
        src/qml/components/preview/transport/PreviewTransportRewindButton.qml
        src/qml/components/preview/transport/PreviewTransportPlayButton.qml
        src/qml/components/preview/transport/PreviewTransportLoopButton.qml
        src/qml/components/preview/transport/PreviewTransportFullscreenButton.qml
        src/qml/components/preview/transport/PreviewTransportFastForwardButton.qml
        src/qml/components/preview/transport/PreviewTransportGoToStartButton.qml
        src/qml/components/preview/transport/PreviewTransportGoToEndButton.qml
        src/qml/components/preview/transform/TransformOverlaySnapGuide.qml
        src/qml/components/preview/transform/TransformOverlaySizeReadout.qml
        src/qml/components/preview/transform/TransformOverlayRotateHandle.qml
        src/qml/components/preview/transform/TransformOverlayResizeGrip.qml
        src/qml/components/preview/transform/TransformOverlayParentOutline.qml
        src/qml/components/preview/transform/TransformOverlayParentChip.qml
        src/qml/components/preview/transform/TransformOverlayLayerChip.qml
        src/qml/components/preview/transform/TransformOverlayBoxBorder.qml
        src/qml/components/preview/text/InlineTextSelectionHandle.qml
        src/qml/components/preview/status/PreviewVoiceoverBadge.qml
        src/qml/components/preview/status/PreviewGapCaption.qml
        src/qml/components/preview/status/PreviewCompactGpuCaption.qml
        src/qml/components/preview/stats/PlaybackStatsLine.qml
        src/qml/components/preview/osd/PreviewOsdZoomChip.qml
        src/qml/components/preview/osd/PreviewOsdOptionsButton.qml
        src/qml/components/preview/osd/PreviewOsdGuidesButton.qml
        src/qml/components/preview/osd/PreviewOsdGuideMenuButton.qml
        src/qml/components/preview/osd/PreviewOsdCluster.qml
        src/qml/components/preview/options/PreviewOptionsRowLabel.qml
        src/qml/components/preview/options/PreviewOptionsDecodeWarning.qml
        src/qml/components/preview/options/PreviewOptionsDecodeDialog.qml
        src/qml/components/preview/multicam/MulticamTimecodeLabel.qml
        src/qml/components/preview/multicam/MulticamProgramSegment.qml
        src/qml/components/preview/multicam/MulticamProgramBadge.qml
        src/qml/components/preview/multicam/MulticamLaneTrack.qml
        src/qml/components/preview/multicam/MulticamLanePlayhead.qml
        src/qml/components/preview/multicam/MulticamAngleTile.qml
        src/qml/components/preview/multicam/MulticamAngleCaption.qml
        src/qml/components/preview/multicam/MulticamAngleBadge.qml
        src/qml/components/preview/media/MediaPreviewTrimSpan.qml
        src/qml/components/preview/media/MediaPreviewTrimShade.qml
        src/qml/components/preview/media/MediaPreviewTrimHandle.qml
        src/qml/components/preview/media/MediaPreviewSavingScrim.qml
        src/qml/components/preview/media/MediaPreviewRulerTick.qml
        src/qml/components/preview/media/MediaPreviewResolutionPill.qml
        src/qml/components/preview/media/MediaPreviewPlayheadLine.qml
        src/qml/components/preview/media/MediaPreviewPlayheadCap.qml
        src/qml/components/preview/media/MediaPreviewGradeChip.qml
        src/qml/components/preview/media/MediaPreviewCropScrim.qml
        src/qml/components/preview/media/MediaPreviewCropGrip.qml
        src/qml/components/preview/media/MediaPreviewCropFrame.qml
        src/qml/components/preview/media/MediaPreviewAudioPlaceholder.qml
        src/qml/components/preview/media/MediaPreviewView.qml
        src/qml/components/preview/mask/MaskOverlayVertexHandle.qml
        src/qml/components/preview/mask/MaskOverlayRotateHandle.qml
        src/qml/components/preview/mask/MaskOverlayResizeGrip.qml
        src/qml/components/preview/mask/MaskOverlayOutline.qml
        src/qml/components/preview/mask/MaskOverlayFeatherRing.qml
        src/qml/components/preview/mask/MaskOverlayEmptyHint.qml
        src/qml/components/preview/mask/MaskOverlayEdgeMidpoint.qml
        src/qml/components/preview/mask/MaskOverlayClipBounds.qml
        src/qml/components/preview/mask/MaskOverlayBarEdge.qml
        src/qml/components/preview/mask/MaskOverlayBarBand.qml
        src/qml/components/preview/guides/GuidesPopoverSetRow.qml
        src/qml/components/preview/guides/GuidesPopoverSectionLabel.qml
        src/qml/components/preview/guides/GuidesPopoverReadOnlyNote.qml
        src/qml/components/preview/guides/GuidesPopoverPercentField.qml
        src/qml/components/preview/guides/GuidesPopoverMarginFields.qml
        src/qml/components/preview/guides/GuidesPopoverLinePosition.qml
        src/qml/components/preview/guides/GuidesPopoverItemRow.qml
        src/qml/components/preview/guides/GuidesPopoverItemHeader.qml
        src/qml/components/preview/guides/GuidesPopoverDivider.qml
        src/qml/components/preview/guides/GuidesPopoverColorRow.qml
        src/qml/components/preview/guides/GuidesPopoverAspectFields.qml
        src/qml/components/preview/guides/GuideEditReadout.qml
        src/qml/components/preview/guides/GuideEditHighlight.qml
        src/qml/components/preview/guides/GuideEditBar.qml
        src/qml/components/preview/guides/GuideEditAddStrip.qml
        src/qml/components/preview/gizmo/TransformGizmoStroke.qml
        src/qml/components/preview/gizmo/TransformGizmoPivot.qml
        src/qml/components/preview/gizmo/TransformGizmoHead.qml
        src/qml/components/preview/gizmo/TransformGizmoHandle.qml
        src/qml/components/preview/depth/DepthLight3dKnob.qml
        src/qml/components/preview/depth/DepthLight3dAimRing.qml
        src/qml/components/preview/depth/DepthEffectLightKnob.qml
        src/qml/components/preview/depth/DepthEffectFocusReticle.qml
        src/qml/components/preview/depth/DepthEffectAimRing.qml
        src/qml/components/preview/depth/DepthEffectAimLine.qml
        src/qml/components/preview/crop/CropOverlayThirdGuides.qml
        src/qml/components/preview/crop/CropOverlaySizeBadge.qml
        src/qml/components/preview/crop/CropOverlayScrim.qml
        src/qml/components/preview/crop/CropOverlayHint.qml
        src/qml/components/preview/crop/CropOverlayGrip.qml
        src/qml/components/preview/crop/CropOverlayFrame.qml
        src/qml/components/preview/axis/ViewAxisShaft.qml
        src/qml/components/preview/axis/ViewAxisKnob.qml
        src/qml/components/preview/axis/ViewAxisDisc.qml
        src/qml/components/preview/PreviewFullscreenTransportScrim.qml
        src/qml/components/preview/PreviewDropClipFrame.qml
        src/qml/components/preview/PreviewDropCanvasFrame.qml
        src/qml/components/preview/Preview3DToolsDivider.qml
        src/qml/components/preview/GuideLayerMark.qml
        src/qml/components/dialogs/ProjectSetupDialog.qml
        src/qml/components/dialogs/LayoutChooserDialog.qml
        src/qml/components/dialogs/LanguageChooserDialog.qml
        src/qml/components/shell/AgentAccessControls.qml
        src/qml/components/dialogs/AgentAccessDialog.qml
        src/qml/components/dialogs/VideoSizeDialog.qml
        src/qml/components/shell/VideoSizeControls.qml
        src/qml/components/dialogs/ProjectPropertiesDialog.qml
        src/qml/components/shell/SettingsPane.qml
        src/qml/components/android/SheetActionRow.qml
        src/qml/components/android/SheetActionTile.qml
        src/qml/components/android/AndroidTransformSnapLine.qml
        src/qml/components/android/AndroidTransformSizeReadout.qml
        src/qml/components/android/AndroidTransformRotateHandle.qml
        src/qml/components/android/AndroidTransformResizeGrip.qml
        src/qml/components/android/AndroidTransformParentFrame.qml
        src/qml/components/android/AndroidTransformLayerChip.qml
        src/qml/components/android/AndroidTransformClipOutline.qml
        src/qml/components/android/AndroidTopBarTitleButton.qml
        src/qml/components/android/AndroidTopBarExportButton.qml
        src/qml/components/android/AndroidTopBarCountBadge.qml
        src/qml/components/android/AndroidTimelineWorkArea.qml
        src/qml/components/android/AndroidTimelineTransitionMark.qml
        src/qml/components/android/AndroidTimelineTrackWash.qml
        src/qml/components/android/AndroidTimelineSnapGuide.qml
        src/qml/components/android/AndroidTimelineRulerTick.qml
        src/qml/components/android/AndroidTimelineRuler.qml
        src/qml/components/android/AndroidTimelinePlayhead.qml
        src/qml/components/android/AndroidTimelineNewTrackGhost.qml
        src/qml/components/android/AndroidTimelineLiveRecording.qml
        src/qml/components/android/AndroidTimelineLaneBar.qml
        src/qml/components/android/AndroidTimelineGapPress.qml
        src/qml/components/android/AndroidTimelineDropGhost.qml
        src/qml/components/android/AndroidTimelineBookmarkFlag.qml
        src/qml/components/android/AndroidTimelineAdjustmentStrip.qml
        src/qml/components/android/AndroidTextEditBar.qml
        src/qml/components/android/AndroidSheetScrim.qml
        src/qml/components/android/AndroidSheetHeader.qml
        src/qml/components/android/AndroidSheetHandle.qml
        src/qml/components/android/AndroidSheetDragArea.qml
        src/qml/components/android/AndroidSheetActionTileFace.qml
        src/qml/components/android/AndroidSheetActionSwitch.qml
        src/qml/components/android/AndroidSheetActionMark.qml
        src/qml/components/android/AndroidSheetActionIconTile.qml
        src/qml/components/android/AndroidRailButton.qml
        src/qml/components/android/AndroidRailAddButton.qml
        src/qml/components/android/AndroidPreviewTransport.qml
        src/qml/components/android/AndroidPreviewOptionsStrip.qml
        src/qml/components/android/AndroidPreviewLetterbox.qml
        src/qml/components/android/AndroidOnMediaReadout.qml
        src/qml/components/android/AndroidMoreToolsHeading.qml
        src/qml/components/android/AndroidMediaTrimHandle.qml
        src/qml/components/android/AndroidMediaSavingScrim.qml
        src/qml/components/android/AndroidMediaPreviewHeader.qml
        src/qml/components/android/AndroidMediaPlayBadge.qml
        src/qml/components/android/AndroidMediaCropShade.qml
        src/qml/components/android/AndroidMediaCropOverlay.qml
        src/qml/components/android/AndroidMediaCropGrip.qml
        src/qml/components/android/AndroidLinkBusyStatus.qml
        src/qml/components/android/AndroidLayoutTemplateCard.qml
        src/qml/components/android/AndroidLayoutSummarySwatch.qml
        src/qml/components/android/AndroidLayoutSectionLabel.qml
        src/qml/components/android/AndroidHomeWordmark.qml
        src/qml/components/android/AndroidHomeStartTile.qml
        src/qml/components/android/AndroidHomeRecentCard.qml
        src/qml/components/android/AndroidFadeSliderRow.qml
        src/qml/components/android/AndroidDownloadJobRow.qml
        src/qml/components/android/AndroidCropHint.qml
        src/qml/components/android/AndroidCropGrip.qml
        src/qml/components/android/AndroidCropFrame.qml
        src/qml/components/android/AndroidCropDimMask.qml
        src/qml/components/android/AndroidComposerTimingEdge.qml
        src/qml/components/android/AndroidClipToolbarSlot.qml
        src/qml/components/android/AndroidClipToolbarHint.qml
        src/qml/components/android/AndroidAttentionDot.qml
        src/qml/components/dialogs/SettingsDialog.qml
        src/qml/components/dialogs/ShellSwitchDialog.qml
        src/qml/components/dialogs/PasteAttributesDialog.qml
        src/qml/components/dialogs/PackageProgressDialog.qml
        src/qml/components/dialogs/ProjectLoadDialog.qml
        src/qml/components/dialogs/ExportDialog.qml
        src/qml/components/dialogs/ExportProgressDialog.qml
        src/qml/components/dialogs/CollectMediaDialog.qml
        src/qml/components/dialogs/ImageSequenceDialog.qml
        src/qml/components/dialogs/ReverseProgressDialog.qml
        src/qml/components/dialogs/SubtitleProgressDialog.qml
        src/qml/components/shell/CircularProgress.qml
        src/qml/components/dialogs/RecoveryDialog.qml
        src/qml/components/dialogs/UnsavedChangesDialog.qml
        src/qml/components/dialogs/PasteAttributesVideoSection.qml
        src/qml/components/dialogs/PasteAttributesTransitionsSection.qml
        src/qml/components/dialogs/PasteAttributesAudioSection.qml
        src/qml/components/dialogs/MissingAddonsRow.qml
        src/qml/components/dialogs/LayoutChooserTemplateRow.qml
        src/qml/components/dialogs/LayoutChooserCategoryButton.qml
        src/qml/components/dialogs/ExportDialogWorkAreaCheck.qml
        src/qml/components/dialogs/ExportDialogVideoSection.qml
        src/qml/components/dialogs/ExportDialogModeBar.qml
        src/qml/components/dialogs/ExportDialogGifSection.qml
        src/qml/components/dialogs/ExportDialogAudioSection.qml
        src/qml/components/dialogs/DebugInfoSystemTab.qml
        src/qml/components/dialogs/DebugInfoPlaybackTab.qml
        src/qml/components/dialogs/DebugInfoBenchRow.qml
        src/qml/components/dialogs/AddonStartupPackRow.qml
        src/qml/components/dialogs/AddonManagerRow.qml
        src/qml/components/UpscaleModelCard.qml
        src/qml/components/shell/RecentProjectsPopup.qml
        src/qml/components/shell/StartScreen.qml
        src/qml/components/NewTrackMenu.qml
        src/qml/components/shell/GlobalSearchPopup.qml
        src/qml/components/shell/GlobalSearchControls.qml
        src/qml/components/shell/ToastHost.qml
        src/qml/components/shell/NotificationList.qml
        src/qml/components/shell/NotificationsPopup.qml
        src/qml/components/shell/EmptyState.qml
        src/qml/components/shell/TouchLiftArea.qml
        src/qml/components/shell/TouchDragGhost.qml
        src/qml/components/shell/SkeletonBox.qml
        src/qml/components/controls/ThemedSwitch.qml
        src/qml/components/controls/ThemedCheckBox.qml
        src/qml/components/controls/ThemedToggleButton.qml
        src/qml/components/controls/ThemedToolTip.qml
        src/qml/components/controls/ThemedContextMenu.qml
        src/qml/components/controls/ThemedMenuItem.qml
        src/qml/components/controls/ThemedMenuSeparator.qml
        src/qml/components/controls/ThemedSliderHandle.qml
        src/qml/components/controls/ThemedComboBoxDelegate.qml
        src/qml/components/controls/ThemedColorDialogThumb.qml
        src/qml/components/shell/LabelledProgressRing.qml
        src/qml/components/shell/StartScreenTitle.qml
        src/qml/components/shell/StartScreenRecents.qml
        src/qml/components/shell/StartScreenProjectRow.qml
        src/qml/components/shell/StartScreenActions.qml
        src/qml/components/shell/SettingsAboutPage.qml
        src/qml/components/shell/SettingsAiPage.qml
        src/qml/components/shell/SettingsAppearanceActions.qml
        src/qml/components/shell/SettingsAppearancePage.qml
        src/qml/components/shell/SettingsAppearanceTranslucency.qml
        src/qml/components/shell/SettingsAudioPage.qml
        src/qml/components/shell/SettingsCloudProvidersSection.qml
        src/qml/components/shell/SettingsGeneralPage.qml
        src/qml/components/shell/SettingsGroupHeader.qml
        src/qml/components/shell/SettingsKeymapPage.qml
        src/qml/components/shell/SettingsMarketplacePage.qml
        src/qml/components/shell/SettingsNetworkPage.qml
        src/qml/components/shell/SettingsPage.qml
        src/qml/components/shell/SettingsPerformancePage.qml
        src/qml/components/shell/SettingsPreviewPage.qml
        src/qml/components/shell/SettingsRow.qml
        src/qml/components/shell/SettingsThemeEntry.qml
        src/qml/components/shell/SettingsThemeList.qml
        src/qml/components/shell/SettingsTimelinePage.qml
        src/qml/components/shell/SettingsToolbarPage.qml
        src/qml/components/shell/SegmentationFrameColumn.qml
        src/qml/components/shell/SegmentationControlColumn.qml
        src/qml/components/shell/RestoreUpscalerPicker.qml
        src/qml/components/shell/RestoreControlColumn.qml
        src/qml/components/shell/RestoreCompareView.qml
        src/qml/components/shell/RecentProjectsProjectRow.qml
        src/qml/components/shell/RecentProjectsActionRow.qml
        src/qml/components/shell/NotificationUpdateRow.qml
        src/qml/components/shell/NotificationHistoryRow.qml
        src/qml/components/shell/MainOverlayInputGuard.qml
        src/qml/components/shell/MainMacMenuBar.qml
        src/qml/components/shell/MainAndroidMediaPreviewOverlay.qml
        src/qml/components/shell/GlobalSearchResultRow.qml
        src/qml/components/shell/EditorWorkspace.qml
        src/qml/components/shell/workspaces/PanelPool.qml
        src/qml/components/shell/workspaces/WorkspaceCell.qml
        src/qml/components/shell/workspaces/WorkspaceHost.qml
        src/qml/components/shell/workspaces/WorkspacePlaceholderPanel.qml
        src/qml/components/shell/workspaces/WorkspaceSlot.qml
        src/qml/components/shell/workspaces/WorkspaceSplit.qml
        src/qml/components/shell/workspaces/WorkspaceWindow.qml
        src/qml/components/shell/workspaces/designer/DesignerDragArea.qml
        src/qml/components/shell/workspaces/designer/DesignerNode.qml
        src/qml/components/shell/workspaces/designer/DesignerSlot.qml
        src/qml/components/shell/workspaces/designer/DesignerTree.js
        src/qml/components/shell/workspaces/designer/DesignerWorkspaceList.qml
        src/qml/components/shell/workspaces/designer/WorkspaceDesigner.qml
        src/qml/components/shell/SettingsWorkspacesPage.qml
        src/qml/components/shell/EditorHeaderSettingsButton.qml
        src/qml/components/shell/EditorHeaderWorkspaceButton.qml
        src/qml/components/shell/AppSettingsMenu.qml
        src/qml/components/shell/EditorHeaderSeparator.qml
        src/qml/components/shell/EditorHeaderSearchField.qml
        src/qml/components/shell/EditorHeaderProjectsButton.qml
        src/qml/components/shell/EditorHeaderNotificationsButton.qml
        src/qml/components/shell/EditorHeaderHairline.qml
        src/qml/components/shell/EditorHeaderExtrasWave.qml
        src/qml/components/shell/EditorHeaderExtrasButton.qml
        src/qml/components/shell/EditorHeaderExportButton.qml
        src/qml/components/shell/EditorHeaderExportBadge.qml
        src/qml/components/shell/EditorHeaderDownloadsButton.qml
        src/qml/components/shell/DenoiseWaveformLane.qml
        src/qml/components/denoise/DenoiseView.qml
    RESOURCES
        # UI chrome: interface font and Lucide SVG icons (always needed, including before addons).
        # Static instances rather than the variable Inter: on Windows Qt registers application
        # fonts through GDI before converting them to a DirectWrite face, and GDI cannot express a
        # variable instance, so the face Qt rasterised with stopped matching the glyph indices it
        # had looked up (issues #115, #162). Regenerate with scripts/gen-ui-font.py.
        resources/fonts/InterUI-Regular.ttf
        resources/fonts/InterUI-Medium.ttf
        resources/fonts/InterUI-SemiBold.ttf
        resources/fonts/InterUI-Bold.ttf
        resources/fonts/InterUI-Italic.ttf
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
target_include_directories(drift PRIVATE src)
# qmltyperegistrar emits bare-basename includes (<ThemeManager.h>) for the qml module's headers.
target_include_directories(drift PRIVATE src/ui/theme src/ui/theme/backdrop)
if(DRIFT_BACKDROP_PLATFORM_LIBS)
    target_link_libraries(drift PRIVATE ${DRIFT_BACKDROP_PLATFORM_LIBS})
endif()
if(DRIFT_BACKDROP_PLATFORM_DEFS)
    target_compile_definitions(drift PRIVATE ${DRIFT_BACKDROP_PLATFORM_DEFS})
endif()

if(ANDROID)
    # FileDialogs registers a QtAndroidPrivate::NewIntentListener so a second ACTION_VIEW reaches a
    # running instance (singleTop delivers it to onNewIntent, which the public API does not expose).
    # CorePrivate is not pulled in by the plain Qt6 Core package, hence the extra find_package.
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    target_link_libraries(drift PRIVATE Qt6::CorePrivate)

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
    # Android bump too (0.6.0 → 600). CI may override with -DDRIFT_ANDROID_VERSION_CODE=.
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

# applicationDirPath() has nothing to stage into on Android (the APK's lib directory) or wasm (a
# virtual filesystem with no files beside the binary), so the file-based package roots the desktop
# build stages next to the binary ride inside it as Qt resources instead.
if(ANDROID OR EMSCRIPTEN)
    # PackageSearchPaths::defaultSearchPaths adds ":/packages/<subdir>" as the bundled root.
    file(GLOB_RECURSE DRIFT_PACKAGE_FILES RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
         "effects/*/effect.json"
         "effects/*/*.frag"
         "effects/*/*.png"
         "effects/*/*.webp"
         "effects/*/*.bin"
         "effects/*/NOTICE"
         "effects/*/LICENSE*"
         "transitions/*/*.json"
         "transitions/*/*.frag"
         "transitions/*/*.png"
         "transitions/*/*.webp"
         "audio-effects/*/audio-effect.json"
         "audio-effects/*/thumbnail.png"
         "audio-effects/*/thumbnail.webp"
         "effect-templates/*/template.json"
         "effect-templates/*/thumbnail.png")
    list(REMOVE_DUPLICATES DRIFT_PACKAGE_FILES)
    if(DRIFT_BUNDLE_CORE_PACKAGES_ONLY)
        set(_drift_keep "")
        foreach(_kind effects transitions audio-effects effect-templates)
            string(TOUPPER "${_kind}" _upper)
            string(REPLACE "-" "_" _upper "${_upper}")
            foreach(_id ${DRIFT_CORE_${_upper}})
                list(APPEND _drift_keep "${_kind}/${_id}/")
            endforeach()
        endforeach()
        set(_drift_filtered "")
        foreach(_file ${DRIFT_PACKAGE_FILES})
            foreach(_prefix ${_drift_keep})
                string(FIND "${_file}" "${_prefix}" _pos)
                if(_pos EQUAL 0)
                    list(APPEND _drift_filtered "${_file}")
                    break()
                endif()
            endforeach()
        endforeach()
        set(DRIFT_PACKAGE_FILES ${_drift_filtered})
    endif()
    qt_add_resources(drift "drift_packages"
        PREFIX "/packages"
        BASE "${CMAKE_SOURCE_DIR}"
        FILES ${DRIFT_PACKAGE_FILES}
    )
endif()

# MCP tool names, JSON schemas, agent errors and copied configs are a machine protocol and must
# stay English. lupdate also ignores QStringLiteral, so those files only leak if someone later adds tr().
file(GLOB DRIFT_MCP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/app/mcp/protocol/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/mcp/protocol/*.h"
    "${CMAKE_SOURCE_DIR}/src/app/mcp/transport/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/mcp/transport/*.h"
    "${CMAKE_SOURCE_DIR}/src/app/mcp/tools/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/mcp/tools/*.h")
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
            src/engine/packages/effects/EffectCatalog.cpp
            src/engine/packages/effects/EffectTemplateCatalog.cpp
            src/engine/packages/audio/AudioEffectCatalog.cpp
            src/engine/packages/transitions/TransitionCatalog.cpp
            src/engine/packages/assets/FontCatalog.cpp
            src/engine/encode/proxies/ReverseRenderer.cpp
            src/engine/encode/proxies/ProxyEncoder.cpp
            src/engine/encode/proxies/PreviewProxyRenderer.cpp
            src/engine/encode/MediaEditor.cpp
            src/engine/project/ProjectBundle.cpp
            src/engine/face/props/FacePropImport.cpp
            src/core/project/Project.cpp
            src/core/project/Transcript.cpp
            src/core/project/GuideSet.cpp
            src/core/interchange/subtitles/SrtIO.cpp
            src/core/interchange/subtitles/VttIO.cpp
            src/core/shapes/ShapeStyle.cpp
            src/core/text/TextStyle.cpp
            src/core/project/SubtitleCue.cpp
        # -locations none: source line numbers in .ts files churn on every edit
        # and make i18n/*.ts conflict across PRs. Translators still see context
        # and source text; they lose jump-to-source in Qt Linguist.
        LUPDATE_OPTIONS -no-obsolete -locations none
    )
    if(NOT TARGET update_translations AND TARGET drift_lupdate)
        add_custom_target(update_translations DEPENDS drift_lupdate)
    endif()

    # An lupdate built before qtdeclarative has no QML parser: it warns "missing qml/javascript
    # support", skips every .qml, and -no-obsolete then strips every QML string out of i18n/*.ts.
    # org.kde.Sdk ships exactly that lupdate, and flatpak-builder does not pass CI into its
    # sandbox, so the Flatpak regenerated its catalogs down to the C++ strings on every build and
    # shipped a UI that was English apart from the toasts (#167). Probe once and stay off where
    # lupdate cannot see QML, rather than letting it gut the committed catalogs.
    if(NOT DEFINED DRIFT_LUPDATE_HAS_QML AND TARGET Qt6::lupdate)
        get_target_property(_drift_lupdate_exe Qt6::lupdate IMPORTED_LOCATION)
        if(NOT _drift_lupdate_exe)
            get_target_property(_drift_lupdate_cfgs Qt6::lupdate IMPORTED_CONFIGURATIONS)
            foreach(_drift_cfg IN LISTS _drift_lupdate_cfgs)
                get_target_property(_drift_lupdate_exe Qt6::lupdate IMPORTED_LOCATION_${_drift_cfg})
                if(_drift_lupdate_exe)
                    break()
                endif()
            endforeach()
        endif()
        set(_drift_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/lupdate-qml-probe")
        file(REMOVE_RECURSE "${_drift_probe_dir}")
        file(WRITE "${_drift_probe_dir}/probe.qml"
            "import QtQuick\nItem { property string s: qsTr(\"drift_lupdate_qml_probe\") }\n")
        if(_drift_lupdate_exe)
            execute_process(
                COMMAND "${_drift_lupdate_exe}" probe.qml -ts probe.ts
                WORKING_DIRECTORY "${_drift_probe_dir}"
                OUTPUT_QUIET ERROR_QUIET)
        endif()
        set(_drift_probe_ts "")
        if(EXISTS "${_drift_probe_dir}/probe.ts")
            file(READ "${_drift_probe_dir}/probe.ts" _drift_probe_ts)
        endif()
        string(FIND "${_drift_probe_ts}" "drift_lupdate_qml_probe" _drift_probe_hit)
        if(_drift_probe_hit GREATER -1)
            set(DRIFT_LUPDATE_HAS_QML ON CACHE INTERNAL "lupdate can parse QML")
        else()
            set(DRIFT_LUPDATE_HAS_QML OFF CACHE INTERNAL "lupdate can parse QML")
        endif()
    endif()

    if(DRIFT_AUTO_UPDATE_TRANSLATIONS AND NOT DRIFT_LUPDATE_HAS_QML)
        message(WARNING
            "lupdate has no QML/JavaScript support; running it would delete every QML string from "
            "i18n/*.ts and ship English-only .qm files. Not running lupdate as part of this build. "
            "Install a Qt Linguist build with QML support to refresh the catalogs.")
        set(DRIFT_AUTO_UPDATE_TRANSLATIONS OFF)
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

if(EMSCRIPTEN)
    # Threads come from Qt's multithread toolchain (-pthread). The pool is pre-spawned because a
    # worker can only be created from the main thread's event loop, which blocks on thread joins.
    # FULL_ES3 emulates glMapBufferRange, which GlRuntime's PBO readback needs.
    set_target_properties(drift PROPERTIES QT_WASM_INITIAL_MEMORY 128MB)
    target_link_options(drift PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMAXIMUM_MEMORY=4GB
        -sPTHREAD_POOL_SIZE=8
        -sFULL_ES3=1)
endif()

# Defined here rather than in the header so there is exactly one place to change them.
target_compile_definitions(drift PRIVATE
    DRIFT_ADDON_INDEX_URL="${DRIFT_ADDON_INDEX_URL}"
    DRIFT_ADDON_CLIENT_TOKEN="${DRIFT_ADDON_CLIENT_TOKEN}"
    DRIFT_MARKET_API_URL="${DRIFT_MARKET_API_URL}"
    DRIFT_MARKET_CLIENT_KEY="${DRIFT_MARKET_CLIENT_KEY}"
    DRIFT_UPDATE_FEED_URL="${DRIFT_UPDATE_FEED_URL}"
    DRIFT_VERSION="${DRIFT_VERSION_STRING}"
    DRIFT_DISTRIBUTION="${DRIFT_DISTRIBUTION}"
)

# Ship a file-based package directory next to the binaries that load the catalog.
#
# Several targets share one output directory (everything under tools/, the two engine tests), so
# the copy is registered once per destination and depended on, rather than as a POST_BUILD step per
# target: two `cmake -E copy_directory` processes writing the same tree concurrently — which is
# what `cmake --build --parallel` does — fail with "Error copying directory".
function(drift_stage_packages target assetdir)
    # ONLY <ids...> stages just those package directories (possibly none) instead of the whole tree.
    set(_ids ${ARGN})
    list(FIND _ids ONLY _only)
    list(REMOVE_ITEM _ids ONLY)

    # Only drift is a bundle, so the shared output directory guarded against below cannot happen.
    get_target_property(_bundle ${target} MACOSX_BUNDLE)
    if(_bundle)
        set(_dest "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/${assetdir}")
        if(_only EQUAL -1)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${CMAKE_SOURCE_DIR}/${assetdir}" "${_dest}"
                COMMENT "Copy ${assetdir} packages into ${target}.app/Contents/Resources")
        else()
            foreach(_id ${_ids})
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/${assetdir}/${_id}" "${_dest}/${_id}"
                    COMMENT "Copy ${assetdir}/${_id} into ${target}.app/Contents/Resources")
            endforeach()
        endif()
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
        if(_only EQUAL -1)
            add_custom_target(${_stage}
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${CMAKE_SOURCE_DIR}/${assetdir}"
                        "${_outdir}/${assetdir}"
                COMMENT "Copy ${assetdir} packages into ${_outdir}"
            )
        else()
            add_custom_target(${_stage} COMMENT "Copy core ${assetdir} packages into ${_outdir}")
            foreach(_id ${_ids})
                add_custom_command(TARGET ${_stage} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/${assetdir}/${_id}"
                            "${_outdir}/${assetdir}/${_id}")
            endforeach()
        endif()
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

# Android and wasm get these through qt_add_resources above; there is no <appDir> to stage into.
if(NOT ANDROID AND NOT EMSCRIPTEN)
    if(DRIFT_BUNDLE_CORE_PACKAGES_ONLY)
        drift_stage_packages(drift effects ONLY ${DRIFT_CORE_EFFECTS})
        drift_stage_packages(drift effect-templates ONLY ${DRIFT_CORE_EFFECT_TEMPLATES})
        drift_stage_packages(drift transitions ONLY ${DRIFT_CORE_TRANSITIONS})
        drift_stage_packages(drift audio-effects ONLY ${DRIFT_CORE_AUDIO_EFFECTS})
    else()
        drift_copy_effects(drift)
        drift_copy_effect_templates(drift)
        drift_copy_transitions(drift)
        drift_copy_audio_effects(drift)
    endif()
    drift_bundle_onnxruntime(drift)
endif()

# Desktop packaging, the headless CLI tools and the QTest binaries. None of it applies to an
# APK or a browser tab, so the Android and wasm configures skip it wholesale.
if(NOT ANDROID AND NOT EMSCRIPTEN)
# --- install (used by packaging, e.g. Flatpak; the dev workflow runs in-place) ---
include(GNUInstallDirs)

# Install one freedesktop metadata file under the channel's app id. On stable that is the source
# file byte for byte. A nightly gets a generated copy with every org.cutwire.Drift occurrence
# rewritten — the AppStream <id>, its <launchable>, the mime type's <icon> and the desktop file's
# Icon= all carry that string — and the human-facing name suffixed, so the launcher shows
# "Drift Nightly" next to a stable entry rather than two identical ones.
function(drift_install_metadata source destination name)
    if(DRIFT_CHANNEL STREQUAL "stable")
        install(FILES "${source}" DESTINATION "${destination}" RENAME "${name}" COMPONENT drift)
        return()
    endif()
    file(READ "${CMAKE_SOURCE_DIR}/${source}" _content)
    string(REPLACE "org.cutwire.Drift" "${DRIFT_APP_ID}" _content "${_content}")
    string(REPLACE "Name=Drift\n" "Name=${DRIFT_APP_NAME}\n" _content "${_content}")
    string(REPLACE "<name>Drift</name>" "<name>${DRIFT_APP_NAME}</name>" _content "${_content}")
    set(_generated "${CMAKE_CURRENT_BINARY_DIR}/metadata/${name}")
    file(WRITE "${_generated}" "${_content}")
    install(FILES "${_generated}" DESTINATION "${destination}" COMPONENT drift)
endfunction()

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
    # the app searches <appDir>/<subdir> (PackageSearchPaths::defaultSearchPaths). Installing
    # them here (rather than via the DRIFT_*_DIR env vars) keeps installed addons able to
    # supersede the bundled copies.
    foreach(_assetdir effects transitions effect-templates audio-effects)
        if(DRIFT_BUNDLE_CORE_PACKAGES_ONLY)
            string(TOUPPER "${_assetdir}" _upper)
            string(REPLACE "-" "_" _upper "${_upper}")
            foreach(_id ${DRIFT_CORE_${_upper}})
                install(DIRECTORY ${CMAKE_SOURCE_DIR}/${_assetdir}/${_id}
                        DESTINATION ${CMAKE_INSTALL_BINDIR}/${_assetdir} COMPONENT drift)
            endforeach()
        else()
            install(DIRECTORY ${CMAKE_SOURCE_DIR}/${_assetdir}
                    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT drift)
        endif()
    endforeach()

    # Desktop integration for Flatpak / freedesktop. Every one of these is installed under the
    # channel's app id: flatpak build-export silently drops anything in /app/share whose name does
    # not start with the app id, so a nightly named after the stable id would export no launcher
    # entry and no icon at all.
    drift_install_metadata(flatpak/org.cutwire.Drift.desktop
        "${CMAKE_INSTALL_DATADIR}/applications" "${DRIFT_APP_ID}.desktop")
    drift_install_metadata(flatpak/org.cutwire.Drift.metainfo.xml
        "${CMAKE_INSTALL_DATADIR}/metainfo" "${DRIFT_APP_ID}.metainfo.xml")
    drift_install_metadata(flatpak/org.cutwire.Drift.xml
        "${CMAKE_INSTALL_DATADIR}/mime/packages" "${DRIFT_APP_ID}.xml")
    # 1024px source is intentionally skipped: flatpak build-export rejects icons
    # larger than 512x512.
    foreach(_size 64 128 256 512)
        install(FILES
            flatpak/share/icons/hicolor/${_size}x${_size}/apps/org.cutwire.Drift.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
            RENAME ${DRIFT_APP_ID}.png COMPONENT drift)
    endforeach()
endif()

# --- CLI tools (headless engine testing, no GUI) ----------------------------
add_subdirectory(tools)

# --- tests -------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
endif() # NOT ANDROID AND NOT EMSCRIPTEN
