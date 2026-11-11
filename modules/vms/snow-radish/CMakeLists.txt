# Snow Radish ADV3 virtual machine. Only depends on Kotonoha::Engine; the game
# session that drives it links Kotonoha::Vmsnow-radish directly.
if(TARGET Kotonoha::Vmsnow-radish)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_vm_snow-radish STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Vm.cpp")
add_library(Kotonoha::Vmsnow-radish ALIAS kotonoha_vm_snow-radish)

target_include_directories(kotonoha_vm_snow-radish PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_snow-radish PUBLIC Kotonoha::Engine)
target_compile_features(kotonoha_vm_snow-radish PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_snow-radish PROPERTIES CXX_EXTENSIONS OFF)
