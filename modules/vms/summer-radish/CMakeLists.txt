# Summer Radish Machine virtual machine. Reads SummerRadish/Pack.hpp, provided
# by Kotonoha::ProviderCMP; the format library itself does not link the VM.
if(TARGET Kotonoha::Vmsummer-radish)
  return()
endif()
if(NOT TARGET Kotonoha::Engine)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
      "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_vm_summer-radish STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Script.cpp")
add_library(Kotonoha::Vmsummer-radish ALIAS kotonoha_vm_summer-radish)
target_include_directories(kotonoha_vm_summer-radish PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_summer-radish PUBLIC Kotonoha::Engine
    PRIVATE Kotonoha::ProviderCMP)
target_compile_features(kotonoha_vm_summer-radish PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_summer-radish PROPERTIES CXX_EXTENSIONS OFF)
