# Amagami SCF virtual machine. The VM reads the Amagami format declarations
# (<Amagami/Formats.hpp>), so it links the format library privately; consumers
# that run the VM (game runtime, tests) link kotonoha_vm_amagami directly.
if(TARGET kotonoha_vm_amagami)
    return()
endif()

add_library(kotonoha_vm_amagami STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Vm.cpp")

target_include_directories(kotonoha_vm_amagami PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_amagami PUBLIC KotonohaEngine PRIVATE AmagamiFormats)
target_compile_features(kotonoha_vm_amagami PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_amagami PROPERTIES CXX_EXTENSIONS OFF)
