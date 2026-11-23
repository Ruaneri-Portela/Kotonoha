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
# A Python.app nested in an embedded Python.framework (Homebrew) or
# Python3.framework (Xcode) is an accidental helper application, not the
# CPython runtime needed by the game. Reject it even if a dependency fixup
# operation reintroduces it after cleanup.
file(GLOB _python_versions LIST_DIRECTORIES TRUE
    "${frameworks}/Python.framework/Versions/*"
    "${frameworks}/Python3.framework/Versions/*")
foreach(_version_dir IN LISTS _python_versions)
    if(EXISTS "${_version_dir}/Resources/Python.app" OR
       IS_SYMLINK "${_version_dir}/Resources/Python.app")
        message(FATAL_ERROR
            "Unexpected nested Python.app in staged framework: ${_version_dir}")
    endif()
endforeach()

if(APP_GAME STREQUAL "doki-doki-literature-club" OR
   APP_GAME STREQUAL "katawa-shoujo" OR
   (APP_GAME STREQUAL "launcher" AND PYTHON_VERSION))
    if(NOT PYTHON_VERSION)
        message(FATAL_ERROR "DDLC/linked launcher bundle requires a Python version")
    endif()
    set(python_stdlib "lib/python${PYTHON_VERSION}/encodings/__init__.py")
    if(NOT EXISTS "${frameworks}/Python.framework/Versions/Current/${python_stdlib}" AND
       NOT EXISTS "${frameworks}/Python3.framework/Versions/Current/${python_stdlib}" AND
       NOT EXISTS "${frameworks}/python/${python_stdlib}")
        message(FATAL_ERROR "DDLC/linked launcher bundle is missing its Python standard library")
    endif()
else()
    file(GLOB python_libraries "${frameworks}/libpython*.dylib"
        "${frameworks}/libpython*.so")
    if(EXISTS "${frameworks}/Python.framework" OR
       EXISTS "${frameworks}/Python3.framework" OR
       EXISTS "${frameworks}/python" OR python_libraries)
        message(FATAL_ERROR "Only RenPy games and the linked launcher may include a Python runtime: ${BUNDLE_CONTENTS}")
    endif()
endif()
