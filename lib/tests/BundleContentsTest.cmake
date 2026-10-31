set(root "${TEST_DIRECTORY}/bundle-contents-test")
set(contents "${root}/Example.app/Contents")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${contents}/MacOS" "${contents}/Resources/assets"
    "${contents}/Resources/game-assets" "${contents}/Frameworks")
file(WRITE "${contents}/MacOS/Example" "test")
file(WRITE "${contents}/Resources/icon.icns" "icon")

function(check_layout game version expected)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DBUNDLE_CONTENTS=${contents}" "-DAPP_GAME=${game}"
        "-DPYTHON_VERSION=${version}" -P "${CHECK_SCRIPT}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if((expected AND NOT result EQUAL 0) OR (NOT expected AND result EQUAL 0))
        message(FATAL_ERROR "Unexpected ${game} bundle check result: ${output}${error}")
    endif()
endfunction()

check_layout(example "" TRUE)
file(MAKE_DIRECTORY "${contents}/Resources/other")
check_layout(example "" FALSE)
file(REMOVE_RECURSE "${contents}/Resources/other")
file(MAKE_DIRECTORY "${contents}/Resources/.hidden")
check_layout(example "" FALSE)
file(REMOVE_RECURSE "${contents}/Resources/.hidden")
file(REMOVE_RECURSE "${contents}/Resources/game-assets")
check_layout(example "" FALSE)
file(MAKE_DIRECTORY "${contents}/Resources/game-assets")

file(MAKE_DIRECTORY "${contents}/Frameworks/Python.framework")
check_layout(example "" FALSE)
file(REMOVE_RECURSE "${contents}/Frameworks/Python.framework")
file(MAKE_DIRECTORY "${contents}/Frameworks/python")
check_layout(example "" FALSE)
file(REMOVE_RECURSE "${contents}/Frameworks/python")
file(WRITE "${contents}/Frameworks/libpython3.13.dylib" "test")
check_layout(example "" FALSE)
file(REMOVE "${contents}/Frameworks/libpython3.13.dylib")

check_layout(doki-doki-literature-club "3.13" FALSE)
file(MAKE_DIRECTORY "${contents}/Frameworks/python/lib/python3.13/encodings")
file(WRITE "${contents}/Frameworks/python/lib/python3.13/encodings/__init__.py" "")
check_layout(doki-doki-literature-club "3.13" TRUE)
check_layout(doki-doki-literature-club "" FALSE)
file(MAKE_DIRECTORY "${contents}/Resources/python")
check_layout(doki-doki-literature-club "3.13" FALSE)
file(REMOVE_RECURSE "${contents}/Resources/python" "${contents}/Frameworks/python")

set(framework "${contents}/Frameworks/Python.framework/Versions")
file(MAKE_DIRECTORY "${framework}/3.13/lib/python3.13/encodings")
file(WRITE "${framework}/3.13/lib/python3.13/encodings/__init__.py" "")
file(CREATE_LINK "3.13" "${framework}/Current" SYMBOLIC)
check_layout(doki-doki-literature-club "3.13" TRUE)

execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DAPP_EXECUTABLE=${contents}/MacOS/Example"
    "-DBUNDLE_CONTENTS=${contents}" -DAPP_GAME=example
    "-DPYTHON_STDLIB=${framework}/3.13/lib/python3.13"
    -DPYTHON_VERSION=3.13 -P "${BUNDLE_SCRIPT}"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
    message(FATAL_ERROR "A non-DDLC app accepted Python bundle arguments")
endif()

file(REMOVE_RECURSE "${root}")
