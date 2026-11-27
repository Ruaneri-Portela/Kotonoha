# Private Windows School Days QA font staging. Never fetch or commit font bytes.
# The approved font is identified by its digest, not by its filename alone.
set(_expected_size 5733060)
set(_expected_sha256
    "7c8597677e9fac0f54d7848ad18bc6a708dfb5baa4ebf4bd91e66efcca313bf3")

if(NOT DEFINED KOTONOHA_FONT_DESTINATION OR
   "${KOTONOHA_FONT_DESTINATION}" STREQUAL "")
    message(FATAL_ERROR "KOTONOHA_FONT_DESTINATION must be specified")
endif()

if(NOT DEFINED KOTONOHA_FONT_ACTION)
    set(KOTONOHA_FONT_ACTION verify)
endif()

function(_verify_launcher_font path)
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
        message(FATAL_ERROR
            "School Days launcher font missing: ${path}. "
            "Supply the approved NotoSansJP-Regular.ttf for private Windows QA.")
    endif()
    file(SIZE "${path}" _size)
    file(SHA256 "${path}" _sha256)
    if(NOT _size EQUAL _expected_size OR
       NOT _sha256 STREQUAL _expected_sha256)
        message(FATAL_ERROR
            "School Days launcher font is not the approved real font: ${path} "
            "(size=${_size}; SHA-256=${_sha256}). "
            "A stub, corrupted file or different revision cannot be packaged.")
    endif()
endfunction()

if(KOTONOHA_FONT_ACTION STREQUAL "stage")
    if(NOT DEFINED KOTONOHA_FONT_SOURCE OR
       "${KOTONOHA_FONT_SOURCE}" STREQUAL "")
        message(FATAL_ERROR
            "KOTONOHA_FONT_SOURCE is required for launcher font staging")
    endif()
    # Validate the source before changing the destination. This catches a
    # developer checkout with a 1.2 KiB placeholder before copying anything.
    _verify_launcher_font("${KOTONOHA_FONT_SOURCE}")
    get_filename_component(_directory "${KOTONOHA_FONT_DESTINATION}" DIRECTORY)
    file(MAKE_DIRECTORY "${_directory}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${KOTONOHA_FONT_SOURCE}" "${KOTONOHA_FONT_DESTINATION}"
        RESULT_VARIABLE _copy_result)
    if(NOT _copy_result EQUAL 0)
        message(FATAL_ERROR "School Days launcher font copy failed")
    endif()
elseif(NOT KOTONOHA_FONT_ACTION STREQUAL "verify")
    message(FATAL_ERROR
        "Unknown KOTONOHA_FONT_ACTION: ${KOTONOHA_FONT_ACTION}")
endif()

# Also validate the final artifact: a later asset copy must not reintroduce
# the placeholder between the staging operation and private ZIP packaging.
_verify_launcher_font("${KOTONOHA_FONT_DESTINATION}")
message(STATUS "School Days launcher font verified: ${KOTONOHA_FONT_DESTINATION}")
