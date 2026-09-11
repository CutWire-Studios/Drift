#[[
Locates the Skia build produced by third_party/build-skia.sh and exposes it as one imported
target, Skia::Skia, carrying the include root, the archives in link order, the system libraries
GN resolved, and — most importantly — the exact preprocessor defines Skia was compiled with.

Lookup order:
  1. DRIFT_SKIA_DIR            a directory containing SkiaConfig.cmake (Flatpak passes /app/lib/skia)
  2. third_party/prebuilt/skia/<target>   what build-skia.sh installs for this host / Android ABI
  3. find_package(unofficial-skia)         vcpkg's port, for Windows builds that prefer it

Sets Skia_FOUND. Fails hard otherwise: a Skia that is half-found (headers without matching
defines) compiles and then miscolours every pixel, so there is no soft fallback here.
#]]

if(TARGET Skia::Skia)
    set(Skia_FOUND TRUE)
    return()
endif()

if(ANDROID)
    set(_skia_target "android-${ANDROID_ABI}")
elseif(WIN32)
    set(_skia_target "win-x64")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_skia_target "mac-arm64")
    else()
        set(_skia_target "mac-x64")
    endif()
else()
    set(_skia_target "linux-x64")
endif()

set(_skia_candidates)
if(DRIFT_SKIA_DIR)
    list(APPEND _skia_candidates "${DRIFT_SKIA_DIR}")
endif()
list(APPEND _skia_candidates "${CMAKE_SOURCE_DIR}/third_party/prebuilt/skia/${_skia_target}")

set(_skia_config "")
foreach(_dir ${_skia_candidates})
    if(EXISTS "${_dir}/SkiaConfig.cmake")
        set(_skia_config "${_dir}/SkiaConfig.cmake")
        break()
    endif()
endforeach()

if(_skia_config)
    include("${_skia_config}")
    get_filename_component(_skia_root "${_skia_config}" DIRECTORY)

    add_library(Skia::Skia INTERFACE IMPORTED)
    target_include_directories(Skia::Skia SYSTEM INTERFACE "${_skia_root}/include")
    target_compile_definitions(Skia::Skia INTERFACE ${SKIA_DEFINES})
    target_link_libraries(Skia::Skia INTERFACE ${SKIA_LIBRARIES} ${SKIA_SYSTEM_LIBRARIES})

    set(Skia_FOUND TRUE)
    message(STATUS "Skia ${SKIA_MILESTONE} (${SKIA_TARGET}) from ${_skia_root}")
    return()
endif()

find_package(unofficial-skia CONFIG QUIET)
if(unofficial-skia_FOUND)
    add_library(Skia::Skia INTERFACE IMPORTED)
    target_link_libraries(Skia::Skia INTERFACE unofficial::skia::skia)
    foreach(_mod skottie sksg skresources jsonreader svg skparagraph skshaper skunicode_icu skunicode_core)
        if(TARGET unofficial::skia::modules::${_mod})
            target_link_libraries(Skia::Skia INTERFACE unofficial::skia::modules::${_mod})
        endif()
    endforeach()
    set(Skia_FOUND TRUE)
    message(STATUS "Skia from vcpkg (unofficial-skia)")
    return()
endif()

message(FATAL_ERROR
    "DRIFT_WITH_SKIA is ON but no Skia build was found. Run:\n"
    "    third_party/build-skia.sh ${_skia_target}\n"
    "or point DRIFT_SKIA_DIR at a directory containing SkiaConfig.cmake.")
