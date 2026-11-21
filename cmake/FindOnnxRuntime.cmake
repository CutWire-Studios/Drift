# Find ONNX Runtime (https://onnxruntime.ai) — headers only.
#
# Drift does not link ONNX Runtime. It compiles against the headers with ORT_API_MANUAL_INIT and
# dlopens whichever runtime the user installed (src/engine/ml/runtime/OrtRuntime.cpp), which is what lets the
# CPU / CUDA / WebGPU choice be an addon rather than a configure-time decision. So the library
# being absent here is not an error; only the headers have to exist.
#
# Set ONNXRUNTIME_ROOT to the install prefix if the headers are not on the default path (e.g. an
# extracted onnxruntime-linux-x64-*.tgz release).
#
# Defines:
#   OnnxRuntime_FOUND
#   OnnxRuntime_INCLUDE_DIRS
#   ONNXRUNTIME_RUNTIME_LIBS  (shared libs found beside the headers, for the dev-tree bundle)
# and an imported target onnxruntime::headers.

find_path(OnnxRuntime_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS
        ${OnnxRuntime_ROOT}
        ${ONNXRUNTIME_ROOT}/include
        ${ONNXRUNTIME_ROOT}/include/onnxruntime
        ${ONNXRUNTIME_ROOT}/include/onnxruntime/core/session
        $ENV{ONNXRUNTIME_ROOT}/include
        ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include
        /usr/include
        /usr/include/onnxruntime
        /usr/include/onnxruntime/core/session
        /usr/local/include
        /usr/local/include/onnxruntime
        /usr/local/include/onnxruntime/core/session
    # Cross-compiling (Android) sets CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY, which confines the
    # search to the NDK sysroot and would ignore every HINT above — including the tree
    # FetchOnnxRuntime just downloaded. These are headers compiled against, never a sysroot
    # library, so escaping the root path is correct rather than a workaround.
    NO_CMAKE_FIND_ROOT_PATH
)

# Optional, and deliberately not part of the found/not-found decision below: it exists only so
# drift_bundle_onnxruntime() has something to stage.
find_library(OnnxRuntime_LIBRARY
    NAMES onnxruntime
    HINTS
        ${OnnxRuntime_ROOT}
        ${ONNXRUNTIME_ROOT}/lib
        ${ONNXRUNTIME_ROOT}/lib64
        $ENV{ONNXRUNTIME_ROOT}/lib
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OnnxRuntime DEFAULT_MSG OnnxRuntime_INCLUDE_DIR)

if(OnnxRuntime_FOUND)
    set(OnnxRuntime_INCLUDE_DIRS ${OnnxRuntime_INCLUDE_DIR})

    # ORT_API_MANUAL_INIT is on the interface rather than one target's private definitions: the
    # header carries a #pragma detect_mismatch that fails the link if two translation units
    # disagree about it, so everything that includes onnxruntime_cxx_api.h has to see it.
    if(NOT TARGET onnxruntime::headers)
        add_library(onnxruntime::headers INTERFACE IMPORTED)
        set_target_properties(onnxruntime::headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${OnnxRuntime_INCLUDE_DIR}"
            INTERFACE_COMPILE_DEFINITIONS "ORT_API_MANUAL_INIT")
    endif()

    # Whatever shared libraries sit beside the one that was found. Never linked —
    # drift_bundle_onnxruntime() stages them into the build tree so a dev build has a runtime to
    # load with no addon service.
    if(OnnxRuntime_LIBRARY)
        get_filename_component(_ort_libdir "${OnnxRuntime_LIBRARY}" DIRECTORY)
        file(GLOB ONNXRUNTIME_RUNTIME_LIBS
            "${_ort_libdir}/*onnxruntime*.dll"
            "${_ort_libdir}/*onnxruntime*.so*"
            "${_ort_libdir}/*onnxruntime*.dylib")
    endif()
endif()

mark_as_advanced(OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY)
