# Ren'Py virtual machine shared by DDLC and Katawa Shoujo.
# CPython interpreter metadata is shared across both Ren'Py game subdirectories.
# FindPython3 does not expose Python3_STDLIB on every CMake version.
# Resolve it with the selected interpreter rather than shipping an empty path.
if(NOT KOTONOHA_USE_PYTHON_STUBS)
    if(ANDROID)
        # Android stages the CPython stdlib via Gradle from vcpkg-python;
        # do not try to execute the cross-compiled target interpreter on host.
        set(KOTONOHA_RENPY_PYTHON_STDLIB "" CACHE INTERNAL
            "Ren'Py shared CPython standard library" FORCE)
        set(KOTONOHA_RENPY_PYTHON_VERSION
            "${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}" CACHE INTERNAL
            "Ren'Py shared CPython version" FORCE)
    else()
        if(NOT Python3_EXECUTABLE OR NOT Python3_VERSION_MAJOR OR NOT Python3_VERSION_MINOR)
            message(FATAL_ERROR "Ren'Py requires a configured CPython interpreter")
        endif()
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -c "import sysconfig; print(sysconfig.get_path('stdlib'))"
            OUTPUT_VARIABLE _renpy_python_stdlib
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _renpy_python_result
        )
        if(NOT _renpy_python_result EQUAL 0 OR
           NOT EXISTS "${_renpy_python_stdlib}/encodings/__init__.py")
            message(FATAL_ERROR
                "Cannot locate the CPython standard library for Ren'Py: ${_renpy_python_stdlib}")
        endif()
        set(KOTONOHA_RENPY_PYTHON_STDLIB "${_renpy_python_stdlib}" CACHE INTERNAL
            "Ren'Py shared CPython standard library" FORCE)
        set(KOTONOHA_RENPY_PYTHON_VERSION
            "${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}" CACHE INTERNAL
            "Ren'Py shared CPython version" FORCE)
    endif()
endif()

if(TARGET kotonoha_vm_renpy)
    return()
endif()

if(KOTONOHA_USE_PYTHON_STUBS)
    set(_renpy_python_source "${CMAKE_CURRENT_LIST_DIR}/src/PythonDummy.cpp")
else()
    set(_renpy_python_source "${CMAKE_CURRENT_LIST_DIR}/src/Python.cpp")
endif()
add_library(kotonoha_vm_renpy STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/Vm.cpp" "${_renpy_python_source}")

target_include_directories(kotonoha_vm_renpy PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
if(EMSCRIPTEN OR KOTONOHA_USE_PYTHON_STUBS)
    target_link_libraries(kotonoha_vm_renpy PUBLIC KotonohaEngine PRIVATE DdlcFormats)
else()
    if(TARGET Python3::Python)
        set_target_properties(Python3::Python PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
    target_link_libraries(kotonoha_vm_renpy PUBLIC KotonohaEngine PRIVATE DdlcFormats Python3::Python)
endif()
target_compile_features(kotonoha_vm_renpy PUBLIC cxx_std_20)
set_target_properties(kotonoha_vm_renpy PROPERTIES CXX_EXTENSIONS OFF)
