# Download a prebuilt ONNX Runtime release when it is not installed locally.
# On success sets ONNXRUNTIME_ROOT (and DRIFT_ONNXRUNTIME_FETCHED).
#
# Only a CPU build is ever fetched, and only two things are taken from it: the headers Drift
# compiles against, and — when DRIFT_BUNDLE_ONNXRUNTIME is on — a runtime for the build tree to
# load so a dev build works before any addon is installed. The library is never linked; which
# runtime the shipped app uses is the user's runtime choice, not a configure-time one
# (src/engine/ml/runtime/OrtRuntime.cpp).

if(DRIFT_ONNXRUNTIME_FETCHED)
    return()
endif()

set(_onnxruntime_version "1.27.0")

# github.com/microsoft/onnxruntime stopped serving its release tarballs (404, Nov 2026), so both
# sources below are Microsoft's package-registry copies of the same release.
#
# Headers only — Android and wasm, which dlopen an addon runtime or none, any build with
# DRIFT_BUNDLE_ONNXRUNTIME off, and Intel Macs, for which upstream publishes no runtime at all:
# the Android AAR from Maven Central. The C and C++ API headers are identical across platforms and
# architectures, and at 44 MB it is the smallest archive that carries them.
#
# A runtime to stage as well: the NuGet package, which carries the headers plus a CPU runtime for
# every desktop platform Drift builds on except osx-x64.
set(_onnxruntime_rid "")
if(NOT ANDROID AND NOT EMSCRIPTEN AND DRIFT_BUNDLE_ONNXRUNTIME)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
            set(_onnxruntime_rid "linux-arm64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            set(_onnxruntime_rid "linux-x64")
        endif()
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
            set(_onnxruntime_rid "osx-arm64")
        else()
            message(STATUS
                "ONNX Runtime publishes no x86_64 macOS build; fetching headers only and disabling "
                "DRIFT_BUNDLE_ONNXRUNTIME. Install an Acceleration addon, or point "
                "DRIFT_ONNXRUNTIME_DIR at an x86_64 runtime.")
            set(DRIFT_BUNDLE_ONNXRUNTIME OFF CACHE BOOL
                "Stage a CPU ONNX Runtime into the build tree for development" FORCE)
        endif()
    elseif(WIN32)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(_onnxruntime_rid "win-arm64")
        else()
            set(_onnxruntime_rid "win-x64")
        endif()
    endif()
    if(NOT _onnxruntime_rid AND DRIFT_BUNDLE_ONNXRUNTIME)
        message(FATAL_ERROR
            "Automatic ONNX Runtime download is not supported for "
            "${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}. "
            "Install ONNX Runtime manually and set ONNXRUNTIME_ROOT.")
    endif()
endif()

if(_onnxruntime_rid)
    set(_onnxruntime_archive "microsoft.ml.onnxruntime.${_onnxruntime_version}.nupkg")
    set(_onnxruntime_url
        "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${_onnxruntime_version}/${_onnxruntime_archive}")
    set(_onnxruntime_sha256 "c725e8cefdfe4de08befb9f79b3f218c162439cdcdb97cbbb5192888d58ea669")
    set(_onnxruntime_headers_in "build/native/include")
    set(_onnxruntime_libs_in "runtimes/${_onnxruntime_rid}/native")
    set(_onnxruntime_flavor "${_onnxruntime_rid}")
else()
    set(_onnxruntime_archive "onnxruntime-android-${_onnxruntime_version}.aar")
    set(_onnxruntime_url
        "https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/${_onnxruntime_version}/${_onnxruntime_archive}")
    set(_onnxruntime_sha256 "077dec5e2d821234c7dc0aba584bec8f999854b546c754cab93a90741c56fbeb")
    set(_onnxruntime_headers_in "headers")
    set(_onnxruntime_libs_in "")
    set(_onnxruntime_flavor "headers")
endif()

set(_onnxruntime_staging "${CMAKE_BINARY_DIR}/_deps/onnxruntime/cpu")
# Laid out as include/ + lib/, the shape FindOnnxRuntime looks for under ONNXRUNTIME_ROOT.
set(_onnxruntime_prefix "${_onnxruntime_staging}/onnxruntime-${_onnxruntime_flavor}-${_onnxruntime_version}")

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    file(MAKE_DIRECTORY "${_onnxruntime_staging}")
    set(_onnxruntime_archive_path "${_onnxruntime_staging}/${_onnxruntime_archive}")

    if(NOT EXISTS "${_onnxruntime_archive_path}")
        message(STATUS "Downloading ONNX Runtime ${_onnxruntime_version}: ${_onnxruntime_url}")
        file(DOWNLOAD "${_onnxruntime_url}" "${_onnxruntime_archive_path}"
            EXPECTED_HASH SHA256=${_onnxruntime_sha256}
            SHOW_PROGRESS)
    endif()

    set(_onnxruntime_patterns "${_onnxruntime_headers_in}/*")
    if(_onnxruntime_libs_in)
        list(APPEND _onnxruntime_patterns "${_onnxruntime_libs_in}/*")
    endif()
    set(_onnxruntime_extract "${_onnxruntime_staging}/extract-${_onnxruntime_flavor}")
    file(REMOVE_RECURSE "${_onnxruntime_extract}")
    file(ARCHIVE_EXTRACT
        INPUT "${_onnxruntime_archive_path}"
        DESTINATION "${_onnxruntime_extract}"
        PATTERNS ${_onnxruntime_patterns})

    file(GLOB _onnxruntime_headers "${_onnxruntime_extract}/${_onnxruntime_headers_in}/*.h")
    file(COPY ${_onnxruntime_headers} DESTINATION "${_onnxruntime_prefix}/include")
    if(_onnxruntime_libs_in)
        file(GLOB _onnxruntime_libs "${_onnxruntime_extract}/${_onnxruntime_libs_in}/*")
        file(COPY ${_onnxruntime_libs} DESTINATION "${_onnxruntime_prefix}/lib")
    endif()
    file(REMOVE_RECURSE "${_onnxruntime_extract}")
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
