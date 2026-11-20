# Ren'Py virtual machine shared by DDLC and Katawa Shoujo.
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
if(EMSCRIPTEN)
    target_link_libraries(kotonoha_vm_renpy PUBLIC KotonohaEngine PRIVATE DdlcFormats)
else()
    target_link_libraries(kotonoha_vm_renpy PUBLIC KotonohaEngine PRIVATE DdlcFormats Python3::Python)
endif()
target_compile_features(kotonoha_vm_renpy PUBLIC cxx_std_20)
set_target_properties(kotonoha_vm_renpy PROPERTIES CXX_EXTENSIONS OFF)
