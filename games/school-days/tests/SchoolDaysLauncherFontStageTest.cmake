# Synthetic, asset-free tests of the Windows-only fail-closed QA stage.
foreach(_required IN ITEMS STAGE_SCRIPT TEST_ROOT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing test parameter: ${_required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${TEST_ROOT}")
set(_missing "${TEST_ROOT}/absent.ttf")
set(_stub "${TEST_ROOT}/placeholder.ttf")
set(_wrong_hash "${TEST_ROOT}/wrong-hash.ttf")
set(_destination "${TEST_ROOT}/staged/NotoSansJP-Regular.ttf")
file(REMOVE "${_missing}" "${_destination}")
string(REPEAT "X" 1204 _placeholder)
file(WRITE "${_stub}" "${_placeholder}")
# A file with the exact expected size must still fail if its bytes differ.
string(REPEAT "X" 5733060 _same_size_bad_bytes)
file(WRITE "${_wrong_hash}" "${_same_size_bad_bytes}")
unset(_same_size_bad_bytes)
file(MAKE_DIRECTORY "${TEST_ROOT}/staged")
file(WRITE "${_destination}" "existing-output-must-survive")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DKOTONOHA_FONT_ACTION=stage"
        "-DKOTONOHA_FONT_SOURCE=${_missing}"
        "-DKOTONOHA_FONT_DESTINATION=${_destination}"
        -P "${STAGE_SCRIPT}"
    RESULT_VARIABLE _missing_result OUTPUT_QUIET ERROR_QUIET)
if(_missing_result EQUAL 0)
    message(FATAL_ERROR "Missing font source was accepted")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DKOTONOHA_FONT_ACTION=stage"
        "-DKOTONOHA_FONT_SOURCE=${_stub}"
        "-DKOTONOHA_FONT_DESTINATION=${_destination}"
        -P "${STAGE_SCRIPT}"
    RESULT_VARIABLE _stub_result OUTPUT_QUIET ERROR_QUIET)
if(_stub_result EQUAL 0)
    message(FATAL_ERROR "1.2 KiB placeholder was accepted for staging")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DKOTONOHA_FONT_ACTION=stage"
        "-DKOTONOHA_FONT_SOURCE=${_wrong_hash}"
        "-DKOTONOHA_FONT_DESTINATION=${_destination}"
        -P "${STAGE_SCRIPT}"
    RESULT_VARIABLE _hash_result OUTPUT_QUIET ERROR_QUIET)
if(_hash_result EQUAL 0)
    message(FATAL_ERROR "Wrong SHA-256 was accepted despite correct file size")
endif()
file(READ "${_destination}" _unchanged)
if(NOT _unchanged STREQUAL "existing-output-must-survive")
    message(FATAL_ERROR "Rejected source unexpectedly changed the destination")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DKOTONOHA_FONT_ACTION=verify"
        "-DKOTONOHA_FONT_DESTINATION=${_destination}"
        -P "${STAGE_SCRIPT}"
    RESULT_VARIABLE _verify_result OUTPUT_QUIET ERROR_QUIET)
if(_verify_result EQUAL 0)
    message(FATAL_ERROR "Final QA bundle accepted an invalid launcher font")
endif()
message(STATUS "Launcher font rejects missing/stub/wrong-hash inputs and invalid bundle")
