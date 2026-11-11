# Amagami SCF virtual machine. The VM reads the Amagami format declarations
# (<Amagami/Formats.hpp>), so it links the format library privately; consumers
# that run the VM (game runtime, tests) link Kotonoha::Vmamagami directly.
if(TARGET Kotonoha::Vmamagami)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_vm_amagami STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Vm.cpp")
add_library(Kotonoha::Vmamagami ALIAS kotonoha_vm_amagami)

target_include_directories(kotonoha_vm_amagami PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_amagami PUBLIC Kotonoha::Engine PRIVATE AmagamiFormats)
target_compile_features(kotonoha_vm_amagami PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_amagami PROPERTIES CXX_EXTENSIONS OFF)
