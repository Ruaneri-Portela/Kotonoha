set(probe "${TEST_DIRECTORY}/DdlcPythonProbe.app")
set(contents "${probe}/Contents")
set(executable "${contents}/MacOS/DokiDoki")
file(REMOVE_RECURSE "${probe}")
file(MAKE_DIRECTORY "${contents}/MacOS" "${contents}/Resources/assets"
    "${contents}/Resources/game-assets" "${contents}/Resources/python")
file(WRITE "${contents}/Resources/python/legacy-test" "")
file(COPY "${TEST_EXECUTABLE}" DESTINATION "${contents}/MacOS")
get_filename_component(test_name "${TEST_EXECUTABLE}" NAME)
file(RENAME "${contents}/MacOS/${test_name}" "${executable}")
file(COPY "${APP_CONTENTS}/Info.plist" DESTINATION "${contents}")

execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DAPP_EXECUTABLE=${executable}" "-DBUNDLE_CONTENTS=${contents}"
    -DAPP_GAME=doki-doki-literature-club "-DDEPENDENCY_DIRS=${DEPENDENCY_DIRS}"
    "-DPYTHON_STDLIB=${PYTHON_STDLIB}" "-DPYTHON_VERSION=${PYTHON_VERSION}"
    -P "${BUNDLE_SCRIPT}"
    RESULT_VARIABLE bundle_result OUTPUT_VARIABLE bundle_output ERROR_VARIABLE bundle_error)
if(bundle_result EQUAL 0)
    if(EXISTS "${contents}/Resources/python")
        set(test_result 1)
        set(test_error "The legacy Python resource directory was not removed")
    else()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONHOME=/nonexistent-kotonoha-python"
            "PYTHONPATH=/nonexistent-kotonoha-python"
            "${executable}" "${contents}/Frameworks"
            WORKING_DIRECTORY "${TEST_DIRECTORY}"
            RESULT_VARIABLE test_result OUTPUT_VARIABLE test_output ERROR_VARIABLE test_error)
        if(test_result EQUAL 0)
            set(framework_stdlib
                "${contents}/Frameworks/Python.framework/Versions/Current/lib/python${PYTHON_VERSION}")
            set(dylib_stdlib "${contents}/Frameworks/python/lib/python${PYTHON_VERSION}")
            set(stdlib "${dylib_stdlib}")
            if(IS_DIRECTORY "${framework_stdlib}")
                file(MAKE_DIRECTORY "${contents}/Frameworks/python/lib")
                file(RENAME "${framework_stdlib}" "${dylib_stdlib}")
                execute_process(COMMAND "${CMAKE_COMMAND}" -E env
                    "PYTHONHOME=/nonexistent-kotonoha-python"
                    "PYTHONPATH=/nonexistent-kotonoha-python"
                    "${executable}" "${contents}/Frameworks"
                    WORKING_DIRECTORY "${TEST_DIRECTORY}"
                    RESULT_VARIABLE fallback_result OUTPUT_VARIABLE fallback_output
                    ERROR_VARIABLE fallback_error)
                if(NOT fallback_result EQUAL 0 OR NOT fallback_output MATCHES "DdlcPythonTest: OK")
                    set(test_result 1)
                    set(test_error "Fallback Python layout failed:\n${fallback_output}${fallback_error}")
                endif()
            endif()
            if(test_result EQUAL 0)
                file(RENAME "${stdlib}" "${stdlib}-missing")
                execute_process(COMMAND "${CMAKE_COMMAND}" -E env
                    "PYTHONHOME=/nonexistent-kotonoha-python"
                    "PYTHONPATH=/nonexistent-kotonoha-python"
                    "${executable}" "${contents}/Frameworks"
                    WORKING_DIRECTORY "${TEST_DIRECTORY}"
                    RESULT_VARIABLE missing_result OUTPUT_VARIABLE missing_output
                    ERROR_VARIABLE missing_error)
            endif()
        endif()
    endif()
endif()
file(REMOVE_RECURSE "${probe}")

if(NOT bundle_result EQUAL 0)
    message(FATAL_ERROR "Could not package the Python probe:\n${bundle_output}${bundle_error}")
endif()
if(NOT test_result EQUAL 0 OR NOT test_output MATCHES "DdlcPythonTest: OK")
    message(FATAL_ERROR "Bundled Python probe failed:\n${test_output}${test_error}")
endif()
if(missing_result EQUAL 0 OR NOT missing_error MATCHES "Cannot initialize CPython")
    message(FATAL_ERROR "Missing bundled Python did not fail explicitly:\n${missing_output}${missing_error}")
endif()
