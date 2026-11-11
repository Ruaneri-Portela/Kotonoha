# Ren'Py (DDLC) virtual machine. Reads Ddlc/Python.hpp and Ddlc/Script.hpp
# from the DDLC format library and embeds Python; cxx_std_20.
if(TARGET Kotonoha::Vmrenpy)
  return()
endif()
if(NOT TARGET Kotonoha::Engine)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
      "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_vm_renpy STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Vm.cpp")
add_library(Kotonoha::Vmrenpy ALIAS kotonoha_vm_renpy)
target_include_directories(kotonoha_vm_renpy PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_renpy PUBLIC Kotonoha::Engine
    PRIVATE DdlcFormats Python3::Python)
target_compile_features(kotonoha_vm_renpy PUBLIC cxx_std_20)
set_target_properties(kotonoha_vm_renpy PROPERTIES CXX_EXTENSIONS OFF)
