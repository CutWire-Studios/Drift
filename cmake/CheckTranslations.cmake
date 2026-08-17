# Fails if i18n/*.ts does not match a fresh lupdate of the sources.
# Invoked as a CTest; required -D: SOURCE_DIR, BINARY_DIR. Optional: CONFIG.

if(NOT SOURCE_DIR OR NOT BINARY_DIR)
    message(FATAL_ERROR "CheckTranslations.cmake needs SOURCE_DIR and BINARY_DIR")
endif()

set(_catalog "${SOURCE_DIR}/i18n/drift.ts")
if(NOT EXISTS "${_catalog}")
    message(FATAL_ERROR
        "Missing ${_catalog}. Run:\n"
        "  cmake --build build --target update_translations")
endif()

set(_snapshot "${BINARY_DIR}/i18n-check/i18n")
file(REMOVE_RECURSE "${BINARY_DIR}/i18n-check")
file(COPY "${SOURCE_DIR}/i18n" DESTINATION "${BINARY_DIR}/i18n-check")

set(_build_cmd "${CMAKE_COMMAND}" --build "${BINARY_DIR}" --target update_translations)
if(CONFIG)
    list(APPEND _build_cmd --config "${CONFIG}")
endif()
execute_process(COMMAND ${_build_cmd} RESULT_VARIABLE _rv)
if(_rv)
    message(FATAL_ERROR "update_translations failed (exit ${_rv})")
endif()

file(READ "${_catalog}" _ts)
if(_ts MATCHES [[filename="src/mcp/]])
    message(FATAL_ERROR
        "MCP protocol strings leaked into i18n/drift.ts. "
        "src/mcp/ must stay English (QT_EXCLUDE_FROM_TRANSLATION).")
endif()

file(GLOB _before_files "${_snapshot}/*.ts")
if(NOT _before_files)
    message(FATAL_ERROR "i18n snapshot at ${_snapshot} is empty")
endif()

set(_stale FALSE)
foreach(_before IN LISTS _before_files)
    get_filename_component(_name "${_before}" NAME)
    set(_after "${SOURCE_DIR}/i18n/${_name}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${_before}" "${_after}"
        RESULT_VARIABLE _diff)
    if(_diff)
        set(_stale TRUE)
        message(WARNING "${_name} changed after update_translations")
    endif()
endforeach()

if(_stale)
    set(_hint "")
    foreach(_before IN LISTS _before_files)
        get_filename_component(_name "${_before}" NAME)
        set(_after "${SOURCE_DIR}/i18n/${_name}")
        execute_process(
            COMMAND git diff --no-index -- "${_before}" "${_after}"
            OUTPUT_VARIABLE _file_diff
            ERROR_VARIABLE _)
        if(_file_diff)
            string(APPEND _hint "\n${_file_diff}")
        endif()
    endforeach()
    message(FATAL_ERROR
        "Translation catalog is stale. Run:\n"
        "  cmake --build build --target update_translations\n"
        "and commit the updated i18n/*.ts files.${_hint}")
endif()
