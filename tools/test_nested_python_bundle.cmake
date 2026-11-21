cmake_minimum_required(VERSION 3.16)

# Portable synthetic fixture for the macOS Python.framework cleanup.
# No macOS SDK, Homebrew Python, or commercial game assets are needed.
include("${CMAKE_CURRENT_LIST_DIR}/../buildsystems/cmake/packaging/PruneNestedPythonApp.cmake")

set(_fixture "${CMAKE_CURRENT_BINARY_DIR}/kotonoha-nested-python-bundle-fixture")
set(_version "${_fixture}/KotonohaLauncher.app/Contents/Frameworks/Python.framework/Versions/3.15")
set(_nested "${_version}/Resources/Python.app")
file(REMOVE_RECURSE "${_fixture}")
file(MAKE_DIRECTORY "${_nested}/Contents/MacOS")
file(MAKE_DIRECTORY "${_version}/lib/python3.15/encodings")
file(WRITE "${_nested}/Contents/MacOS/Python" "nested app helper")
file(WRITE "${_version}/Resources/Info.plist" "framework metadata")
file(WRITE "${_version}/lib/python3.15/encodings/__init__.py" "stdlib fixture")

kotonoha_prune_nested_python_app("${_version}")
if(EXISTS "${_nested}" OR IS_SYMLINK "${_nested}")
    message(FATAL_ERROR "Python.app was not pruned from the staged framework")
endif()
if(NOT EXISTS "${_version}/Resources/Info.plist" OR
   NOT EXISTS "${_version}/lib/python3.15/encodings/__init__.py")
    message(FATAL_ERROR "Pruning damaged Python.framework runtime resources")
endif()

# Multiple POST_BUILD invocations must be safe without a clean build.
kotonoha_prune_nested_python_app("${_version}")

# A symlinked helper must be removed without following its target.
file(MAKE_DIRECTORY "${_fixture}/external-python-helper")
file(WRITE "${_fixture}/external-python-helper/keep.txt" "unrelated content")
file(CREATE_LINK "${_fixture}/external-python-helper" "${_nested}"
     SYMBOLIC RESULT _symlink_result)
if(NOT _symlink_result STREQUAL "0")
    message(FATAL_ERROR "Cannot create synthetic Python.app symlink: ${_symlink_result}")
endif()
kotonoha_prune_nested_python_app("${_version}")
if(EXISTS "${_nested}" OR IS_SYMLINK "${_nested}" OR
   NOT EXISTS "${_fixture}/external-python-helper/keep.txt")
    message(FATAL_ERROR "Python.app symlink cleanup removed unrelated files")
endif()

file(REMOVE_RECURSE "${_fixture}")
message(STATUS "[Kotonoha/Python] Nested Python.app regression test passed")
