if(NOT IS_DIRECTORY "${BUNDLE_CONTENTS}/Resources" OR
   NOT IS_DIRECTORY "${BUNDLE_CONTENTS}/Resources/assets" OR
   NOT IS_DIRECTORY "${BUNDLE_CONTENTS}/Resources/game-assets")
    message(FATAL_ERROR "Bundle must contain Resources/assets and Resources/game-assets")
endif()

file(GLOB resource_items LIST_DIRECTORIES true
    "${BUNDLE_CONTENTS}/Resources/*" "${BUNDLE_CONTENTS}/Resources/.[!.]*"
    "${BUNDLE_CONTENTS}/Resources/..?*")
foreach(item IN LISTS resource_items)
    if(IS_DIRECTORY "${item}")
        get_filename_component(name "${item}" NAME)
        if(NOT name STREQUAL "assets" AND NOT name STREQUAL "game-assets")
            message(FATAL_ERROR "Unexpected bundle resource directory: ${item}")
        endif()
    endif()
endforeach()

set(frameworks "${BUNDLE_CONTENTS}/Frameworks")
if(APP_GAME STREQUAL "doki-doki-literature-club")
    if(NOT PYTHON_VERSION)
        message(FATAL_ERROR "DDLC bundle requires a Python version")
    endif()
    set(python_stdlib "lib/python${PYTHON_VERSION}/encodings/__init__.py")
    if(NOT EXISTS "${frameworks}/Python.framework/Versions/Current/${python_stdlib}" AND
       NOT EXISTS "${frameworks}/python/${python_stdlib}")
        message(FATAL_ERROR "DDLC bundle is missing its Python standard library")
    endif()
else()
    file(GLOB python_libraries "${frameworks}/libpython*.dylib"
        "${frameworks}/libpython*.so")
    if(EXISTS "${frameworks}/Python.framework" OR
       EXISTS "${frameworks}/python" OR python_libraries)
        message(FATAL_ERROR "Only DDLC may include a Python runtime: ${BUNDLE_CONTENTS}")
    endif()
endif()
