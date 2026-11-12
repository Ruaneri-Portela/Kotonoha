if(TARGET Kotonoha::Modules::VMs::Pmail)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_vm_pmail STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Vm.cpp")
add_library(Kotonoha::Modules::VMs::Pmail ALIAS kotonoha_vm_pmail)
add_library(Kotonoha::Vmpmail ALIAS kotonoha_vm_pmail)

target_include_directories(kotonoha_vm_pmail PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_pmail PUBLIC Kotonoha::Engine)
target_compile_features(kotonoha_vm_pmail PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_pmail PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
