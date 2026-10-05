# Drift's audio DSP as WebAssembly, for Drift Forge's live preview. Not part of the app build:
#
#   emcmake cmake -S wasm -B build-wasm && cmake --build build-wasm
#
# It compiles the same sources as driftaudiodsp — the stages, the pedal catalog, the graph and its
# C API (src/engine/audio/DriftGraphApi.h) — plus juce_dsp and SoundTouch, so a preview in the
# browser runs the code Drift runs. Forge's scripts/build-audio-wasm.sh drives this and copies the
# result into Forge.
cmake_minimum_required(VERSION 3.24)
project(DriftAudioWasm LANGUAGES CXX C)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure with emcmake: this project only targets WebAssembly.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(DRIFT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
include(FetchContent)

# Same pins as the app (CMakeLists.txt, third_party/build-android.sh). A local checkout can stand
# in for either download.
set(DRIFT_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag")
set(DRIFT_JUCE_DIR "" CACHE PATH "JUCE source tree; downloaded when empty")
set(DRIFT_SOUNDTOUCH_TAG "2.4.1" CACHE STRING "SoundTouch release tag")
set(DRIFT_SOUNDTOUCH_DIR "" CACHE PATH "SoundTouch source tree; downloaded when empty")

if(NOT DRIFT_JUCE_DIR)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                         GIT_TAG ${DRIFT_JUCE_TAG} GIT_SHALLOW ON)
    FetchContent_GetProperties(juce)
    if(NOT juce_POPULATED)
        FetchContent_Populate(juce)
    endif()
    set(DRIFT_JUCE_DIR "${juce_SOURCE_DIR}")
endif()
if(NOT DRIFT_SOUNDTOUCH_DIR)
    FetchContent_Declare(soundtouch GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
                         GIT_TAG ${DRIFT_SOUNDTOUCH_TAG} GIT_SHALLOW ON)
    FetchContent_GetProperties(soundtouch)
    if(NOT soundtouch_POPULATED)
        FetchContent_Populate(soundtouch)
    endif()
    set(DRIFT_SOUNDTOUCH_DIR "${soundtouch_SOURCE_DIR}")
endif()

# JUCE 8 has wasm branches in juce_core but its thread-priority table has no wasm row, so the
# static_assert on its size fails. Build from a copy of the four modules with that row added.
set(_juce "${CMAKE_BINARY_DIR}/juce")
if(NOT EXISTS "${_juce}/.patched")
    file(REMOVE_RECURSE "${_juce}")
    foreach(module juce_core juce_audio_basics juce_audio_formats juce_dsp)
        file(COPY "${DRIFT_JUCE_DIR}/modules/${module}" DESTINATION "${_juce}/modules")
    endforeach()
    set(_priorities "${_juce}/modules/juce_core/native/juce_ThreadPriorities_native.h")
    file(READ "${_priorities}" _text)
    string(FIND "${_text}" "#if JUCE_LINUX || JUCE_BSD" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "JUCE ${DRIFT_JUCE_TAG}: thread priority table changed; revisit the wasm fix-up")
    endif()
    string(REPLACE "#if JUCE_LINUX || JUCE_BSD" "#if JUCE_LINUX || JUCE_BSD || JUCE_WASM" _text "${_text}")
    file(WRITE "${_priorities}" "${_text}")
    file(TOUCH "${_juce}/.patched")
endif()

# SoundTouch's headers are included as <soundtouch/SoundTouch.h>, the layout its install makes.
file(GLOB _soundtouch_headers "${DRIFT_SOUNDTOUCH_DIR}/include/*.h")
file(COPY ${_soundtouch_headers} DESTINATION "${CMAKE_BINARY_DIR}/include/soundtouch")
file(GLOB _soundtouch_sources "${DRIFT_SOUNDTOUCH_DIR}/source/SoundTouch/*.cpp")

set(_audio "${DRIFT_ROOT}/src/engine/audio")
add_executable(drift-audio
    ${_juce}/modules/juce_core/juce_core.cpp
    ${_juce}/modules/juce_core/juce_core_CompilationTime.cpp
    ${_juce}/modules/juce_audio_basics/juce_audio_basics.cpp
    ${_juce}/modules/juce_audio_formats/juce_audio_formats.cpp
    ${_juce}/modules/juce_dsp/juce_dsp.cpp
    ${_soundtouch_sources}
    juce_wasm_shim.cpp
    ${_audio}/AudioEffectProcessor.cpp
    ${_audio}/FilterProcessors.cpp
    ${_audio}/DynamicsProcessors.cpp
    ${_audio}/ModulationProcessors.cpp
    ${_audio}/SoundTouchPitchProcessor.cpp
    ${_audio}/AudioEffectFactory.cpp
    ${_audio}/PedalCatalog.cpp
    ${_audio}/WavReader.cpp
    ${_audio}/AudioGraph.cpp
    ${_audio}/GraphPedals.cpp
    ${_audio}/GraphProcessor.cpp
    ${_audio}/DriftGraphApi.cpp
)
# juce_SystemStats_wasm.cpp calls emscripten_get_now() without including its header.
set_source_files_properties(${_juce}/modules/juce_core/juce_core.cpp PROPERTIES
    COMPILE_OPTIONS "-include;emscripten/emscripten.h")

target_include_directories(drift-audio PRIVATE
    "${DRIFT_ROOT}/src" "${_juce}/modules" "${CMAKE_BINARY_DIR}/include" "${DRIFT_SOUNDTOUCH_DIR}/include")
target_compile_definitions(drift-audio PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_MODULE_AVAILABLE_juce_core=1
    JUCE_MODULE_AVAILABLE_juce_audio_basics=1
    JUCE_MODULE_AVAILABLE_juce_audio_formats=1
    JUCE_MODULE_AVAILABLE_juce_dsp=1
    SOUNDTOUCH_DISABLE_X86_OPTIMIZATIONS=1
    ST_NO_EXCEPTION_HANDLING=1
    NDEBUG=1
)
# JUCE throws inside its JSON parser, so exceptions stay on, as native wasm exceptions (Chrome 95,
# Firefox 100, Safari 15.2). Never -pthread: an AudioWorklet cannot spawn threads, and threads
# would need cross-origin isolation headers on Forge's pages.
target_compile_options(drift-audio PRIVATE -O2 -fwasm-exceptions -ffp-contract=off)

set(_exports
    dg_stage_file dg_clear_files dg_create dg_last_error dg_destroy dg_param_index dg_set_param
    dg_node_index dg_set_knob dg_modulator_index dg_modulator_count dg_set_modulator_knob
    dg_set_modulator_step dg_modulator_values dg_reset dg_latency dg_prime_frames dg_io
    dg_io_capacity dg_process dg_enable_taps dg_tap_slots dg_tap_stride dg_collect_taps
    dg_pedal_catalog malloc free)
list(TRANSFORM _exports PREPEND "_")
list(JOIN _exports "," _exports)

target_link_options(drift-audio PRIVATE
    -O2 -fwasm-exceptions
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createDriftAudio
    # AudioWorkletGlobalScope is neither a window nor a worker, so Forge hands the glue a compiled
    # WebAssembly.Module instead of letting it fetch. Strings fall back to plain JS where an engine
    # has no TextDecoder there.
    -sENVIRONMENT=web,worker,node -sFILESYSTEM=0
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432
    "-sEXPORTED_FUNCTIONS=${_exports}"
    -sEXPORTED_RUNTIME_METHODS=HEAPF32,HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8)
set_target_properties(drift-audio PROPERTIES SUFFIX ".mjs")
