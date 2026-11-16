# Initial TJS2 source bootstrap VM. Depends on the XP3 asset provider, but
# the provider never depends on the VM.
if(TARGET Kotonoha::Modules::VMs::TJS2)
    return()
endif()
if(NOT TARGET Kotonoha::ProviderXP3)
    message(FATAL_ERROR "TJS2 bootstrap VM requires Kotonoha::ProviderXP3")
endif()

add_library(kotonoha_vm_tjs2 STATIC "${CMAKE_CURRENT_LIST_DIR}/src/TjsStartupVm.cpp")
add_library(Kotonoha::Modules::VMs::TJS2 ALIAS kotonoha_vm_tjs2)
add_library(Kotonoha::Vmtjs2 ALIAS kotonoha_vm_tjs2)

target_include_directories(kotonoha_vm_tjs2 PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_tjs2 PUBLIC Kotonoha::ProviderXP3)
target_compile_features(kotonoha_vm_tjs2 PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_tjs2 PROPERTIES CXX_EXTENSIONS OFF)
