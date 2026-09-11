# Download a prebuilt ONNX Runtime release when it is not installed locally.
# On success sets ONNXRUNTIME_ROOT (and DRIFT_ONNXRUNTIME_FETCHED).
#
# Only the CPU release is ever fetched, and only two things are taken from it: the headers Drift
# compiles against, and — when DRIFT_BUNDLE_ONNXRUNTIME is on — a runtime for the build tree to
# load so a dev build works before any addon is installed. The library is never linked; which
# runtime the shipped app uses is the user's runtime choice, not a configure-time one
# (src/engine/OrtRuntime.cpp).

if(DRIFT_ONNXRUNTIME_FETCHED)
    return()
endif()

set(_onnxruntime_version "1.27.0")

if(ANDROID)
    # Only the headers are ever used here — the app links no ONNX Runtime at all and dlopens one at
    # startup (src/engine/OrtRuntime.cpp), which on Android arrives as an Acceleration addon. The
    # C and C++ API headers are identical across platforms and architectures, so the linux-x64
    # tarball is a valid source for them; nothing from its lib/ directory is linked or staged
    # (drift_bundle_onnxruntime is skipped on Android, and scripts/build.sh passes
    # DRIFT_BUNDLE_ONNXRUNTIME=OFF). Using the official android AAR instead would mean unzipping a
    # second archive format to obtain byte-identical headers.
    set(_onnxruntime_archive "onnxruntime-linux-x64-${_onnxruntime_version}.tgz")
    set(_onnxruntime_sha256 "547e40a48f1fe73e3f812d7c88a948612c23f896b91e4e2ee1e232d7b468246f")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
        set(_onnxruntime_archive "onnxruntime-linux-aarch64-${_onnxruntime_version}.tgz")
        set(_onnxruntime_sha256 "3e4d83ac06924a32a07b6d7f91ce6f852876153fc0bbdf931bf517a140bfbe48")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(_onnxruntime_archive "onnxruntime-linux-x64-${_onnxruntime_version}.tgz")
        set(_onnxruntime_sha256 "547e40a48f1fe73e3f812d7c88a948612c23f896b91e4e2ee1e232d7b468246f")
    endif()
elseif(APPLE)
    # Upstream stopped publishing an osx-x86_64 (and universal2) release; 1.27.0 ships arm64 only.
    # An Intel Mac used to fall through to the FATAL_ERROR below and could not configure at all.
    # The headers are identical across platforms and architectures — the same reason the Android
    # branch above reads them out of the linux-x64 tarball — so the arm64 archive is a valid
    # source for them here too. Its dylib is not: bundling is forced off rather than stage an
    # arm64 runtime an x86_64 process would fail to dlopen with no explanation.
    set(_onnxruntime_archive "onnxruntime-osx-arm64-${_onnxruntime_version}.tgz")
    set(_onnxruntime_sha256 "545e81c58152353acb0d1e8bd6ce4b62f830c0961f5b3acfedc790ffd76e477a")
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        if(DRIFT_BUNDLE_ONNXRUNTIME)
            message(STATUS
                "ONNX Runtime publishes no x86_64 macOS build; taking headers from the arm64 "
                "release and disabling DRIFT_BUNDLE_ONNXRUNTIME. Install an Acceleration addon, "
                "or point DRIFT_ONNXRUNTIME_DIR at an x86_64 runtime.")
        endif()
        set(DRIFT_BUNDLE_ONNXRUNTIME OFF CACHE BOOL
            "Stage a CPU ONNX Runtime into the build tree for development" FORCE)
    endif()
elseif(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_onnxruntime_archive "onnxruntime-win-arm64-${_onnxruntime_version}.zip")
        set(_onnxruntime_sha256 "a32f2650575b3c20df462e337519fd1cc4105356130d11dba9771c6f374d952f")
    else()
        set(_onnxruntime_archive "onnxruntime-win-x64-${_onnxruntime_version}.zip")
        set(_onnxruntime_sha256 "c5c81710938e68079ff1a192b04897faabe4b43830d48f39f27ecd4e16138bfc")
    endif()
endif()

if(NOT _onnxruntime_archive)
    message(FATAL_ERROR
        "Automatic ONNX Runtime download is not supported for "
        "${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}. "
        "Install ONNX Runtime manually and set ONNXRUNTIME_ROOT.")
endif()

set(_onnxruntime_url
    "https://github.com/microsoft/onnxruntime/releases/download/v${_onnxruntime_version}/${_onnxruntime_archive}")

set(_onnxruntime_staging "${CMAKE_BINARY_DIR}/_deps/onnxruntime/cpu")

file(GLOB _onnxruntime_prefixes LIST_DIRECTORIES true
    "${_onnxruntime_staging}/onnxruntime-*")
if(_onnxruntime_prefixes)
    list(GET _onnxruntime_prefixes 0 _onnxruntime_prefix)
else()
    set(_onnxruntime_prefix "${_onnxruntime_staging}/${_onnxruntime_archive}")
    get_filename_component(_onnxruntime_prefix "${_onnxruntime_prefix}" NAME_WE)
endif()

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    file(MAKE_DIRECTORY "${_onnxruntime_staging}")
    set(_onnxruntime_archive_path "${_onnxruntime_staging}/${_onnxruntime_archive}")

    if(NOT EXISTS "${_onnxruntime_archive_path}")
        message(STATUS "Downloading ONNX Runtime ${_onnxruntime_version}: ${_onnxruntime_url}")
        file(DOWNLOAD "${_onnxruntime_url}" "${_onnxruntime_archive_path}"
            EXPECTED_HASH SHA256=${_onnxruntime_sha256}
            SHOW_PROGRESS)
    endif()

    file(ARCHIVE_EXTRACT
        INPUT "${_onnxruntime_archive_path}"
        DESTINATION "${_onnxruntime_staging}")
endif()

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    file(GLOB _onnxruntime_prefixes LIST_DIRECTORIES true
        "${_onnxruntime_staging}/onnxruntime-*")
    if(_onnxruntime_prefixes)
        list(GET _onnxruntime_prefixes 0 _onnxruntime_prefix)
    endif()
endif()

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "Failed to extract ONNX Runtime from ${_onnxruntime_archive}")
endif()

# find_path caches its result, so a build tree configured against a previous prefix would keep
# resolving to it.
unset(OnnxRuntime_INCLUDE_DIR CACHE)

set(ONNXRUNTIME_ROOT "${_onnxruntime_prefix}" CACHE PATH
    "ONNX Runtime install prefix (auto-downloaded)" FORCE)
set(OnnxRuntime_ROOT "${_onnxruntime_prefix}" CACHE PATH
    "ONNX Runtime install prefix (auto-downloaded)" FORCE)
set(DRIFT_ONNXRUNTIME_VERSION "${_onnxruntime_version}" CACHE STRING
    "Version of the fetched ONNX Runtime" FORCE)
set(DRIFT_ONNXRUNTIME_FETCHED TRUE)
message(STATUS "ONNX Runtime ${_onnxruntime_version} headers ready at ${ONNXRUNTIME_ROOT}")
