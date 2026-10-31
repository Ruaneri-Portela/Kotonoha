add_executable(DdlcPythonTest DdlcPythonTest.cpp)
target_link_libraries(DdlcPythonTest PRIVATE DdlcFormats)
add_test(NAME DokiDoki.Python COMMAND DdlcPythonTest)

if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND KOTONOHA_BUILD_MACOS_BUNDLES)
    add_test(NAME DokiDoki.BundledPython
        COMMAND "${CMAKE_COMMAND}"
            "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
            "-DTEST_EXECUTABLE=$<TARGET_FILE:DdlcPythonTest>"
            "-DAPP_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:DokiDoki>"
            "-DBUNDLE_SCRIPT=${KOTONOHA_PROJECT_ROOT}/cmake/BundleApp.cmake"
            "-DDEPENDENCY_DIRS=$<TARGET_FILE_DIR:SDL3::SDL3>"
            "-DPYTHON_STDLIB=${Python3_STDLIB}"
            "-DPYTHON_VERSION=${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/BundledPythonTest.cmake")
endif()

add_executable(DdlcAssetsTest DdlcAssetsTest.cpp)
target_link_libraries(DdlcAssetsTest PRIVATE doki_doki_literature_club)
add_test(NAME DokiDoki.Assets COMMAND DdlcAssetsTest)

set(DDLC_DATA_DIR "" CACHE PATH
    "Doki Doki Literature Club game directory for the retail-data tests")
if(DDLC_DATA_DIR)
    if(NOT IS_DIRECTORY "${DDLC_DATA_DIR}")
        message(FATAL_ERROR "DDLC_DATA_DIR is not a directory: ${DDLC_DATA_DIR}")
    endif()
    add_executable(DdlcScriptTest DdlcScriptTest.cpp)
    target_link_libraries(DdlcScriptTest PRIVATE DdlcFormats)
    add_test(NAME DokiDoki.Script COMMAND DdlcScriptTest "${DDLC_DATA_DIR}")
    add_executable(DdlcPlaythroughTest DdlcPlaythroughTest.cpp)
    target_link_libraries(DdlcPlaythroughTest PRIVATE doki_doki_literature_club)
    add_test(NAME DokiDoki.Playthrough COMMAND DdlcPlaythroughTest "${DDLC_DATA_DIR}")
endif()
